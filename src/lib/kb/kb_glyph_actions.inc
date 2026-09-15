typedef struct kbts__sequence_lookup_result
{
  kbts__sequence_lookup_record *Records;
  kbts_glyph_ref OnePastLastGlyphMatched;
  const kbts__compiled_outcome *Outcome;

  // This is specified _nowhere_ in the docs, BUT some sequence lookups have 0 records, and exist just to prevent the
  // next lookups from executing.
  // So, checking if we got any records is not enough to figure out whether we need to "apply" this lookup.
  // We need to have an explicit bool as well.
  // Sigh.
  kbts_b32 Matched;

  kbts_u32 RecordCount;
  kbts_u32 InputSequenceCountIncludingSkippedGlyphs;
} kbts__sequence_lookup_result;

#include "kb_context_match.inc"

static const kbts__compiled_program *kbts__CompiledProgram(kbts_shape_scratchpad *Scratchpad,
    kbts_shaping_table ShapingTable, kbts_un LookupIndex, kbts_un SubtableIndex)
{
  kbts_un FlatLookup = kbts__FlatLookupIndex(Scratchpad, ShapingTable, LookupIndex);
  kbts_un FlatSubtable = Scratchpad->LookupSubtableIndexOffsets[FlatLookup] + SubtableIndex;
  return kbts__ProgramForSubtable(Scratchpad->Config->Font->CompiledContexts, FlatSubtable);
}

static kbts_b32 kbts__MatchCoverageSequence(kbts_glyph_storage *Storage, const kbts__unpacked_lookup *Lookup, kbts_u32 SkipFlags, kbts_u32 SkipUnicodeFlags,
                                                        void *Base, kbts_u16 *CoverageOffsets, kbts_un CoverageCount,
                                                        kbts_glyph_ref AtGlyph, int Backward
                                                        KB_PROFILE_ONLY(, KbProfileStage ProfileStage, KbProfileWork ProfileWork))
{
#if !defined(KB_PROFILE_CORE_ONLY) || !KB_PROFILE_CORE_ONLY
  KB_PROFILE_SCOPE(ProfileStage);
#endif
  kbts_un CoverageIndex = 0;
  while(kbts__GlyphIsValid(Storage, AtGlyph) && (CoverageIndex < CoverageCount))
  {
    KB_PROFILE_WORK(ProfileWork, 1);
    kbts__coverage *Coverage = KBTS__POINTER_OFFSET(kbts__coverage, Base, CoverageOffsets[CoverageIndex]);

    if(!kbts__SkipGlyph(kbts__Glyph(Storage, AtGlyph), Lookup, SkipFlags, SkipUnicodeFlags))
    {
      kbts__cover_glyph_result Cover = kbts__CoverGlyph(Coverage, kbts__Glyph(Storage, AtGlyph)->Id);

      if(Cover.Valid)
      {
        CoverageIndex += 1;
      }
      else
      {
        break;
      }
    }

    AtGlyph = Backward ? kbts__PrevGlyphRef(Storage, AtGlyph) : kbts__NextGlyphRef(Storage, AtGlyph);
  }

  return CoverageIndex == CoverageCount;
}



static void kbts__ApplyValueRecord(kbts_glyph *Glyph, kbts__unpacked_value_record *Unpacked)
{
  Glyph->OffsetX += Unpacked->PlacementX;
  Glyph->OffsetY += Unpacked->PlacementY;

  Glyph->AdvanceX += Unpacked->AdvanceX;
  Glyph->AdvanceY += Unpacked->AdvanceY;
}

static kbts_b32 kbts__NextGlyph(kbts_glyph_storage *Storage, const kbts__unpacked_lookup *Lookup, kbts_glyph_ref AtGlyph, kbts__skip_flags SkipFlags, kbts_u32 SkipUnicodeFlags, kbts_glyph_ref *Match, int Backward)
{
  kbts_glyph_ref MatchingGlyph = 0;

  while(kbts__GlyphIsValid(Storage, AtGlyph))
  {
    if(!kbts__SkipGlyph(kbts__Glyph(Storage, AtGlyph), Lookup, SkipFlags, SkipUnicodeFlags))
    {
      MatchingGlyph = AtGlyph;
      break;
    }
    else
    {
      AtGlyph = Backward ? kbts__PrevGlyphRef(Storage, AtGlyph) : kbts__NextGlyphRef(Storage, AtGlyph);
    }
  }

  *Match = MatchingGlyph;
  return MatchingGlyph != 0;
}

/* Index zero is the supplied matched anchor. Subsequent positions are resolved
 * against the parent's live filtered sequence after every preceding action. */
static kbts_glyph_ref kbts__SequenceTarget(kbts_glyph_storage *Storage,
    const kbts__unpacked_lookup *Lookup, kbts_glyph_ref Anchor, kbts_un SequenceIndex,
    kbts__skip_flags SkipFlags, kbts_u32 SkipUnicodeFlags, kbts_un *Distance)
{
  *Distance = 0;
  if(!kbts__GlyphIsValid(Storage, Anchor)) return 0;
  while(SequenceIndex)
  {
    Anchor = kbts__NextGlyphRef(Storage, Anchor);
    ++*Distance;
    if(!kbts__GlyphIsValid(Storage, Anchor)) return 0;
    KB_PROFILE_WORK(KBW_INPUT_GLYPHS, 1);
    if(!kbts__SkipGlyph(kbts__Glyph(Storage, Anchor), Lookup, SkipFlags, SkipUnicodeFlags))
      --SequenceIndex;
  }
  return Anchor;
}

static void kbts__AttachGlyph(kbts_glyph_storage *Storage, kbts_glyph_ref Parent, kbts_glyph_ref Child, kbts_s32 X, kbts_s32 Y)
{
  kbts_glyph *ParentGlyph = kbts__Glyph(Storage, Parent);
  kbts_glyph *ChildGlyph = kbts__Glyph(Storage, Child);
  kbts_s32 DeltaOffsetX = X - ChildGlyph->OffsetX;
  kbts_s32 DeltaOffsetY = Y - ChildGlyph->OffsetY;

  kbts__glyph_link *ChildLink = &Storage->Links[Child - 1];
  if(ChildGlyph->AttachGlyph != Parent)
  {
    if(kbts__GlyphIsValid(Storage, ChildGlyph->AttachGlyph))
    {
      if(ChildLink->PrevAttached)
        Storage->Links[ChildLink->PrevAttached - 1].NextAttached = ChildLink->NextAttached;
      else
        Storage->Links[ChildGlyph->AttachGlyph - 1].FirstAttached = ChildLink->NextAttached;
      if(ChildLink->NextAttached)
        Storage->Links[ChildLink->NextAttached - 1].PrevAttached = ChildLink->PrevAttached;
    }
    kbts__glyph_link *ParentLink = &Storage->Links[Parent - 1];
    ChildLink->PrevAttached = 0;
    ChildLink->NextAttached = ParentLink->FirstAttached;
    if(ChildLink->NextAttached)
      Storage->Links[ChildLink->NextAttached - 1].PrevAttached = Child;
    ParentLink->FirstAttached = Child;
  }

  ChildGlyph->OffsetX = X;
  ChildGlyph->OffsetY = Y;
  ChildGlyph->Flags |= KBTS_GLYPH_FLAG_USED_IN_GPOS | KBTS_GLYPH_FLAG_NO_BREAK;
  ChildGlyph->AttachGlyph = Parent;

  ParentGlyph->Flags |= KBTS_GLYPH_FLAG_USED_IN_GPOS;
  kbts_u32 ChildEnd = ChildGlyph->SortKey;

  /* GPOS is nonstructural after logical-order keys are assigned. Attachment
   * owns the sole parent-to-child NO_BREAK walk, including ligature marks. */
  for(kbts_glyph_ref Middle = kbts__NextGlyphRef(Storage, Parent);
      Middle && kbts__Glyph(Storage, Middle)->SortKey < ChildEnd;
      Middle = kbts__NextGlyphRef(Storage, Middle))
    kbts__Glyph(Storage, Middle)->Flags |= KBTS_GLYPH_FLAG_NO_BREAK;

  /* Preserve eager, forward-only, one-level propagation. Visit actual direct
   * children instead of rescanning the unrelated remainder of the run. */
  for(kbts_glyph_ref Post = ChildLink->FirstAttached; Post;
      Post = Storage->Links[Post - 1].NextAttached)
  {
    kbts_glyph *PostGlyph = kbts__Glyph(Storage, Post);
    if(PostGlyph->SortKey > ChildEnd)
    {
      PostGlyph->OffsetX += DeltaOffsetX;
      PostGlyph->OffsetY += DeltaOffsetY;
    }
  }
}

static void kbts__SetLookupOnePastLastGlyph(kbts_shape_scratchpad *Scratchpad, kbts_un Index, kbts_glyph_ref Glyph)
{
  if(Index > Scratchpad->LookupOnePastLastGlyphIndex)
  {
    Scratchpad->LookupOnePastLastGlyphIndex = (kbts_u32)Index;
    Scratchpad->LookupOnePastLastGlyph = Glyph;
  }
}

typedef kbts_u32 kbts__cursive_flags;
enum kbts__cursive_flags_enum
{
  KBTS__CURSIVE_FLAG_NONE,
  KBTS__CURSIVE_FLAG_START = (1 << 0),
  KBTS__CURSIVE_FLAG_END = (1 << 1),
};

KBTS_INLINE kbts__cursive_flags kbts__GetCursiveFlags(kbts_glyph *Glyph)
{
  kbts__cursive_flags Result = (kbts__cursive_flags)Glyph->MarkOrdering;
  return Result;
}

KBTS_INLINE void kbts__SetCursiveFlags(kbts_glyph *Glyph, kbts__cursive_flags CursiveFlags)
{
  Glyph->MarkOrdering = (kbts_u8)CursiveFlags;
}

static kbts_b32 kbts__DoAdjustment(kbts_shape_scratchpad *Scratchpad, kbts_shape_config *Config,
    kbts_glyph_storage *Storage, kbts_un LookupIndex, kbts_glyph_ref CurrentGlyph,
    kbts_un StartIndex, kbts__skip_flags RequestedSkipFlags);

