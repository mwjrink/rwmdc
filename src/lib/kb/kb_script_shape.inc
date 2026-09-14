typedef struct kbts__attach_state
{
  kbts_u32 CurrentKey;
  kbts_u32 LastKeyThatWasNotPreBaseMatra;
  kbts_indic_syllabic_class LastClass;
  kbts_glyph_ref OnePastLast;
  kbts_glyph_ref At;
} kbts__attach_state;

static kbts_glyph_ref kbts__BeginCluster(kbts_shape_scratchpad *Scratchpad, kbts_glyph_storage *Storage,
                                      kbts_glyph_ref Glyph)
{
  kbts_shape_config *Config = Scratchpad->Config;
  kbts_font *Font = Config->Font;
  kbts_glyph_ref Result = kbts__NextGlyphRef(Storage, Glyph);

  Scratchpad->RealCluster = 0;

  switch(Config->Shaper)
  {
  case KBTS_SHAPER_INDIC:
  {
    kbts_lookup_list *LookupList = kbts__GetLookupList(kbts__BlobTableDataType(Font->Blob, KBTS_BLOB_TABLE_ID_GSUB, kbts__gsub_gpos));

    int NonCluster = 0;
    while(kbts__GlyphIsValid(Storage, Glyph) &&
          ((kbts__Glyph(Storage, Glyph)->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_OTHER) ||
           (kbts__Glyph(Storage, Glyph)->SyllabicClass >= KBTS_INDIC_SYLLABIC_CLASS_COUNT)))
    {
      NonCluster = 1;
      Glyph = kbts__NextGlyphRef(Storage, Glyph);
    }

    Result = Glyph;

    if(!NonCluster)
    {
      kbts_un ScanGlyphIndex = 0;
      kbts_un State = 0;
      kbts_un Broken = 1;
      kbts_un BrokenState = 0;
      kbts_glyph_ref FirstGlyphs[3];

      while(kbts__GlyphIsValid(Storage, Glyph) &&
            (State < KBTS_INDIC_SYLLABIC_STATE_COUNT))
      {
        kbts_un Class = kbts__Glyph(Storage, Glyph)->SyllabicClass;

        if(KBTS__IN_SET(Class, KBTS__SET32((KBTS_INDIC_SYLLABIC_CLASS_RA)
                                         (KBTS_INDIC_SYLLABIC_CLASS_CONSONANT)
                                         (KBTS_INDIC_SYLLABIC_CLASS_VOWEL)
                                         (KBTS_INDIC_SYLLABIC_CLASS_DOTTED_CIRCLE)
                                         (KBTS_INDIC_SYLLABIC_CLASS_PLACEHOLDER)
                                         (KBTS_INDIC_SYLLABIC_CLASS_SYMBOL))))
        {
          Broken = 0;
        }
        BrokenState = (BrokenState << 1) | Broken;

        if(ScanGlyphIndex < KBTS__ARRAY_LENGTH(FirstGlyphs))
        {
          FirstGlyphs[ScanGlyphIndex] = Glyph;
        }

        State = kbts_IndicSyllabicTransition[Class][State];
        State -= 1; // @Incomplete @Cleanup: Decrement every state by 1 in the transition table.
        Glyph = kbts__NextGlyphRef(Storage, Glyph);
        ScanGlyphIndex += 1;
      }

      if(State > KBTS_INDIC_SYLLABIC_STATE_COUNT)
      {
        ScanGlyphIndex -= State - KBTS_INDIC_SYLLABIC_STATE_COUNT;
        BrokenState >>= State - KBTS_INDIC_SYLLABIC_STATE_COUNT;

        while(State > KBTS_INDIC_SYLLABIC_STATE_COUNT)
        {
          Glyph = kbts__PrevGlyphRef(Storage, Glyph);
          State -= 1;
        }

        if(!ScanGlyphIndex)
        {
          // If we backtrack all the way to the beginning, still eat a character.
          ScanGlyphIndex = 1;
        }
      }

      // NOTE: Symbols, vedics, modifiers, matras have their syllabic positions tagged at build time.
      // It is tempting to use Unicode's positional information for consonants, too, but OpenType
      // _strongly_ recommends that shapers use the font's tables instead, ie. doing tentative lookups
      // with specific features.

      kbts__gsub_frame *Frames = (kbts__gsub_frame *)Scratchpad->ScratchMemory;
      kbts_glyph_ref OnePastLastSyllableGlyph = Glyph;
      kbts_glyph_ref FirstGlyph = FirstGlyphs[0];
      kbts_glyph_ref OneBeforeSyllableGlyph = kbts__PrevGlyphRef(Storage, FirstGlyph);

      if((Config->Script == KBTS_SCRIPT_KANNADA) && (ScanGlyphIndex >= 3) &&
         (kbts__Glyph(Storage, FirstGlyphs[0])->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_RA) &&
         (kbts__Glyph(Storage, FirstGlyphs[1])->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_HALANT) &&
         (kbts__Glyph(Storage, FirstGlyphs[2])->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_ZWJ))
      {
        // In older versions of Unicode (~4.x, pre-2004), Ra-Virama-Zwj was recommended in Kannada
        // to communicate the intent of displaying (Ra + (next consonant as below-base form)).
        // This was changed in Unicode 5. Now, the recommended sequence to display (Ra +
        // (next consonant as below-base form)) is Ra-Zwj-Virama.
        // Since Ra-Virama-Zwj is not a useful sequence that happens organically in Kannada,
        // we are free to transform it into Ra-Zwj-Virama here without losing information.
        kbts__SwapGlyphs(Storage, FirstGlyphs[1], FirstGlyphs[2]);

        // Also swap this, because this is used below for OnePastReph.
        kbts_glyph_ref Swap = FirstGlyphs[1];
        FirstGlyphs[1] = FirstGlyphs[2];
        FirstGlyphs[2] = Swap;
      }

      kbts_glyph_flags RephFlags = KBTS_GLYPH_FLAG_RPHF;
      kbts_glyph_ref OnePastReph = FirstGlyphs[0];
      kbts_un OnePastRephIndex = 0;
      { // Reph tagging.
        // This first pass only figures out where the reph ends.
        // We delay updating glyph properties until we know whether the reph is the base or not.
        kbts_glyph_ref Second = (ScanGlyphIndex >= 2) ? FirstGlyphs[1] : 0;
        kbts_glyph_ref Third = (ScanGlyphIndex >= 3) ? FirstGlyphs[2] : 0;
        kbts__feature *Rphf = Config->Rphf;

        switch(Config->IndicScriptProperties.RephEncoding)
        {
        case KBTS__REPH_ENCODING_LOGICAL_REPHA:
          if(kbts__Glyph(Storage, FirstGlyph)->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_REPHA)
          {
            RephFlags = 0;
            OnePastRephIndex = 1;
          }
          break;

        case KBTS__REPH_ENCODING_IMPLICIT:
          if((ScanGlyphIndex >= 2) &&
             (kbts__Glyph(Storage, Second)->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_HALANT))
          {
            kbts_glyph Scratch[2];
            Scratch[0] = *kbts__Glyph(Storage, FirstGlyphs[0]);
            Scratch[1] = *kbts__Glyph(Storage, FirstGlyphs[1]);

            if(kbts__WouldSubstitute(Scratchpad, Config, Storage, LookupList, Frames, Rphf, 0, Scratch, 2))
            {
              OnePastRephIndex = 2;
            }
          }
          break;

        case KBTS__REPH_ENCODING_EXPLICIT:
          if((ScanGlyphIndex >= 3) &&
             (kbts__Glyph(Storage, Second)->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_HALANT) &&
             (kbts__Glyph(Storage, Third)->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_ZWJ))
          {
            kbts_glyph Scratch[3];
            Scratch[0] = *kbts__Glyph(Storage, FirstGlyphs[0]);
            Scratch[1] = *kbts__Glyph(Storage, FirstGlyphs[1]);
            Scratch[2] = *kbts__Glyph(Storage, FirstGlyphs[2]);

            if(kbts__WouldSubstitute(Scratchpad, Config, Storage, LookupList, Frames, Rphf, 0, Scratch, 3))
            {
              OnePastRephIndex = 3;
            }
          }
          break;
        }

        // Extend the reph with suffixed joiners.
        if(OnePastRephIndex)
        {
          OnePastReph = kbts__NextGlyphRef(Storage, FirstGlyphs[OnePastRephIndex - 1]);
          while(OnePastReph != OnePastLastSyllableGlyph &&
                KBTS__IN_SET(kbts__Glyph(Storage, OnePastReph)->SyllabicClass, KBTS__SET32((KBTS_INDIC_SYLLABIC_CLASS_ZWJ)
                                                                   (KBTS_INDIC_SYLLABIC_CLASS_ZWNJ))))
          {
            OnePastReph = kbts__NextGlyphRef(Storage, OnePastReph);
          }
        }
      }

      kbts__feature *Pref = Config->Pref;
      kbts_glyph_ref BaseGlyph = OnePastLastSyllableGlyph;
      kbts_glyph_ref LastConsonant = 0;
      kbts__syllabic_position LastConsonantPosition = KBTS__SYLLABIC_POSITION_SYLLABLE_BASE;
      if(Config->DottedCircle.Id && (BrokenState & 1))
      {
        BaseGlyph = kbts__InsertGlyphBefore(Storage, OnePastReph, &Config->DottedCircle);
        if(!BaseGlyph)
        {
          goto OutOfMemory;
        }

        kbts__Glyph(Storage, BaseGlyph)->UserIdOrCodepointIndex = kbts__Glyph(Storage, OnePastReph)->UserIdOrCodepointIndex;
        /* The inserted base is now the first glyph after reph. Keeping the
         * old boundary would start the forward pre-base attachment walk after
         * its end and let it escape into following clusters. */
        OnePastReph = BaseGlyph;
      }
      else
      {
        // Going backwards, tag blwf and pstf consonants until we get to the base.
        kbts_glyph Scratch[3];
        Scratch[0] = Config->Virama;
        Scratch[2] = Config->Virama;

        kbts__feature *Locl = Config->Locl;
        kbts__feature *Vatu = Config->Vatu;

        // pstf forms come last, then blwf.
        kbts__feature *SectionFeature = Config->Pstf;
        kbts__syllabic_position SectionPosition = KBTS__SYLLABIC_POSITION_POSTBASE_CONSONANT;
        // If we see no consonant/matra, then surely we want to attach to the base.
        kbts__syllabic_position SyllabicPosition = KBTS__SYLLABIC_POSITION_SYLLABLE_BASE;
        kbts_indic_syllabic_class LastClass = KBTS_INDIC_SYLLABIC_CLASS_COUNT;

        /* Search only the post-reph interval. It may be empty when the
         * entire cluster is reph; that case uses the base fallback below. */
        for(Glyph = OnePastLastSyllableGlyph; Glyph != OnePastReph;)
        {
          Glyph = kbts__PrevGlyphRef(Storage, Glyph);
          kbts_indic_syllabic_class Class = kbts__Glyph(Storage, Glyph)->SyllabicClass;

          switch(Class)
          {
          case KBTS_INDIC_SYLLABIC_CLASS_CONSONANT:
          case KBTS_INDIC_SYLLABIC_CLASS_RA:
          case KBTS_INDIC_SYLLABIC_CLASS_CONSONANT_WITH_STACKER:
          case KBTS_INDIC_SYLLABIC_CLASS_CONSONANT_MEDIAL:
            // Apply locl first.
            Scratch[1] = kbts__Substitute1(Scratchpad, Config, 0, LookupList, Locl, KBTS__SKIP_FLAG_ZWNJ | KBTS__SKIP_FLAG_ZWJ, kbts__Glyph(Storage, Glyph));

            // Microsoft says to "move backwards until a consonant is found that does not have a below-base
            // or post-base form".
            // As with everything else the spec says, it is wrong. There are cases in which the only consonant
            // in a syllable has a below-base or post-base form. In those cases, we still want them to be the
            // syllable base.
            // So, as we sweep backward, set the base index on consonants unconditionally.
            // @Robustness: Do we want to update Base only if we do not match Pref/Vatu?
            BaseGlyph = Glyph;

            if(kbts__WouldSubstitute(Scratchpad, Config, Storage, LookupList, Frames, Pref, 0, Scratch, 2) ||
               kbts__WouldSubstitute(Scratchpad, Config, Storage, LookupList, Frames, Pref, 0, Scratch + 1, 2) ||
               kbts__WouldSubstitute(Scratchpad, Config, Storage, LookupList, Frames, Vatu, 0, Scratch, 2) ||
               kbts__WouldSubstitute(Scratchpad, Config, Storage, LookupList, Frames, Vatu, 0, Scratch + 1, 2))
            {
              SyllabicPosition = KBTS__SYLLABIC_POSITION_POSTBASE_CONSONANT;
            }
            else
            {
              for(;;)
              {
                if(kbts__WouldSubstitute(Scratchpad, Config, Storage, LookupList, Frames, SectionFeature, 0, Scratch, 2) ||
                   kbts__WouldSubstitute(Scratchpad, Config, Storage, LookupList, Frames, SectionFeature, 0, Scratch + 1, 2))
                {
                  SyllabicPosition = SectionPosition;
                  break;
                }
                else if(SectionPosition == KBTS__SYLLABIC_POSITION_POSTBASE_CONSONANT)
                {
                  SectionFeature = Config->Blwf;
                  SectionPosition = KBTS__SYLLABIC_POSITION_BELOWBASE_CONSONANT;
                  // Retry with Blwf.
                }
                else
                {
                  goto DoneScanningForBase;
                }
              }
            }

            if(!LastConsonant)
            {
              LastConsonant = Glyph;
              LastConsonantPosition = SyllabicPosition;
            }

            kbts__Glyph(Storage, Glyph)->SyllabicPosition = SyllabicPosition;
            break;

          case KBTS_INDIC_SYLLABIC_CLASS_VOWEL:
          case KBTS_INDIC_SYLLABIC_CLASS_PLACEHOLDER:
          case KBTS_INDIC_SYLLABIC_CLASS_DOTTED_CIRCLE:
            BaseGlyph = Glyph;
            goto DoneScanningForBase;
            break;

          case KBTS_INDIC_SYLLABIC_CLASS_HALANT:
            if(LastClass == KBTS_INDIC_SYLLABIC_CLASS_ZWJ)
            {
              // Explicit half form.
              // Since half forms are always before the base, we can safely stop here.
              goto DoneScanningForBase;
            }
            KBTS__FALLTHROUGH;
          case KBTS_INDIC_SYLLABIC_CLASS_NUKTA:
          case KBTS_INDIC_SYLLABIC_CLASS_ZWJ:
          case KBTS_INDIC_SYLLABIC_CLASS_ZWNJ:
            // @Incomplete: Register Shifter
            kbts__Glyph(Storage, Glyph)->SyllabicPosition = SyllabicPosition;
            break;
          }

          LastClass = Class;
        }
      DoneScanningForBase:;
      }

      // Fix reph position now.
      if(OnePastReph != FirstGlyph)
      {
        kbts__syllabic_position RephPosition = KBTS__SYLLABIC_POSITION_RA_TO_BECOME_REPH;
        kbts_glyph_flags Flags = RephFlags;
        kbts_un RephGlyphCount = OnePastRephIndex;
        if(BaseGlyph == OnePastLastSyllableGlyph)
        {
          RephPosition = KBTS__SYLLABIC_POSITION_SYLLABLE_BASE;
          Flags = 0;

          BaseGlyph = OnePastReph = FirstGlyph;
        }

        KBTS__FOR(GlyphIndex, 0, RephGlyphCount)
        {
          kbts_glyph_ref RephGlyph = FirstGlyphs[GlyphIndex];
          kbts__Glyph(Storage, RephGlyph)->SyllabicPosition = RephPosition;
          kbts__Glyph(Storage, RephGlyph)->Flags |= Flags;
        }
      }

      if(BaseGlyph != OnePastLastSyllableGlyph)
      {
        kbts__Glyph(Storage, BaseGlyph)->SyllabicPosition = KBTS__SYLLABIC_POSITION_SYLLABLE_BASE;

        if(!LastConsonant)
        {
          LastConsonant = BaseGlyph;
          // LastConsonantPosition is already set to SYLLABLE_BASE.
        }
      }

      { // Reorder marks.
        // @Cleanup @Speed: Supposedly, NORMALIZE already does this...
        kbts_glyph_ref ReorderGlyph = kbts__PrevGlyphRef(Storage, OnePastLastSyllableGlyph);
        while(ReorderGlyph != OneBeforeSyllableGlyph)
        {
          if(kbts__Glyph(Storage, ReorderGlyph)->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_NUKTA)
          {
            kbts_glyph_ref Prev = kbts__PrevGlyphRef(Storage, ReorderGlyph);

            while((Prev != OneBeforeSyllableGlyph) &&
                  KBTS__IN_SET(kbts__Glyph(Storage, Prev)->SyllabicClass, KBTS__SET32((KBTS_INDIC_SYLLABIC_CLASS_HALANT)
                                                              (KBTS_INDIC_SYLLABIC_CLASS_VEDIC_SIGN))))
            {
              kbts_glyph_ref NextPrev = kbts__PrevGlyphRef(Storage, Prev);

              kbts__SwapGlyphs(Storage, Prev, kbts__NextGlyphRef(Storage, Prev));

              Prev = NextPrev;
            }

            ReorderGlyph = Prev;
          }
          else
          {
            ReorderGlyph = kbts__PrevGlyphRef(Storage, ReorderGlyph);
          }
        }
      }

      { /* Script keys live only through this reorder. GPOS assigns fresh
         * logical-order keys after structural shaping has finished. */
        // Left matras sort backwards within their canonical syllabic position.
        kbts_un LeftMatraCount = 0;
        for(Glyph = kbts__NextGlyphRef(Storage, OneBeforeSyllableGlyph);
            Glyph != OnePastLastSyllableGlyph;
            Glyph = kbts__NextGlyphRef(Storage, Glyph))
        {
          kbts_un SyllabicPosition = kbts__Glyph(Storage, Glyph)->SyllabicPosition;

          SyllabicPosition <<= 4;

          if(SyllabicPosition == (KBTS__SYLLABIC_POSITION_PREBASE_MATRA << 4))
          {
            SyllabicPosition += (15 - LeftMatraCount++) & 0xF;
          }

          kbts__Glyph(Storage, Glyph)->SortKey = (kbts_u32)SyllabicPosition;
        }
      }

      { // Attach stuff to the right of consonants/matras.
        // Pre-base consonants, the base consonant, and matras all have right attachments,
        // while post-base consonants have left attachments.
        // Since matras always come after all consonants, we try to separate the buffer into three parts:
        // a pre-base+base part (right attachments), a post-base consonant part (left attachments), and
        // a post-consonant part (right attachments again).
        // This part handles right attachments. Left attachments were handled in the base syllable search.
        kbts__attach_state States[2] = {
          {
            KBTS__SYLLABIC_POSITION_RA_TO_BECOME_REPH << 4,
            KBTS__SYLLABIC_POSITION_RA_TO_BECOME_REPH << 4,
            KBTS_INDIC_SYLLABIC_CLASS_COUNT,
            BaseGlyph,
            OnePastReph,
          },
          {
            (kbts_u32)LastConsonantPosition << 4,
            (kbts_u32)LastConsonantPosition << 4,
            KBTS_INDIC_SYLLABIC_CLASS_COUNT,
            OnePastLastSyllableGlyph,
            LastConsonant ? kbts__NextGlyphRef(Storage, LastConsonant) : OnePastLastSyllableGlyph,
          },
        };
        for(;;)
        {
          int Done = 1;

          KBTS__FOR(StateIndex, 0, KBTS__ARRAY_LENGTH(States))
          {
            kbts__attach_state *Attach = &States[StateIndex];

            if(Attach->At != Attach->OnePastLast)
            {
              Glyph = Attach->At;

              kbts_indic_syllabic_class Class = kbts__Glyph(Storage, Glyph)->SyllabicClass;
              switch(Class)
              {
              case KBTS_INDIC_SYLLABIC_CLASS_CONSONANT:
              case KBTS_INDIC_SYLLABIC_CLASS_RA:
              case KBTS_INDIC_SYLLABIC_CLASS_CONSONANT_WITH_STACKER:
              case KBTS_INDIC_SYLLABIC_CLASS_CONSONANT_MEDIAL:
                // This only happens in the pre-base state.
                kbts__Glyph(Storage, Glyph)->SyllabicPosition = KBTS__SYLLABIC_POSITION_PREBASE_CONSONANT;
                kbts__Glyph(Storage, Glyph)->SortKey = KBTS__SYLLABIC_POSITION_PREBASE_CONSONANT << 4;
                Attach->CurrentKey = KBTS__SYLLABIC_POSITION_PREBASE_CONSONANT << 4;
                break;

              case KBTS_INDIC_SYLLABIC_CLASS_MATRA_POST:
                // This specific category (which is composed of a single character) may come after
                // a syllable modifier in a sequence like Consonant-SyllableModifier-MatraPost.
                // In this very particular case (!), the syllable modifier is attached _to the matra_,
                // and not to the consonant.
                if(Attach->LastClass == KBTS_INDIC_SYLLABIC_CLASS_SYLLABLE_MODIFIER)
                {
                  kbts__Glyph(Storage, kbts__PrevGlyphRef(Storage, Glyph))->SyllabicPosition = kbts__Glyph(Storage, Glyph)->SyllabicPosition;
                  kbts__Glyph(Storage, kbts__PrevGlyphRef(Storage, Glyph))->SortKey = kbts__Glyph(Storage, Glyph)->SortKey;
                }
                KBTS__FALLTHROUGH;
              case KBTS_INDIC_SYLLABIC_CLASS_MATRA:
                Attach->CurrentKey = kbts__Glyph(Storage, Glyph)->SortKey;
                if(kbts__Glyph(Storage, Glyph)->SyllabicPosition != KBTS__SYLLABIC_POSITION_PREBASE_MATRA)
                {
                  Attach->LastKeyThatWasNotPreBaseMatra = Attach->CurrentKey;
                }
                break;

              case KBTS_INDIC_SYLLABIC_CLASS_HALANT:
                if((Attach->CurrentKey >> 4) == KBTS__SYLLABIC_POSITION_PREBASE_MATRA)
                {
                  // Uniscribe does not reorder halants with pre-base matras. Not sure why.
                  kbts__Glyph(Storage, Glyph)->SortKey = Attach->LastKeyThatWasNotPreBaseMatra;
                  kbts__Glyph(Storage, Glyph)->SyllabicPosition = (kbts_u8)(Attach->LastKeyThatWasNotPreBaseMatra >> 4);
                  break;
                }
                KBTS__FALLTHROUGH;
              case KBTS_INDIC_SYLLABIC_CLASS_NUKTA:
              case KBTS_INDIC_SYLLABIC_CLASS_ZWJ:
              case KBTS_INDIC_SYLLABIC_CLASS_ZWNJ:
                // @Incomplete: Register Shifter
                kbts__Glyph(Storage, Glyph)->SortKey = Attach->CurrentKey;
                kbts__Glyph(Storage, Glyph)->SyllabicPosition = (kbts_u8)(Attach->CurrentKey >> 4);
                break;
              }

              Attach->LastClass = Class;
              Attach->At = kbts__NextGlyphRef(Storage, Glyph);
              Done = 0;
            }
          }

          if(Done)
          {
            break;
          }
        }
      }

      { // Do the sort!
        // NOTE: It is important that we sort _now_, because some characters will be reordered across the base from
        // where they started, which can change which features will apply to them.
        KBTS__SORT_GLYPHS(Storage, kbts__NextGlyphRef(Storage, OneBeforeSyllableGlyph), OnePastLastSyllableGlyph, SortKey);

#ifdef KBTS_SANITY_CHECK
        {
          kbts_glyph_ref Glyph0 = kbts__NextGlyphRef(Storage, OneBeforeSyllableGlyph);
          if(Glyph0 != OnePastLastSyllableGlyph)
          {
            for(kbts_glyph_ref Glyph1 = kbts__NextGlyphRef(Storage, Glyph0);
                kbts__GlyphIsValid(Storage, Glyph1);
                Glyph1 = kbts__NextGlyphRef(Storage, Glyph1))
            {
              if(Glyph1 != OnePastLastSyllableGlyph)
              {
                KBTS_ASSERT(kbts__Glyph(Storage, Glyph0)->SortKey <= kbts__Glyph(Storage, Glyph1)->SortKey);
              }
              else
              {
                break;
              }
            }
          }
        }
        #endif
      }

      // This is what Harfbuzz does:

      // - NUKT, AKHN, RKRF, VATU, CJCT are active for all characters, but they count ZWJ/ZWNJ in their sequence
      // lookups.
      // -   Note that, by default, Harfbuzz skips ZWJ/ZWNJ in sequence lookups, and selectively disables it for CJCT.
      // -   Anyway, since these are active for all characters, we don't need flags for them.

      // (RPHF was already handled before sorting.)

      { // All pre-base glyphs get HALF, which is then selectively disabled in front of ZWNJs.
        kbts_glyph_flags BaseFlags = 0;
        if(!Config->IndicScriptProperties.BlwfPostOnly)
        {
          BaseFlags = KBTS_GLYPH_FLAG_BLWF;
        }

        kbts_indic_syllabic_class LastClass = 0;
        for(Glyph = kbts__PrevGlyphRef(Storage, BaseGlyph);
            Glyph != OneBeforeSyllableGlyph;
            Glyph = kbts__PrevGlyphRef(Storage, Glyph))
        {
          kbts_glyph_flags Flags = BaseFlags;

          if(LastClass != KBTS_INDIC_SYLLABIC_CLASS_ZWNJ)
          {
            Flags |= KBTS_GLYPH_FLAG_HALF;
          }

          kbts__Glyph(Storage, Glyph)->Flags |= Flags;
          LastClass = kbts__Glyph(Storage, Glyph)->SyllabicClass;
        }
      }

      // All post-base glyphs get BLWF, ABVF, PSTF. Some get PREF.
      kbts_glyph Scratch[2]; Scratch[0] = Scratch[1] = KBTS__ZERO_TYPE(kbts_glyph);
      kbts_glyph_ref LastGlyph; LastGlyph = 0;
      for(Glyph = kbts__NextGlyphRef(Storage, BaseGlyph);
          Glyph != OnePastLastSyllableGlyph;
          Glyph = kbts__NextGlyphRef(Storage, Glyph))
      {
        // It is tempting to apply PREF to every glyph after the base.
        // However, the PREF flag is also used to find the syllable base again in EndCluster,
        // so we have to be precise with it.
        Scratch[1] = *kbts__Glyph(Storage, Glyph);
        if(kbts__WouldSubstitute(Scratchpad, Config, Storage, LookupList, Frames, Pref, 0, Scratch, 2))
        {
          kbts__Glyph(Storage, LastGlyph)->Flags |= KBTS_GLYPH_FLAG_PREF;
          kbts__Glyph(Storage, Glyph)->Flags |= KBTS_GLYPH_FLAG_PREF;
        }

        kbts__Glyph(Storage, Glyph)->Flags |= (KBTS_GLYPH_FLAG_BLWF | KBTS_GLYPH_FLAG_ABVF | KBTS_GLYPH_FLAG_PSTF);
        Scratch[0] = Scratch[1];
        LastGlyph = Glyph;
      }

      // The base only inherits stuff after it, up to whatever and not including what was grabbed by
      // post-base consonants.
      if(BaseGlyph != OnePastLastSyllableGlyph)
      {
        kbts__Glyph(Storage, BaseGlyph)->SyllabicPosition = KBTS__SYLLABIC_POSITION_SYLLABLE_BASE;

        kbts_u64 Classes = (1ull << KBTS_INDIC_SYLLABIC_CLASS_HALANT) |
                           (1ull << KBTS_INDIC_SYLLABIC_CLASS_VEDIC_SIGN) |
                           (1ull << KBTS_INDIC_SYLLABIC_CLASS_NUKTA) |
                           (1ull << KBTS_INDIC_SYLLABIC_CLASS_ZWJ) |
                           (1ull << KBTS_INDIC_SYLLABIC_CLASS_ZWNJ);

        kbts_glyph_ref Next = kbts__NextGlyphRef(Storage, BaseGlyph);
        while((Next != OnePastLastSyllableGlyph) &&
              (kbts__Glyph(Storage, Next)->SyllabicPosition < KBTS__SYLLABIC_POSITION_SYLLABLE_BASE) &&
              ((1ull << kbts__Glyph(Storage, Next)->SyllabicClass) & Classes))
        {
          kbts__Glyph(Storage, Next)->SyllabicPosition = KBTS__SYLLABIC_POSITION_SYLLABLE_BASE;

          Next = kbts__NextGlyphRef(Storage, Next);
        }
      }

      Result = OnePastLastSyllableGlyph;
      Scratchpad->RealCluster = 1;
    }
  } break;

  case KBTS_SHAPER_USE:
  {
    int NonCluster = 0;
    while(kbts__GlyphIsValid(Storage, Glyph) &&
          ((kbts__Glyph(Storage, Glyph)->UseClass == KBTS_USE_SYLLABIC_CLASS_OTHER) ||
           (kbts__Glyph(Storage, Glyph)->UseClass == KBTS_USE_SYLLABIC_CLASS_CONS_FINAL_MOD_POST)))
    {
      NonCluster = 1;

      Glyph = kbts__NextGlyphRef(Storage, Glyph);
    }

    if(!NonCluster)
    {
      // Parse a real cluster.
      kbts_u8 State = 0;

      kbts_glyph_ref StartGlyph = Glyph;
      while(kbts__GlyphIsValid(Storage, Glyph) &&
            (State < KBTS_USE_STATE_s0))
      {
        State = kbts_UseTransition[kbts__Glyph(Storage, Glyph)->UseClass][State];
        // @Incomplete: Remove this!!!
        State -= 1;
        Glyph = kbts__NextGlyphRef(Storage, Glyph);
      }

      if(State >= KBTS_USE_STATE_s0)
      {
        Glyph = kbts__PrevGlyphRef(Storage, Glyph);
      }
      if(kbts__GlyphIsValid(Storage, Glyph) &&
         (kbts__Glyph(Storage, Glyph)->UseClass == KBTS_USE_SYLLABIC_CLASS_ZWNJ))
      {
        Glyph = kbts__NextGlyphRef(Storage, Glyph);
      }
      if(Glyph == StartGlyph)
      {
        Glyph = kbts__NextGlyphRef(Storage, Glyph);
      }

      Scratchpad->RealCluster = 1;
    }

    Result = Glyph;
  } break;

  case KBTS_SHAPER_KHMER:
  {
    int NonCluster = 0;
    while(kbts__GlyphIsValid(Storage, Glyph) &&
          ((kbts__Glyph(Storage, Glyph)->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_OTHER) ||
           (kbts__Glyph(Storage, Glyph)->SyllabicClass >= KBTS_KHMER_SYLLABIC_CLASS_COUNT)))
    {
      Glyph = kbts__NextGlyphRef(Storage, Glyph);
      NonCluster = 1;
    }

    Result = Glyph;

    if(!NonCluster)
    {
      kbts_u8 State = 0;
      kbts_glyph_ref FirstGlyph = Glyph;
      kbts_glyph_ref OneBeforeSyllableGlyph = kbts__PrevGlyphRef(Storage, Glyph);

      while(kbts__GlyphIsValid(Storage, Glyph) &&
            (State < KBTS_KHMER_SYLLABIC_STATE_COUNT))
      {
        kbts_indic_syllabic_class Class = kbts__Glyph(Storage, Glyph)->SyllabicClass;

        State = kbts_KhmerSyllabicTransition[Class][State];
        // @Incomplete: Remove this!
        State -= 1;
        Glyph = kbts__NextGlyphRef(Storage, Glyph);
      }

      if(State >= KBTS_KHMER_SYLLABIC_STATE_COUNT)
      {
        Glyph = kbts__PrevGlyphRef(Storage, Glyph);
      }
      if(Glyph == FirstGlyph)
      {
        Glyph = kbts__NextGlyphRef(Storage, Glyph);
      }

      kbts_glyph_ref OnePastLastSyllableGlyph = Glyph;

      if(Config->DottedCircle.Id &&
         !KBTS__IN_SET64(kbts__Glyph(Storage, FirstGlyph)->SyllabicClass, KBTS__SET64((KBTS_INDIC_SYLLABIC_CLASS_CONSONANT)
                                                              (KBTS_INDIC_SYLLABIC_CLASS_RA)
                                                              (KBTS_INDIC_SYLLABIC_CLASS_VOWEL)
                                                              (KBTS_INDIC_SYLLABIC_CLASS_DOTTED_CIRCLE)
                                                              (KBTS_INDIC_SYLLABIC_CLASS_PLACEHOLDER))))
      {
        // Broken cluster.
        // Insert a dotted circle.
        kbts_glyph_ref NewFirstGlyph = kbts__InsertGlyphBefore(Storage, FirstGlyph, &Config->DottedCircle);
        if(!NewFirstGlyph)
        {
          goto OutOfMemory;
        }

        kbts__Glyph(Storage, NewFirstGlyph)->UserIdOrCodepointIndex = kbts__Glyph(Storage, FirstGlyph)->UserIdOrCodepointIndex;
        FirstGlyph = NewFirstGlyph;
      }

      // In Khmer, the first glyph is always the base.

      kbts_glyph_ref PreBaseGlyphs[3];
      int SawHalantRa = 0;
      kbts_un PreBaseGlyphCount = 0;
      kbts_un Classes = 0;

      kbts_u32 AddFlags = KBTS_GLYPH_FLAG_ABVF | KBTS_GLYPH_FLAG_BLWF | KBTS_GLYPH_FLAG_PSTF;

      kbts_glyph_ref LastGlyph = OnePastLastSyllableGlyph;
      for(Glyph = kbts__PrevGlyphRef(Storage, OnePastLastSyllableGlyph);
          Glyph != OneBeforeSyllableGlyph;
          Glyph = kbts__PrevGlyphRef(Storage, Glyph))
      {
        kbts_indic_syllabic_class Class = kbts__Glyph(Storage, Glyph)->SyllabicClass;

        if(Glyph == FirstGlyph)
        {
          AddFlags = 0;
        }

        kbts__Glyph(Storage, Glyph)->Flags |= AddFlags;

        Classes = ((Classes << 8) | Class) & 0xFFFF;

        if(Class == KBTS_INDIC_SYLLABIC_CLASS_VOWEL_PRE)
        {
          PreBaseGlyphs[PreBaseGlyphCount++] = Glyph;
        }
        else if(!SawHalantRa && (Classes == (KBTS_INDIC_SYLLABIC_CLASS_HALANT | (KBTS_INDIC_SYLLABIC_CLASS_RA << 8))))
        {
          kbts__Glyph(Storage, Glyph)->Flags |= KBTS_GLYPH_FLAG_PREF;
          kbts__Glyph(Storage, LastGlyph)->Flags |= KBTS_GLYPH_FLAG_PREF;

          PreBaseGlyphs[PreBaseGlyphCount++] = Glyph;
          PreBaseGlyphs[PreBaseGlyphCount++] = LastGlyph;

          // All of the glyphs written so far need to be flagged with cfar.
          // @Speed: Detect the Halant-Ra sequence in the FSM loop.
          for(kbts_glyph_ref PostRaGlyph = kbts__NextGlyphRef(Storage, LastGlyph);
              PostRaGlyph != OnePastLastSyllableGlyph;
              PostRaGlyph = kbts__NextGlyphRef(Storage, PostRaGlyph))
          {
            kbts__Glyph(Storage, PostRaGlyph)->Flags |= KBTS_GLYPH_FLAG_CFAR;
          }

          SawHalantRa = 1;
        }

        LastGlyph = Glyph;
      }

      while(PreBaseGlyphCount)
      {
        kbts_glyph_ref PreBaseGlyph = PreBaseGlyphs[--PreBaseGlyphCount];
        kbts__MoveGlyphAfter(Storage, PreBaseGlyph, OneBeforeSyllableGlyph);
      }

      Result = OnePastLastSyllableGlyph;
    }
  } break;

  case KBTS_SHAPER_MYANMAR:
  {
    int NonCluster = 0;
    while(kbts__GlyphIsValid(Storage, Glyph) &&
          ((kbts__Glyph(Storage, Glyph)->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_OTHER) ||
           (kbts__Glyph(Storage, Glyph)->SyllabicClass >= KBTS_MYANMAR_SYLLABIC_CLASS_COUNT)))
    {
      NonCluster = 1;
      Glyph = kbts__NextGlyphRef(Storage, Glyph);
    }

    if(!NonCluster)
    {
      kbts_u8 State = 0;
      kbts_glyph_ref FirstGlyph = Glyph;
      while(kbts__GlyphIsValid(Storage, Glyph) &&
            (State < KBTS_MYANMAR_SYLLABIC_STATE_COUNT))
      {
        kbts_indic_syllabic_class Class = kbts__Glyph(Storage, Glyph)->SyllabicClass;

        State = kbts_MyanmarSyllabicTransition[Class][State];
        // @Incomplete: Remove this!
        State -= 1;

        Glyph = kbts__NextGlyphRef(Storage, Glyph);
      }

      if(State >= KBTS_MYANMAR_SYLLABIC_STATE_COUNT)
      {
        Glyph = kbts__PrevGlyphRef(Storage, Glyph);
      }
      if(Glyph == FirstGlyph)
      {
        Glyph = kbts__NextGlyphRef(Storage, Glyph);
      }

      Scratchpad->RealCluster = 1;
    }

    Result = Glyph;
  } break;
  }

  if(0)
  {
    OutOfMemory:;
    Scratchpad->Error = KBTS_SHAPE_ERROR_OUT_OF_MEMORY;
  }

  return Result;
}

