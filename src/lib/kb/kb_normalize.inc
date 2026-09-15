/* Expand one original input in place. The caller retains its original successor,
 * so emitted leaves are never mistaken for fresh input, including unsupported
 * decompositions and leaves carrying DO_NOT_DECOMPOSE. */
static kbts_u32 kbts__DecomposeGlyph(kbts_shape_scratchpad *Scratchpad,
    kbts_glyph_storage *Storage, kbts_u32 Slot)
{
  KBTS_INSTRUMENT_BLOCK_BEGIN(Decompose);
  kbts_font *Font = Scratchpad->Config->Font;
  kbts_glyph *Pending = (kbts_glyph *)Scratchpad->ScratchMemory;
  kbts_glyph_ref Before = kbts__PrevGlyphRef(Storage, Slot + 1);
  kbts_glyph_ref Anchor = Before;
  kbts_un Count = 1;
  Pending[0] = Storage->Glyphs[Slot];
  kbts__FreeGlyph(Scratchpad, Storage, Slot + 1);

  while(Count)
  {
    kbts_glyph Glyph = Pending[--Count];
    kbts_u64 Decomposition = 0;
    kbts_u32 Size = 0;
    kbts_b32 Unsupported = 0;
    kbts_glyph Children[2];
    if(!(Glyph.Flags & KBTS_GLYPH_FLAG_DO_NOT_DECOMPOSE))
    {
      Decomposition = Glyph.Decomposition;
      Size = kbts__GetDecompositionSize(Decomposition);
      KBTS__FOR(I, 0, Size)
      {
        Children[I] = kbts_CodepointToGlyph(Font,
            (int)kbts__GetDecompositionCodepoint(Decomposition, I), Glyph.Config,
            Glyph.UserIdOrCodepointIndex);
        Unsupported |= !Children[I].Id;
      }
    }
    if(Unsupported || !Size)
    {
      Anchor = kbts__InsertGlyphAfter(Storage, Anchor, &Glyph);
      if(!Anchor)
      {
        Scratchpad->Error = KBTS_SHAPE_ERROR_OUT_OF_MEMORY;
        return Storage->End;
      }
    }
    else
    {
      KBTS_ASSERT(Count + Size <= KBTS__MAXIMUM_DECOMPOSITION_CODEPOINTS);
      if(Decomposition & KBTS_UNICODE_DECOMPOSITION_DO_NOT_RECURSE0)
        Children[0].Flags |= KBTS_GLYPH_FLAG_DO_NOT_DECOMPOSE;
      if(Decomposition & KBTS_UNICODE_DECOMPOSITION_DO_NOT_RECURSE1)
        Children[1].Flags |= KBTS_GLYPH_FLAG_DO_NOT_DECOMPOSE;
      KBTS__FOR(I, 0, Size) Pending[Count++] = Children[Size - 1 - I];
    }
  }
  KBTS_INSTRUMENT_BLOCK_END(Decompose);
  return kbts__NextGlyphRef(Storage, Before) - 1;
}

typedef struct kbts__normalization_span
{
  kbts_u32 First, End, InputEnd;
} kbts__normalization_span;

/* Finish one nonstarter sequence before exposing any of it to composition.
 * Its boundary is the next decomposed starter, not the next input starter.
 * Use the same stable insertion order as SORT_GLYPHS, inserting each mark as
 * it arrives rather than collecting sort keys and walking the sequence again. */
static kbts__normalization_span kbts__OrderNormalizationMarks(kbts_shape_scratchpad *Scratchpad,
    kbts_glyph_storage *Storage, kbts_u32 First, kbts_u32 InputEnd)
{
  KBTS_INSTRUMENT_BLOCK_BEGIN(MarkReordering);
  kbts_glyph_ref Before = kbts__PrevGlyphRef(Storage, First + 1);
  kbts_u32 Slot = First;
  while(Slot != Storage->End)
  {
    if(Slot == InputEnd)
    {
      InputEnd = kbts__NextGlyphSlot(Storage, Slot);
      if(kbts__GetDecompositionSize(Storage->Glyphs[Slot].Decomposition))
      {
        Slot = kbts__DecomposeGlyph(Scratchpad, Storage, Slot);
        if(Scratchpad->Error) break;
      }
    }
    kbts_u8 CombiningClass = Storage->Glyphs[Slot].CombiningClass;
    if(!CombiningClass) break;

    kbts_u32 Next = kbts__NextGlyphSlot(Storage, Slot);
    kbts_glyph_ref Mark = Slot + 1, Destination = Mark;
    kbts_glyph_ref Previous = kbts__PrevGlyphRef(Storage, Mark);
    while(Previous != Before &&
          kbts__Glyph(Storage, Previous)->CombiningClass > CombiningClass)
    {
      Destination = Previous;
      Previous = kbts__PrevGlyphRef(Storage, Previous);
    }
    if(Destination != Mark) kbts__MoveGlyphBefore(Storage, Mark, Destination);
    Slot = Next;
  }
  KBTS_INSTRUMENT_BLOCK_END(MarkReordering);
  kbts__normalization_span Result = {kbts__NextGlyphRef(Storage, Before) - 1, Slot, InputEnd};
  return Result;
}