static kbts_b32 kbts__DoSingleAdjustment(kbts_shape_scratchpad *Scratchpad, kbts_shape_config *Config, kbts_glyph_storage *Storage,
                                         kbts_un LookupIndex, kbts_un SubtableIndex, const kbts__unpacked_lookup *Lookup, kbts_u16 *Base,
                                         kbts_glyph_ref CurrentGlyph, kbts_un StartIndex, kbts__skip_flags RequestedSkipFlags)
{
  KB_PROFILE_SCOPE(KBP_ADJUST);
  KB_PROFILE_WORK(KBW_SUBTABLE_VISITS, 1);
  KBTS_INSTRUMENT_FUNCTION_BEGIN;
  enum{SkipUnicodeFlags = KBTS_UNICODE_FLAG_DEFAULT_IGNORABLE};
  kbts__skip_flags RegularSkipFlags = KBTS__SKIP_FLAGS_GPOS_REGULAR(RequestedSkipFlags);
  kbts__skip_flags SequenceSkipFlags = KBTS__SKIP_FLAGS_GPOS_SEQUENCE(RequestedSkipFlags);

  kbts_b32 Result = 0;

  kbts_un OnePastLastGlyphIndex = StartIndex + 1;
  kbts_glyph_ref OnePastLastGlyph = kbts__NextGlyphRef(Storage, CurrentGlyph);

  if(kbts__GlyphIncludedInLookupSubtable(Scratchpad, KBTS_SHAPING_TABLE_GPOS, LookupIndex, SubtableIndex, kbts__Glyph(Storage, CurrentGlyph)))
  {

    kbts__cover_glyph_result Cover = KBTS__ZERO;
    Cover.Valid = kbts__GlyphPassesLookupFilter(kbts__Glyph(Storage, CurrentGlyph), Lookup);
    kbts__coverage *Coverage = 0;
    if(Cover.Valid && kbts__GposLookupBeginsWithCoverage(Lookup->Type, Base[0]))
    {
      kbts_u16 *CoverageOffset = Base + 1;
      Coverage = KBTS__POINTER_OFFSET(kbts__coverage, Base, *CoverageOffset);
      Cover = kbts__CoverGlyph(Coverage, kbts__Glyph(Storage, CurrentGlyph)->Id);
    }

    if(Cover.Valid)
    {
      kbts_un OnePastLastGlyphOffset = 0;
      
      switch(Lookup->Type)
      {
      case 1:
      {
        kbts_u16 ValueFormat = Base[2];

        kbts__unpacked_value_record Unpacked = KBTS__ZERO;

        if(Base[0] == 1)
        {
          kbts__single_adjustment_1 *Adjust = (kbts__single_adjustment_1 *)Base;

          Unpacked = kbts__UnpackValueRecord(Adjust, ValueFormat, KBTS__POINTER_AFTER(kbts_u16, Adjust));
        }
        else if(Base[0] == 2)
        {
          kbts__single_adjustment_2 *Adjust = (kbts__single_adjustment_2 *)Base;

          kbts_un RecordSize = kbts__PopCount32(ValueFormat);
          kbts_u16 *Records = KBTS__POINTER_AFTER(kbts_u16, Adjust);
          kbts_u16 *Record = Records + RecordSize * Cover.Index;

          Unpacked = kbts__UnpackValueRecord(Adjust, ValueFormat, Record);
        }

        kbts__ApplyValueRecord(kbts__Glyph(Storage, CurrentGlyph), &Unpacked);

        kbts__Glyph(Storage, CurrentGlyph)->Flags |= KBTS_GLYPH_FLAG_USED_IN_GPOS;

        Result = 1;
      } break;

      case 2:
      {
        KBTS_INSTRUMENT_BLOCK_BEGIN(PairPositioning);

        kbts_glyph_ref NextGlyph;
        if(kbts__NextGlyph(Storage, Lookup, kbts__NextGlyphRef(Storage, CurrentGlyph), RegularSkipFlags, SkipUnicodeFlags, &NextGlyph, 0))
        {
          kbts_u16 NextGlyphId = kbts__Glyph(Storage, NextGlyph)->Id;

          kbts_u16 *Unpacked1Base;
          kbts_u16 *Unpacked2Base;
          kbts_u16 ValueFormat1;
          kbts_u16 ValueFormat2 = 0;
          kbts_un Size1;
          kbts_un Size2;

          {
            kbts__pair_adjustment_1 *Adjust = (kbts__pair_adjustment_1 *)Base;
            ValueFormat1 = Adjust->ValueFormat1;
            ValueFormat2 = Adjust->ValueFormat2;

            Size1 = kbts__PopCount32(ValueFormat1);
            Size2 = kbts__PopCount32(ValueFormat2);
          }

          if(Base[0] == 1)
          {
            kbts__pair_adjustment_1 *Adjust = (kbts__pair_adjustment_1 *)Base;

            kbts_u16 *SetOffsets = KBTS__POINTER_AFTER(kbts_u16, Adjust);
            kbts__pair_set *Set = KBTS__POINTER_OFFSET(kbts__pair_set, Adjust, SetOffsets[Cover.Index]);

            kbts_un PairRecordSize = Size1 + Size2 + 1; // + 1 because each pair stores the next glyph ID.
            kbts__pair_value_record *PairRecords = KBTS__POINTER_AFTER(kbts__pair_value_record, Set);

            kbts_un PairCount = Set->Count;

            KBTS_INSTRUMENT_BLOCK_BEGIN(PairSearch);

            #define KBTS__PAIR_SEARCH(PairSize) \
            kbts_un PairSizeTimesPairIndex = 0; \
            while(PairCount > 1) \
            { \
              kbts_un HalfCount = PairCount / 2; \
              PairSizeTimesPairIndex = (PairRecords[PairSizeTimesPairIndex + HalfCount*PairSize - PairSize].SecondGlyph < NextGlyphId) ? (PairSizeTimesPairIndex + HalfCount*PairSize) : PairSizeTimesPairIndex; \
              PairCount -= HalfCount; \
            } \
            kbts__pair_value_record *PairRecord = PairRecords + PairSizeTimesPairIndex; \
            if(PairRecord->SecondGlyph == NextGlyphId) \
            { \
              kbts_u16 *Records = KBTS__POINTER_AFTER(kbts_u16, PairRecord); \
              Unpacked1Base = Records; \
              Unpacked2Base = Records + Size1; \
              KBTS_INSTRUMENT_BLOCK_END(PairSearch); \
              goto ApplyPairPositioning; \
            }

            if(PairRecordSize == 2)
            {
              KBTS__PAIR_SEARCH(2);
            }
            else if(PairRecordSize == 3)
            {
              KBTS__PAIR_SEARCH(3);
            }
            else
            {
              kbts__pair_value_record *PairRecord = PairRecords;

              while(PairCount > 1)
              {
                kbts_un HalfCount = PairCount / 2;

                kbts__pair_value_record *OnePastProbe = PairRecord + HalfCount * PairRecordSize;
                kbts__pair_value_record *Probe = OnePastProbe - PairRecordSize;

                if(Probe->SecondGlyph < NextGlyphId)
                {
                  PairRecord = OnePastProbe;
                }

                PairCount -= HalfCount;
              }

              if(PairRecord->SecondGlyph == NextGlyphId)
              {
                kbts_u16 *Records = KBTS__POINTER_AFTER(kbts_u16, PairRecord);

                Unpacked1Base = Records;
                Unpacked2Base = Records + Size1;
                KBTS_INSTRUMENT_BLOCK_END(PairSearch);

                goto ApplyPairPositioning;
              }
            }

            #undef KBTS__PAIR_SEARCH
            KBTS_INSTRUMENT_BLOCK_END(PairSearch);
          }
          else if(Base[0] == 2)
          {
            KBTS_INSTRUMENT_BLOCK_BEGIN(PairClasses);

            kbts__pair_adjustment_2 *Adjust = (kbts__pair_adjustment_2 *)Base;
            kbts_u16 *ClassDef1 = KBTS__POINTER_OFFSET(kbts_u16, Adjust, Adjust->ClassDefinition1Offset);
            kbts_u16 *ClassDef2 = KBTS__POINTER_OFFSET(kbts_u16, Adjust, Adjust->ClassDefinition2Offset);

            kbts_un PairRecordSize = Size1 + Size2;
            kbts_u16 *PairRecords = KBTS__POINTER_AFTER(kbts_u16, Adjust);

            // From the Microsoft docs:
            //   PairPosFormat2 requires that each glyph in all pairs be assigned to a class, which is
            //   identified by an integer called a class value.
            // This _seems_ like it would mean that, if either glyph is not specified in its respective
            // class definition table, then we should skip the lookup.
            // However, this seems wrong in practice. Undefined classes seem to just default to 0, and then
            // the bounds check takes care of deciding whether the lookup is okay to apply or not.
            // @Speed: Interleave these lookups.
            kbts__glyph_class_from_table_result Class1 = kbts__GlyphClassFromTable(ClassDef1, kbts__Glyph(Storage, CurrentGlyph)->Id);
            kbts__glyph_class_from_table_result Class2 = kbts__GlyphClassFromTable(ClassDef2, NextGlyphId);

            if((Class1.Class < Adjust->Class1Count) &&
               (Class2.Class < Adjust->Class2Count))
            {
              kbts_u16 *PairRecord = PairRecords + Class1.Class * PairRecordSize * Adjust->Class2Count + Class2.Class * PairRecordSize;

              Unpacked1Base = PairRecord;
              Unpacked2Base = PairRecord + Size1;

              KBTS_INSTRUMENT_BLOCK_END(PairClasses);
              goto ApplyPairPositioning;
            }

            KBTS_INSTRUMENT_BLOCK_END(PairClasses);
          }

          if(0)
          {
            ApplyPairPositioning:;

            KBTS_INSTRUMENT_BLOCK_BEGIN(ApplyPairPositioning);

            kbts__unpacked_value_record Unpacked1 = kbts__UnpackValueRecord(Base, ValueFormat1, Unpacked1Base);
            kbts__ApplyValueRecord(kbts__Glyph(Storage, CurrentGlyph), &Unpacked1);

            kbts__Glyph(Storage, CurrentGlyph)->Flags |= KBTS_GLYPH_FLAG_USED_IN_GPOS;
            kbts__Glyph(Storage, NextGlyph)->Flags |= KBTS_GLYPH_FLAG_USED_IN_GPOS;

            OnePastLastGlyph = kbts__NextGlyphRef(Storage, CurrentGlyph);

            if(ValueFormat2)
            {
              kbts__unpacked_value_record Unpacked2 = kbts__UnpackValueRecord(Base, ValueFormat2, Unpacked2Base);
              kbts__ApplyValueRecord(kbts__Glyph(Storage, NextGlyph), &Unpacked2);
              OnePastLastGlyph = kbts__NextGlyphRef(Storage, NextGlyph);
            }

            KBTS_INSTRUMENT_BLOCK_END(ApplyPairPositioning);

            Result = 1;
          }
        }

        KBTS_INSTRUMENT_BLOCK_END(PairPositioning);
      } break;

      // All three types of attachment (cursive, mark-to-base, mark-to-ligature) look backward instead of forward.
      case 3:
      {
        kbts__cursive_attachment *Adjust = (kbts__cursive_attachment *)Base;
        kbts__entry_exit *EntryExits = KBTS__POINTER_AFTER(kbts__entry_exit, Adjust);

        kbts_glyph_ref Prev;
        if(kbts__NextGlyph(Storage, Lookup, kbts__PrevGlyphRef(Storage, CurrentGlyph), RegularSkipFlags, SkipUnicodeFlags, &Prev, 1))
        {
          // For two cursive glyphs to be aligned, they both need to be defined by the same cursive attachment table.
          kbts__cover_glyph_result PrevCover = kbts__CoverGlyph(Coverage, kbts__Glyph(Storage, Prev)->Id);

          if(PrevCover.Valid)
          {
            // Get anchor points for both glyphs.
            kbts__entry_exit *PrevEntryExit = &EntryExits[PrevCover.Index];
            kbts__entry_exit *EntryExit = &EntryExits[Cover.Index];

            if(PrevEntryExit->ExitAnchorOffset && EntryExit->EntryAnchorOffset)
            {
              kbts__anchor *PrevExitAnchor = KBTS__POINTER_OFFSET(kbts__anchor, Adjust, PrevEntryExit->ExitAnchorOffset);
              kbts__anchor *EntryAnchor = KBTS__POINTER_OFFSET(kbts__anchor, Adjust, EntryExit->EntryAnchorOffset);
              kbts_s32 Anchor0X = PrevExitAnchor->X;
              kbts_s32 Anchor0Y = PrevExitAnchor->Y;
              kbts_s32 Anchor1X = EntryAnchor->X;
              kbts_s32 Anchor1Y = EntryAnchor->Y;
              kbts_s32 Advance0X = kbts__Glyph(Storage, Prev)->AdvanceX;
              kbts_s32 Advance1X = kbts__Glyph(Storage, CurrentGlyph)->AdvanceX;
              kbts_s32 Offset0X = kbts__Glyph(Storage, Prev)->OffsetX;
              kbts_s32 Offset0Y = kbts__Glyph(Storage, Prev)->OffsetY;
              kbts_s32 Offset1X = kbts__Glyph(Storage, CurrentGlyph)->OffsetX;
              kbts_s32 Offset1Y = kbts__Glyph(Storage, CurrentGlyph)->OffsetY;

              // Cursive positioning is made up of two parts.
              // The first part is glyph-to-glyph alignment, which depends on the text direction.
              // Here, we are only concerned with making the relative placements of the glyphs correct,
              // insofar as they follow the specified anchor positions.
              //
              // The OpenType spec makes no attempt to describe how the two glyphs should be aligned, mathematically
              // speaking.
              //
              // This is all we get:
              //
              //   "To position glyphs using the CursivePosFormat1 subtable, a text-processing client aligns the exit
              //   anchor point of a glyph with the entry anchor point of the following glyph."
              //
              // In practice, we reproduce harfbuzz's behavior here, which is:
              // - Glyph1.Offset.X is unaffected
              // - Glyph1.Advance.X = Glyph1.Offset.X + Glyph1.Anchor.X, aka. the _actual position of its anchor_
              // - Glyph2.Offset.X = -Glyph2.Anchor.X
              // - Glyph2.Offset.Y = Glyph1.Anchor.Y - Glyph2.Anchor.Y
              // - Glyph2.Advance.X -= Glyph2.Anchor.X
              //
              // (...where "X" here just means "the main direction". In top-to-bottom or bottom-to-top contexts, that
              // would be the Y coordinate, but we do not support those yet.)
              //
              // So, instead of aligning everything with offsets, we use Advance to align _the rest of the string_
              // horizontally with the first glyph, and not just the next glyph.
              //
              // It might seem dumb to modify both Glyph2.Offset _and_ Glyph2.Advance, because, when the glyphs are next
              // to one another, this is equivalent to modifying Glyph1.Advance. However, due to lookup flag nonsense,
              // there may be some glyphs between Glyph1 and Glyph2, and, _apparently_, it is fine to adjust them by the
              // position of Glyph1's anchor point, and it is not fine to adjust them by the position of Glyph2's anchor
              // point.
              //
              // This Advance tweak seems like it would be wrong, because any marks attached to Glyph1 will then have
              // invalid positions. The hope is that this does not matter in practice, because 'mark' is applied after
              // 'curs', and font designers are expected to know that and to know never to apply mark attachments
              // before cursive positioning, I guess.
              if(!kbts__ShaperRtl(Config->Shaper))
              {
                Advance0X = Offset0X + Anchor0X;
                kbts_s32 Dx = -Anchor1X - Offset1X;
                Advance1X += Dx;
                Offset1X += Dx;
              }
              else
              {
                kbts_s32 Dx = -Anchor0X - Offset0X;
                Advance0X += Dx;
                Offset0X += Dx;
                Advance1X = Offset1X + Anchor1X;
              }

              kbts__Glyph(Storage, Prev)->AdvanceX = Advance0X;
              kbts__Glyph(Storage, Prev)->OffsetX = Offset0X;
              kbts__Glyph(Storage, Prev)->Flags |= (KBTS_GLYPH_FLAG_CURSIVE | KBTS_GLYPH_FLAG_USED_IN_GPOS | KBTS_GLYPH_FLAG_NO_BREAK);
              kbts__SetCursiveFlags(kbts__Glyph(Storage, Prev), KBTS__CURSIVE_FLAG_START);
              kbts__Glyph(Storage, CurrentGlyph)->AdvanceX = Advance1X;
              kbts__Glyph(Storage, CurrentGlyph)->OffsetX = Offset1X;
              kbts__Glyph(Storage, CurrentGlyph)->Flags |= KBTS_GLYPH_FLAG_CURSIVE | KBTS_GLYPH_FLAG_USED_IN_GPOS;
              kbts__SetCursiveFlags(kbts__Glyph(Storage, CurrentGlyph), KBTS__CURSIVE_FLAG_END);

              for(kbts_glyph_ref CursiveGlyph = kbts__NextGlyphRef(Storage, Prev);
                  CursiveGlyph != CurrentGlyph;
                  CursiveGlyph = kbts__NextGlyphRef(Storage, CursiveGlyph))
              {
                kbts__Glyph(Storage, CursiveGlyph)->Flags |= KBTS_GLYPH_FLAG_NO_BREAK;
                kbts__SetCursiveFlags(kbts__Glyph(Storage, CursiveGlyph), 0);
              }

              // The second part is aligning the newly-formed cursive cluster to the "baseline". It is trickier than you'd expect.
              //
              // When applying cursive attachments, Glyph1 (RIGHT_TO_LEFT=0) or Glyph2 (RIGHT_TO_LEFT=1) is treated as being
              // "on the baseline" and its Y coordinate is used as the reference Y coordinate for the entire cursive glyph
              // cluster that has been formed so far.
              // This means that, anytime we apply a cursive attachment, we have to offset _all_ of the cursive glyphs to the
              // left (RIGHT_TO_LEFT=0) or to the right (RIGHT_TO_LEFT=1) of it.
              // This is obviously O(n^2), and @Speed: it might be worthwhile to record cursive offsets as we go, and only resolve
              // them when we actually need them, i.e. when applying a non-cursive lookup or, at the latest, when we are done with
              // positioning.
              //
              // The glyph being attached is set at a hard relative offset from the reference glyph. Its previous Y coordinate only
              // matters insofar as we need to align every other cursive glyph in its direction.

              kbts_s32 CursiveDy = 0;
              kbts_glyph_ref CursiveGlyph = CurrentGlyph;
              int Backward = 0;
              kbts_u32 AdjustNearbyGlyphs = (kbts__Glyph(Storage, CurrentGlyph)->Flags & KBTS_GLYPH_FLAG_CURSIVE);

              if(!(Lookup->Flags & KBTS__LOOKUP_FLAG_RIGHT_TO_LEFT))
              {
                kbts_s32 NewOffset1Y = Offset0Y + Anchor0Y - Anchor1Y;
                CursiveDy = NewOffset1Y - Offset1Y;
              }
              else
              {
                kbts_s32 NewOffset0Y = Offset1Y + Anchor1Y - Anchor0Y;
                CursiveDy = NewOffset0Y - Offset0Y;
                Backward = 1;
                CursiveGlyph = Prev;
                AdjustNearbyGlyphs = 1;
              }

              kbts__Glyph(Storage, CursiveGlyph)->OffsetY += CursiveDy;

              if(AdjustNearbyGlyphs)
              {
                kbts__SetCursiveFlags(kbts__Glyph(Storage, CursiveGlyph), 0);
                CursiveGlyph = Backward ? kbts__PrevGlyphRef(Storage, CursiveGlyph) : kbts__NextGlyphRef(Storage, CursiveGlyph);

                kbts__cursive_flags StopFlag = Backward ? KBTS__CURSIVE_FLAG_END : KBTS__CURSIVE_FLAG_START;

                while(kbts__GlyphIsValid(Storage, CursiveGlyph))
                {
                  kbts__cursive_flags CursiveFlags = kbts__GetCursiveFlags(kbts__Glyph(Storage, CursiveGlyph));

                  if(CursiveFlags & StopFlag)
                  {
                    break;
                  }
                  else if(kbts__Glyph(Storage, CursiveGlyph)->Flags & KBTS_GLYPH_FLAG_CURSIVE)
                  {
                    kbts__Glyph(Storage, CursiveGlyph)->OffsetY += CursiveDy;
                  }
                  else if(!(kbts__Glyph(Storage, CursiveGlyph)->Classes.Class & KBTS__GLYPH_CLASS_MARK)) // Ignore marks.
                  {
                    break;
                  }

                  CursiveGlyph = Backward ? kbts__PrevGlyphRef(Storage, CursiveGlyph) : kbts__NextGlyphRef(Storage, CursiveGlyph);
                }
              }

              Result = 1;
            }
          }
        }
      } break;

      case 4:
      case 6:
      {
        kbts__mark_to_base_attachment *Adjust = (kbts__mark_to_base_attachment *)Base;

        // We want to know which glyph to attach to, and how far that glyph is from us.
        // To do that, we add up all advances between it and us.
        // There is a slight wrinkle here: most shapers end up zeroing mark advances at the _end_
        // of the shaping process, which would retroactively make a naive advance accumulation wrong.
        // We have to take that into account here, and only accumulate mark advances for shapers that
        // don't do that zeroing at the end (either because they do it at the beginning of GPOS, or
        // because they don't do it at all).
        kbts_b32 CountMarkAdvances = !kbts__ShaperClearsMarkAdvancesInPostGposFixup(Config->Shaper);
        kbts__coverage *BaseCoverage = KBTS__POINTER_OFFSET(kbts__coverage, Adjust, Adjust->BaseCoverageOffset);
        kbts_un MarkClassCount = Adjust->MarkClassCount;
        kbts_s32 AdvanceSinceBaseX = 0;
        kbts_s32 AdvanceSinceBaseY = 0;
        kbts_u32 BaseClasses = Lookup->Type == 6 ? (1 << KBTS__GLYPH_CLASS_MARK) : (1 | (1 << KBTS__GLYPH_CLASS_BASE) | (1 << KBTS__GLYPH_CLASS_LIGATURE) | (1 << KBTS__GLYPH_CLASS_COMPONENT));
        kbts_glyph_ref BaseGlyph = 0;

        for(kbts_glyph_ref PrevGlyph = kbts__PrevGlyphRef(Storage, CurrentGlyph); kbts__GlyphIsValid(Storage, PrevGlyph); PrevGlyph = kbts__PrevGlyphRef(Storage, PrevGlyph))
        {
          if(CountMarkAdvances || (kbts__Glyph(Storage, PrevGlyph)->Classes.Class != KBTS__GLYPH_CLASS_MARK))
          {
            AdvanceSinceBaseX += kbts__Glyph(Storage, PrevGlyph)->AdvanceX;
            AdvanceSinceBaseY += kbts__Glyph(Storage, PrevGlyph)->AdvanceY;
          }

          if(!kbts__SkipGlyph(kbts__Glyph(Storage, PrevGlyph), Lookup, RegularSkipFlags, SkipUnicodeFlags))
          {
            if((1 << kbts__Glyph(Storage, PrevGlyph)->Classes.Class) & BaseClasses)
            {
              // :MultipleSubstSadness
              // There is some sadness when we have to look for bases here...
              // In multiple substitutions, we allow skipping covered glyphs if they are:
              // - Not the first in the multiple substitution
              // - Not preceded by a mark
              //
              // More details on the sadness can be found here:
              //   https://github.com/harfbuzz/harfbuzz/issues/740
              //   https://github.com/harfbuzz/harfbuzz/issues/1020
              //   https://github.com/harfbuzz/harfbuzz/issues/4124
              if((Lookup->Type == 4) &&
                 ((kbts__Glyph(Storage, PrevGlyph)->Flags & (KBTS_GLYPH_FLAG_FIRST_IN_MULTIPLE_SUBSTITUTION | KBTS_GLYPH_FLAG_MULTIPLE_SUBSTITUTION)) == KBTS_GLYPH_FLAG_MULTIPLE_SUBSTITUTION) && // Stop if we see the first of a multiple substitution.
                 (kbts__Glyph(Storage, kbts__PrevGlyphRef(Storage, PrevGlyph))->Classes.Class != KBTS__GLYPH_CLASS_MARK)) // Stop if we see any mark.
              {
                // Otherwise, we allow skipping uncovered glyphs.
                kbts_b32 ValidBase = 0;

                kbts__cover_glyph_result BaseCover = kbts__CoverGlyph(BaseCoverage, kbts__Glyph(Storage, PrevGlyph)->Id);
                ValidBase = BaseCover.Valid;

                if(ValidBase)
                {
                  BaseGlyph = PrevGlyph;
                  break;
                }
              }
              else
              {
                BaseGlyph = PrevGlyph;
                break;
              }
            }
            else if(Lookup->Type == 6)
            {
              // Unskipped non-marks block mark attachments.
              break;
            }
          }
        }

        if(BaseGlyph)
        {
          kbts_b32 Ok = (Lookup->Type == 4) || // This is a mark-to-base attachment
                        ((kbts__Glyph(Storage, BaseGlyph)->LigatureUid == kbts__Glyph(Storage, CurrentGlyph)->LigatureUid) && (kbts__Glyph(Storage, BaseGlyph)->LigatureComponentIndexPlusOne == kbts__Glyph(Storage, CurrentGlyph)->LigatureComponentIndexPlusOne)) || // This is a mark-to-mark attachment, and both marks belong to the same ligature component
                        ((kbts__Glyph(Storage, BaseGlyph)->Flags | kbts__Glyph(Storage, CurrentGlyph)->Flags) & KBTS_GLYPH_FLAG_LIGATURE); // This is a mark-to-mark attachment, and either mark was created by a ligature substitution
          if(Ok)
          {
            // @Speed: This is duplicating work in the :MultipleSubstSadness case.
            kbts__cover_glyph_result BaseCover = kbts__CoverGlyph(BaseCoverage, kbts__Glyph(Storage, BaseGlyph)->Id);
            kbts_un BaseIndex = BaseCover.Index;
            kbts_b32 ValidBase = BaseCover.Valid;

            if(ValidBase)
            {
              kbts__base_array *BaseArray = KBTS__POINTER_OFFSET(kbts__base_array, Adjust, Adjust->BaseArrayOffset);
              kbts_u16 *BaseAnchorOffsets = KBTS__POINTER_AFTER(kbts_u16, BaseArray) + BaseIndex * MarkClassCount;
              kbts__mark_info MarkInfo = kbts__GetMarkInfo(Adjust, Adjust->MarkArrayOffset, Cover.Index);

              kbts_u16 BaseAnchorOffset = BaseAnchorOffsets[MarkInfo.Record->Class];
              if(BaseAnchorOffset)
              {
                kbts__anchor *BaseAnchor = KBTS__POINTER_OFFSET(kbts__anchor, BaseArray, BaseAnchorOffset);

                /* From the Microsoft docs:
                     When a mark is combined with a given base, the mark placement is adjusted so that the mark anchor is
                   aligned with the base anchor for the applicable mark class. Placement of the base glyph and advances of
                   both glyphs are not affected.
                */

                kbts_s32 NewOffsetX = kbts__Glyph(Storage, BaseGlyph)->OffsetX - AdvanceSinceBaseX + (BaseAnchor->X - MarkInfo.Anchor->X);
                kbts_s32 NewOffsetY = kbts__Glyph(Storage, BaseGlyph)->OffsetY - AdvanceSinceBaseY + (BaseAnchor->Y - MarkInfo.Anchor->Y);

                kbts__AttachGlyph(Storage, BaseGlyph, CurrentGlyph, NewOffsetX, NewOffsetY);

                Result = 1;
              }
            }
          }
        }
      } break;

      case 5:
      {
        kbts__mark_to_ligature_attachment *Adjust = (kbts__mark_to_ligature_attachment *)Base;
        kbts_s32 AdvanceSinceBaseX = 0;
        kbts_s32 AdvanceSinceBaseY = 0;
        kbts_u32 BaseClasses = (1 | (1 << KBTS__GLYPH_CLASS_BASE) | (1 << KBTS__GLYPH_CLASS_LIGATURE) | (1 << KBTS__GLYPH_CLASS_COMPONENT));
        kbts_glyph_ref LigatureGlyph = 0;
        for(kbts_glyph_ref PrevGlyph = kbts__PrevGlyphRef(Storage, CurrentGlyph);
            kbts__GlyphIsValid(Storage, PrevGlyph);
            PrevGlyph = kbts__PrevGlyphRef(Storage, PrevGlyph))
        {
          AdvanceSinceBaseX += kbts__Glyph(Storage, PrevGlyph)->AdvanceX;
          AdvanceSinceBaseY += kbts__Glyph(Storage, PrevGlyph)->AdvanceY;

          if(!kbts__SkipGlyph(kbts__Glyph(Storage, PrevGlyph), Lookup, RegularSkipFlags, SkipUnicodeFlags) && ((1 << kbts__Glyph(Storage, PrevGlyph)->Classes.Class) & BaseClasses))
          {
            LigatureGlyph = PrevGlyph;

            break;
          }
        }

        if(LigatureGlyph)
        {
          kbts__ligature_array *LigatureArray = KBTS__POINTER_OFFSET(kbts__ligature_array, Adjust, Adjust->LigatureArrayOffset);

          kbts__cover_glyph_result LigatureCover = kbts__CoverGlyph(KBTS__POINTER_OFFSET(kbts__coverage, Adjust, Adjust->LigatureCoverageOffset),
                                                                    kbts__Glyph(Storage, LigatureGlyph)->Id);

          kbts_un LigatureIndex = LigatureCover.Index;
          kbts_b32 ValidLigature = LigatureCover.Valid;

          if(ValidLigature)
          {
            kbts__ligature_attach *LigatureAttach = kbts__GetLigatureAttach(LigatureArray, LigatureIndex);
            kbts__mark_info MarkInfo = kbts__GetMarkInfo(Adjust, Adjust->MarkArrayOffset, Cover.Index);
            kbts_un LigatureComponentIndexPlusOne = kbts__Glyph(Storage, CurrentGlyph)->LigatureComponentIndexPlusOne;

            if(kbts__Glyph(Storage, CurrentGlyph)->LigatureUid != kbts__Glyph(Storage, LigatureGlyph)->Uid)
            {
              // If the mark is not yet attached to the ligature, attach it now.
              // Apparently, in this case, the mark should be considered part of the _last_ component of the ligature.
              LigatureComponentIndexPlusOne = LigatureAttach->Count;
            }

            if(LigatureComponentIndexPlusOne <= LigatureAttach->Count)
            {
              kbts_un AnchorIndex = LigatureComponentIndexPlusOne;
              if(AnchorIndex)
              {
                AnchorIndex -= 1;
              }
              else
              {
                AnchorIndex = LigatureAttach->Count - 1;
              }

              kbts__anchor *LigatureAnchor = kbts__GetLigatureAttachAnchor(Adjust, LigatureAttach, MarkInfo.Record->Class, AnchorIndex);

              kbts_s32 NewOffsetX = kbts__Glyph(Storage, LigatureGlyph)->OffsetX - AdvanceSinceBaseX + (LigatureAnchor->X - MarkInfo.Anchor->X);
              kbts_s32 NewOffsetY = kbts__Glyph(Storage, LigatureGlyph)->OffsetY - AdvanceSinceBaseY + (LigatureAnchor->Y - MarkInfo.Anchor->Y);

              kbts__AttachGlyph(Storage, LigatureGlyph, CurrentGlyph, NewOffsetX, NewOffsetY);

              kbts__Glyph(Storage, CurrentGlyph)->LigatureComponentIndexPlusOne = (kbts_u16)LigatureComponentIndexPlusOne;
              kbts__Glyph(Storage, CurrentGlyph)->LigatureUid = kbts__Glyph(Storage, LigatureGlyph)->Uid;
              // I'm not sure why, but Harfbuzz only clears this in mark-to-ligature substitutions, and not in mark-to-base or
              // mark-to-mark.
              kbts__Glyph(Storage, CurrentGlyph)->AdvanceX = 0;
              kbts__Glyph(Storage, CurrentGlyph)->AdvanceY = 0;

              Result = 1;
            }
          }
        }
      } break;

      case 7:
      case 8:
      {
        const kbts__compiled_program *Program = kbts__CompiledProgram(Scratchpad, KBTS_SHAPING_TABLE_GPOS, LookupIndex, SubtableIndex);
        if(Program->FirstFiltered &&
           kbts__SkipGlyph(kbts__Glyph(Storage, CurrentGlyph), Lookup, SequenceSkipFlags, SkipUnicodeFlags))
          break;
        KB_PROFILE_CONTEXT((Lookup->Type == 8 ? 3 : 0) + Base[0] - 1);
        kbts__sequence_lookup_result SequenceLookup = kbts__MatchCompiledContext(Scratchpad, Storage, Lookup, Program, CurrentGlyph, SequenceSkipFlags, SkipUnicodeFlags);
        if(SequenceLookup.Matched)
        {
          Result = 1;

          KBTS__FOR(RecordIndex, 0, SequenceLookup.RecordCount)
          {
            kbts__sequence_lookup_record *Record = &SequenceLookup.Records[RecordIndex];
            kbts_un Distance;
            kbts_glyph_ref Target = kbts__SequenceTarget(Storage, Lookup, CurrentGlyph,
                Record->SequenceIndex, SequenceSkipFlags, SkipUnicodeFlags, &Distance);
            if(Target)
              kbts__DoAdjustment(Scratchpad, Config, Storage, Record->LookupListIndex,
                                 Target, StartIndex + Distance, RequestedSkipFlags);
          }

          OnePastLastGlyphIndex = StartIndex + SequenceLookup.InputSequenceCountIncludingSkippedGlyphs;
          OnePastLastGlyph = SequenceLookup.OnePastLastGlyphMatched;
        }
      } break;
      }

      kbts__SetLookupOnePastLastGlyph(Scratchpad, OnePastLastGlyphIndex + OnePastLastGlyphOffset, OnePastLastGlyph);
    }

  }

  KBTS_INSTRUMENT_FUNCTION_END;
  return Result;
}

