KB_PROFILE_ONLY(
static kbts_u32 kbts__GsubStreamContextFormat(kbts_shape_config *Config, kbts_u32 LookupIndex, kbts_u32 Subtable)
{
  const kbts__font_lookup *Lookup = kbts__FontLookup(Config->Font, KBTS_SHAPING_TABLE_GSUB, LookupIndex);
  const kbts__font_subtable *Resolved = kbts__FontSubtable(Config->Font, Lookup, Subtable);
  const kbts_u16 *Table = (const kbts_u16 *)Resolved->Data;
  return (Resolved->Type == 6 ? 3 : 0) + Table[0] - 1;
}
)

/* Derived admission indexes over the canonical active glyph range. */
static kbts_b32 kbts__GsubStreamSlice(kbts_un *Size, kbts_un Count, kbts_un Width,
                                     kbts_un Alignment, kbts_un *Offset)
{
  const kbts_un Limit = (kbts_un)-1;
  kbts_un Padding = (Alignment - (*Size & (Alignment - 1))) & (Alignment - 1);
  if(Padding > Limit - *Size) return 0;
  *Size += Padding;
  *Offset = *Size;
  if(!Width) return 1;
  if(Count > (Limit - *Size) / Width) return 0;
  *Size += Count * Width;
  return 1;
}

/* Fixed families are independent: a native interval does not clear symbol
 * rows. Rows are readable only through WordCount; dormant rows are initialized
 * on admission, never speculatively across capacity. */
static kbts_b32 kbts__ReserveGsubStream(kbts_shape_scratchpad *Scratchpad, kbts_un Required)
{
  kbts__gsub_stream *Stream = &Scratchpad->Stream;
  const kbts__gsub_plan *Plan = Scratchpad->Config->GsubPlan;
  kbts_un Capacity = Scratchpad->GlyphCapacity;
  kbts_un Stride = Stream->SymbolStride;
  kbts_un Words = Required / 64 + !!(Required & 63);
  kbts_un StageWords = (kbts_un)Plan->StageCount / 64 + !!(Plan->StageCount & 63);
  kbts_un MetaSize = 0, StageOffset, PresenceOffset, SymbolSize = 0, NativeSize = 0, Ignored;
  if(Required > Capacity || Stream->Capacity != Capacity ||
     Stride != Capacity / 64 + !!(Capacity & 63) ||
     Stride > (kbts_un)-1 / sizeof(kbts_u64) ||
     !kbts__GsubStreamSlice(&MetaSize, StageWords, sizeof(kbts_u64), KBTS_ALIGNOF(kbts_u64), &StageOffset) ||
     !kbts__GsubStreamSlice(&MetaSize, Stream->IndexSymbols ? Plan->SymbolWordCount : 0,
         sizeof(kbts_u32), KBTS_ALIGNOF(kbts_u32), &PresenceOffset) ||
     !kbts__GsubStreamSlice(&SymbolSize, Stream->IndexSymbols ? Plan->SymbolCount : 0,
         Stride * sizeof(kbts_u64), KBTS_ALIGNOF(kbts_u64), &Ignored) ||
     !kbts__GsubStreamSlice(&NativeSize, Stream->IndexNative ? Stream->NativeCount : 0,
         Stride * sizeof(kbts_u64), KBTS_ALIGNOF(kbts_u64), &Ignored))
    goto Failure;
  if(MetaSize > Stream->MetadataCapacity || SymbolSize > Stream->SymbolCapacity ||
     NativeSize > Stream->NativeCapacity ||
     (MetaSize && !Stream->Memory) || (SymbolSize && !Stream->SymbolMemory) ||
     (NativeSize && !Stream->NativeMemory))
    goto Failure;
  Stream->StageCandidates = KBTS__POINTER_OFFSET(kbts_u64,
      KBTS__ALIGN_POINTER(void, Stream->Memory, KBTS_ALIGNOF(kbts_u64)), StageOffset);
  Stream->PresentSymbols = Stream->IndexSymbols ? KBTS__POINTER_OFFSET(kbts_u32,
      KBTS__ALIGN_POINTER(void, Stream->Memory, KBTS_ALIGNOF(kbts_u64)), PresenceOffset) : 0;
  Stream->SymbolCandidates = Stream->IndexSymbols ?
      KBTS__ALIGN_POINTER(kbts_u64, Stream->SymbolMemory, KBTS_ALIGNOF(kbts_u64)) : 0;
  Stream->NativeCandidates = Stream->IndexNative ?
      KBTS__ALIGN_POINTER(kbts_u64, Stream->NativeMemory, KBTS_ALIGNOF(kbts_u64)) : 0;
  if(Stream->Indexing && Words > Stream->WordCount)
  {
    KBTS_ASSERT(Stream->BoundConfig == Scratchpad->Config);
    /* Fixed strides preserve existing words; admit only the newly readable
     * suffix of active rows. First admission initializes every dormant row. */
    if(Stream->IndexSymbols)
      for(kbts_u32 S = 0; S < Plan->SymbolCount; ++S)
        if(Stream->PresentSymbols[S / 32] & (1u << (S & 31)))
        {
          kbts_u64 *Row = Stream->SymbolCandidates + (kbts_un)S * Stride;
          KBTS_MEMSET(Row + Stream->WordCount, 0, (Words - Stream->WordCount) * sizeof(*Row));
        }
    if(Stream->IndexNative)
      for(kbts_u32 I = Stream->FirstLookup; I < Stream->LastLookup; ++I)
      {
        const kbts__gsub_node *Node = &Plan->Nodes[I];
        if(Node->Kind != KBTS__GSUB_NATIVE || Node->Stage < Stream->MinimumStage ||
           !(Stream->StageCandidates[Node->Stage / 64] & ((kbts_u64)1 << (Node->Stage & 63)))) continue;
        kbts_u64 *Row = Stream->NativeCandidates +
            (kbts_un)(Node->NativeIndex - Stream->NativeFirst) * Stride;
        KBTS_MEMSET(Row + Stream->WordCount, 0, (Words - Stream->WordCount) * sizeof(*Row));
      }
  }
  Stream->WordCount = Words;
  Stream->BoundConfig = Scratchpad->Config;
  return 1;
Failure:
  Scratchpad->Error = KBTS_SHAPE_ERROR_OUT_OF_MEMORY;
  return 0;
}