static void kbts__EndCluster(kbts_shape_scratchpad *Scratchpad, kbts_glyph_storage *Storage)
{
  kbts_shape_config *Config = Scratchpad->Config;

  switch(Config->Shaper)
  {
  case KBTS_SHAPER_INDIC:
  if(kbts__GlyphIsValid(Storage, kbts__FirstGlyph(Storage)))
  {
    // Final syllable reordering.
    // We are just copying Harfbuzz here, because I've read all the documentation I can and, frankly, I have no idea.
    kbts__feature *Pref = Config->Pref;

    kbts_u32 ViramaId = Config->Virama.Id;
    KBTS__FOR_GLYPH(Storage, Glyph)
    {
      // Harfbuzz tests for (ligated | multiplied) here.
      // I don't really get why, because, supposedly, all viramas should be
      // classified as halants regardless.
      if(ViramaId && (kbts__Glyph(Storage, Glyph)->Id == ViramaId))
      {
        kbts__Glyph(Storage, Glyph)->SyllabicClass = KBTS_INDIC_SYLLABIC_CLASS_HALANT;
        kbts__Glyph(Storage, Glyph)->Flags &= ~(KBTS_GLYPH_FLAG_LIGATURE | KBTS_GLYPH_FLAG_MULTIPLE_SUBSTITUTION);
      }
      else if(kbts__Glyph(Storage, Glyph)->Flags & KBTS_GLYPH_FLAG_LIGATURE)
      {
        // We can't know the syllabic classes of ligatures for sure.
        kbts__Glyph(Storage, Glyph)->SyllabicClass = KBTS_INDIC_SYLLABIC_CLASS_COUNT;
      }
    }

    // Locate the syllable base.
    kbts_glyph_ref BaseGlyph = 0;
    {
      kbts_glyph_ref AfterPreBase = kbts__FirstGlyph(Storage);
      while(kbts__GlyphIsValid(Storage, AfterPreBase) &&
            (kbts__Glyph(Storage, AfterPreBase)->SyllabicPosition < KBTS__SYLLABIC_POSITION_SYLLABLE_BASE))
      {
        AfterPreBase = kbts__NextGlyphRef(Storage, AfterPreBase);
      }

      if(kbts__GlyphIsValid(Storage, AfterPreBase))
      {
        BaseGlyph = AfterPreBase;

        if(Pref && kbts__GlyphIsValid(Storage, kbts__NextGlyphRef(Storage, AfterPreBase)))
        {
          // If we find a pre-base-reordering ra, then the base is probably right after it!
          kbts_glyph_ref AtGlyph = kbts__NextGlyphRef(Storage, AfterPreBase);
          while(kbts__GlyphIsValid(Storage, AtGlyph) &&
                !(kbts__Glyph(Storage, AtGlyph)->Flags & KBTS_GLYPH_FLAG_PREF))
          {
            AtGlyph = kbts__NextGlyphRef(Storage, AtGlyph);
          }

          // If we are not a consonant, then we are attached to a consonant.
          // If we are a pref-able consonant, but we did not form a pref, then we should be right after the base.
          // Either way, we are very close to the base.
          kbts_glyph_ref Prefable = AtGlyph; // Read like "pref-able". :)
          if(kbts__GlyphIsValid(Storage, Prefable) &&
             (!(kbts__Glyph(Storage, Prefable)->Flags & KBTS_GLYPH_FLAG_GENERATED_BY_GSUB) ||
              !(kbts__Glyph(Storage, Prefable)->Flags & KBTS_GLYPH_FLAG_LIGATURE) ||
              (kbts__Glyph(Storage, Prefable)->Flags & KBTS_GLYPH_FLAG_MULTIPLE_SUBSTITUTION)))
          {
            while(kbts__GlyphIsValid(Storage, Prefable) &&
                  (kbts__Glyph(Storage, Prefable)->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_HALANT))
            {
              Prefable = kbts__NextGlyphRef(Storage, Prefable);
            }

            if(kbts__GlyphIsValid(Storage, Prefable))
            {
              kbts__Glyph(Storage, Prefable)->SyllabicPosition = KBTS__SYLLABIC_POSITION_SYLLABLE_BASE;
              BaseGlyph = Prefable;
              // Fall through to the Malayalam test.
            }
          }
        }

        if(Config->Script == KBTS_SCRIPT_MALAYALAM)
        {
          // In Malayalam, the base is after below-base forms.
          // (We actually just give all below-base forms the base pos.)
          kbts_glyph_ref AtGlyph = kbts__NextGlyphRef(Storage, BaseGlyph);
          while(kbts__GlyphIsValid(Storage, AtGlyph))
          {
            while(kbts__GlyphIsValid(Storage, AtGlyph) &&
                  KBTS__IN_SET(kbts__Glyph(Storage, AtGlyph)->SyllabicClass, KBTS__SET32((KBTS_INDIC_SYLLABIC_CLASS_ZWJ)
                                                                 (KBTS_INDIC_SYLLABIC_CLASS_ZWNJ))))
            {
              AtGlyph = kbts__NextGlyphRef(Storage, AtGlyph);
            }

            if(kbts__GlyphIsValid(Storage, AtGlyph) &&
               (kbts__Glyph(Storage, AtGlyph)->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_HALANT))
            {
              AtGlyph = kbts__NextGlyphRef(Storage, AtGlyph);
            }
            else
            {
              break;
            }

            while(kbts__GlyphIsValid(Storage, AtGlyph) &&
                  KBTS__IN_SET(kbts__Glyph(Storage, AtGlyph)->SyllabicClass, KBTS__SET32((KBTS_INDIC_SYLLABIC_CLASS_ZWJ)
                                                                 (KBTS_INDIC_SYLLABIC_CLASS_ZWNJ))))
            {
              AtGlyph = kbts__NextGlyphRef(Storage, AtGlyph);
            }

            if(kbts__GlyphIsValid(Storage, AtGlyph))
            {
              if(kbts__SyllabicClassIsConsonant(kbts__Glyph(Storage, AtGlyph)->SyllabicClass) &&
                 (kbts__Glyph(Storage, AtGlyph)->SyllabicPosition == KBTS__SYLLABIC_POSITION_BELOWBASE_CONSONANT))
              {
                kbts__Glyph(Storage, AtGlyph)->SyllabicPosition = KBTS__SYLLABIC_POSITION_SYLLABLE_BASE;
                BaseGlyph = AtGlyph;
                // Do not break; keep matching below-base forms.
              }
            }
          }
        }
      }

      if(kbts__GlyphIsValid(Storage, BaseGlyph) &&
         kbts__GlyphIsValid(Storage, kbts__PrevGlyphRef(Storage, BaseGlyph)) &&
         (kbts__Glyph(Storage, BaseGlyph)->SyllabicPosition > KBTS__SYLLABIC_POSITION_SYLLABLE_BASE))
      {
        BaseGlyph = kbts__PrevGlyphRef(Storage, BaseGlyph);
      }

      if(!kbts__GlyphIsValid(Storage, BaseGlyph) &&
        (kbts__Glyph(Storage, kbts__PrevGlyphRef(Storage, BaseGlyph))->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_ZWJ))
      {
        BaseGlyph = kbts__PrevGlyphRef(Storage, BaseGlyph);
      }

      if(kbts__GlyphIsValid(Storage, BaseGlyph))
      {
        while(kbts__GlyphIsValid(Storage, kbts__PrevGlyphRef(Storage, BaseGlyph)) &&
              KBTS__IN_SET(kbts__Glyph(Storage, BaseGlyph)->SyllabicClass, KBTS__SET32((KBTS_INDIC_SYLLABIC_CLASS_NUKTA)
                                                               (KBTS_INDIC_SYLLABIC_CLASS_HALANT))))
        {
          BaseGlyph = kbts__PrevGlyphRef(Storage, BaseGlyph);
        }
      }
    }

    // Reorder matras.
    if(kbts__GlyphIsValid(Storage, kbts__PrevGlyphRef(Storage, BaseGlyph)) &&
       (kbts__LastGlyph(Storage) != kbts__FirstGlyph(Storage)) /* GlyphCount > 1 */)
    {
      kbts_glyph_ref FirstGlyph = kbts__FirstGlyph(Storage);
      kbts_glyph_ref To = kbts__PrevGlyphRef(Storage, BaseGlyph);
      if(!kbts__GlyphIsValid(Storage, BaseGlyph))
      {
        To = kbts__PrevGlyphRef(Storage, To);
      }

      if((Config->Script != KBTS_SCRIPT_MALAYALAM) && (Config->Script != KBTS_SCRIPT_TAMIL))
      {
        kbts_glyph_ref LastGlyph = kbts__NextGlyphRef(Storage, To);

        for(;;)
        {
          while((To != FirstGlyph) &&
                !KBTS__IN_SET(kbts__Glyph(Storage, To)->SyllabicClass, KBTS__SET32((KBTS_INDIC_SYLLABIC_CLASS_MATRA)
                                                           (KBTS_INDIC_SYLLABIC_CLASS_MATRA_POST)
                                                           (KBTS_INDIC_SYLLABIC_CLASS_HALANT))))
          {
            LastGlyph = To;
            To = kbts__PrevGlyphRef(Storage, To);
          }

          if((kbts__Glyph(Storage, To)->SyllabicClass != KBTS_INDIC_SYLLABIC_CLASS_HALANT) ||
             (kbts__Glyph(Storage, To)->SyllabicPosition == KBTS__SYLLABIC_POSITION_PREBASE_MATRA))
          {
            // We have nothing to move.
            To = FirstGlyph;

            break;
          }
          else if((To != FirstGlyph) &&
                  kbts__GlyphIsValid(Storage, LastGlyph) &&
                  (kbts__Glyph(Storage, LastGlyph)->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_ZWJ))
          {
            // Zwj+Halant cancels matra movement when the Halant is not attached to the matra.
            To = kbts__PrevGlyphRef(Storage, To);
          }
          else
          {
            break;
          }
        }
      }

      if((To != FirstGlyph) &&
         (kbts__Glyph(Storage, To)->SyllabicPosition != KBTS__SYLLABIC_POSITION_PREBASE_MATRA))
      {
        for(kbts_glyph_ref Matra = kbts__PrevGlyphRef(Storage, To);
            kbts__GlyphIsValid(Storage, Matra);
            )
        {
          kbts_glyph_ref MatraPrev = kbts__PrevGlyphRef(Storage, Matra);

          if(kbts__Glyph(Storage, Matra)->SyllabicPosition == KBTS__SYLLABIC_POSITION_PREBASE_MATRA)
          {
            kbts__MoveGlyphAfter(Storage, Matra, To);

            To = kbts__PrevGlyphRef(Storage, Matra);
          }

          Matra = MatraPrev;
        }
      }
    }

    // Reorder reph.
    // We should only reorder reph when it is a single Repha glyph, and not a Ra sequence.
    kbts_glyph_ref First = kbts__FirstGlyph(Storage);
    kbts_glyph_ref Second = kbts__NextGlyphRef(Storage, First);
    if((kbts__Glyph(Storage, First)->SyllabicPosition == KBTS__SYLLABIC_POSITION_RA_TO_BECOME_REPH) &&
       kbts__GlyphIsValid(Storage, Second) &&
       (kbts__Glyph(Storage, Second)->SyllabicPosition != KBTS__SYLLABIC_POSITION_RA_TO_BECOME_REPH))
    {
      kbts__reph_position RephPosition = Config->IndicScriptProperties.RephPosition;
      kbts_glyph_ref To = 0;

      for(kbts_glyph_ref Glyph = First;
          Glyph != BaseGlyph;
          )
      {
        kbts_glyph_ref Next = kbts__NextGlyphRef(Storage, Glyph);

        if(kbts__Glyph(Storage, Glyph)->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_HALANT)
        {
          To = Glyph;

          if(Next != BaseGlyph)
          {
            kbts_u8 NextClass = kbts__Glyph(Storage, Next)->SyllabicClass;

            if((NextClass == KBTS_INDIC_SYLLABIC_CLASS_ZWJ) ||
               (NextClass == KBTS_INDIC_SYLLABIC_CLASS_ZWNJ))
            {
              To = Next;
            }
          }

          break;
        }

        Glyph = Next;
      }

      if(!To)
      {
        // @Cleanup: We can merge these loops.
        if(RephPosition == KBTS__REPH_POSITION_AFTER_SUBJOINED)
        {
          for(kbts_glyph_ref Glyph = BaseGlyph;
              kbts__GlyphIsValid(Storage, Glyph);
              Glyph = kbts__NextGlyphRef(Storage, Glyph))
          {
            kbts_u8 SyllabicPosition = kbts__Glyph(Storage, Glyph)->SyllabicPosition;

            if(KBTS__IN_SET(SyllabicPosition, KBTS__SET32((KBTS__SYLLABIC_POSITION_POSTBASE_CONSONANT)
                                                        (KBTS__SYLLABIC_POSITION_AFTER_POST)
                                                        (KBTS__SYLLABIC_POSITION_SMVD))))
            {
              To = kbts__PrevGlyphRef(Storage, Glyph);

              break;
            }
          }
        }
        else if(RephPosition == KBTS__REPH_POSITION_AFTER_MAIN)
        {
          for(kbts_glyph_ref Glyph = BaseGlyph;
              kbts__GlyphIsValid(Storage, Glyph);
              Glyph = kbts__NextGlyphRef(Storage, Glyph))
          {
            if(kbts__Glyph(Storage, Glyph)->SyllabicPosition > KBTS__SYLLABIC_POSITION_AFTER_MAIN)
            {
              To = kbts__PrevGlyphRef(Storage, Glyph);

              break;
            }
          }
        }
      }

      if(!To)
      {
        // As a fallback, move reph to the end of the syllable.
        for(kbts_glyph_ref Glyph = kbts__LastGlyph(Storage);
            kbts__GlyphIsValid(Storage, Glyph);
            Glyph = kbts__PrevGlyphRef(Storage, Glyph))
        {
          if(kbts__Glyph(Storage, Glyph)->SyllabicPosition != KBTS__SYLLABIC_POSITION_SMVD)
          {
            To = Glyph;

            break;
          }
        }

        // Matras will still come after reph.
        kbts_glyph_ref Glyph = To;
        if(kbts__Glyph(Storage, Glyph)->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_HALANT)
        {
          for(kbts_glyph_ref Matra = kbts__NextGlyphRef(Storage, BaseGlyph);
              Matra != To;
              Matra = kbts__NextGlyphRef(Storage, Matra))
          {
            kbts_u8 Class = kbts__Glyph(Storage, Matra)->SyllabicClass;

            if(KBTS__IN_SET(Class, KBTS__SET32((KBTS_INDIC_SYLLABIC_CLASS_MATRA)
                                             (KBTS_INDIC_SYLLABIC_CLASS_MATRA_POST)
                                             (KBTS_INDIC_SYLLABIC_CLASS_CONSONANT_MEDIAL))))
            {
              To = kbts__PrevGlyphRef(Storage, To);
            }
          }
        }
      }

      if(To &&
         (First != To))
      {
        // Do the reorder!
        kbts__MoveGlyphAfter(Storage, First, To);
      }
    }

    // Reorder pre-base-reordering consonants.
    if(Pref)
    {
      First = kbts__FirstGlyph(Storage);
      kbts_glyph_ref PrefGlyph = 0;

      for(kbts_glyph_ref Glyph = kbts__NextGlyphRef(Storage, BaseGlyph);
          kbts__GlyphIsValid(Storage, Glyph);
          Glyph = kbts__NextGlyphRef(Storage, Glyph))
      {
        if(kbts__Glyph(Storage, Glyph)->Flags & KBTS_GLYPH_FLAG_PREF)
        {
          PrefGlyph = Glyph;

          break;
        }
      }

      if(PrefGlyph)
      {
        // Harfbuzz says the ideal is to check that this glyph was generated by the pref feature specifically,
        // but then just uses a generic flag like this.
        // The ideal solution would not be very difficult to implement!
        if(kbts__Glyph(Storage, PrefGlyph)->Flags & KBTS_GLYPH_FLAG_GENERATED_BY_GSUB)
        {
          kbts_glyph_ref To = First;

          kbts_glyph_ref Last = BaseGlyph;
          for(kbts_glyph_ref Glyph = kbts__PrevGlyphRef(Storage, BaseGlyph);
              kbts__GlyphIsValid(Storage, Glyph);
              Glyph = kbts__PrevGlyphRef(Storage, Glyph))
          {
            kbts_u8 Class = kbts__Glyph(Storage, Glyph)->SyllabicClass;

            if(KBTS__IN_SET(Class, KBTS__SET32((KBTS_INDIC_SYLLABIC_CLASS_MATRA)
                                             (KBTS_INDIC_SYLLABIC_CLASS_MATRA_POST)
                                             (KBTS_INDIC_SYLLABIC_CLASS_CONSONANT_MEDIAL)
                                             (KBTS_INDIC_SYLLABIC_CLASS_HALANT))))
            {
              To = Last;

              break;
            }

            Last = Glyph;
          }

          if(To != First)
          {
            kbts_glyph_ref Prev = kbts__PrevGlyphRef(Storage, To);
            kbts_u8 CurrentClass = kbts__Glyph(Storage, To)->SyllabicClass;

            if((kbts__Glyph(Storage, Prev)->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_HALANT) &&
               KBTS__IN_SET(CurrentClass, KBTS__SET32((KBTS_INDIC_SYLLABIC_CLASS_ZWJ)
                                                    (KBTS_INDIC_SYLLABIC_CLASS_ZWNJ))))
            {
              To = kbts__NextGlyphRef(Storage, To);
            }
          }

          // Do the reorder!
          kbts__MoveGlyphBefore(Storage, PrefGlyph, To);
        }
      }
    }

    { // Mark matras at the beginning of a word for init application.
      // It is tempting to check for this at the same time as the reph reordering, but glyphs could have been
      // reordered since then and the reordering procedure is clearly defined to happen in sequential steps.
      First = kbts__FirstGlyph(Storage); // I don't think this needs to be reloaded, but you never know.

      if((kbts__Glyph(Storage, First)->SyllabicPosition == KBTS__SYLLABIC_POSITION_PREBASE_MATRA) && Scratchpad->ClusterAtStartOfWord)
      {
        kbts__Glyph(Storage, First)->Flags |= KBTS_GLYPH_FLAG_INIT;
      }
    }
  }
  break;

  case KBTS_SHAPER_USE:
  {
    kbts_u64 PostBaseSet = KBTS__SET64((KBTS_USE_SYLLABIC_CLASS_CONS_FINAL_ABOVE)     (KBTS_USE_SYLLABIC_CLASS_CONS_FINAL_BELOW)
                                      (KBTS_USE_SYLLABIC_CLASS_CONS_FINAL_MOD_ABOVE) (KBTS_USE_SYLLABIC_CLASS_CONS_FINAL_MOD_BELOW)
                                      (KBTS_USE_SYLLABIC_CLASS_CONS_FINAL_MOD_POST)  (KBTS_USE_SYLLABIC_CLASS_CONS_MED_ABOVE)
                                      (KBTS_USE_SYLLABIC_CLASS_CONS_MED_BELOW)       (KBTS_USE_SYLLABIC_CLASS_CONS_MED_POST)
                                      (KBTS_USE_SYLLABIC_CLASS_CONS_MED_PRE)         (KBTS_USE_SYLLABIC_CLASS_VOWEL_ABOVE)
                                      (KBTS_USE_SYLLABIC_CLASS_VOWEL_BELOW)          (KBTS_USE_SYLLABIC_CLASS_VOWEL_POST)
                                      (KBTS_USE_SYLLABIC_CLASS_VOWEL_PRE)            (KBTS_USE_SYLLABIC_CLASS_VOWEL_MOD_ABOVE)
                                      (KBTS_USE_SYLLABIC_CLASS_VOWEL_MOD_BELOW)      (KBTS_USE_SYLLABIC_CLASS_VOWEL_MOD_POST)
                                      (KBTS_USE_SYLLABIC_CLASS_VOWEL_MOD_PRE));
    kbts_u64 HalantSet = KBTS__SET64((KBTS_USE_SYLLABIC_CLASS_HALANT)
                                    (KBTS_USE_SYLLABIC_CLASS_HALANT_OR_VOWEL_MODIFIER)
                                    (KBTS_USE_SYLLABIC_CLASS_INVISIBLE_STACKER));

    // In the reorderings below, we check for halants because that's what Harfbuzz does.
    // Nowhere does Microsoft mention that halants should block reorderings!
    kbts_glyph_ref Repha = kbts__FirstGlyph(Storage);
    if((kbts__Glyph(Storage, Repha)->UseClass == KBTS_USE_SYLLABIC_CLASS_REPHA) ||
       (kbts__Glyph(Storage, Repha)->Flags & KBTS_GLYPH_FLAG_RPHF))
    {
      kbts_glyph_ref To = kbts__NextGlyphRef(Storage, Repha);
      for(;
          kbts__GlyphIsValid(Storage, To);
          To = kbts__NextGlyphRef(Storage, To))
      {
        // Microsoft says that rephas are reordered "after a following full base", which is needlessly vague.
        // Harfbuzz reorders them in front of the first post-base glyph.
        if(KBTS__IN_SET64(kbts__Glyph(Storage, To)->UseClass, PostBaseSet) ||
           (!(kbts__Glyph(Storage, To)->Flags & KBTS_GLYPH_FLAG_LIGATURE) &&
            KBTS__IN_SET64(kbts__Glyph(Storage, To)->UseClass, HalantSet)))
        {
          break;
        }
      }

      kbts__MoveGlyphBefore(Storage, Repha, To);
    }

    {
      // Microsoft says that pre-base vowels and vowel modifiers should be reordered "before the base glyph
      // and, if present, before a pre-base glyph reordered via the 'pref' feature".
      // In practice, we tag 'pref'-generated glyphs with the VOWEL_PRE class so that they get reordered here.
      // Note that the final reordered glyphs end up backwards from their original order.
      kbts_glyph_ref PreBaseVowels[16]; // @Robustness: How many vowels can there realistically be in a single cluster?
      kbts_un PreBaseVowelCount = 0;
      kbts_u64 SawBase = 0;
      for(kbts_glyph_ref Glyph = kbts__LastGlyph(Storage);
          kbts__GlyphIsValid(Storage, Glyph);
          Glyph = kbts__PrevGlyphRef(Storage, Glyph))
      {
        SawBase |= KBTS__IN_SET64(kbts__Glyph(Storage, Glyph)->UseClass, KBTS__SET64((KBTS_USE_SYLLABIC_CLASS_BASE)
                                                             (KBTS_USE_SYLLABIC_CLASS_BASE_OTHER)
                                                             (KBTS_USE_SYLLABIC_CLASS_BASE_NUM)));

        kbts_u32 Flags = kbts__Glyph(Storage, Glyph)->Flags;
        // Only accept the first glyphs produced by multiple substitutions.
        if(((Flags & KBTS_GLYPH_FLAG_FIRST_IN_MULTIPLE_SUBSTITUTION) ||
            !(Flags & KBTS_GLYPH_FLAG_MULTIPLE_SUBSTITUTION)) &&
           (KBTS__IN_SET64(kbts__Glyph(Storage, Glyph)->UseClass, KBTS__SET64((KBTS_USE_SYLLABIC_CLASS_VOWEL_PRE)
                                                      (KBTS_USE_SYLLABIC_CLASS_VOWEL_MOD_PRE))) ||
            (Flags & KBTS_GLYPH_FLAG_PREF)))
        {
          if(PreBaseVowelCount < KBTS__ARRAY_LENGTH(PreBaseVowels))
          {
            PreBaseVowels[PreBaseVowelCount++] = Glyph;
          }
        }
        else if(!(kbts__Glyph(Storage, Glyph)->Flags & KBTS_GLYPH_FLAG_LIGATURE) &&
                KBTS__IN_SET64(kbts__Glyph(Storage, Glyph)->UseClass, HalantSet))
        {
          // Reordering stops at halants, apparently.
          // We want to write the vowels in the reverse order they appear in the glyph sequence.
          // Since we are iterating backwards, they already _are_ backwards in the PreBaseVowels array,
          // so there's no need to do anything.
          kbts_glyph_ref InsertAfter = Glyph;
          KBTS__FOR(PreBaseVowelIndex, 0, PreBaseVowelCount)
          {
            kbts_glyph_ref PreBaseVowel = PreBaseVowels[PreBaseVowelIndex];

            kbts__MoveGlyphAfter(Storage, PreBaseVowel, InsertAfter);
            InsertAfter = PreBaseVowel;
          }
          PreBaseVowelCount = 0;

          // Valid bases only show up before halants.
          SawBase = 0;
        }
      }

      kbts_glyph_ref DottedCircleInsertBefore = kbts__FirstGlyph(Storage);

      { // Flush the rest of the vowels.
        kbts_glyph_ref InsertAfter = 0;
        KBTS__FOR(PreBaseVowelIndex, 0, PreBaseVowelCount)
        {
          kbts_glyph_ref PreBaseVowel = PreBaseVowels[PreBaseVowelIndex];

          kbts__MoveGlyphAfter(Storage, PreBaseVowel, InsertAfter);

          InsertAfter = PreBaseVowel;
        }
      }

      if(Scratchpad->RealCluster && !SawBase && Config->DottedCircle.Id)
      {
        kbts_glyph_ref DottedCircle = kbts__InsertGlyphBefore(Storage, DottedCircleInsertBefore, &Config->DottedCircle);
        if(!DottedCircle)
        {
          goto OutOfMemory;
        }

        kbts__Glyph(Storage, DottedCircle)->UserIdOrCodepointIndex = kbts__Glyph(Storage, DottedCircleInsertBefore)->UserIdOrCodepointIndex;
      }
    }
  } break;

  case KBTS_SHAPER_MYANMAR:
  if(Scratchpad->RealCluster)
  {
    kbts_glyph_ref BaseGlyph = 0;
    kbts_glyph_ref First = kbts__NextGlyphRef(Storage, BaseGlyph);
    kbts_glyph_ref Second = kbts__NextGlyphRef(Storage, First);
    kbts_glyph_ref Third = kbts__NextGlyphRef(Storage, Second);
    kbts_glyph_ref OnePastReph = First;

    kbts_un MinimumGlyphCount = 0;

    if(kbts__GlyphIsValid(Storage, First))
    {
      MinimumGlyphCount += 1;

      if(kbts__GlyphIsValid(Storage, Second))
      {
        MinimumGlyphCount += 1;

        if(kbts__GlyphIsValid(Storage, Third))
        {
          MinimumGlyphCount += 1;
        }
      }
    }

    if((MinimumGlyphCount == 3) &&
       (kbts__Glyph(Storage, First)->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_RA) &&
       (kbts__Glyph(Storage, Second)->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_ASAT) &&
       (kbts__Glyph(Storage, Third)->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_HALANT))
    {
      kbts__Glyph(Storage, First)->SyllabicPosition = KBTS__SYLLABIC_POSITION_AFTER_MAIN;
      kbts__Glyph(Storage, Second)->SyllabicPosition = KBTS__SYLLABIC_POSITION_AFTER_MAIN;
      kbts__Glyph(Storage, Third)->SyllabicPosition = KBTS__SYLLABIC_POSITION_AFTER_MAIN;

      OnePastReph = kbts__NextGlyphRef(Storage, Third);
      BaseGlyph = First;
    }

    kbts_u64 BaseSet = KBTS__SET64((KBTS_INDIC_SYLLABIC_CLASS_CONSONANT)
                                   (KBTS_INDIC_SYLLABIC_CLASS_CONSONANT_WITH_STACKER)
                                   (KBTS_INDIC_SYLLABIC_CLASS_RA)
                                   (KBTS_INDIC_SYLLABIC_CLASS_VOWEL)
                                   (KBTS_INDIC_SYLLABIC_CLASS_PLACEHOLDER)
                                   (KBTS_INDIC_SYLLABIC_CLASS_DOTTED_CIRCLE));

    // Scan for a non-reph base glyph.
    for(kbts_glyph_ref Glyph = OnePastReph;
        kbts__GlyphIsValid(Storage, Glyph);
        Glyph = kbts__NextGlyphRef(Storage, Glyph))
    {
      kbts_indic_syllabic_class Class = kbts__Glyph(Storage, Glyph)->SyllabicClass;

      if(!kbts__GlyphIsValid(Storage, BaseGlyph) &&
         KBTS__IN_SET64(Class, BaseSet))
      {
        BaseGlyph = Glyph;

        break;
      }
    }

    // Otherwise, reph can be the base.
    // @Cleanup: I'm pretty sure this is @Duplication with the reph check above.
    if(!kbts__GlyphIsValid(Storage, BaseGlyph) &&
       (OnePastReph != First))
    {
      BaseGlyph = First;
    }

    if(kbts__GlyphIsValid(Storage, BaseGlyph))
    {
      kbts__syllabic_position LastPosition = KBTS__SYLLABIC_POSITION_SYLLABLE_BASE;
      kbts__syllabic_position SectionPosition = KBTS__SYLLABIC_POSITION_AFTER_MAIN;
      kbts_glyph_ref LastPreBaseMatra = 0;
      for(kbts_glyph_ref Glyph = kbts__NextGlyphRef(Storage, BaseGlyph);
          kbts__GlyphIsValid(Storage, Glyph);
          )
      {
        kbts_glyph_ref Next = kbts__NextGlyphRef(Storage, Glyph); // Preserve the next source identity when reversing.
        kbts__syllabic_position Position = SectionPosition;

        switch(kbts__Glyph(Storage, Glyph)->SyllabicClass)
        {
        case KBTS_INDIC_SYLLABIC_CLASS_MEDIAL_RA:
          Position = KBTS__SYLLABIC_POSITION_PREBASE_CONSONANT;
          break;

        case KBTS_INDIC_SYLLABIC_CLASS_VOWEL_PRE:
        {
          Position = KBTS__SYLLABIC_POSITION_PREBASE_MATRA;

          // According to Unicode 15.0:
          // For combining characters placed to the left of a base character, the combining characters start from the base
          // character and are arranged leftward.
          // Since these guys will get reordered into being pre-base, we need to flip them.
          if(kbts__GlyphIsValid(Storage, LastPreBaseMatra))
          {
            kbts__ReverseGlyphRange(Storage, LastPreBaseMatra, kbts__NextGlyphRef(Storage, Glyph));
          }
          else
          {
            // When we reverse, this will become the "last" pre-base matra again, so we only need to set it once.
            LastPreBaseMatra = Glyph;
          }
        } break;

        case KBTS_INDIC_SYLLABIC_CLASS_VARIATION_SELECTOR:
          Position = LastPosition;
          break;

        case KBTS_INDIC_SYLLABIC_CLASS_VOWEL_BELOW:
          if(Position == KBTS__SYLLABIC_POSITION_AFTER_MAIN)
          {
            Position = KBTS__SYLLABIC_POSITION_BELOWBASE_CONSONANT;
          }
          else if(Position == KBTS__SYLLABIC_POSITION_BELOWBASE_CONSONANT)
          {
            Position = SectionPosition;
          }
          break;

        case KBTS_INDIC_SYLLABIC_CLASS_VEDIC_SIGN:
          if(Position == KBTS__SYLLABIC_POSITION_BELOWBASE_CONSONANT)
          {
            Position = KBTS__SYLLABIC_POSITION_BEFORE_SUBJOINED;
          }
          break;
        }

        if((SectionPosition == KBTS__SYLLABIC_POSITION_BELOWBASE_CONSONANT) && (kbts__Glyph(Storage, Glyph)->SyllabicClass == KBTS_INDIC_SYLLABIC_CLASS_VEDIC_SIGN))
        {
          SectionPosition = KBTS__SYLLABIC_POSITION_AFTER_SUBJOINED;
          Position = SectionPosition;
        }

        kbts__Glyph(Storage, Glyph)->SyllabicPosition = Position;
        LastPosition = Position;
        Glyph = Next;
      }
    }

    if(MinimumGlyphCount > 1)
    {
      KBTS__SORT_GLYPHS(Storage, kbts__FirstGlyph(Storage), 0, SyllabicPosition);

      #ifdef KBTS_SANITY_CHECK
      {
        kbts_glyph_ref Left = kbts__FirstGlyph(Storage);
        kbts_glyph_ref Right = kbts__NextGlyphRef(Storage, Left);
        while(kbts__GlyphIsValid(Storage, Right))
        {
          kbts_glyph_ref Next = kbts__NextGlyphRef(Storage, Right);

          KBTS_ASSERT(kbts__Glyph(Storage, Left)->SyllabicPosition <= kbts__Glyph(Storage, Right)->SyllabicPosition);

          Left = Right;
          Right = Next;
        }
      }
      #endif
    }
  } break;
  }

  if(0)
  {
    OutOfMemory:;
    Scratchpad->Error = KBTS_SHAPE_ERROR_OUT_OF_MEMORY;
  }
}