/* A lookup's subtables are ordered alternatives, for roots and children alike.
 * Distinct contextual records call this separately and all remain observable. */
static kbts_b32 kbts__DoAdjustment(kbts_shape_scratchpad *Scratchpad, kbts_shape_config *Config,
    kbts_glyph_storage *Storage, kbts_un LookupIndex, kbts_glyph_ref CurrentGlyph,
    kbts_un StartIndex, kbts__skip_flags RequestedSkipFlags)
{
  const kbts__font_lookup *Descriptor = kbts__FontLookup(Config->Font, KBTS_SHAPING_TABLE_GPOS, LookupIndex);
  for(kbts_un Index = 0; Index < Descriptor->Filter.SubtableCount; ++Index)
  {
    const kbts__font_subtable *Subtable = kbts__FontSubtable(Config->Font, Descriptor, Index);
    if(kbts__DoSingleAdjustment(Scratchpad, Config, Storage, LookupIndex, Index,
                               &Descriptor->Filter, Subtable->Data, CurrentGlyph,
                               StartIndex, RequestedSkipFlags))
      return 1;
  }
  return 0;
}

typedef kbts_u32 kbts__mcm_sequence_state;
enum kbts__mcm_sequence_state_enum
{
  KBTS__MCM_SEQUENCE_STATE_NONE,
  KBTS__MCM_SEQUENCE_STATE_OUT,
  KBTS__MCM_SEQUENCE_STATE_IN,
};