/* Config-owned stage words already aggregate every glyph's static lookup
 * eligibility. Overrides select the possible row; live flags, disabled
 * features, required features and nonbinary values are checked at execution. */
static inline kbts_u64 kbts__GsubAdmissionWord(const kbts_shape_config *Config,
    const kbts_glyph *Glyph, kbts_u32 Word)
{
  const kbts__gsub_plan *Plan = Config->GsubPlan;
  kbts_u64 Bits = ~(kbts_u64)0;
  if(Glyph->Id < Config->GlyphCount)
  {
    const kbts_u64 *Rows = Glyph->Config && Glyph->Config->HasEnabledLookups ?
        Plan->PossibleStages : Plan->DefaultStages;
    Bits = Rows[(kbts_un)Glyph->Id * Plan->AdmissionStride + Word];
  }
  if(Glyph->Config && Glyph->Config->DisabledGsubStages)
    Bits &= ~Glyph->Config->DisabledGsubStages[Word];
  return Bits;
}

/* Initial construction and live edits share row admission and initialization. */
static inline void kbts__GsubAdmitStageWord(const kbts__gsub_plan *Plan,
    kbts__gsub_stream *Stream, kbts_u64 Bits, kbts_u64 NativeMask,
    kbts_u32 Word, kbts_u32 Slot)
{
  kbts_u64 New = Bits & ~Stream->StageCandidates[Word];
  Stream->StageCandidates[Word] |= Bits;
  Bits &= NativeMask;
  while(Bits)
  {
    kbts_u32 Low = (kbts_u32)Bits;
    kbts_u32 B = Low ? kbts__LsbPositionOrBitWidth32(Low) :
        32 + kbts__LsbPositionOrBitWidth32((kbts_u32)(Bits >> 32));
    kbts_u32 Stage = Word * 64 + B;
    const kbts__gsub_node *Node = &Plan->Nodes[Plan->Stages[Stage].FirstLookup];
    kbts_u64 *Row = Stream->NativeCandidates +
        (kbts_un)(Node->NativeIndex - Stream->NativeFirst) * Stream->SymbolStride;
    if(New & ((kbts_u64)1 << B)) KBTS_MEMSET(Row, 0, Stream->WordCount * sizeof(*Row));
    Row[Slot / 64] |= (kbts_u64)1 << (Slot & 63);
    Bits &= Bits - 1;
  }
}

static void kbts__GsubAdmitStages(kbts_shape_config *Config, kbts__gsub_stream *Stream,
    const kbts_glyph *Glyph, kbts_u32 Slot, kbts_u32 FirstLookup, kbts_u32 LastLookup,
    kbts_u32 MinimumStage)
{
  const kbts__gsub_plan *Plan = Config->GsubPlan;
  if(FirstLookup >= LastLookup || MinimumStage >= Plan->StageCount) return;
  kbts_u32 FirstStage = KBTS__MAX(MinimumStage, Plan->Nodes[FirstLookup].Stage);
  kbts_u32 LastStage = Plan->Nodes[LastLookup - 1].Stage;
  if(FirstStage > LastStage) return;
  for(kbts_u32 W = FirstStage / 64; W <= LastStage / 64; ++W)
  {
    kbts_u64 Bits = kbts__GsubAdmissionWord(Config, Glyph, W);
    if(W == FirstStage / 64) Bits &= ~(kbts_u64)0 << (FirstStage & 63);
    if(W == LastStage / 64) Bits &= ~(kbts_u64)0 >> (63 - (LastStage & 63));
    kbts_u64 NativeMask = Stream->IndexNative ? Plan->NativeStages[W] : 0;
    kbts__GsubAdmitStageWord(Plan, Stream, Bits, NativeMask, W, Slot);
  }
}

