static void kbts__ExecuteOp(kbts_shape_scratchpad *Scratchpad, kbts_glyph_storage *Storage)
{
  KB_PROFILE_SCOPE(KBP_EXECUTE_OP);
  KBTS_INSTRUMENT_FUNCTION_BEGIN;
  kbts_shape_config *Config = Scratchpad->Config;

  if(Config)
  {
    kbts_font *Font = Config->Font;
    kbts__op_kind OpKind = Scratchpad->OpKind;

    switch(OpKind)
    {
    case KBTS__OP_KIND_PRE_NORMALIZE_DOTTED_CIRCLES:
    {
      KBTS_INSTRUMENT_BLOCK_BEGIN(PRE_NORMALIZE_DOTTED_CIRCLES);

      // Before even trying to normalize anything, there are some _exceptions_ we have to take care of.
      // The USE spec gives us a list of codepoint sequences which necessitate insertion of a dotted circle.
      // These sequences are from IndicShapingInvalidClusters.txt.
      kbts_u64 Codepoints; Codepoints = 0; // 0xABBBBBCCCCCDDDDD
      for(kbts_u32 GlyphIndex = Storage->First; GlyphIndex != Storage->End; GlyphIndex = kbts__NextGlyphSlot(Storage, GlyphIndex))
      {
        kbts_u32 NewCodepoint; NewCodepoint = Storage->Glyphs[GlyphIndex].Codepoint & 0x1FFFFF;
        Codepoints = (Codepoints << 21) | NewCodepoint;

        // This switch is faster than any table lookup I could come up with in my tests.
        #define KBTS_C2(A, B) case (((kbts_u64)(A) << 21) | (kbts_u64)(B)):
        switch(Codepoints & (((kbts_u64)1 << 42) - 1))
        {
        default:
          if((Codepoints & (((kbts_u64)1 << 63) - 1)) == (((kbts_u64)0x930 << 42) | ((kbts_u64)0x94D << 21) | ((kbts_u64)0x907)))
          {
          KBTS_C2(0x905, 0x93A)
          KBTS_C2(0x905, 0x93B)
          KBTS_C2(0x905, 0x93E)
          KBTS_C2(0x905, 0x945)
          KBTS_C2(0x905, 0x946)
          KBTS_C2(0x905, 0x949)
          KBTS_C2(0x905, 0x94A)
          KBTS_C2(0x905, 0x94B)
          KBTS_C2(0x905, 0x94C)
          KBTS_C2(0x905, 0x94F)
          KBTS_C2(0x905, 0x956)
          KBTS_C2(0x905, 0x957)
          KBTS_C2(0x906, 0x93A)
          KBTS_C2(0x906, 0x945)
          KBTS_C2(0x906, 0x946)
          KBTS_C2(0x906, 0x947)
          KBTS_C2(0x906, 0x948)
          KBTS_C2(0x909, 0x941)
          KBTS_C2(0x90F, 0x945)
          KBTS_C2(0x90F, 0x946)
          KBTS_C2(0x90F, 0x947)
          KBTS_C2(0x985, 0x9BE)
          KBTS_C2(0x98B, 0x9C3)
          KBTS_C2(0x98C, 0x9E2)
          KBTS_C2(0xA05, 0xA3E)
          KBTS_C2(0xA05, 0xA48)
          KBTS_C2(0xA05, 0xA4C)
          KBTS_C2(0xA72, 0xA3F)
          KBTS_C2(0xA72, 0xA40)
          KBTS_C2(0xA72, 0xA47)
          KBTS_C2(0xA73, 0xA41)
          KBTS_C2(0xA73, 0xA42)
          KBTS_C2(0xA73, 0xA4B)
          KBTS_C2(0xA85, 0xABE)
          KBTS_C2(0xA85, 0xAC5)
          KBTS_C2(0xA85, 0xAC7)
          KBTS_C2(0xA85, 0xAC8)
          KBTS_C2(0xA85, 0xAC9)
          KBTS_C2(0xA85, 0xACB)
          KBTS_C2(0xA85, 0xACC)
          KBTS_C2(0xAC5, 0xABE)
          KBTS_C2(0xB05, 0xB3E)
          KBTS_C2(0xB0F, 0xB57)
          KBTS_C2(0xB13, 0xB57)
          KBTS_C2(0xB85, 0xBC2)
          KBTS_C2(0xC12, 0xC4C)
          KBTS_C2(0xC12, 0xC55)
          KBTS_C2(0xC3F, 0xC55)
          KBTS_C2(0xC46, 0xC55)
          KBTS_C2(0xC4A, 0xC55)
          KBTS_C2(0xC89, 0xCBE)
          KBTS_C2(0xC8B, 0xCBE)
          KBTS_C2(0xC92, 0xCCC)
          KBTS_C2(0xD07, 0xD57)
          KBTS_C2(0xD09, 0xD57)
          KBTS_C2(0xD0E, 0xD46)
          KBTS_C2(0xD12, 0xD3E)
          KBTS_C2(0xD12, 0xD57)
          KBTS_C2(0xD85, 0xDCF)
          KBTS_C2(0xD85, 0xDD0)
          KBTS_C2(0xD85, 0xDD1)
          KBTS_C2(0xD8B, 0xDDF)
          KBTS_C2(0xD8D, 0xDD8)
          KBTS_C2(0xD8F, 0xDDF)
          KBTS_C2(0xD91, 0xDCA)
          KBTS_C2(0xD91, 0xDD9)
          KBTS_C2(0xD91, 0xDDA)
          KBTS_C2(0xD91, 0xDDC)
          KBTS_C2(0xD91, 0xDDD)
          KBTS_C2(0xD91, 0xDDE)
          KBTS_C2(0xD94, 0xDDF)
          KBTS_C2(0x11005, 0x11038)
          KBTS_C2(0x1100B, 0x1103E)
          KBTS_C2(0x1100F, 0x11042)
          KBTS_C2(0x11200, 0x1122C)
          KBTS_C2(0x11200, 0x11231)
          KBTS_C2(0x11200, 0x11233)
          KBTS_C2(0x11206, 0x1122C)
          KBTS_C2(0x1122C, 0x11230)
          KBTS_C2(0x1122C, 0x11231)
          KBTS_C2(0x11240, 0x1122E)
          KBTS_C2(0x112B0, 0x112E0)
          KBTS_C2(0x112B0, 0x112E5)
          KBTS_C2(0x112B0, 0x112E6)
          KBTS_C2(0x112B0, 0x112E7)
          KBTS_C2(0x112B0, 0x112E8)
          KBTS_C2(0x11481, 0x114B0)
          KBTS_C2(0x1148B, 0x114BA)
          KBTS_C2(0x1148D, 0x114BA)
          KBTS_C2(0x114AA, 0x114B5)
          KBTS_C2(0x114AA, 0x114B6)
          KBTS_C2(0x11600, 0x11639)
          KBTS_C2(0x11600, 0x1163A)
          KBTS_C2(0x11601, 0x11639)
          KBTS_C2(0x11601, 0x1163A)
          KBTS_C2(0x11680, 0x116AD)
          KBTS_C2(0x11680, 0x116B4)
          KBTS_C2(0x11680, 0x116B5)
          KBTS_C2(0x11686, 0x116B2)
          {
            kbts_glyph_ref Glyph = Storage->Glyphs[GlyphIndex].Ref;
            kbts_glyph_ref NewGlyph = kbts__InsertGlyphBefore(Storage, Glyph, &Config->DottedCircle);
            if(!NewGlyph)
            {
              goto OutOfMemory;
            }
          } break;
          }
        }
        #undef KBTS_C2
      }

      KBTS_INSTRUMENT_BLOCK_END(PRE_NORMALIZE_DOTTED_CIRCLES);
    } break;

    case KBTS__OP_KIND_NORMALIZE:
    {
      KB_PROFILE_SCOPE(KBP_NORMALIZE);
      KBTS_INSTRUMENT_BLOCK_BEGIN(NORMALIZE);

      KBTS_INSTRUMENT_BLOCK_BEGIN(Decompose);
      { // Full NFD decomposition
        kbts_glyph *DecompositionGlyphs = (kbts_glyph *)Scratchpad->ScratchMemory;
        kbts_un CodepointsToDecomposeCount = 0;

        for(kbts_u32 GlyphIndex = Storage->First; GlyphIndex != Storage->End; GlyphIndex = kbts__NextGlyphSlot(Storage, GlyphIndex))
        {
          kbts_glyph *Glyph = &Storage->Glyphs[GlyphIndex];
          if(kbts__GetDecompositionSize(Glyph->Decomposition))
          {
            kbts_glyph_ref GlyphRef = Glyph->Ref;
            kbts_glyph_ref PrevAnchor = kbts__PrevGlyphRef(Storage, GlyphRef);

            DecompositionGlyphs[0] = *Glyph;
            CodepointsToDecomposeCount = 1;

            kbts__FreeGlyph(Scratchpad, Storage, GlyphRef);

            while(CodepointsToDecomposeCount)
            {
              kbts_glyph GlyphToDecompose = DecompositionGlyphs[--CodepointsToDecomposeCount];
              kbts_u64 Decomposition = 0;
              kbts_u32 DecompositionSize = 0;
              int AnyUnsupported = 0;
              kbts_glyph Decomposed[2];

              if(!(GlyphToDecompose.Flags & KBTS_GLYPH_FLAG_DO_NOT_DECOMPOSE))
              {
                Decomposition = GlyphToDecompose.Decomposition;
                DecompositionSize = kbts__GetDecompositionSize(Decomposition);

                // Only decompose when the font supports the decomposed form.
                KBTS__FOR(DecompositionIndex, 0, DecompositionSize)
                {
                  kbts_glyph DecompositionGlyph = kbts_CodepointToGlyph(Font, (int)kbts__GetDecompositionCodepoint(Decomposition, DecompositionIndex), 0, 0);
                  DecompositionGlyph.Config = GlyphToDecompose.Config;
                  DecompositionGlyph.UserIdOrCodepointIndex = GlyphToDecompose.UserIdOrCodepointIndex;

                  AnyUnsupported |= !DecompositionGlyph.Id;
                  Decomposed[DecompositionIndex] = DecompositionGlyph;
                }
              }

              if(AnyUnsupported | !DecompositionSize)
              {
                kbts_glyph_ref NewGlyph = kbts__InsertGlyphAfter(Storage, PrevAnchor, &GlyphToDecompose);
                if(!NewGlyph)
                {
                  goto OutOfMemory;
                }

                PrevAnchor = NewGlyph;
              }
              else
              {
                KBTS_ASSERT((CodepointsToDecomposeCount + DecompositionSize) <= KBTS__MAXIMUM_DECOMPOSITION_CODEPOINTS);

                if(Decomposition & KBTS_UNICODE_DECOMPOSITION_DO_NOT_RECURSE0)
                {
                  Decomposed[0].Flags |= KBTS_GLYPH_FLAG_DO_NOT_DECOMPOSE;
                }
                if(Decomposition & KBTS_UNICODE_DECOMPOSITION_DO_NOT_RECURSE1)
                {
                  Decomposed[1].Flags |= KBTS_GLYPH_FLAG_DO_NOT_DECOMPOSE;
                }

                KBTS__FOR(DecompositionIndex, 0, DecompositionSize)
                {
                  // We reverse the glyphs here because we use a stack.
                  DecompositionGlyphs[CodepointsToDecomposeCount++] = Decomposed[DecompositionSize - 1 - DecompositionIndex];
                }
              }
            }

            GlyphIndex = PrevAnchor - 1;
          }
        }
      }
      KBTS_INSTRUMENT_BLOCK_END(Decompose);

      KBTS_INSTRUMENT_BLOCK_BEGIN(Recompose);
      { // Selective recomposition.
        // The OpenType shaping documents say that Hebrew Alphabetic Presentation Form compositions aren't canonical,
        // but looking at UnicodeData.txt, it seems like they totally are, so they are handled here.
        kbts_glyph_ref LastBase = 0;
        kbts_un LastBaseParentCount = 0;
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
          kbts_un DoubleRecompositionCount = LastBaseParentCount;

          if(!CurrentGlyph->CombiningClass)
          {
            LastBase = Glyph;
            // From the Microsoft docs:
            // USE decomposes split vowel characters belonging to UISC = Vowel_Dependent according to character
            // decomposition mappings defined in UnicodeData.txt
            // Cluster validation, is done based on the decomposed state of a split vowel.
            //
            // (Note: our Matra corresponds to Vowel_Dependent + Pure_Killer.)
            if((Config->Shaper != KBTS_SHAPER_USE) ||
               (CurrentGlyph->SyllabicClass != KBTS_INDIC_SYLLABIC_CLASS_MATRA))
            {
              kbts_s32 *LastBaseParentDeltas = kbts__GetParentInfoDeltas(CurrentGlyph->ParentInfo);
              kbts_un ParentCount = kbts__GetParentInfoCount(CurrentGlyph->ParentInfo);

              kbts_un DoubleDecompositionCount = 0;
              KBTS__FOR(ParentIndex, 0, ParentCount)
              {
                kbts_glyph_parent Parent = KBTS__ZERO;
                Parent.Codepoint = CurrentGlyph->Codepoint + (kbts_u32)LastBaseParentDeltas[ParentIndex];

                kbts_u64 Decomposition = kbts__GetUnicodeDecomposition(Parent.Codepoint);
                Parent.Codepoint1 = kbts__GetDecompositionCodepoint(Decomposition, 1);

                kbts_un DecompositionSize = kbts__GetDecompositionSize(Decomposition);
                if(DecompositionSize == 1)
                {
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

            DoubleRecompositionCount = 0;
          }

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
                kbts_u16 ParentId = LastBaseParentIds[ParentIndex];

                if(!(LastBaseParentsLoaded & (1u << ParentIndex)))
                {
                  ParentId = (kbts_u16)kbts_CodepointToGlyphId(Font, (int)Parent->Codepoint);
                  LastBaseParentIds[ParentIndex] = ParentId;
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
                  CurrentGlyph = Next != Storage->End ? &Storage->Glyphs[Next] : &Storage->Boundary;
                  Glyph = CurrentGlyph->Ref;

                  Recomposed = 1;
                  kbts__SetGlyphPreserveIdentityAndUserId(kbts__Glyph(Storage, LastBase), &ParentGlyph);

                  break;
                }
                else
                {
                  // This glyph is never good. Forget it.
                  LastBaseParents[ParentIndex] = LastBaseParents[LastBaseParentCount - 1];
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
          }

          if(!Recomposed)
          {
            // Recomposition revisits the surviving glyph or the continuation after deletion.
            GlyphIndex = kbts__NextGlyphSlot(Storage, GlyphIndex);
          }
        }
      }
      KBTS_INSTRUMENT_BLOCK_END(Recompose);

      KBTS_INSTRUMENT_BLOCK_BEGIN(MarkReordering);
      { // Unicode mark reordering.
        for(kbts_u32 GlyphIndex = Storage->First; GlyphIndex != Storage->End;)
        {
          kbts_glyph *Glyph = &Storage->Glyphs[GlyphIndex];
          kbts_u8 CombiningClass = Glyph->CombiningClass;

          if(CombiningClass)
          {
            Glyph->MarkOrdering = CombiningClass;
            kbts_u32 SequenceEnd = kbts__NextGlyphSlot(Storage, GlyphIndex);
            for(; SequenceEnd != Storage->End; SequenceEnd = kbts__NextGlyphSlot(Storage, SequenceEnd))
            {
              kbts_glyph *SequenceGlyph = &Storage->Glyphs[SequenceEnd];
              kbts_u8 SequenceGlyphCombiningClass = SequenceGlyph->CombiningClass;
              if(!SequenceGlyphCombiningClass) break;
              SequenceGlyph->MarkOrdering = SequenceGlyphCombiningClass;
            }

            kbts_glyph_ref First = Glyph->Ref;
            kbts_glyph_ref End = SequenceEnd != Storage->End ? Storage->Glyphs[SequenceEnd].Ref : 0;
            KBTS__SORT_GLYPHS(Storage, First, End, MarkOrdering);

            #ifdef KBTS_SANITY_CHECK
            for(kbts_glyph_ref Last = kbts__PrevGlyphRef(Storage, End), Previous = kbts__PrevGlyphRef(Storage, Last);
                Previous && kbts__Glyph(Storage, Previous)->CombiningClass;
                Last = Previous, Previous = kbts__PrevGlyphRef(Storage, Previous))
            {
              KBTS_ASSERT(kbts__Glyph(Storage, Previous)->MarkOrdering <= kbts__Glyph(Storage, Last)->MarkOrdering);
            }
            #endif

            GlyphIndex = SequenceEnd != Storage->End ? kbts__NextGlyphSlot(Storage, SequenceEnd) : Storage->End;
          }
          else
          {
            GlyphIndex = kbts__NextGlyphSlot(Storage, GlyphIndex);
          }
        }
      }
      KBTS_INSTRUMENT_BLOCK_END(MarkReordering);

      if(Config->Script == KBTS_SCRIPT_ARABIC)
      {
        enum {KBTS_REMAPPED_CCC_33 = 27};

        for(kbts_u32 GlyphIndex = Storage->First; GlyphIndex != Storage->End;)
        {
          kbts_glyph *Glyph = &Storage->Glyphs[GlyphIndex];
          kbts_u32 NextIndex = kbts__NextGlyphSlot(Storage, GlyphIndex);

          // Find a mark sequence.
          kbts_u8 CombiningClass = Glyph->CombiningClass;

          if(CombiningClass)
          {
            // Arabic: Reorder sequences of mark glyphs.
            //
            // From the Unicode standard:
            // - Move any shadda characters (ccc=33) to the beginning of S.
            // - If a sequence of ccc=230 characters begins with any MCM characters, move the sequence of such MCM
            // characters
            //   to the beginning of S (before any characters with ccc=33).
            // - If a sequence of ccc=220 characters begins with any MCM characters, move the sequence of such MCM
            // characters
            //   to the beginning of S (before any MCM with ccc=230 or ccc=33).
            //
            // Final ordering: 220 230 shadda other

            kbts__mcm_sequence_state Mcm220SequenceState = 0;
            kbts__mcm_sequence_state Mcm230SequenceState = 0;

            kbts_u32 SequenceEnd = GlyphIndex;
            for(; SequenceEnd != Storage->End; SequenceEnd = kbts__NextGlyphSlot(Storage, SequenceEnd))
            {
              kbts_glyph *SequenceGlyph = &Storage->Glyphs[SequenceEnd];
              kbts_u16 SequenceGlyphCombiningClass = SequenceGlyph->CombiningClass;
              kbts_u16 SequenceGlyphFlags = SequenceGlyph->UnicodeFlags;

              kbts_u8 MarkOrdering = 3;

              if(SequenceGlyphCombiningClass == KBTS_REMAPPED_CCC_33)
              {
                MarkOrdering = 2;
              }
              else if(SequenceGlyphCombiningClass == 220)
              {
                if(SequenceGlyphFlags & KBTS_UNICODE_FLAG_MODIFIER_COMBINING_MARK)
                {
                  if(Mcm220SequenceState != KBTS__MCM_SEQUENCE_STATE_OUT)
                  {
                    Mcm220SequenceState = KBTS__MCM_SEQUENCE_STATE_IN;
                  }
                }
                else
                {
                  Mcm220SequenceState = KBTS__MCM_SEQUENCE_STATE_OUT;
                }

                if(Mcm220SequenceState == KBTS__MCM_SEQUENCE_STATE_IN)
                {
                  MarkOrdering = 0;
                }

                Mcm230SequenceState = KBTS__MCM_SEQUENCE_STATE_NONE;
              }
              else if(SequenceGlyphCombiningClass == 230)
              {
                if(SequenceGlyphFlags & KBTS_UNICODE_FLAG_MODIFIER_COMBINING_MARK)
                {
                  if(Mcm230SequenceState != KBTS__MCM_SEQUENCE_STATE_OUT)
                  {
                    Mcm230SequenceState = KBTS__MCM_SEQUENCE_STATE_IN;
                  }
                }
                else
                {
                  Mcm230SequenceState = KBTS__MCM_SEQUENCE_STATE_OUT;
                }

                if(Mcm230SequenceState == KBTS__MCM_SEQUENCE_STATE_IN)
                {
                  MarkOrdering = 1;
                }
              }
              else if(SequenceGlyphCombiningClass)
              {
                Mcm220SequenceState = KBTS__MCM_SEQUENCE_STATE_NONE;
                Mcm230SequenceState = KBTS__MCM_SEQUENCE_STATE_NONE;
              }
              else
              {
                break;
              }

              SequenceGlyph->MarkOrdering = MarkOrdering;
            }

            kbts_glyph_ref First = Glyph->Ref;
            kbts_glyph_ref End = SequenceEnd != Storage->End ? Storage->Glyphs[SequenceEnd].Ref : 0;
            KBTS__SORT_GLYPHS(Storage, First, End, MarkOrdering);

            #ifdef KBTS_SANITY_CHECK
            for(kbts_glyph_ref Last = kbts__PrevGlyphRef(Storage, End), Previous = kbts__PrevGlyphRef(Storage, Last);
                Previous && kbts__Glyph(Storage, Previous)->CombiningClass;
                Last = Previous, Previous = kbts__PrevGlyphRef(Storage, Previous))
            {
              KBTS_ASSERT(kbts__Glyph(Storage, Previous)->MarkOrdering <= kbts__Glyph(Storage, Last)->MarkOrdering);
            }
            #endif

            NextIndex = SequenceEnd;
          }

          GlyphIndex = NextIndex;
        }
      }
      else if((Config->Script == KBTS_SCRIPT_THAI) || (Config->Script == KBTS_SCRIPT_LAO))
      {
        // Decompose sara/sala ams.
        kbts_glyph_ref AboveBaseGlyph = 0;

        for(kbts_u32 GlyphIndex = Storage->First; GlyphIndex != Storage->End; GlyphIndex = kbts__NextGlyphSlot(Storage, GlyphIndex))
        {
          kbts_glyph *CurrentGlyph = &Storage->Glyphs[GlyphIndex];
          kbts_glyph_ref Glyph = CurrentGlyph->Ref;
          kbts_u32 Codepoint = CurrentGlyph->Codepoint;

          switch(Codepoint)
          {
          // Sara am/sala am.
          // We match both because storing the sara am codepoint that corresponds to the current script
          // doesn't seem that worthwhile, given that this is already a pretty big switch case.
          // If we choose to use a unicode flag or indic syllabic category to notate above-base marks,
          // then this loops gets a lot tighter and it would probably become the right call to pre-determine
          // the sara am codepoint.
          case 0xE33: case 0xEB3: // Sara am
          {
            if(!AboveBaseGlyph)
            {
              AboveBaseGlyph = Glyph;
            }

            kbts_glyph_ref NewGlyph = kbts__InsertGlyphBefore(Storage, AboveBaseGlyph, &Config->Nikhahit);
            if(!NewGlyph)
            {
              goto OutOfMemory;
            }

            CurrentGlyph = &Storage->Glyphs[GlyphIndex];
            kbts_glyph_config *GlyphConfig = CurrentGlyph->Config;
            kbts__SetGlyphPreserveIdentityAndUserId(CurrentGlyph, &Config->SaraAa);
            CurrentGlyph->Config = GlyphConfig;

            AboveBaseGlyph = 0;
          } break;

          case 0xE31: case 0xE34: case 0xE35: case 0xE36: case 0xE37: case 0xE3B:
          case 0xE47: case 0xE48: case 0xE49: case 0xE4A: case 0xE4B: case 0xE4C: case 0xE4D: case 0xE4E:
          case 0xEB1: case 0xEB4: case 0xEB5: case 0xEB6: case 0xEB7: case 0xEBB:
          case 0xEC7: case 0xEC8: case 0xEC9: case 0xECA: case 0xECB: case 0xECC: case 0xECD: case 0xECE:
            if(!AboveBaseGlyph)
            {
              AboveBaseGlyph = Glyph;
            }
            break;

          default:
            AboveBaseGlyph = 0;
            break;
          }
        }
      }

      KBTS_INSTRUMENT_BLOCK_END(NORMALIZE);
    } break;

    case KBTS__OP_KIND_NORMALIZE_HANGUL:
    {
      KB_PROFILE_SCOPE(KBP_NORMALIZE);
      KBTS_INSTRUMENT_BLOCK_BEGIN(NORMALIZE_HANGUL);

      for(kbts_u32 GlyphIndex = Storage->First; GlyphIndex != Storage->End;)
      {
        kbts_glyph *CurrentGlyph = &Storage->Glyphs[GlyphIndex];
        kbts_u32 NextIndex = kbts__NextGlyphSlot(Storage, GlyphIndex);

        kbts_un L = 0;
        kbts_un V = 0;
        kbts_un T = 0;
        kbts_un LvtGlyphCount = 0;
        kbts_glyph LvtGlyphs[4];

        kbts__hangul_syllable_info LInfo = kbts__HangulSyllableInfo(CurrentGlyph->Codepoint);
        if(LInfo.Type >= KBTS__HANGUL_SYLLABLE_TYPE_LV)
        {
          kbts_un SIndex = (CurrentGlyph->Codepoint - 0xAC00);

          L = 0x1100 + SIndex / 588;
          V = 0x1161 + (SIndex % 588) / 28;

          kbts_un TIndex = SIndex % 28;
          if(TIndex)
          {
            T = 0x11A7 + TIndex;
          }
        }
        else if(LInfo.Type == KBTS__HANGUL_SYLLABLE_TYPE_L)
        {
          L = CurrentGlyph->Codepoint;
        }

        if(L)
        {
          kbts__hangul_syllable_info VInfo = KBTS__ZERO;

          if(!V && NextIndex != Storage->End)
          {
            kbts_u32 VCodepoint = Storage->Glyphs[NextIndex].Codepoint;

            VInfo = kbts__HangulSyllableInfo(VCodepoint);

            if(VInfo.Type == KBTS__HANGUL_SYLLABLE_TYPE_V)
            {
              V = VCodepoint;

              NextIndex = kbts__NextGlyphSlot(Storage, NextIndex);
            }
          }

          if(V)
          {
            kbts__hangul_syllable_info TInfo = KBTS__ZERO;

            if(!T && NextIndex != Storage->End)
            {
              kbts_u32 TCodepoint = Storage->Glyphs[NextIndex].Codepoint;

              TInfo = kbts__HangulSyllableInfo(TCodepoint);

              if(TInfo.Type == KBTS__HANGUL_SYLLABLE_TYPE_T)
              {
                T = TCodepoint;

                NextIndex = kbts__NextGlyphSlot(Storage, NextIndex);
              }
            }

            // Check for any tone marks that we need to swap to the front of the syllable.
            // The OpenType shaping documents say that we need to do this after applying GSUB features, but
            // harfbuzz does it before, so it's probably fine to do it here?
            // It's also basically free to do here, which is nice.
            if(NextIndex != Storage->End)
            {
              kbts_u32 ToneMarkCodepoint = Storage->Glyphs[NextIndex].Codepoint;

              if((ToneMarkCodepoint >= 0x302E) && (ToneMarkCodepoint <= 0x302F))
              {
                LvtGlyphs[LvtGlyphCount++] = kbts_CodepointToGlyph(Font, (int)ToneMarkCodepoint, 0, 0);

                NextIndex = kbts__NextGlyphSlot(Storage, NextIndex);
              }
            }

            if(LInfo.Composable & VInfo.Composable & TInfo.Composable)
            {
              // Try LVT.
              kbts_un LvtCodepoint = 0xAC00 + (L - 0x1100) * 588 + (V - 0x1161) * 28 + (T - 0x11A7);

              kbts_glyph LvtGlyph = kbts_CodepointToGlyph(Font, (int)LvtCodepoint, 0, 0);
              if(LvtGlyph.Id)
              {
                LvtGlyphs[LvtGlyphCount++] = LvtGlyph;
              }
            }

            /* REVIEW[GLYPH4 SUSPECT]: LvtGlyphCount includes a prefixed tone mark,
             * yet zero count also selects the LV/L-V fallback. For L+V+tone with
             * no T, the tone makes this branch ineligible before any syllable is
             * emitted. Unproved: trace that case through deletion below and separate
             * prefix count from successful syllable construction if needed. */
            if(!LvtGlyphCount)
            {
              kbts_glyph LvGlyph = KBTS__ZERO;
              if(LInfo.Composable & VInfo.Composable)
              {
                // Try LV.
                kbts_un LvCodepoint = 0xAC00 + (L - 0x1100) * 588 + (V - 0x1161) * 28;

                LvGlyph = kbts_CodepointToGlyph(Font, (int)LvCodepoint, 0, 0);
              }

              if(LvGlyph.Id)
              {
                LvtGlyphs[LvtGlyphCount++] = LvGlyph;
              }
              else
              {
                // Do L-V.
                kbts_glyph LGlyph = kbts_CodepointToGlyph(Font, (int)L, 0, 0);
                LGlyph.Flags |= KBTS_GLYPH_FLAG_LJMO;

                kbts_glyph VGlyph = kbts_CodepointToGlyph(Font, (int)V, 0, 0);
                VGlyph.Flags |= KBTS_GLYPH_FLAG_VJMO;

                LvtGlyphs[LvtGlyphCount++] = LGlyph;
                LvtGlyphs[LvtGlyphCount++] = VGlyph;
              }

              if(T)
              {
                kbts_glyph TGlyph = kbts_CodepointToGlyph(Font, (int)T, 0, 0);
                TGlyph.Flags |= KBTS_GLYPH_FLAG_TJMO;

                LvtGlyphs[LvtGlyphCount++] = TGlyph;
              }
            }
          }
        }

        if(!LvtGlyphCount)
        {
          kbts_glyph NewGlyph = kbts_CodepointToGlyph(Font, (int)CurrentGlyph->Codepoint, 0, 0);

          LvtGlyphs[LvtGlyphCount++] = NewGlyph;
        }

        /* REVIEW[GLYPH5 SUSPECT]: Every replacement above comes from
         * CodepointToGlyph(..., 0, 0), including the unchanged-codepoint fallback;
         * deleting the input therefore discards its source ID and feature Config.
         * Unproved: check nonzero source IDs and per-glyph overrides through Hangul,
         * and define which consumed source each output (including tone marks) owns. */
        { // Insert the LVT glyphs.
          kbts_glyph_ref Glyph = CurrentGlyph->Ref;
          kbts_glyph_ref Next = NextIndex != Storage->End ? Storage->Glyphs[NextIndex].Ref : 0;
          while(Glyph != Next)
          {
            Glyph = kbts__FreeGlyph(Scratchpad, Storage, Glyph);
          }

          for(kbts_un LvtGlyphIndex = 0;
              LvtGlyphIndex < LvtGlyphCount;
              ++LvtGlyphIndex)
          {
            kbts_glyph *LvtGlyph = &LvtGlyphs[LvtGlyphIndex];
            kbts_glyph_ref NewGlyph = kbts__InsertGlyphBefore(Storage, Next, LvtGlyph);

            if(!NewGlyph)
            {
              goto OutOfMemory;
            }
          }
          GlyphIndex = NextIndex;
        }
      }

      KBTS_INSTRUMENT_BLOCK_END(NORMALIZE_HANGUL);
    }
    break;

    case KBTS__OP_KIND_BEGIN_GSUB:
      break;

    case KBTS__OP_KIND_GSUB_FEATURES:
    {
      KB_PROFILE_SCOPE(KBP_GSUB);
      KBTS_INSTRUMENT_BLOCK_BEGIN(GSUB_FEATURES);

      // Script operations between feature stages may reorder the logical sequence.
      // Each invocation uses the current active glyph range.
      kbts_un FeatureStageIndex = kbts__CurrentBakedFeatureStageIndex(Scratchpad);
      kbts_un FirstSequentialLookupIndex = Config->FeatureStageFirstLookupIndices[FeatureStageIndex];
      kbts_un OnePastLastSequentialLookupIndex = Config->FeatureStageFirstLookupIndices[FeatureStageIndex + 1];
      kbts__RunGsubStages(Scratchpad, Config, Storage, FirstSequentialLookupIndex,
          OnePastLastSequentialLookupIndex);
      if(Scratchpad->Error) return;

      Scratchpad->SequentialLookupIndexIndex = (kbts_u32)OnePastLastSequentialLookupIndex;

      KBTS_INSTRUMENT_BLOCK_END(GSUB_FEATURES);
    }
    break;

    case KBTS__OP_KIND_FLAG_JOINING_LETTERS:
    {
      KBTS_INSTRUMENT_BLOCK_BEGIN(FLAG_JOINING_LETTERS);
      kbts_u64 JoiningTypesMatchLookup =
        (((1ull << KBTS_UNICODE_JOINING_TYPE_RIGHT) | (1ull << KBTS_UNICODE_JOINING_TYPE_DUAL) | (1ull << KBTS_UNICODE_JOINING_TYPE_FORCE)) << (8 * KBTS_UNICODE_JOINING_TYPE_LEFT)) |
        (((1ull << KBTS_UNICODE_JOINING_TYPE_RIGHT) | (1ull << KBTS_UNICODE_JOINING_TYPE_DUAL) | (1ull << KBTS_UNICODE_JOINING_TYPE_FORCE)) << (8 * KBTS_UNICODE_JOINING_TYPE_DUAL)) |
        (((1ull << KBTS_UNICODE_JOINING_TYPE_RIGHT) | (1ull << KBTS_UNICODE_JOINING_TYPE_DUAL) | (1ull << KBTS_UNICODE_JOINING_TYPE_FORCE)) << (8 * KBTS_UNICODE_JOINING_TYPE_FORCE));

      kbts_u64 JoiningFeatureTransition = ((kbts_u64)KBTS_JOINING_FEATURE_INIT << (8 * KBTS_JOINING_FEATURE_ISOL)) | ((kbts_u64)KBTS_JOINING_FEATURE_MEDI << (8 * KBTS_JOINING_FEATURE_FINA)) |
                                          ((kbts_u64)KBTS_JOINING_FEATURE_MEDI << (8 * KBTS_JOINING_FEATURE_MEDI)) | ((kbts_u64)KBTS_JOINING_FEATURE_MED2 << (8 * KBTS_JOINING_FEATURE_MED2));

      // Tag letters for joining features.
      kbts_glyph PreviousGlyph_ = KBTS__ZERO;
      PreviousGlyph_.JoiningType = Scratchpad->JoiningBefore;
      PreviousGlyph_.JoiningFeature = KBTS_JOINING_FEATURE_ISOL;
      kbts_glyph *PreviousGlyph = &PreviousGlyph_;
      for(kbts_u32 GlyphIndex = Storage->First; GlyphIndex != Storage->End; GlyphIndex = kbts__NextGlyphSlot(Storage, GlyphIndex))
      {
        kbts_glyph *Glyph = &Storage->Glyphs[GlyphIndex];
        if(Glyph->JoiningType != KBTS_UNICODE_JOINING_TYPE_TRANSPARENT)
        {
          Glyph->JoiningFeature = (kbts_joining_feature)(!PreviousGlyph->JoiningType ? KBTS_JOINING_FEATURE_INIT : KBTS_JOINING_FEATURE_FINA);

          if(JoiningTypesMatchLookup & (1ull << (Glyph->JoiningType + 8 * PreviousGlyph->JoiningType)))
          {
            PreviousGlyph->JoiningFeature = (JoiningFeatureTransition >> (8 * PreviousGlyph->JoiningFeature)) & 0xFF;
            PreviousGlyph->Flags = (PreviousGlyph->Flags & ~KBTS__JOINING_FEATURE_MASK) | KBTS__JOINING_FEATURE_TO_GLYPH_FLAG(PreviousGlyph->JoiningFeature);

            Glyph->JoiningFeature = KBTS_JOINING_FEATURE_FINA;
          }
          else
          {
            Glyph->JoiningFeature = KBTS_JOINING_FEATURE_ISOL;
          }

          if(Glyph->JoiningFeature)
          {
            // Be careful that this properly maps kbts_joining_feature to KBTS_GLYPH_FLAG!
            Glyph->Flags = (Glyph->Flags & ~KBTS__JOINING_FEATURE_MASK) | KBTS__JOINING_FEATURE_TO_GLYPH_FLAG(Glyph->JoiningFeature);
          }

          PreviousGlyph = Glyph;
        }
      }
      if(PreviousGlyph != &PreviousGlyph_ &&
         (JoiningTypesMatchLookup & (1ull << (Scratchpad->JoiningAfter + 8 * PreviousGlyph->JoiningType))))
      {
        PreviousGlyph->JoiningFeature = (JoiningFeatureTransition >> (8 * PreviousGlyph->JoiningFeature)) & 0xFF;
        PreviousGlyph->Flags = (PreviousGlyph->Flags & ~KBTS__JOINING_FEATURE_MASK) |
          KBTS__JOINING_FEATURE_TO_GLYPH_FLAG(PreviousGlyph->JoiningFeature);
      }

      KBTS_INSTRUMENT_BLOCK_END(FLAG_JOINING_LETTERS);
    }
    break;

    case KBTS__OP_KIND_GPOS_METRICS:
    {
      KB_PROFILE_SCOPE(KBP_GPOS_METRICS);
      KBTS_INSTRUMENT_BLOCK_BEGIN(GPOS_METRICS);
      // GSUB owns no lookup buckets. Seed only the positioning lookups now,
      // after resizing and cluster reordering have established final logical order.
      kbts_u32 SortKey = 1;
      for(kbts_u32 GlyphIndex = Storage->First; GlyphIndex != Storage->End; GlyphIndex = kbts__NextGlyphSlot(Storage, GlyphIndex))
      {
        kbts_glyph *Glyph = &Storage->Glyphs[GlyphIndex];
        Glyph->SortKey = SortKey;
        SortKey += 1;
        kbts__BucketGlyph(Scratchpad, Glyph, Scratchpad->GposFirstLookup, 0);
        if(Scratchpad->Error) return;
      }
      // hmtx/vmtx pass.
      kbts_b32 ClearMarkAdvances = (Config->Shaper == KBTS_SHAPER_MYANMAR) || (Config->Shaper == KBTS_SHAPER_USE);

      kbts_u32 Orientation = KBTS_ORIENTATION_HORIZONTAL; // @Hardcoded
      kbts_blob_table_id HeaTableId = KBTS_BLOB_TABLE_ID_HHEA;
      kbts_blob_table_id MtxTableId = KBTS_BLOB_TABLE_ID_HMTX;
      if(Orientation == KBTS_ORIENTATION_VERTICAL)
      {
        HeaTableId = KBTS_BLOB_TABLE_ID_VHEA;
        MtxTableId = KBTS_BLOB_TABLE_ID_VMTX;
      }

      kbts__hea *Hea = kbts__BlobTableDataType(Font->Blob, HeaTableId, kbts__hea);
      kbts_u16 *Mtx = kbts__BlobTableDataType(Font->Blob, MtxTableId, kbts_u16);

      kbts__long_mtx *LongMetrics = 0;
      // :LeftSideBearing
      // kbts_s16 *ShortMetrics = 0;
      if(Hea && Mtx)
      {
        LongMetrics = (kbts__long_mtx *)Mtx;
        // :LeftSideBearing
        // ShortMetrics = (kbts_s16 *)(LongMetrics + Hea->MetricCount);
      }

      kbts__long_mtx DefaultMetric = {1024, 0};
      kbts__head *Head = kbts__BlobTableDataType(Font->Blob, KBTS_BLOB_TABLE_ID_HEAD, kbts__head);
      if(Head)
      {
        DefaultMetric.Advance = Head->UnitsPerEm;
        if(Orientation == KBTS_ORIENTATION_HORIZONTAL)
        {
          DefaultMetric.Advance /= 2;
        }
      }

      Storage->AttachmentEnd = 0;
      for(kbts_u32 GlyphIndex = Storage->First; GlyphIndex != Storage->End; GlyphIndex = kbts__NextGlyphSlot(Storage, GlyphIndex))
      {
        kbts_glyph *Glyph = &Storage->Glyphs[GlyphIndex];
        kbts__SetCursiveFlags(Glyph, 0);
        if(Glyph->AttachGlyph) Storage->AttachmentEnd = Glyph->SortKey;

        kbts__long_mtx Metric = DefaultMetric;
        if(LongMetrics)
        {
          kbts_u32 Id = Glyph->Id;

          // At the end of shaping, default ignorable glyphs that are not generated by GSUB are replaced with zero-width
          // whitespace glyphs (or a zero-width empty glyph if no whitespace glyph is present).
          // (By the way, we do this because Harfbuzz does it, and Harfbuzz does it probably because Uniscribe does it.)
          // We handle this in two steps:
          // - The first is here. We want cursive attachments, and mark-to-base attachments, and other relative placements
          //   to take the zero width into account, and zeroing the width right at the beginning of GPOS is the most
          //   straighforward way to accomplish this.
          //   (@Incomplete: It is likely that we also need to take the default ignorable case into account when accumulating
          //    cursive offsets.)
          // - In POST_GPOS_FIXUP, we perform the glyph ID substitution. We have to wait until all GPOS features have been
          //   executed to do this because they might possibly match the original ID.
          if(!(Glyph->Flags & KBTS_GLYPH_FLAG_GENERATED_BY_GSUB) && (Glyph->UnicodeFlags & KBTS_UNICODE_FLAG_DEFAULT_IGNORABLE))
          {
            Metric.Advance = 0;
            // :LeftSideBearing
            // Metric.PreviousSideBearing = 0;
          }
          else if(Id < Hea->MetricCount)
          {
            Metric = LongMetrics[Id];
          }
          else
          {
            Metric.Advance = LongMetrics[Hea->MetricCount - 1].Advance;
            // :LeftSideBearing
            // Metric.PreviousSideBearing = ShortMetrics[Id - Hea->MetricCount];
          }
        }

        if(!ClearMarkAdvances | (Glyph->Classes.Class != KBTS__GLYPH_CLASS_MARK))
        {
          if(Orientation == KBTS_ORIENTATION_HORIZONTAL)
          {
            // :LeftSideBearing
            // Why does harfbuzz not take bearings into account in these tests?
            // P.X += Metric.PreviousSideBearing;
            Glyph->AdvanceX = Metric.Advance;
          }
          else
          {
            // :LeftSideBearing
            // Why does harfbuzz not take bearings into account in these tests?
            // P.Y += Metric.PreviousSideBearing;
            Glyph->AdvanceY = Metric.Advance;
          }
        }
      }

      KBTS_INSTRUMENT_BLOCK_END(GPOS_METRICS);
    }
    break;

    case KBTS__OP_KIND_GPOS_FEATURES:
    {
      KB_PROFILE_SCOPE(KBP_GPOS);
      KBTS_INSTRUMENT_BLOCK_BEGIN(GPOS_FEATURES);

      kbts__gsub_gpos *Gpos = kbts__BlobTableDataType(Font->Blob, KBTS_BLOB_TABLE_ID_GPOS, kbts__gsub_gpos);
      kbts__gdef *Gdef = kbts__BlobTableDataType(Font->Blob, KBTS_BLOB_TABLE_ID_GDEF, kbts__gdef);

      kbts_lookup_list *LookupList = kbts__GetLookupList(Gpos);
      kbts_un FeatureStageIndex = kbts__CurrentBakedFeatureStageIndex(Scratchpad);

      kbts_un FirstSequentialLookupIndex = Config->FeatureStageFirstLookupIndices[FeatureStageIndex];
      kbts_un OnePastLastSequentialLookupIndex = Config->FeatureStageFirstLookupIndices[FeatureStageIndex + 1];
      KBTS__FOR(SequentialLookupIndex, FirstSequentialLookupIndex, OnePastLastSequentialLookupIndex)
      {
        kbts__sequential_lookup *SequentialLookup = &Config->SequentialLookups[SequentialLookupIndex];
        kbts_un LookupIndex = SequentialLookup->LookupIndex;
        kbts_u32 SkipFlags = SequentialLookup->SkipFlags;

        kbts__bucketed_glyph_block_header *Sentinel = &Scratchpad->LookupGlyphBuckets[SequentialLookupIndex - Scratchpad->GposFirstLookup];

        if(Sentinel->Next != Sentinel)
        {
          kbts__lookup *PackedLookup = kbts__GetLookup(LookupList, LookupIndex);
          KB_PROFILE_LOOKUP_SCOPE(1, LookupIndex, PackedLookup->Type, KBL_TOTAL);
          kbts__unpacked_lookup Lookup = kbts__UnpackLookup(Gdef, PackedLookup);
          kbts_un SubtableCount = PackedLookup->SubtableCount;

          KBTS_INSTRUMENT_BLOCK_BEGIN(GposSortBucket);

          if(PackedLookup->Type >= 2)
          {
            KB_PROFILE_LOOKUP_SCOPE(1, LookupIndex, PackedLookup->Type, KBL_SORT);
            kbts__SortGlyphBucket(Scratchpad, SequentialLookupIndex);
            if(Scratchpad->Error) return;
          }

          KBTS_INSTRUMENT_BLOCK_END(GposSortBucket);

          KB_PROFILE_LOOKUP_SCOPE(1, LookupIndex, PackedLookup->Type, KBL_APPLY);
          for(kbts__bucketed_glyph_block_header *BlockHeader = Sentinel->Next;
              BlockHeader != Sentinel;
              BlockHeader = BlockHeader->Next)
          {
            kbts__bucketed_glyph_block *Block = (kbts__bucketed_glyph_block *)BlockHeader;
            kbts_un BlockGlyphCount = Block->Count;

            KBTS__FOR(GlyphIndex, 0, BlockGlyphCount)
            {
              kbts__bucketed_glyph *Bucketed = &Block->Glyphs[GlyphIndex];

              if(kbts__BucketedGlyphIsValid(Bucketed))
              {
                kbts_glyph_ref Glyph = Bucketed->Glyph;
                kbts__BeginLookupApplication(Scratchpad, Glyph);

                kbts_u16 *SubtableOffsets = KBTS__POINTER_AFTER(kbts_u16, PackedLookup);

                KBTS__FOR(SubtableIndex, 0, SubtableCount)
                {
                  kbts_u16 *Subtable = KBTS__POINTER_OFFSET(kbts_u16, PackedLookup, SubtableOffsets[SubtableIndex]);

                  if(kbts__DoSingleAdjustment(Scratchpad, Config, Storage,
                                              LookupList, LookupIndex, SubtableIndex, &Lookup, Subtable,
                                              Glyph, 0, SkipFlags))
                  {
                    break;
                  }
                }

                kbts_glyph_ref OnePastLast = kbts__EndLookupApplication(Scratchpad);

                KBTS_INSTRUMENT_BLOCK_BEGIN(GposRebucketTouchedGlyphs);

                for(kbts_glyph_ref Matched = Glyph;
                    Matched != OnePastLast;
                    Matched = kbts__NextGlyphRef(Storage, Matched))
                {
                  kbts_glyph *MatchedGlyph = kbts__Glyph(Storage, Matched);
                  // Queue for the next bucket.

                  if(MatchedGlyph->Bucketed &&
                     (MatchedGlyph->BucketedBucketIndex == SequentialLookupIndex))
                  {
                    // We are scheduled for the current bucket. Cancel that, and move on to the next.
                    kbts__UnbucketGlyph(Scratchpad, MatchedGlyph);
                    kbts__BucketGlyph(Scratchpad, MatchedGlyph, SequentialLookupIndex + 1, 1);
                  }
                }

                KBTS_INSTRUMENT_BLOCK_END(GposRebucketTouchedGlyphs);
              }
            }
          }
        }

        kbts__FreeGlyphBucket(Scratchpad, SequentialLookupIndex);
      }

      KBTS_INSTRUMENT_BLOCK_END(GPOS_FEATURES);
    }
    break;

    case KBTS__OP_KIND_POST_GPOS_FIXUP:
    {
      KB_PROFILE_SCOPE(KBP_GPOS_FIXUP);
      KBTS_INSTRUMENT_BLOCK_BEGIN(POST_GPOS_FIXUP);
      kbts_glyph WhitespaceGlyph = Config->Whitespace;
      int ClearMarkAdvances = kbts__ShaperClearsMarkAdvancesInPostGposFixup(Config->Shaper);

      if(Scratchpad->RunDirection == KBTS_DIRECTION_RTL)
      {
        // Flip direction.
        // This might seem like a totally superfluous thing to do, because we have to do a bunch
        // of work to reverse glyph order while still preserving their relative positions.
        // However, for mainly LTR documents, the anchor position will naturally be left-aligned,
        // while, in RTL documents, it will naturally right-align.
        // As such, going through the glyph sequence and reversing it is needed _anyway_ at some point,
        // and we might as well do it here, because this is where we can take resolve attachments
        // for relatively cheap, since they are always back-looking. (The next paragraph explains this
        // in more detail.)

        // The unintuitive part about this pass is the glyph advances.
        // Normally, when iterating over a sequence of glyphs, we see base glyphs before marks. Obviously.
        // However, when flipping the sequence, we see the marks before seeing the bases, which means we
        // won't have accumulated the base glyph's advance yet! So we have to go through the sequence here
        // and compensate for missing advances by precomputing them and baking them into the marks.
        // Another way to reach the same result would be to keep marks on the same side as their bases when
        // flipping, but that is not what Harfbuzz does, and, the day we decide we want to diverge from Harfbuzz,
        // we will very likely be better off not flipping the sequence at all and deleting all of this garbage code.
        kbts_s32 MarkAttachAdvanceX = 0;
        kbts_s32 MarkAttachAdvanceY = 0;
        for(kbts_u32 GlyphIndex = Storage->First; GlyphIndex != Storage->End; GlyphIndex = kbts__NextGlyphSlot(Storage, GlyphIndex))
        {
          kbts_glyph *Glyph = &Storage->Glyphs[GlyphIndex];
          if(Glyph->AttachGlyph)
          {
            kbts_glyph *Attach = kbts__Glyph(Storage, Glyph->AttachGlyph);
            kbts_s32 AttachAdvanceX = MarkAttachAdvanceX;
            kbts_s32 AttachAdvanceY = MarkAttachAdvanceY;
            if(Attach->Classes.Class != KBTS__GLYPH_CLASS_MARK)
            {
              AttachAdvanceX = Attach->AdvanceX;
              AttachAdvanceY = Attach->AdvanceY;
            }
            Glyph->OffsetX += AttachAdvanceX;
            Glyph->OffsetY += AttachAdvanceY;
            MarkAttachAdvanceX = AttachAdvanceX;
            MarkAttachAdvanceY = AttachAdvanceY;
          }
        }

        kbts__ReverseGlyphRange(Storage, 0, 0);
      }

      // Make default ignorables that weren't explicitly created by the font invisible.
      //
      // It is tempting to put this loop inside of an if(WhitespaceGlyph.Id), just like we do
      // for dotted circle insertion. However, Harfbuzz does not do this!
      // When the font does not contain a dotted circle, nothing is inserted, but when the font
      // does not contain a whitespace glyph, _we still insert an empty glyph_.
      for(kbts_u32 GlyphIndex = Storage->First; GlyphIndex != Storage->End;)
      {
        kbts_glyph *Glyph = &Storage->Glyphs[GlyphIndex];
        int Keep = 0;

        // It might be tempting to keep glyphs that were used in GPOS, but Harfbuzz doesn't care.
        if(!(Glyph->Flags & KBTS_GLYPH_FLAG_GENERATED_BY_GSUB) && (Glyph->UnicodeFlags & KBTS_UNICODE_FLAG_DEFAULT_IGNORABLE))
        {
          // This glyph is default ignorable and should be ignored.
          if(WhitespaceGlyph.Id)
          {
            Keep = 1;

            kbts_glyph_config *GlyphConfig = Glyph->Config;
            kbts__SetGlyphPreserveIdentityAndUserId(Glyph, &WhitespaceGlyph);
            Glyph->Config = GlyphConfig;
          }
        }
        else
        {
          if(ClearMarkAdvances && (Glyph->Classes.Class == KBTS__GLYPH_CLASS_MARK))
          {
            Glyph->AdvanceX = Glyph->AdvanceY = 0;
          }
          Keep = 1;
        }

        if(!Keep)
        {
          kbts_u32 Next = kbts__NextGlyphSlot(Storage, GlyphIndex);
          kbts__FreeGlyph(Scratchpad, Storage, Glyph->Ref);
          GlyphIndex = Next;
        }
        else
        {
          GlyphIndex = kbts__NextGlyphSlot(Storage, GlyphIndex);
        }
      }

      KBTS_INSTRUMENT_BLOCK_END(POST_GPOS_FIXUP);
    }
    break;

    case KBTS__OP_KIND_STCH_POSTPASS:
    break;
    }

    if(0)
    {
      OutOfMemory:;
      Scratchpad->Error = KBTS_SHAPE_ERROR_OUT_OF_MEMORY;
    }
  }

  KBTS_INSTRUMENT_FUNCTION_END;
}