typedef kbts_u32 kbts__hangul_syllable_type;
enum kbts__hangul_syllable_type_enum
{
  KBTS__HANGUL_SYLLABLE_TYPE_NONE,

  KBTS__HANGUL_SYLLABLE_TYPE_L,
  KBTS__HANGUL_SYLLABLE_TYPE_V,
  KBTS__HANGUL_SYLLABLE_TYPE_T,
  KBTS__HANGUL_SYLLABLE_TYPE_LV,
  KBTS__HANGUL_SYLLABLE_TYPE_LVT,

  KBTS__HANGUL_SYLLABLE_TYPE_COUNT,
};

typedef struct kbts__hangul_syllable_info
{
  kbts__hangul_syllable_type Type;
  kbts_u32 Composable;
} kbts__hangul_syllable_info;

static kbts__hangul_syllable_info kbts__HangulSyllableInfo(kbts_u32 Codepoint)
{
  kbts__hangul_syllable_info Result = KBTS__ZERO;

  if((Codepoint >= 0x1100) && (Codepoint <= 0x115F))
  {
    Result.Type = KBTS__HANGUL_SYLLABLE_TYPE_L;
    Result.Composable = (Codepoint < 0x1113);
  }
  else if((Codepoint >= 0x1160) && (Codepoint <= 0x11A7))
  {
    Result.Type = KBTS__HANGUL_SYLLABLE_TYPE_V;
    Result.Composable = ((Codepoint >= 0x1161) & (Codepoint <= 0x1175));
  }
  else if((Codepoint >= 0x11A8) && (Codepoint <= 0x11FF))
  {
    Result.Type = KBTS__HANGUL_SYLLABLE_TYPE_T;
    Result.Composable = (Codepoint < 0x11C3);
  }
  else if((Codepoint >= 0xA960) && (Codepoint <= 0xA97C))
  {
    Result.Type = KBTS__HANGUL_SYLLABLE_TYPE_L;
  }
  else if((Codepoint >= 0xAC00) && (Codepoint <= 0xD7A3))
  {
    Result.Type = KBTS__HANGUL_SYLLABLE_TYPE_LVT;

    if(!((Codepoint - 0xAC00) % 28))
    {
      Result.Type = KBTS__HANGUL_SYLLABLE_TYPE_LV;
    }
  }
  else if((Codepoint >= 0xD7B0) && (Codepoint <= 0xD7C6))
  {
    Result.Type = KBTS__HANGUL_SYLLABLE_TYPE_V;
  }
  else if((Codepoint >= 0xD7CB) && (Codepoint <= 0xD7FB))
  {
    Result.Type = KBTS__HANGUL_SYLLABLE_TYPE_T;
  }

  return Result;
}