/* Presence marks initialized rows through the current readable word extent. */
KBTS_INLINE kbts_u64 *kbts__GsubSymbolRow(kbts__gsub_stream *Stream, kbts_u32 Symbol)
{
  kbts_u64 *Row = Stream->SymbolCandidates + (kbts_un)Symbol * Stream->SymbolStride;
  kbts_u32 Bit = (kbts_u32)1 << (Symbol & 31);
  if(!(Stream->PresentSymbols[Symbol / 32] & Bit))
  {
    Stream->PresentSymbols[Symbol / 32] |= Bit;
    KBTS_MEMSET(Row, 0, Stream->WordCount * sizeof(*Row));
  }
  return Row;
}

/* Build once per contiguous GSUB interval, directly from stable slots. Hoist
 * interval masks per stage word; subsequent edits admit only their changed slot. */
static void kbts__GsubBuildIndexes(kbts_shape_config *Config, kbts_glyph_storage *Storage, kbts__gsub_stream *Stream,
    kbts_u32 FirstLookup, kbts_u32 LastLookup, kbts_u32 MinimumStage)
{
  const kbts__gsub_plan *Plan = Config->GsubPlan;
  KBTS_MEMSET(Stream->StageCandidates, 0,
      ((kbts_un)Plan->StageCount / 64 + !!(Plan->StageCount & 63)) * sizeof(kbts_u64));
  Stream->HasIgnorables = 0;
  if(Stream->IndexSymbols)
  {
    KBTS_MEMSET(Stream->PresentSymbols, 0, (kbts_un)Plan->SymbolWordCount * sizeof(kbts_u32));
    for(kbts_u32 Slot = Storage->First; Slot != Storage->End; Slot = kbts__NextGlyphSlot(Storage, Slot))
    {
      const kbts_glyph *Glyph = &Storage->Glyphs[Slot];
      Stream->HasIgnorables |= !!(Glyph->UnicodeFlags & KBTS_UNICODE_FLAG_DEFAULT_IGNORABLE);
      kbts__GsubSymbolRow(Stream, Glyph->CompiledSymbol)[Slot / 64] |= (kbts_u64)1 << (Slot & 63);
    }
  }
  if(FirstLookup >= LastLookup || MinimumStage >= Plan->StageCount) return;
  kbts_u32 FirstStage = KBTS__MAX(MinimumStage, Plan->Nodes[FirstLookup].Stage);
  kbts_u32 LastStage = Plan->Nodes[LastLookup - 1].Stage;
  if(FirstStage > LastStage) return;
  for(kbts_u32 W = FirstStage / 64; W <= LastStage / 64; ++W)
  {
    kbts_u64 Mask = ~(kbts_u64)0;
    if(W == FirstStage / 64) Mask &= ~(kbts_u64)0 << (FirstStage & 63);
    if(W == LastStage / 64) Mask &= ~(kbts_u64)0 >> (63 - (LastStage & 63));
    kbts_u64 NativeMask = Stream->IndexNative ? Plan->NativeStages[W] : 0;
    for(kbts_u32 Slot = Storage->First; Slot != Storage->End; Slot = kbts__NextGlyphSlot(Storage, Slot))
    {
      kbts_u64 Bits = kbts__GsubAdmissionWord(Config, &Storage->Glyphs[Slot], W) & Mask;
      kbts__GsubAdmitStageWord(Plan, Stream, Bits, NativeMask, W, Slot);
    }
  }
}


KBTS_INLINE kbts_b32 kbts__GsubSkipGlyph(kbts_glyph *Glyph, kbts__skip_flags SkipFlags)
{
  if(!(Glyph->UnicodeFlags & KBTS_UNICODE_FLAG_DEFAULT_IGNORABLE) ||
     (Glyph->Flags & KBTS_GLYPH_FLAG_GENERATED_BY_GSUB)) return 0;
  /* Constant parents have no GDEF filter. All flags are live. */
  kbts__unpacked_lookup Lookup = KBTS__ZERO;
  return kbts__SkipGlyph(Glyph, &Lookup, SkipFlags, KBTS_UNICODE_FLAG_DEFAULT_IGNORABLE);
}