/* Plain spans stay in the composition loop; only decompositions and complete
 * mark sequences take the preparation helpers. Both frontiers are logical
 * glyph slots: InputEnd excludes already-expanded leaves, and ReadyEnd excludes
 * marks not yet ordered. Composition may revisit or remove prepared glyphs. */
static void kbts__NormalizeGlyphs(kbts_shape_scratchpad *Scratchpad, kbts_glyph_storage *Storage)
{
  kbts_shape_config *Config = Scratchpad->Config;
  kbts_font *Font = Config->Font;
  kbts_u32 InputEnd = Storage->First, ReadyEnd = Storage->First;
  // The OpenType shaping documents say that Hebrew Alphabetic Presentation Form compositions aren't canonical,
  // but looking at UnicodeData.txt, it seems like they totally are, so they are handled here.
  kbts_glyph_ref LastBase = 0;
  kbts_un LastBaseParentCount = 0;
  kbts_u8 LastCombiningClass = 0;
  kbts_b32 LastBaseParentsDirty = 0;
  kbts_un PreSlashDecimalDigitCount = 0;
  kbts_glyph_ref PreSlashGlyph = 0;
  kbts_glyph_ref DigitGlyph = 0;
  kbts_un DecimalDigitCount = 0;
  kbts_b32 InFraction = 0;
  kbts_u32 LastBaseParentsLoaded = 0;
  kbts_glyph_parent LastBaseParents[KBTS_MAXIMUM_RECOMPOSITION_PARENTS];
  kbts_u16 LastBaseParentIds[KBTS_MAXIMUM_RECOMPOSITION_PARENTS];

  kbts_u32 BeforeFractionSlashGlyphFlags = KBTS_GLYPH_FLAG_NUMR | KBTS_GLYPH_FLAG_FRAC;
  kbts_u32 AfterFractionSlashGlyphFlags = KBTS_GLYPH_FLAG_DNOM | KBTS_GLYPH_FLAG_FRAC;
  if(kbts__ShaperRtl(Config->Shaper))
  {
    // RTL needs to invert NUMR and DNOM.
    kbts_u32 Swap = BeforeFractionSlashGlyphFlags;
    BeforeFractionSlashGlyphFlags = AfterFractionSlashGlyphFlags;
    AfterFractionSlashGlyphFlags = Swap;
  }

  kbts_b32 ShouldFlip = (Scratchpad->RunDirection == KBTS_DIRECTION_RTL);

  for(kbts_u32 GlyphIndex = Storage->First; GlyphIndex != Storage->End;)
  {
    if(GlyphIndex == ReadyEnd)
    {
      if(GlyphIndex == InputEnd)
      {
        InputEnd = kbts__NextGlyphSlot(Storage, GlyphIndex);
        if(kbts__GetDecompositionSize(Storage->Glyphs[GlyphIndex].Decomposition))
        {
          GlyphIndex = kbts__DecomposeGlyph(Scratchpad, Storage, GlyphIndex);
          if(Scratchpad->Error) return;
        }
      }
      ReadyEnd = kbts__NextGlyphSlot(Storage, GlyphIndex);
      if(Storage->Glyphs[GlyphIndex].CombiningClass)
      {
        kbts__normalization_span Marks = kbts__OrderNormalizationMarks(Scratchpad,
            Storage, GlyphIndex, InputEnd);
        if(Scratchpad->Error) return;
        GlyphIndex = Marks.First;
        ReadyEnd = Marks.End;
        InputEnd = Marks.InputEnd;
      }
    }
    kbts_glyph *CurrentGlyph = &Storage->Glyphs[GlyphIndex];
    kbts_glyph_ref Glyph = CurrentGlyph->Ref;
    CurrentGlyph->Uid = kbts__NextGlyphUid(Scratchpad);

    // In RTL, mirror all glyphs when their mirror is covered.
    if(ShouldFlip &&
       (CurrentGlyph->UnicodeFlags & KBTS_UNICODE_FLAG_MIRRORED))
    {
      kbts_u32 MatchingBracketCodepoint = kbts__GetUnicodeMirrorCodepoint(CurrentGlyph->Codepoint);
      kbts_glyph MatchingBracket = kbts_CodepointToGlyph(Font, (int)MatchingBracketCodepoint, CurrentGlyph->Config, 0);
      if(MatchingBracket.Id)
      {
        kbts__SetGlyphPreserveIdentityAndUserId(CurrentGlyph, &MatchingBracket);
      }
    }

    kbts_u32 SingleRecompositionCodepoints[KBTS_MAXIMUM_RECOMPOSITION_PARENTS];
    kbts_un SingleRecompositionCodepointCount = 0;

    if(!CurrentGlyph->CombiningClass)
    {
      LastBase = Glyph;
      LastBaseParentCount = 0;
      LastBaseParentsDirty = 1;
    }
    // A supported base needs no parent list until a combining glyph
    // consumes it. Missing bases still need singleton-alias fallback.
    if(LastBaseParentsDirty && (CurrentGlyph->CombiningClass || !CurrentGlyph->Id))
    {
      kbts_glyph *BaseGlyph = kbts__Glyph(Storage, LastBase);
      // From the Microsoft docs:
      // USE decomposes split vowel characters belonging to UISC = Vowel_Dependent according to character
      // decomposition mappings defined in UnicodeData.txt
      // Cluster validation, is done based on the decomposed state of a split vowel.
      //
      // (Note: our Matra corresponds to Vowel_Dependent + Pure_Killer.)
      if((Config->Shaper != KBTS_SHAPER_USE) ||
         (BaseGlyph->SyllabicClass != KBTS_INDIC_SYLLABIC_CLASS_MATRA))
      {
        kbts_s32 *LastBaseParentDeltas = kbts__GetParentInfoDeltas(BaseGlyph->ParentInfo);
        kbts_un ParentCount = kbts__GetParentInfoCount(BaseGlyph->ParentInfo);

        kbts_un DoubleDecompositionCount = 0;
        KBTS__FOR(ParentIndex, 0, ParentCount)
        {
          kbts_glyph_parent Parent = KBTS__ZERO;
          Parent.Codepoint = BaseGlyph->Codepoint + (kbts_u32)LastBaseParentDeltas[ParentIndex];

          kbts_u64 Decomposition = kbts__GetUnicodeDecomposition(Parent.Codepoint);
          Parent.Codepoint1 = kbts__GetDecompositionCodepoint(Decomposition, 1);

          kbts_un DecompositionSize = kbts__GetDecompositionSize(Decomposition);
          if(DecompositionSize == 1)
          {
            // Canonical singletons are composition exclusions. Use an
            // alias only as missing-glyph fallback, never over a mapping.
            if(BaseGlyph == CurrentGlyph && !CurrentGlyph->Id)
              SingleRecompositionCodepoints[SingleRecompositionCodepointCount++] = Parent.Codepoint;
          }
          else
          {
            LastBaseParents[DoubleDecompositionCount++] = Parent;
          }
        }

        LastBaseParentCount = DoubleDecompositionCount;
        LastBaseParentsLoaded = 0;
      }
      else
      {
        LastBaseParentCount = 0;
      }

      LastBaseParentsDirty = 0;
    }
    kbts_un DoubleRecompositionCount = CurrentGlyph->CombiningClass &&
      (!LastCombiningClass || LastCombiningClass < CurrentGlyph->CombiningClass)
      ? LastBaseParentCount : 0;

    kbts_b32 Recomposed = 0;

    if(!Recomposed)
    {
      KBTS_INSTRUMENT_BLOCK_BEGIN(ParentNSquaredStupidity);
      KBTS__FOR(ParentIndex, 0, DoubleRecompositionCount)
      {
        kbts_glyph_parent *Parent = &LastBaseParents[ParentIndex];
        kbts_u32 Codepoint1 = Parent->Codepoint1;

        if(CurrentGlyph->Codepoint == Codepoint1)
        {
          kbts_u16 ParentId;
          if(LastBaseParentsLoaded & (1u << ParentIndex))
          {
            ParentId = LastBaseParentIds[ParentIndex];
          }
          else
          {
            ParentId = (kbts_u16)kbts_CodepointToGlyphId(Font, (int)Parent->Codepoint);
            LastBaseParentIds[ParentIndex] = ParentId;
            LastBaseParentsLoaded |= 1u << ParentIndex;
          }

          if(ParentId)
          {
            // Both match. Reclaim space.
            kbts_glyph ParentGlyph = kbts_CodepointToGlyph(Font, (int)Parent->Codepoint, 0, 0);
            kbts_glyph *BaseGlyph = kbts__Glyph(Storage, LastBase);
            ParentGlyph.Uid = BaseGlyph->Uid;
            ParentGlyph.UserIdOrCodepointIndex = BaseGlyph->UserIdOrCodepointIndex;
            ParentGlyph.Config = BaseGlyph->Config;

            kbts_u32 Next = kbts__NextGlyphSlot(Storage, GlyphIndex);
            kbts__FreeGlyph(Scratchpad, Storage, Glyph);
            GlyphIndex = Next;

            Recomposed = 1;
            kbts__SetGlyphPreserveIdentityAndUserId(kbts__Glyph(Storage, LastBase), &ParentGlyph);
            // The next mark must compose with the new base, not replace it
            // using a parent of the original decomposed character.
            LastBaseParentsDirty = 1;

            break;
          }
          else
          {
            // This glyph is never good. Forget it.
            LastBaseParents[ParentIndex] = LastBaseParents[LastBaseParentCount - 1];
            if(LastBaseParentsLoaded & (1u << (LastBaseParentCount - 1)))
              LastBaseParentIds[ParentIndex] = LastBaseParentIds[LastBaseParentCount - 1];
            LastBaseParentsLoaded &= ~(1u << ParentIndex);
            LastBaseParentsLoaded |= (LastBaseParentsLoaded & (1u << (LastBaseParentCount - 1))) >> (LastBaseParentCount - 1 - ParentIndex);
            
            LastBaseParentCount -= 1;
            DoubleRecompositionCount -= 1;
            ParentIndex -= 1;
          }
        }
      }
      KBTS_INSTRUMENT_BLOCK_END(ParentNSquaredStupidity);
    }

    if(!Recomposed)
    {
      KBTS__FOR(SingleRecompositionIndex, 0, SingleRecompositionCodepointCount)
      {
        kbts_u16 ParentGlyphId = (kbts_u16)kbts_CodepointToGlyphId(Font, (int)SingleRecompositionCodepoints[SingleRecompositionIndex]);

        if(ParentGlyphId)
        {
          kbts_glyph ParentGlyph = kbts_CodepointToGlyph(Font, (int)SingleRecompositionCodepoints[SingleRecompositionIndex], 0, 0);
          ParentGlyph.Config = CurrentGlyph->Config;

          kbts__SetGlyphPreserveIdentityAndUserId(CurrentGlyph, &ParentGlyph);
          Recomposed = 1;
          break;
        }
      }
    }
    if(Recomposed) continue;

    // It is safe to look for fractions here, because decimal digits/the fraction slash are not marks or
    // jamos, so they should not get reordered after this pass.
    if(CurrentGlyph->UnicodeFlags & KBTS_UNICODE_FLAG_DECIMAL_DIGIT)
    {
      if(InFraction)
      {
        // We are in the post-slash part of the fraction.
        CurrentGlyph->Flags |= AfterFractionSlashGlyphFlags;
        // Only flag the pre-slash part of the fraction if there is a post-slash part.
        KBTS__FOR(DecimalDigitIndex, 0, PreSlashDecimalDigitCount)
        {
          kbts__Glyph(Storage, PreSlashGlyph)->Flags |= BeforeFractionSlashGlyphFlags;
          PreSlashGlyph = kbts__NextGlyphRef(Storage, PreSlashGlyph);
        }
        PreSlashDecimalDigitCount = 0;
      }
      if(!DecimalDigitCount)
      {
        DigitGlyph = Glyph;
      }
      DecimalDigitCount += 1;
    }
    else if((CurrentGlyph->Codepoint == 0x2044) &&
            (!InFraction || DecimalDigitCount))
    {
      // Fraction slash.
      CurrentGlyph->Flags |= KBTS_GLYPH_FLAG_FRAC;
      PreSlashDecimalDigitCount = DecimalDigitCount;
      PreSlashGlyph = DigitGlyph;
      InFraction = DecimalDigitCount != 0;
      DecimalDigitCount = 0;
    }
    else
    {
      InFraction = 0;
      DecimalDigitCount = 0;
    }

    LastCombiningClass = CurrentGlyph->CombiningClass;
    GlyphIndex = kbts__NextGlyphSlot(Storage, GlyphIndex);
  }
}