KBTS_INLINE kbts_u32 kbts__SyllabicClassIsConsonant(kbts_indic_syllabic_class Class)
{
  kbts_u32 Result =
    KBTS__IN_SET(Class, KBTS__SET32((KBTS_INDIC_SYLLABIC_CLASS_CONSONANT)(KBTS_INDIC_SYLLABIC_CLASS_RA)(KBTS_INDIC_SYLLABIC_CLASS_CONSONANT_WITH_STACKER)(KBTS_INDIC_SYLLABIC_CLASS_CONSONANT_MEDIAL)));
  return Result;
}

enum
{
  KBTS__FEATURE_MATCH_FLAG_KA_JA = (1 << 0),
  KBTS__FEATURE_MATCH_FLAG_SSA_NYA = (1 << 1),
  KBTS__FEATURE_MATCH_FLAG_RA_HA_VA = (1 << 2),
  KBTS__FEATURE_MATCH_FLAG_YA = (1 << 3),
  KBTS__FEATURE_MATCH_FLAG_CONSONANT = (1 << 4),
  KBTS__FEATURE_MATCH_FLAG_NUKTA = (1 << 5),
  KBTS__FEATURE_MATCH_FLAG_HALANT = (1 << 6),
  KBTS__FEATURE_MATCH_FLAG_ZWJ = (1 << 7),
  KBTS__FEATURE_MATCH_FLAG_ZWNJ = (1 << 8),
  KBTS__FEATURE_MATCH_FLAG_INITIAL = (1 << 10),
  KBTS__FEATURE_MATCH_FLAG_POST_BASE = (1 << 11),

  KBTS__FEATURE_MATCH_FLAG_RPHF = KBTS__FEATURE_FLAG0(rphf),
};
#  define KBTS__FEATURE_MATCH_MASK(W0, W1, W2, W3) (((kbts_u64)(W0) << 48) | ((kbts_u64)(W1) << 32) | ((kbts_u64)(W0) << 16) | (kbts_u64)(W0))
#  define KBTS__FEATURE_MATCH_MASK_AKHN KBTS__FEATURE_MATCH_MASK(0, KBTS__FEATURE_MATCH_FLAG_KA_JA, KBTS__FEATURE_MATCH_FLAG_HALANT, KBTS__FEATURE_MATCH_FLAG_SSA_NYA)
#  define KBTS__FEATURE_MATCH_MASK_BLWF_PRE KBTS__FEATURE_MATCH_MASK(0, 0, KBTS__FEATURE_MATCH_FLAG_RA_HA_VA | KBTS__FEATURE_MATCH_FLAG_INITIAL, KBTS__FEATURE_MATCH_FLAG_HALANT)
#  define KBTS__FEATURE_MATCH_MASK_BLWF_PRE_OK KBTS__FEATURE_MATCH_MASK(0, 0, KBTS__FEATURE_MATCH_FLAG_RA_HA_VA, KBTS__FEATURE_MATCH_FLAG_HALANT)
#  define KBTS__FEATURE_MATCH_MASK_BLWF_POST KBTS__FEATURE_MATCH_MASK(0, 0, KBTS__FEATURE_MATCH_FLAG_HALANT, KBTS__FEATURE_MATCH_FLAG_RA_HA_VA)
#  define KBTS__FEATURE_MATCH_MASK_CJCT KBTS__FEATURE_MATCH_MASK(0, KBTS__FEATURE_MATCH_FLAG_CONSONANT, KBTS__FEATURE_MATCH_FLAG_HALANT, KBTS__FEATURE_MATCH_FLAG_CONSONANT)
#  define KBTS__FEATURE_MATCH_MASK_HALF KBTS__FEATURE_MATCH_MASK(KBTS__FEATURE_MATCH_FLAG_CONSONANT | KBTS__FEATURE_MATCH_FLAG_RPHF | KBTS__FEATURE_MATCH_FLAG_POST_BASE, KBTS__FEATURE_MATCH_FLAG_HALANT, KBTS__FEATURE_MATCH_FLAG_ZWNJ, KBTS__FEATURE_MATCH_FLAG_CONSONANT)
#  define KBTS__FEATURE_MATCH_MASK_HALF_OK KBTS__FEATURE_MATCH_MASK(KBTS__FEATURE_MATCH_FLAG_CONSONANT, KBTS__FEATURE_MATCH_FLAG_HALANT, 0, 0)
#  define KBTS__FEATURE_MATCH_MASK_NUKT KBTS__FEATURE_MATCH_MASK(0, 0, KBTS__FEATURE_MATCH_FLAG_CONSONANT, KBTS__FEATURE_MATCH_FLAG_HALANT)
#  define KBTS__FEATURE_MATCH_MASK_PSTF KBTS__FEATURE_MATCH_MASK(0, 0, KBTS__FEATURE_MATCH_FLAG_HALANT | KBTS__FEATURE_MATCH_FLAG_INITIAL, KBTS__FEATURE_MATCH_FLAG_YA)
#  define KBTS__FEATURE_MATCH_MASK_VATU KBTS__FEATURE_MATCH_MASK(0, KBTS__FEATURE_MATCH_FLAG_CONSONANT, KBTS__FEATURE_MATCH_FLAG_HALANT, KBTS__FEATURE_MATCH_FLAG_RA_HA_VA)

typedef kbts_u32 kbts__substitution_result_flags;
enum kbts__substitution_result_flags_enum
{
  KBTS__SUBSTITUTION_RESULT_FLAG_MATCHED_SUBSTITUTION = (1 << 0),
  KBTS__SUBSTITUTION_RESULT_FLAG_TRIED_TO_INSERT_WHILE_CHECK_ONLY = (1 << 1),
  KBTS__SUBSTITUTION_RESULT_FLAG_MATCHED_SEQUENCE = (1 << 2),
};

static void kbts__BeginLookupApplication(kbts_shape_scratchpad *Scratchpad, kbts_glyph_ref Glyph)
{
  Scratchpad->LookupOnePastLastGlyph = kbts__NextGlyphRef(Scratchpad->Storage, Glyph);
  Scratchpad->LookupOnePastLastGlyphIndex = 0;
}
static kbts_glyph_ref kbts__EndLookupApplication(kbts_shape_scratchpad *Scratchpad)
{
  kbts_glyph_ref Result = Scratchpad->LookupOnePastLastGlyph;
  return Result;
}

static kbts_un kbts__CurrentBakedFeatureStageIndex(kbts_shape_scratchpad *Scratchpad)
{
  kbts_un Result = Scratchpad->FeatureStagesRead - 1;
  return Result;
}

KBTS_INLINE kbts_u16 kbts__NextGlyphUid(kbts_shape_scratchpad *Scratchpad)
{
  kbts_u16 Result = (kbts_u16)++Scratchpad->NextGlyphUid;
  return Result;
}