static const kbts__compiled_outcome *kbts__MatchGsubStream(kbts_shape_scratchpad *Scratchpad,
    const kbts__compiled_program *Program, kbts_u32 Anchor,
    kbts__skip_flags SkipFlags, kbts_u32 *InputEnd)
{
  KB_PROFILE_SCOPE(KBP_SEQUENCE);
  if(!Program->StageCount || !Program->CandidateCount) return 0;
  KBTS_ASSERT(!Program->FirstFiltered);
  kbts__gsub_stream *Stream = &Scratchpad->Stream;
  kbts_glyph_storage *Storage = Scratchpad->Storage;
  kbts_glyph *Glyphs = Storage->Glyphs;
  const kbts__compiled_contexts *Cache = Scratchpad->Config->Font->CompiledContexts;
  kbts_u32 *Symbols = Scratchpad->CompiledSymbols;
  kbts_u32 *Offsets = Scratchpad->CompiledOffsets;
  kbts_u32 Center = Program->MaxBacktrack;
  const kbts_glyph *First = &Glyphs[Anchor];
  kbts__compiled_bucket Bucket = Program->Buckets[kbts__CompiledBucketIndex(Program, First->CompiledSymbol)];
  if(!Bucket.Count) return 0;
  KBTS_ASSERT((kbts_un)Center + Bucket.Ahead + 1 <= Scratchpad->CompiledWindowCapacity);
  Symbols[Center] = Program->LocalMasks ? kbts__CompiledLocalMask(Program, First->CompiledSymbol) : First->CompiledSymbol;
  Offsets[Center] = Anchor;
  kbts_u32 Cursor = Anchor;
  {
    KB_PROFILE_SCOPE(KBP_INPUT_GATHER);
    for(kbts_u32 Index = 1; Index <= Bucket.Ahead; ++Index)
    {
      if(Cursor != Storage->End) Cursor = kbts__NextGlyphSlot(Storage, Cursor);
      if(Stream->HasIgnorables)
      {
        while(Cursor != Storage->End)
        {
          KB_PROFILE_WORK(KBW_INPUT_GLYPHS, 1);
          if(!kbts__GsubSkipGlyph(&Glyphs[Cursor], SkipFlags)) break;
          Cursor = kbts__NextGlyphSlot(Storage, Cursor);
        }
      }
      else KB_PROFILE_WORK(KBW_INPUT_GLYPHS, Cursor != Storage->End);
      kbts_u32 Symbol = Cursor != Storage->End ? Glyphs[Cursor].CompiledSymbol : 0;
      Symbols[Center + Index] = Program->LocalMasks ? kbts__CompiledLocalMask(Program, Symbol) : Symbol;
      Offsets[Center + Index] = Cursor;
    }
  }
  Cursor = Anchor;
  {
    KB_PROFILE_SCOPE(KBP_BACKTRACK_GATHER);
    for(kbts_u32 Index = 1; Index <= Bucket.Backtrack; ++Index)
    {
      if(Cursor != KBTS__GLYPH_END)
        Cursor = Cursor == Storage->First ? KBTS__GLYPH_END : kbts__PrevGlyphSlot(Storage, Cursor);
      if(Stream->HasIgnorables)
      {
        while(Cursor != KBTS__GLYPH_END)
        {
          KB_PROFILE_WORK(KBW_BACKTRACK_GLYPHS, 1);
          if(!kbts__GsubSkipGlyph(&Glyphs[Cursor], SkipFlags)) break;
          Cursor = Cursor == Storage->First ? KBTS__GLYPH_END : kbts__PrevGlyphSlot(Storage, Cursor);
        }
      }
      else KB_PROFILE_WORK(KBW_BACKTRACK_GLYPHS, Cursor != KBTS__GLYPH_END);
      kbts_u32 Symbol = Cursor != KBTS__GLYPH_END ? Glyphs[Cursor].CompiledSymbol : 0;
      Symbols[Center - Index] = Program->LocalMasks ? kbts__CompiledLocalMask(Program, Symbol) : Symbol;
    }
  }
  kbts_u32 Candidate = kbts__SelectCompiledCandidate(Cache, Program, Bucket, Symbols + Center);
  if(Candidate == (kbts_u32)-1) return 0;
  const kbts__compiled_outcome *Outcome = &Cache->Outcomes[kbts__CompiledOutcomeIndex(Program, Candidate)];
  kbts_u32 LastInput = Outcome->InputCount ? Offsets[Center + Outcome->InputCount - 1] : Anchor;
  *InputEnd = LastInput == Storage->End ? Storage->End : kbts__NextGlyphSlot(Storage, LastInput);
  KB_PROFILE_WORK(KBW_SEQUENCE_MATCHES, 1);
  return Outcome;
}

KBTS_INLINE kbts_u32 kbts__GsubCandidateBit(kbts_u64 Bits)
{
  kbts_u32 Low = (kbts_u32)Bits;
  return Low ? kbts__LsbPositionOrBitWidth32(Low) : 32 + kbts__LsbPositionOrBitWidth32((kbts_u32)(Bits >> 32));
}

/* Repair exact symbol positions locally, but only add stage/native admission
 * bits: execution checks live eligibility, so stale admissions are harmless. */
static void kbts__GsubUpdateCandidates(kbts_shape_config *Config, kbts__gsub_stream *Stream,
    const kbts_glyph *Glyph, kbts_u32 Slot, kbts_u32 OldSymbol,
    kbts_u32 FirstLookup, kbts_u32 LastLookup, kbts_u32 MinimumStage)
{
  kbts_u64 Bit = (kbts_u64)1 << (Slot & 63);
  if(Stream->IndexSymbols && OldSymbol != Glyph->CompiledSymbol)
  {
    Stream->SymbolCandidates[(kbts_un)OldSymbol * Stream->SymbolStride + Slot / 64] &= ~Bit;
    kbts__GsubSymbolRow(Stream, Glyph->CompiledSymbol)[Slot / 64] |= Bit;
  }
  kbts__GsubAdmitStages(Config, Stream, Glyph, Slot, FirstLookup, LastLookup, MinimumStage);
}

static void kbts__GsubRecordMutation(kbts_shape_scratchpad *Scratchpad, kbts_glyph *Glyph,
    kbts_u16 OldId, kbts_u32 OldFlags, kbts_u16 OldSymbol)
{
  kbts__gsub_stream *Stream = &Scratchpad->Stream;
  if(!Stream->Indexing || Scratchpad->Error) return;
  kbts_glyph_storage *Storage = Scratchpad->Storage;
  if(OldId == Glyph->Id && OldFlags == Glyph->Flags) return;
  kbts_u32 Slot = (kbts_u32)(Glyph - Storage->Glyphs);
  kbts__GsubUpdateCandidates(Scratchpad->Config, Stream, Glyph, Slot, OldSymbol,
      Stream->FirstLookup, Stream->LastLookup, Stream->MinimumStage);
}

static void kbts__GsubInsertSlot(kbts_shape_scratchpad *Scratchpad, kbts_u32 Slot)
{
  kbts__gsub_stream *Stream = &Scratchpad->Stream;
  if(!Stream->Indexing || Scratchpad->Error) return;
  if((Scratchpad->Storage->Count > Scratchpad->GlyphCapacity ||
      Scratchpad->Storage->Count > Stream->WordCount * 64) &&
     !kbts__ReserveGsubStream(Scratchpad, Scratchpad->Storage->Count)) return;
  const kbts_glyph *Glyph = &Scratchpad->Storage->Glyphs[Slot];
  if(Stream->IndexSymbols)
  {
    Stream->HasIgnorables |= !!(Glyph->UnicodeFlags & KBTS_UNICODE_FLAG_DEFAULT_IGNORABLE);
    kbts__GsubSymbolRow(Stream, Glyph->CompiledSymbol)[Slot / 64] |= (kbts_u64)1 << (Slot & 63);
  }
  kbts__GsubAdmitStages(Scratchpad->Config, Stream, Glyph, Slot,
      Stream->FirstLookup, Stream->LastLookup, Stream->MinimumStage);
}

static void kbts__GsubRemoveSlot(kbts_shape_scratchpad *Scratchpad, kbts_u32 Slot)
{
  kbts__gsub_stream *Stream = &Scratchpad->Stream;
  if(!Stream->Indexing || Slot >= Stream->Capacity) return;
  const kbts__gsub_plan *Plan = Scratchpad->Config->GsubPlan;
  const kbts_glyph *Glyph = &Scratchpad->Storage->Glyphs[Slot];
  kbts_u64 Bit = (kbts_u64)1 << (Slot & 63);
  if(Stream->IndexSymbols)
    Stream->SymbolCandidates[(kbts_un)Glyph->CompiledSymbol * Stream->SymbolStride + Slot / 64] &= ~Bit;
  /* Completed rows are unobservable. The next operation resets admissions,
   * and first admission clears its readable extent before any row is read. */
  if(Stream->IndexNative)
    for(kbts_u32 W = Stream->MinimumStage / 64;
        W <= Plan->Nodes[Stream->LastLookup - 1].Stage / 64; ++W)
    {
      kbts_u64 Stages = Stream->StageCandidates[W] & Plan->NativeStages[W];
      if(W == Stream->MinimumStage / 64) Stages &= ~(kbts_u64)0 << (Stream->MinimumStage & 63);
      while(Stages)
      {
        kbts_u32 Stage = W * 64 + kbts__GsubCandidateBit(Stages);
        const kbts__gsub_node *Node = &Plan->Nodes[Plan->Stages[Stage].FirstLookup];
        Stream->NativeCandidates[(kbts_un)(Node->NativeIndex - Stream->NativeFirst) * Stream->SymbolStride + Slot / 64] &= ~Bit;
        Stages &= Stages - 1;
      }
    }
}

/* Ordered storage can enumerate physical bitmaps. Reorders only change the
 * iterator: use the same candidate membership in logical order otherwise. */
static kbts_u32 kbts__GsubConstantCandidate(const kbts__gsub_plan *Plan,
    kbts__gsub_stream *Stream, kbts_glyph_storage *Storage,
    const kbts_u32 *FirstSymbols, kbts_u32 Position, kbts_un *CachedWord, kbts_u64 *CachedBits)
{
  if(Position == Storage->End) return Storage->End;
  if(!Storage->Ordered)
  {
    for(; Position != Storage->End; Position = kbts__NextGlyphSlot(Storage, Position))
    {
      kbts_u32 Symbol = Storage->Glyphs[Position].CompiledSymbol;
      if(FirstSymbols[Symbol / 32] & (1u << (Symbol & 31))) return Position;
    }
    return Storage->End;
  }
  for(kbts_un Word = Position / 64; Word < ((kbts_un)Storage->Count + 63) / 64; ++Word)
  {
    kbts_u64 Bits = *CachedBits;
    if(Word != *CachedWord)
    {
      Bits = 0;
      for(kbts_u32 W = 0; W < Plan->SymbolWordCount; ++W)
      {
        kbts_u32 Symbols = FirstSymbols[W] & Stream->PresentSymbols[W];
        while(Symbols)
        {
          kbts_u32 Symbol = W * 32 + kbts__LsbPositionOrBitWidth32(Symbols);
          Symbols &= Symbols - 1;
          Bits |= Stream->SymbolCandidates[(kbts_un)Symbol * Stream->SymbolStride + Word];
        }
      }
      *CachedWord = Word;
      *CachedBits = Bits;
    }
    if(Word == Position / 64) Bits &= ~(kbts_u64)0 << (Position & 63);
    if(Bits) return (kbts_u32)(Word * 64 + kbts__GsubCandidateBit(Bits));
  }
  return Storage->End;
}