static kbts_u32 kbts__ApplyCompiledFusion(kbts_shape_scratchpad *Scratchpad, kbts_glyph_storage *Storage,
    const kbts__compiled_program *Program, const kbts__compiled_outcome *Outcome,
    kbts_glyph_ref CurrentGlyph, kbts_un SequentialLookupIndex, kbts_u16 FeatureValue,
    kbts_u32 GeneratedGlyphFlags, kbts_glyph_ref *OnePast)
{
  kbts_shape_config *Config = Scratchpad->Config;
  kbts_font *Font = Config->Font;
  kbts_u32 Count = 1;
  kbts_glyph_ref Last = CurrentGlyph;
  kbts_u32 Filter = Config->SequentialLookups[SequentialLookupIndex].GlyphFilter;
  while(Count < Outcome->FusionCount)
  {
    kbts_glyph_ref Next = kbts__NextGlyphRef(Storage, Last);
    kbts_u16 NextFeatureValue;
    if(!kbts__GlyphIsValid(Storage, Next) ||
       Next != Scratchpad->CompiledGlyphs[Program->MaxBacktrack + Count] ||
       !kbts__GsubLookupEnabled(Config, kbts__Glyph(Storage, Next), (kbts_u32)SequentialLookupIndex, Filter, &NextFeatureValue) ||
       NextFeatureValue != FeatureValue)
      break;
    Last = Next;
    ++Count;
  }
  const kbts_u16 *Ids = Font->CompiledContexts->FusionGlyphs + Outcome->FusionOffset;
  kbts_glyph_ref Changed = CurrentGlyph;
  for(kbts_u32 Index = 0; Index < Count; ++Index)
  {
    kbts__GsubMutate(Scratchpad, Font, kbts__Glyph(Storage, Changed), Ids[Index],
                     GeneratedGlyphFlags | KBTS_GLYPH_FLAG_GENERATED_BY_GSUB);
    Changed = kbts__NextGlyphRef(Storage, Changed);
  }
  *OnePast = kbts__NextGlyphRef(Storage, Last);
  KB_PROFILE_WORK(KBW_SUBSTITUTION_MATCHES, Count);
  return Count;
}