static void kbts__GsubConstantNode(kbts_shape_scratchpad *Scratchpad, kbts_shape_config *Config,
    kbts_u32 LookupIndex, kbts_u32 LastLookup)
{
  kbts__gsub_stream *Stream = &Scratchpad->Stream;
  kbts_glyph_storage *Storage = Scratchpad->Storage;
  const kbts__gsub_node *Node = &Config->GsubPlan->Nodes[LookupIndex];
  const kbts__sequential_lookup *Sequential = &Config->SequentialLookups[LookupIndex];
  kbts_u32 Filter = Config->GsubPlan->RequiredFlags[LookupIndex];
  kbts_u32 Generated = (Sequential->GlyphFilter & (KBTS_GLYPH_FLAG_RPHF | KBTS_GLYPH_FLAG_PREF)) |
                      KBTS_GLYPH_FLAG_GENERATED_BY_GSUB;
  const kbts__compiled_contexts *Cache = Config->Font->CompiledContexts;
  const kbts__gsub_plan *Plan = Config->GsubPlan;
  const kbts_u32 *FirstSymbols = Cache->LookupSummaries[Sequential->LookupIndex].FirstSymbols;
  kbts_u32 Any = 0;
  for(kbts_u32 W = 0; W < Plan->SymbolWordCount; ++W)
    Any |= FirstSymbols[W] & Stream->PresentSymbols[W];
  if(!Any) return;
  Stream->FirstLookup = LookupIndex;
  Stream->LastLookup = LastLookup;
  Stream->MinimumStage = Node->Stage;
  kbts_u32 Progress = Storage->First;
  /* Constant actions write only their consumed prefix. No unconsumed candidate
   * in this lookup changes membership, so assemble each position word once. */
  kbts_un CachedWord = (kbts_un)-1;
  kbts_u64 CachedBits = 0;
  while(Progress != Storage->End)
  {
      kbts_u32 Anchor = kbts__GsubConstantCandidate(Plan, Stream, Storage, FirstSymbols,
          Progress, &CachedWord, &CachedBits);
      if(Anchor == Storage->End) break;
      Progress = kbts__NextGlyphSlot(Storage, Anchor);
      kbts_glyph *Glyph = &Storage->Glyphs[Anchor];
      kbts_u16 FeatureValue;
      if(!kbts__GsubLookupEnabled(Config, Glyph, LookupIndex, Filter, &FeatureValue)) continue;
      KB_PROFILE_SCOPE(KBP_SUBSTITUTE);
      for(kbts_u32 Subtable = 0; Subtable < Node->SubtableCount; ++Subtable)
      {
        KB_PROFILE_WORK(KBW_SUBTABLE_VISITS, 1);
        const kbts__compiled_program *Program = kbts__ProgramForSubtable(Cache, Node->FirstSubtable + Subtable);
        KB_PROFILE_ONLY(
          if(Program->CandidateCount && Program->Buckets[kbts__CompiledBucketIndex(Program, Glyph->CompiledSymbol)].Count)
            KB_PROFILE_CONTEXT(kbts__GsubStreamContextFormat(Config, Sequential->LookupIndex, Subtable));
        )
        kbts_u32 InputEnd;
        const kbts__compiled_outcome *Outcome = kbts__MatchGsubStream(Scratchpad, Program, Anchor,
            KBTS__SKIP_FLAGS_GSUB_SEQUENCE(Sequential->SkipFlags), &InputEnd);
        if(!Outcome) continue;
        if(Outcome->FusionCount)
        {
          kbts_u32 Count = 1;
          kbts_u32 Slot = kbts__NextGlyphSlot(Storage, Anchor);
          while(Count < Outcome->FusionCount && Slot != Storage->End)
          {
            const kbts_glyph *Next = &Storage->Glyphs[Slot];
            kbts_u16 NextFeature;
            if(Scratchpad->CompiledOffsets[Program->MaxBacktrack + Count] != Slot ||
               !kbts__GsubLookupEnabled(Config, Next, LookupIndex, Filter, &NextFeature) ||
               NextFeature != FeatureValue) break;
            ++Count;
            Slot = kbts__NextGlyphSlot(Storage, Slot);
          }
          const kbts_u16 *Ids = Cache->FusionGlyphs + Outcome->FusionOffset;
          Slot = Anchor;
          for(kbts_u32 Index = 0; Index < Count; ++Index)
          {
            kbts_glyph *Replacement = &Storage->Glyphs[Slot];
            kbts__GsubMutate(Scratchpad, Config->Font, Replacement, Ids[Index], Generated);
            Slot = kbts__NextGlyphSlot(Storage, Slot);
          }
          Progress = Slot;
          KB_PROFILE_WORK(KBW_SUBSTITUTION_MATCHES, Count);
        }
        else
        {
          KBTS_ASSERT(!Outcome->RecordCount);
          /* A blocker consumes input for this node without writing anything. */
          Progress = InputEnd;
        }
        break;
      }
  }
}


/* Inclusive candidate search, with logical traversal after nonmonotonic edits. */
KBTS_INLINE kbts_u32 kbts__GsubNativeCandidate(const kbts_u64 *Candidates,
    kbts_glyph_storage *Storage, kbts_u32 Position, kbts_b32 Reverse)
{
  if(Position == KBTS__GLYPH_END || Position == Storage->End) return KBTS__GLYPH_END;
  if(!Storage->Ordered)
  {
    while(Position != KBTS__GLYPH_END && Position != Storage->End)
    {
      if(Candidates[Position / 64] & ((kbts_u64)1 << (Position & 63))) return Position;
      Position = Reverse ? (Position == Storage->First ? KBTS__GLYPH_END :
          kbts__PrevGlyphSlot(Storage, Position)) : kbts__NextGlyphSlot(Storage, Position);
    }
    return KBTS__GLYPH_END;
  }
  kbts_u32 Word = Position / 64;
  kbts_u64 Bits = Candidates[Word];
  if(Reverse)
  {
    Bits &= ~(kbts_u64)0 >> (63 - (Position & 63));
    for(;;)
    {
      if(Bits)
      {
        kbts_u32 High = (kbts_u32)(Bits >> 32);
        kbts_u32 Bit = High ? 32 + kbts__MsbPositionOrZero32(High) : kbts__MsbPositionOrZero32((kbts_u32)Bits);
        return Word * 64 + Bit;
      }
      if(!Word) return KBTS__GLYPH_END;
      Bits = Candidates[--Word];
    }
  }
  Bits &= ~(kbts_u64)0 << (Position & 63);
  for(;;)
  {
    if(Bits) return Word * 64 + kbts__GsubCandidateBit(Bits);
    if(Word == (Storage->Count - 1) / 64) return KBTS__GLYPH_END;
    Bits = Candidates[++Word];
  }
}

/* Native candidates are conservative, not a frozen anchor queue. Every
 * continuation reloads even the current word to see contextual child writes.
 * Structural edits repair just their own slot; newly readable words are
 * initialized without moving the fixed candidate rows. */
static kbts_b32 kbts__GsubNativeNode(kbts_shape_scratchpad *Scratchpad, kbts_shape_config *Config,
    kbts_glyph_storage *Storage, kbts_u32 LookupIndex,
    kbts_u32 StageIndex, kbts_u32 LastLookup)
{
  kbts__gsub_stream *Stream = &Scratchpad->Stream;
  kbts_b32 IndexCandidates = Stream->IndexNative;
  const kbts__gsub_node *Node = &Config->GsubPlan->Nodes[LookupIndex];
  const kbts__sequential_lookup *Sequential = &Config->SequentialLookups[LookupIndex];
  kbts_b32 Reverse = kbts__FontLookup(Config->Font, KBTS_SHAPING_TABLE_GSUB, Sequential->LookupIndex)->Reverse;
  kbts_u32 Filter = Config->GsubPlan->RequiredFlags[LookupIndex];
  kbts_u32 Generated = Sequential->GlyphFilter & (KBTS_GLYPH_FLAG_RPHF | KBTS_GLYPH_FLAG_PREF);
  kbts__gsub_frame *Frames = (kbts__gsub_frame *)Scratchpad->ScratchMemory;
  Stream->FirstLookup = LookupIndex;
  Stream->LastLookup = LastLookup;
  Stream->MinimumStage = StageIndex;
  /* Children may admit another anchor in this lookup, not just later stages. */
  kbts_u32 Position = Reverse ? kbts__LastGlyphSlot(Storage) : Storage->First;
  while(Position != KBTS__GLYPH_END && Position != Storage->End)
  {
    if(IndexCandidates)
    {
      const kbts_u64 *Candidates = Stream->NativeCandidates + (kbts_un)(Node->NativeIndex - Stream->NativeFirst) * Stream->SymbolStride;
      Position = kbts__GsubNativeCandidate(Candidates, Storage, Position, Reverse);
      if(Position == KBTS__GLYPH_END) break;
    }
    const kbts_glyph *Current = &Storage->Glyphs[Position];
    kbts_glyph_ref Glyph = Current->Ref;
    kbts_u16 FeatureValue;
    if(!kbts__GsubLookupEnabled(Config, Current, LookupIndex, Filter, &FeatureValue))
    {
      Position = Reverse ? (Position == Storage->First ? KBTS__GLYPH_END :
          kbts__PrevGlyphSlot(Storage, Position)) : kbts__NextGlyphSlot(Storage, Position);
      continue;
    }
    kbts__BeginLookupApplication(Scratchpad, Glyph);
    Frames[0] = KBTS__ZERO_TYPE(kbts__gsub_frame);
    Frames[0].LookupIndex = Sequential->LookupIndex;
    Frames[0].InputGlyph = Glyph;
    kbts_un FrameCount = 1;
    while(FrameCount && !Scratchpad->Error)
    {
      kbts__DoSubstitution(Scratchpad, Config, Storage, LookupIndex, FeatureValue,
          Frames, &FrameCount, 0, Sequential->SkipFlags, Generated, 1);
    }
    if(Scratchpad->Error) return 0;
    if(!Reverse)
    {
      kbts_glyph_ref Next = kbts__EndLookupApplication(Scratchpad);
      Position = Next ? Next - 1 : Storage->End;
      if(Next == Glyph) Position = kbts__NextGlyphSlot(Storage, Position);
    }
    else Position = Position == Storage->First ? KBTS__GLYPH_END : kbts__PrevGlyphSlot(Storage, Position);
  }
  return 1;
}