static kbts__substitution_result_flags kbts__DoSubstitution(kbts_shape_scratchpad *Scratchpad, kbts_shape_config *Config, kbts_glyph_storage *Storage,
                                                            kbts_un SequentialLookupIndex, kbts_u16 FeatureValue,
                                                            kbts__gsub_frame *Frames, kbts_un *FrameCount_,
                                                            int CheckOnly, kbts__skip_flags RequestedSkipFlags, kbts_u32 GeneratedGlyphFlags, int AllowFusion)
{
  KB_PROFILE_SCOPE(KBP_SUBSTITUTE);
  kbts__substitution_result_flags Result = 0;
  kbts_font *Font = Config->Font;
  enum {SkipUnicodeFlags = KBTS_UNICODE_FLAG_DEFAULT_IGNORABLE};
  kbts__skip_flags RegularSkipFlags = KBTS__SKIP_FLAGS_GSUB_REGULAR(RequestedSkipFlags);
  kbts__skip_flags SequenceSkipFlags = KBTS__SKIP_FLAGS_GSUB_SEQUENCE(RequestedSkipFlags);
  GeneratedGlyphFlags |= KBTS_GLYPH_FLAG_GENERATED_BY_GSUB;

  kbts_un FrameCount = *FrameCount_;
  kbts__gsub_frame *Frame = &Frames[FrameCount - 1];
  if(!kbts__GlyphIsValid(Storage, Frame->InputGlyph))
  {
    *FrameCount_ = FrameCount - 1;
    return Result;
  }

  const kbts__font_lookup *Descriptor = kbts__FontLookup(Font, KBTS_SHAPING_TABLE_GSUB, Frame->LookupIndex);
  const kbts__unpacked_lookup *Lookup = &Descriptor->Filter;

  kbts_glyph_ref CurrentGlyph = Frame->InputGlyph;

  while(Frame->SubtableIndex < Lookup->SubtableCount)
  {
    KB_PROFILE_WORK(KBW_SUBTABLE_VISITS, 1);
    kbts_u16 *Subtable = kbts__FontSubtable(Font, Descriptor, Frame->SubtableIndex)->Data;

    if(kbts__GlyphIncludedInLookupSubtable(Scratchpad, KBTS_SHAPING_TABLE_GSUB, Frame->LookupIndex, Frame->SubtableIndex, kbts__Glyph(Storage, CurrentGlyph)))
    {
      kbts__cover_glyph_result Cover = KBTS__ZERO;
      Cover.Valid = kbts__GlyphPassesLookupFilter(kbts__Glyph(Storage, CurrentGlyph), Lookup);
      if(Cover.Valid && Lookup->Type != 5 && Lookup->Type != 6 && kbts__GsubLookupBeginsWithCoverage(Lookup->Type, Subtable[0]))
      {
        kbts__coverage *Coverage = KBTS__POINTER_OFFSET(kbts__coverage, Subtable, Subtable[1]);
        Cover = kbts__CoverGlyph(Coverage, kbts__Glyph(Storage, CurrentGlyph)->Id);
      }

      if(Cover.Valid)
      {
        kbts_un OnePastLastGlyphOffset = 1;
        kbts_glyph_ref OnePastLastGlyph = kbts__NextGlyphRef(Storage, CurrentGlyph);

        if((Lookup->Type == 5) || (Lookup->Type == 6))
        {
          const kbts__compiled_program *Program = kbts__CompiledProgram(Scratchpad, KBTS_SHAPING_TABLE_GSUB, Frame->LookupIndex, Frame->SubtableIndex);
          if(Program->FirstFiltered &&
             kbts__SkipGlyph(kbts__Glyph(Storage, CurrentGlyph), Lookup, SequenceSkipFlags, SkipUnicodeFlags))
          {
            ++Frame->SubtableIndex;
            continue;
          }
          KB_PROFILE_CONTEXT((Lookup->Type == 6 ? 3 : 0) + Subtable[0] - 1);
          kbts__sequence_lookup_result SequenceLookup = kbts__MatchCompiledContext(Scratchpad, Storage, Lookup, Program, CurrentGlyph, SequenceSkipFlags, SkipUnicodeFlags);

          if(SequenceLookup.Matched)
          {
            Frame->Records = SequenceLookup.Records;
            Frame->RecordCount = (kbts_u16)SequenceLookup.RecordCount;
            Frame->RecordIndex = 0;

            Frame->SubtableIndex = 0xFFFE;

            OnePastLastGlyphOffset = SequenceLookup.InputSequenceCountIncludingSkippedGlyphs;
            OnePastLastGlyph = SequenceLookup.OnePastLastGlyphMatched;
            const kbts__compiled_outcome *Outcome = SequenceLookup.Outcome;
            if(AllowFusion && !CheckOnly && FrameCount == 1 && Outcome->FusionCount)
            {
              OnePastLastGlyphOffset = kbts__ApplyCompiledFusion(Scratchpad, Storage, Program, Outcome,
                  CurrentGlyph, SequentialLookupIndex, FeatureValue, GeneratedGlyphFlags, &OnePastLastGlyph);
              Frame->RecordCount = 0;
              Result |= KBTS__SUBSTITUTION_RESULT_FLAG_MATCHED_SUBSTITUTION;
            }
          }
        }
        else
        {
          // Do single substitution.

          switch(Lookup->Type)
          {
          case 1:
          {
            Result |= KBTS__SUBSTITUTION_RESULT_FLAG_MATCHED_SUBSTITUTION;
            KB_PROFILE_WORK(KBW_SUBSTITUTION_MATCHES, 1);

            if(!CheckOnly)
            {
              kbts__single_substitution *Subst = (kbts__single_substitution *)Subtable;

              kbts_u16 NewId = 0;
              if(Subst->Format == 1)
              {
                // From the Microsoft docs:
                // "Addition of deltaGlyphID is modulo 65536."
                NewId = (kbts__Glyph(Storage, CurrentGlyph)->Id + (kbts_u32)Subst->DeltaOrCount.DeltaGlyphId) & 0xFFFF;
              }
              else if(Subst->Format == 2)
              {
                kbts_u16 *SubstituteGlyphIds = KBTS__POINTER_AFTER(kbts_u16, Subst);
                NewId = SubstituteGlyphIds[Cover.Index];
              }

              kbts__GsubMutate(Scratchpad, Font, kbts__Glyph(Storage, CurrentGlyph), NewId, GeneratedGlyphFlags);
            }
          } break;

          case 2:
          {
            kbts__multiple_substitution *Subst = (kbts__multiple_substitution *)Subtable;
            kbts__sequence *Sequence = kbts__GetSequence(Subst, Cover.Index);
            Result |= KBTS__SUBSTITUTION_RESULT_FLAG_MATCHED_SUBSTITUTION;
            KB_PROFILE_WORK(KBW_SUBSTITUTION_MATCHES, 1);

            if(!CheckOnly)
            {
              kbts_u16 *SubstGlyphIds = KBTS__POINTER_AFTER(kbts_u16, Sequence);

              kbts_un GrowCount = Sequence->GlyphCount - 1;

              if(Sequence->GlyphCount)
              {
                kbts_glyph OriginalGlyph = *kbts__Glyph(Storage, CurrentGlyph);
                kbts_glyph_ref LastInsert = CurrentGlyph;

                KBTS__FOR(SubstGlyphIndex, 0, Sequence->GlyphCount)
                {
                  kbts_u32 NewGlyphFlags = GeneratedGlyphFlags | KBTS_GLYPH_FLAG_MULTIPLE_SUBSTITUTION;
                  kbts_u16 NewGlyphId = kbts__ReadU16Unaligned(&SubstGlyphIds[SubstGlyphIndex]);

                  kbts_glyph_ref NewGlyph = CurrentGlyph;
                  if(SubstGlyphIndex)
                  {
                    NewGlyph = kbts__InsertGlyphAfter(Storage, LastInsert, &OriginalGlyph);

                    if(!NewGlyph)
                    {
                      Scratchpad->Error = KBTS_SHAPE_ERROR_OUT_OF_MEMORY;
                      return Result;
                    }

                    kbts__Glyph(Storage, NewGlyph)->Uid = kbts__NextGlyphUid(Scratchpad);
                  }
                  else
                  {
                    NewGlyphFlags |= KBTS_GLYPH_FLAG_FIRST_IN_MULTIPLE_SUBSTITUTION;
                  }

                  kbts__GsubMutate(Scratchpad, Font, kbts__Glyph(Storage, NewGlyph), NewGlyphId, GeneratedGlyphFlags | NewGlyphFlags);

                  LastInsert = NewGlyph;
                }

                OnePastLastGlyph = kbts__NextGlyphRef(Storage, LastInsert);
              }
              else
              {
                KBTS__FOR(FrameIndex, 0, FrameCount)
                {
                  if(Frames[FrameIndex].InputGlyph == CurrentGlyph)
                    Frames[FrameIndex].InputGlyph = 0;
                }
                kbts__FreeGlyph(Scratchpad, Storage, CurrentGlyph);
              }

              OnePastLastGlyphOffset = 1 + GrowCount;

              // Shift other frames' input cursors.
              KBTS__FOR(FrameIndex, 0, FrameCount - 1)
              {
                kbts__gsub_frame *OtherFrame = &Frames[FrameIndex];

                if(OtherFrame->StartIndex > Frame->StartIndex)
                {
                  OtherFrame->StartIndex = (kbts_u16)(OtherFrame->StartIndex + GrowCount);
                }
              }
            }
            else if(Sequence->GlyphCount > 1)
            {
              Result |= KBTS__SUBSTITUTION_RESULT_FLAG_TRIED_TO_INSERT_WHILE_CHECK_ONLY;
            }
          } break;

          case 3:
          {
            Result |= KBTS__SUBSTITUTION_RESULT_FLAG_MATCHED_SUBSTITUTION;
            KB_PROFILE_WORK(KBW_SUBSTITUTION_MATCHES, 1);

            if(!CheckOnly)
            {
              kbts__alternate_substitution *Subst = (kbts__alternate_substitution *)Subtable;
              kbts__alternate_set *Set = kbts__GetAlternateSet(Subst, Cover.Index);
              kbts_u16 *AltGlyphIds = KBTS__POINTER_AFTER(kbts_u16, Set);

              kbts_un AlternateIndex = FeatureValue - 1;
              if(AlternateIndex >= Set->GlyphCount)
              {
                AlternateIndex = 0;
              }

              kbts_u16 NewId = AltGlyphIds[AlternateIndex];
              kbts__GsubMutate(Scratchpad, Font, kbts__Glyph(Storage, CurrentGlyph), NewId, GeneratedGlyphFlags);
            }
          } break;

          case 4:
          {
            KBTS_INSTRUMENT_BLOCK_BEGIN(GSUB_Ligature);

            kbts__ligature_substitution *Subst = (kbts__ligature_substitution *)Subtable;
            kbts__ligature_set *Set = kbts__GetLigatureSet(Subst, Cover.Index);

            KBTS__FOR(LigatureIndex, 0, Set->Count)
            {
              KB_PROFILE_WORK(KBW_RULE_VISITS, 1);
              kbts__ligature *Ligature = kbts__GetLigature(Set, LigatureIndex);
              kbts_u16 *ComponentIds = KBTS__POINTER_AFTER(kbts_u16, Ligature);
              kbts_un ComponentCount = Ligature->ComponentCount;

              kbts_un MatchingGlyphCount = 1;

              {
                KB_PROFILE_SCOPE(KBP_INPUT_GATHER);
                kbts_glyph_ref LigatureGlyphCursor = kbts__NextGlyphRef(Storage, CurrentGlyph);
                while(kbts__GlyphIsValid(Storage, LigatureGlyphCursor) &&
                      (MatchingGlyphCount < Ligature->ComponentCount))
                {
                  KB_PROFILE_WORK(KBW_INPUT_GLYPHS, 1);
                  KB_PROFILE_WORK(KBW_COMPARE_WORDS, 1);
                  // A ligature may contain an explicit ZWJ, which SkipGlyph() would probably skip.
                  // The expected behavior in that case is to assume the font designer knows what they are doing
                  // and match the ZWJ.
                  if(kbts__Glyph(Storage, LigatureGlyphCursor)->Id == kbts__ReadU16Unaligned(&ComponentIds[MatchingGlyphCount - 1]))
                  {
                    MatchingGlyphCount += 1;
                  }
                  else if(!kbts__SkipGlyph(kbts__Glyph(Storage, LigatureGlyphCursor), Lookup, RegularSkipFlags, SkipUnicodeFlags))
                  {
                    break;
                  }

                  LigatureGlyphCursor = kbts__NextGlyphRef(Storage, LigatureGlyphCursor);
                }
              }

              if(MatchingGlyphCount == ComponentCount)
              {
                Result |= KBTS__SUBSTITUTION_RESULT_FLAG_MATCHED_SUBSTITUTION;
                KB_PROFILE_WORK(KBW_SUBSTITUTION_MATCHES, 1);

                if(!CheckOnly)
                {
                  kbts_u32 LigatureUid = kbts__NextGlyphUid(Scratchpad);

                  { // For glyphs that aren't part of the ligature, store which component it is attached to.
                    // For glyphs that _are_, eat them.
                    kbts_un LigatureGlyphCount = 1;
                    kbts_un GrowCount = 0;

                    // Consider this example:
                    // ABCD -> DB (A and C form a ligature)
                    // DB -> DEB (E is inserted in whatever way)
                    // DEB -> FB (D and E form a ligature)
                    // In that case, 'B' was supposed to be attached to D, but D does not exist anymore.
                    // To handle this case, we go through the ligature, counting the total flattened components
                    // and the ligature info of the last glyph that is part of the ligature.
                    // Then, all trailing marks get are re-attached to the new ligature, and their component indices
                    // are adjusted to take into account the new (flattened) component count.
                    // (I say 'flattened' here because a previous ligature glyph counts as multiple components.)
                    kbts_un PreviousLigatureUid = kbts__Glyph(Storage, CurrentGlyph)->LigatureUid;
                    kbts_un PreviousComponentCount = kbts__Glyph(Storage, CurrentGlyph)->LigatureComponentCount;
                    if(!PreviousComponentCount)
                    {
                      PreviousComponentCount = 1;
                    }
                    kbts_un RunningComponentCount = PreviousComponentCount;

                    kbts_glyph_ref LigatureGlyphCursor = kbts__NextGlyphRef(Storage, CurrentGlyph);
                    while(kbts__GlyphIsValid(Storage, LigatureGlyphCursor) && (LigatureGlyphCount < ComponentCount))
                    {
                      kbts_glyph_ref Next = kbts__NextGlyphRef(Storage, LigatureGlyphCursor);

                      if(kbts__Glyph(Storage, LigatureGlyphCursor)->Id == ComponentIds[LigatureGlyphCount - 1])
                      {
                        PreviousLigatureUid = kbts__Glyph(Storage, LigatureGlyphCursor)->LigatureUid;
                        PreviousComponentCount = kbts__Glyph(Storage, LigatureGlyphCursor)->LigatureComponentCount;
                        if(!PreviousComponentCount)
                        {
                          PreviousComponentCount = 1;
                        }
                        RunningComponentCount += PreviousComponentCount;

                        LigatureGlyphCount += 1;

                        // Eat!
                        KBTS__FOR(FrameIndex, 0, FrameCount)
                        {
                          if(Frames[FrameIndex].InputGlyph == LigatureGlyphCursor)
                            Frames[FrameIndex].InputGlyph = 0;
                        }
                        kbts__FreeGlyph(Scratchpad, Storage, LigatureGlyphCursor);
                        GrowCount -= 1;
                      }
                      else if(kbts__SkipGlyph(kbts__Glyph(Storage, LigatureGlyphCursor), Lookup, RegularSkipFlags, SkipUnicodeFlags))
                      {
                        kbts__Glyph(Storage, LigatureGlyphCursor)->LigatureComponentIndexPlusOne = (kbts_u16)LigatureGlyphCount;
                        kbts__Glyph(Storage, LigatureGlyphCursor)->LigatureUid = (kbts_u16)LigatureUid;
                        kbts__Glyph(Storage, LigatureGlyphCursor)->LigatureComponentCount = (kbts_u16)ComponentCount;
                      }
                      else
                      {
                        break;
                      }

                      LigatureGlyphCursor = Next;
                    }

                    OnePastLastGlyph = kbts__NextGlyphRef(Storage, CurrentGlyph);

                    // Re-attach trailing marks that were part of a component ligature to the new ligature.
                    if(PreviousLigatureUid)
                    {
                      kbts_un DeltaComponentCount = RunningComponentCount - PreviousComponentCount;

                      while(kbts__GlyphIsValid(Storage, LigatureGlyphCursor))
                      {
                        kbts_un PreviousComponentIndexPlusOne = kbts__Glyph(Storage, LigatureGlyphCursor)->LigatureComponentIndexPlusOne;

                        if((kbts__Glyph(Storage, LigatureGlyphCursor)->Classes.Class == KBTS__GLYPH_CLASS_MARK) &&
                           (kbts__Glyph(Storage, LigatureGlyphCursor)->LigatureUid == PreviousLigatureUid) &&
                           PreviousComponentIndexPlusOne)
                        {
                          if(PreviousComponentIndexPlusOne > PreviousComponentCount)
                          {
                            PreviousComponentIndexPlusOne = PreviousComponentCount;
                          }

                          kbts_un NewComponentIndexPlusOne = PreviousComponentIndexPlusOne + DeltaComponentCount;

                          kbts__Glyph(Storage, LigatureGlyphCursor)->LigatureUid = (kbts_u16)LigatureUid;
                          kbts__Glyph(Storage, LigatureGlyphCursor)->LigatureComponentIndexPlusOne = (kbts_u16)NewComponentIndexPlusOne;
                        }
                        else
                        {
                          break;
                        }

                        LigatureGlyphCursor = kbts__NextGlyphRef(Storage, LigatureGlyphCursor);
                      }
                    }

                    // Update frame input cursors.
                    KBTS__FOR(FrameIndex, 0, FrameCount - 1)
                    {
                      kbts__gsub_frame *OtherFrame = &Frames[FrameIndex];

                      if(OtherFrame->StartIndex >= Frame->StartIndex)
                      {
                        OtherFrame->StartIndex += (kbts_u16)GrowCount;
                      }
                    }
                  }

                  // Currently, we only take the main glyph's config into account while making the ligature's config.
                  // Maybe we should merge all of the components' configs into one instead?
                  kbts__GsubMutate(Scratchpad, Font, kbts__Glyph(Storage, CurrentGlyph), Ligature->Glyph, GeneratedGlyphFlags | KBTS_GLYPH_FLAG_LIGATURE);
                  kbts__Glyph(Storage, CurrentGlyph)->Uid = (kbts_u16)LigatureUid;
                  kbts__Glyph(Storage, CurrentGlyph)->LigatureUid = (kbts_u16)LigatureUid;
                  kbts__Glyph(Storage, CurrentGlyph)->LigatureComponentCount = (kbts_u16)ComponentCount;

                  // Harfbuzz does this, because Uniscribe does this, and so we do the same. Sigh.
                  kbts__Glyph(Storage, CurrentGlyph)->Flags &= ~KBTS_GLYPH_FLAG_MULTIPLE_SUBSTITUTION;
                }

                break;
              }
            }

            KBTS_INSTRUMENT_BLOCK_END(GSUB_Ligature);
          } break;

          case 8:
          {
            kbts__reverse_chain_substitution *Subst = (kbts__reverse_chain_substitution *)Subtable;
            kbts__unpacked_reverse_chain_substitution Unpacked = kbts__UnpackReverseChainSubstitution(Subst, 0);

            // :BoundsChecking
            if(Cover.Index < Unpacked.GlyphCount)
            {
              // Should we use regular or sequence skip flags here?
              kbts_b32 BacktrackMatch = kbts__MatchCoverageSequence(Storage, Lookup, RegularSkipFlags, SkipUnicodeFlags, Subst, Unpacked.BacktrackCoverageOffsets, Unpacked.BacktrackCount,
                                                                                kbts__PrevGlyphRef(Storage, CurrentGlyph), 1
                                                                                KB_PROFILE_ONLY(, KBP_BACKTRACK_GATHER, KBW_BACKTRACK_GLYPHS));
              kbts_b32 LookaheadMatch = kbts__MatchCoverageSequence(Storage, Lookup, RegularSkipFlags, SkipUnicodeFlags, Subst, Unpacked.LookaheadCoverageOffsets, Unpacked.LookaheadCount,
                                                                                kbts__NextGlyphRef(Storage, CurrentGlyph), 0
                                                                                KB_PROFILE_ONLY(, KBP_LOOKAHEAD_GATHER, KBW_LOOKAHEAD_GLYPHS));
              if(BacktrackMatch && LookaheadMatch)
              {
                Result |= KBTS__SUBSTITUTION_RESULT_FLAG_MATCHED_SUBSTITUTION;
                KB_PROFILE_WORK(KBW_SUBSTITUTION_MATCHES, 1);
                if(!CheckOnly)
                  kbts__GsubMutate(Scratchpad, Font, kbts__Glyph(Storage, CurrentGlyph), Unpacked.SubstituteGlyphIds[Cover.Index], GeneratedGlyphFlags);
              }
            }
          } break;
          }

          if(Result & KBTS__SUBSTITUTION_RESULT_FLAG_MATCHED_SUBSTITUTION)
          {
            Frame->SubtableIndex = 0xFFFE;

            // From the Microsoft docs:
            // To move to the “next” glyph, the client skips all the glyphs that participated in the lookup operation:
            // glyphs that were substituted/positioned as well as any other glyphs in the matched input sequence.
          }
        }

        kbts__SetLookupOnePastLastGlyph(Scratchpad, Frame->StartIndex + OnePastLastGlyphOffset, OnePastLastGlyph);
      }
    }

    Frame->SubtableIndex += 1;
  }

  if(Frame->InputGlyph && Frame->RecordIndex < Frame->RecordCount)
  {
    kbts__sequence_lookup_record *SequenceRecord = &Frame->Records[Frame->RecordIndex++];

    if(FrameCount < KBTS_LOOKUP_STACK_SIZE)
    {
      kbts__gsub_frame NewFrame = KBTS__ZERO;
      NewFrame.LookupIndex = SequenceRecord->LookupListIndex;
      kbts_un Distance;
      NewFrame.InputGlyph = kbts__SequenceTarget(Storage, Lookup, Frame->InputGlyph,
          SequenceRecord->SequenceIndex, SequenceSkipFlags, SkipUnicodeFlags, &Distance);
      NewFrame.StartIndex = Frame->StartIndex + Distance;

      Frames[FrameCount++] = NewFrame;
    }
  }

  // Only actually pop the frame if we haven't pushed anything else in the meantime.
  if(Frame == &Frames[FrameCount - 1])
  {
    FrameCount -= 1;
  }

  *FrameCount_ = FrameCount;

  return Result;
}