static void kbts__RunGsubStages(kbts_shape_scratchpad *Scratchpad, kbts_shape_config *Config,
    kbts_glyph_storage *Storage, kbts_un FirstSequentialLookupIndex,
    kbts_un OnePastLastSequentialLookupIndex)
{
  Scratchpad->Storage = Storage;
  Storage->IndexedScratchpad = 0;
  Scratchpad->Stream.Indexing = 0;
  const kbts__gsub_plan *Plan = Config->GsubPlan;
  if(FirstSequentialLookupIndex >= OnePastLastSequentialLookupIndex || Scratchpad->Error) return;
  KBTS_ASSERT(OnePastLastSequentialLookupIndex <= Plan->GsubLookupCount);
  kbts_u32 FirstLookup = (kbts_u32)FirstSequentialLookupIndex;
  kbts_u32 LastLookup = (kbts_u32)OnePastLastSequentialLookupIndex;
  kbts__gsub_stream *Stream = &Scratchpad->Stream;
  Stream->HasIgnorables = 0;
  if(Storage->First == Storage->End) return;
  kbts_u32 NativeEnd = LastLookup < Plan->GsubLookupCount ? Plan->Nodes[LastLookup].NativeIndex : Plan->NativeCount;
  Stream->NativeFirst = Plan->Nodes[FirstLookup].NativeIndex;
  Stream->NativeCount = NativeEnd - Stream->NativeFirst;
  Stream->IndexSymbols = Stream->NativeCount != LastLookup - FirstLookup;
  /* Bounded active-range probe: outside live glyphs do not justify indexing. */
  kbts_u32 Probe = Storage->First, ActiveCount = 0;
  while(Stream->NativeCount && Probe != Storage->End && ActiveCount <= 64)
  {
    ++ActiveCount;
    Probe = kbts__NextGlyphSlot(Storage, Probe);
  }
  Stream->IndexNative = Stream->NativeCount && ActiveCount > 64;
  if(Stream->IndexNative || Stream->IndexSymbols)
  {
    if(!kbts__ReserveGsubStream(Scratchpad, Storage->Count)) return;
    kbts__GsubBuildIndexes(Config, Storage, Stream, FirstLookup, LastLookup, 0);
  }
  Stream->Indexing = Stream->IndexNative || Stream->IndexSymbols;
  Storage->IndexedScratchpad = Stream->Indexing ? Scratchpad : 0;
  for(kbts_u32 StageIndex = Plan->Nodes[FirstLookup].Stage;
      StageIndex <= Plan->Nodes[LastLookup - 1].Stage; ++StageIndex)
  {
    const kbts__gsub_stage *Stage = &Plan->Stages[StageIndex];
    if(Stage->OnePastLastLookup <= FirstLookup || Stage->FirstLookup >= LastLookup) continue;
    if(Stream->Indexing && !(Stream->StageCandidates[StageIndex / 64] & ((kbts_u64)1 << (StageIndex & 63)))) continue;
    kbts_u32 NodeCount = Stage->NodeCount;
    for(kbts_u32 NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex)
    {
      /* Merged stages are contiguous lookup intervals. Live execution preserves
       * original order even when symbol-only proofs permit a DAG reorder. */
      kbts_u32 Lookup = Stage->FirstLookup + NodeIndex;
      if(Lookup < FirstLookup || Lookup >= LastLookup) continue;
      const kbts__sequential_lookup *Sequential = &Config->SequentialLookups[Lookup];
      const kbts__font_lookup *Descriptor = kbts__FontLookup(Config->Font, KBTS_SHAPING_TABLE_GSUB, Sequential->LookupIndex);
      KB_PROFILE_LOOKUP_SCOPE(0, Sequential->LookupIndex, Descriptor->Filter.Type, KBL_TOTAL);
      KB_PROFILE_LOOKUP_SCOPE(0, Sequential->LookupIndex, Descriptor->Filter.Type, KBL_APPLY);
      (void)Descriptor;
      if(Plan->Nodes[Lookup].Kind == KBTS__GSUB_NATIVE)
      {
        KBTS_ASSERT(NodeCount == 1);
        if(!kbts__GsubNativeNode(Scratchpad, Config, Storage, Lookup, StageIndex,
            LastLookup)) goto Finished;
      }
      else kbts__GsubConstantNode(Scratchpad, Config, Lookup, LastLookup);
    }
  }
Finished:
  Stream->Indexing = 0;
  Storage->IndexedScratchpad = 0;
}