static int kbts__WouldSubstitute(kbts_shape_scratchpad *Scratchpad, kbts_shape_config *Config, kbts_glyph_storage *Storage,
                                     kbts_lookup_list *LookupList,
                                     kbts__gsub_frame *Frames, kbts__feature *Feature, kbts__skip_flags SkipFlags,
                                     kbts_glyph *Glyphs, kbts_un GlyphCount)
{
  int Result = 0;
  KBTS_ASSERT(GlyphCount <= 4);
  if(!GlyphCount) return Result;

  kbts_glyph Scratch[4];
  kbts__glyph_link Links[4];
  kbts_u32 FreeSlots[4];
  kbts_glyph_storage Temporary = KBTS__ZERO;
  Temporary.Glyphs = Scratch;
  Temporary.Links = Links;
  Temporary.FreeSlots = FreeSlots;
  Temporary.Count = Temporary.LiveCount = (kbts_u32)GlyphCount;
  Temporary.Tail = (kbts_u32)GlyphCount - 1;
  Temporary.End = KBTS__GLYPH_END;
  Temporary.Ordered = 1;
  Temporary.Capacity = 4;
  KBTS__FOR(GlyphIndex, 0, GlyphCount)
  {
    Scratch[GlyphIndex] = Glyphs[GlyphIndex];
    Scratch[GlyphIndex].Ref = (kbts_glyph_ref)GlyphIndex + 1;
    Scratch[GlyphIndex].Bucketed = 0;
    Scratch[GlyphIndex].AttachGlyph = 0;
    Links[GlyphIndex].Prev = GlyphIndex ? (kbts_u32)GlyphIndex - 1 : KBTS__GLYPH_END;
    Links[GlyphIndex].Next = GlyphIndex + 1 < GlyphCount ? (kbts_u32)GlyphIndex + 1 : KBTS__GLYPH_END;
  }

  kbts_glyph_storage *SavedStorage = Scratchpad->Storage;
  kbts_glyph_ref SavedOnePast = Scratchpad->LookupOnePastLastGlyph;
  kbts_u32 SavedOnePastIndex = Scratchpad->LookupOnePastLastGlyphIndex;
  Scratchpad->Storage = Storage = &Temporary;

  kbts__iterate_lookups IterateLookups = kbts__IterateLookups(LookupList, Feature);
  while(kbts__NextLookup(&IterateLookups) && !Scratchpad->Error)
  {
    kbts_glyph_ref CurrentGlyph = kbts__FirstGlyph(Storage);
    while(kbts__GlyphIsValid(Storage, CurrentGlyph) && !Scratchpad->Error)
    {
      kbts__gsub_frame *Frame = &Frames[0];
      *Frame = KBTS__ZERO_TYPE(kbts__gsub_frame);
      Frame->LookupIndex = IterateLookups.LookupIndex;
      Frame->InputGlyph = CurrentGlyph;
      kbts_un FrameCount = 1;
      kbts__BeginLookupApplication(Scratchpad, CurrentGlyph);
      while(FrameCount && !Scratchpad->Error)
      {
        kbts__substitution_result_flags SubstitutionResult = kbts__DoSubstitution(Scratchpad, Config, Storage,
            0, 0, Frames, &FrameCount, 1, SkipFlags, 0, 0);
        if(SubstitutionResult & KBTS__SUBSTITUTION_RESULT_FLAG_MATCHED_SUBSTITUTION)
        {
          Result = 1;
          goto Done;
        }
      }
      CurrentGlyph = kbts__EndLookupApplication(Scratchpad);
    }
  }

Done:;
  Scratchpad->Storage = SavedStorage;
  Scratchpad->LookupOnePastLastGlyph = SavedOnePast;
  Scratchpad->LookupOnePastLastGlyphIndex = SavedOnePastIndex;
  return Result;
}

static kbts_glyph kbts__Substitute1(kbts_shape_scratchpad *Scratchpad, kbts_shape_config *Config, kbts_glyph_storage *ProbeStorage,
                                   kbts_lookup_list *LookupList, kbts__feature *Feature, kbts__skip_flags SkipFlags, kbts_glyph *Glyph)
{
  kbts_glyph Result = *Glyph;
  kbts_glyph_ref OriginalRef = Glyph->Ref;
  kbts_glyph_ref OriginalAttachment = Glyph->AttachGlyph;
  kbts_glyph_storage Temporary = KBTS__ZERO;
  kbts_glyph_storage *Storage = ProbeStorage ? ProbeStorage : &Temporary;
  if(ProbeStorage)
  {
    /* Config construction advertises and lends this complete fixed workspace. */
    KBTS_ASSERT(!Storage->LiveCount && Storage->Capacity);
    KBTS_ASSERT(Storage->Glyphs && Storage->Links && Storage->FreeSlots);
  }
  else
  {
    kbts_un Capacity = 1;
    if(Feature)
    {
      const kbts_u16 *Roots = KBTS__POINTER_AFTER(kbts_u16, Feature);
      for(kbts_u32 I = 0; I < Feature->LookupIndexCount; ++I)
      {
        kbts_u32 Inserted = Config->Font->CompiledContexts->LookupSummaries[Roots[I]].MaxInserted;
        if(Inserted > 0xfffffffeu - Capacity)
        { Scratchpad->Error = KBTS_SHAPE_ERROR_OUT_OF_MEMORY; return Result; }
        Capacity += Inserted;
      }
    }
    if(Capacity > Scratchpad->ProbeCapacity || !Scratchpad->ProbeMemory)
    { Scratchpad->Error = KBTS_SHAPE_ERROR_OUT_OF_MEMORY; return Result; }
    kbts__BindGlyphStorage(&Temporary, Scratchpad->ProbeMemory, (kbts_u32)Scratchpad->ProbeCapacity);
  }
  Storage->Count = Storage->LiveCount = 1;
  Storage->First = Storage->Tail = 0;
  Storage->End = KBTS__GLYPH_END;
  Storage->FreeSlotCount = 0;
  Storage->Ordered = 1;
  Storage->Links[0].Prev = Storage->Links[0].Next = KBTS__GLYPH_END;
  kbts_glyph_storage *SavedStorage = Scratchpad->Storage;
  kbts_glyph_ref SavedOnePast = Scratchpad->LookupOnePastLastGlyph;
  kbts_u32 SavedOnePastIndex = Scratchpad->LookupOnePastLastGlyphIndex;
  kbts__glyph_range SavedCluster = Scratchpad->Cluster;
  kbts_u32 SavedNextGlyphUid = Scratchpad->NextGlyphUid;
  Scratchpad->Storage = Storage;
  Scratchpad->Cluster = KBTS__ZERO_TYPE(kbts__glyph_range);
  Result.Bucketed = 0;
  Result.AttachGlyph = 0;
  Result.Ref = 1;
  Storage->Glyphs[0] = Result;

  {
    kbts__iterate_lookups IterateLookups = kbts__IterateLookups(LookupList, Feature);
    while(kbts__NextLookup(&IterateLookups) && !Scratchpad->Error && Storage->First != Storage->End)
    {
      kbts__gsub_frame Frames[KBTS_LOOKUP_STACK_SIZE];
      Frames[0] = KBTS__ZERO_TYPE(kbts__gsub_frame);
      Frames[0].LookupIndex = IterateLookups.LookupIndex;
      Frames[0].InputGlyph = kbts__FirstGlyph(Storage);
      kbts_un FrameCount = 1;
      kbts__BeginLookupApplication(Scratchpad, Frames[0].InputGlyph);
      while(FrameCount && !Scratchpad->Error)
        kbts__DoSubstitution(Scratchpad, Config, Storage,
            Scratchpad->SequentialLookupIndexIndex, 0, Frames, &FrameCount, 0, SkipFlags, 0, 0);
    }
    kbts_glyph_ref First = kbts__FirstGlyph(Storage);
    Result = First ? *kbts__Glyph(Storage, First) : KBTS__ZERO_TYPE(kbts_glyph);
  }

  if(ProbeStorage)
  {
    Storage->Count = Storage->LiveCount = Storage->FreeSlotCount = 0;
    Storage->First = Storage->Tail = Storage->End = KBTS__GLYPH_END;
  }
  Scratchpad->Storage = SavedStorage;
  Scratchpad->LookupOnePastLastGlyph = SavedOnePast;
  Scratchpad->LookupOnePastLastGlyphIndex = SavedOnePastIndex;
  Scratchpad->Cluster = SavedCluster;
  Scratchpad->NextGlyphUid = SavedNextGlyphUid;
  Result.Ref = OriginalRef;
  Result.AttachGlyph = OriginalAttachment;
  return Result;
}
