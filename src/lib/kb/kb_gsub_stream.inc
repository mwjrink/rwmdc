KB_PROFILE_ONLY(
static kbts_u32 kbts__GsubStreamContextFormat(kbts_shape_config *Config, kbts_u32 LookupIndex, kbts_u32 Subtable)
{
  kbts__gsub_gpos *Gsub = kbts__BlobTableDataType(Config->Font->Blob, KBTS_BLOB_TABLE_ID_GSUB, kbts__gsub_gpos);
  kbts__lookup *Lookup = kbts__GetLookup(kbts__GetLookupList(Gsub), LookupIndex);
  kbts_u16 *Offsets = KBTS__POINTER_AFTER(kbts_u16, Lookup);
  kbts_u16 *Table = KBTS__POINTER_OFFSET(kbts_u16, Lookup, Offsets[Subtable]);
  kbts_u32 Type = Lookup->Type;
  while(Type == 7)
  {
    kbts__extension *Extension = (kbts__extension *)Table;
    Type = Extension->LookupType;
    Table = KBTS__POINTER_OFFSET(kbts_u16, Extension, kbts__ReadU32Unaligned(&Extension->Offset));
  }
  return (Type == 6 ? 3 : 0) + Table[0] - 1;
}
)

/* Derived admission indexes over the canonical active glyph range. */
static kbts_b32 kbts__GsubStreamSlice(kbts_un *Size, kbts_un Count, kbts_un Width,
                                     kbts_un Alignment, kbts_un *Offset)
{
  const kbts_un Limit = 0xFFFFFFFFu;
  kbts_un Padding = (Alignment - (*Size & (Alignment - 1))) & (Alignment - 1);
  if(Padding > Limit - *Size) return 0;
  *Size += Padding;
  *Offset = *Size;
  if(!Width) return 1;
  if(Count > (Limit - *Size) / Width) return 0;
  *Size += Count * Width;
  return 1;
}

/* No glyph records are allocated here. Keep the old index allocation intact
 * on failure, including fixed-arena exhaustion. */
static kbts_b32 kbts__ReserveGsubStream(kbts_shape_scratchpad *Scratchpad, kbts_un Required)
{
  kbts__gsub_stream *Stream = &Scratchpad->Stream;
  if(Required <= Stream->Capacity) return 1;
  kbts_un Capacity = Stream->Capacity ? Stream->Capacity : 64;
  while(Capacity < Required)
  {
    if(Capacity > 0xFFFFFFFFu / 2) { Capacity = Required; break; }
    Capacity *= 2;
  }
  kbts_un Offsets[4], Size = 0;
  const kbts__gsub_plan *Plan = Scratchpad->Config->GsubPlan;
  kbts_un Stride = Capacity / 64 + !!(Capacity & 63);
  kbts_un SymbolCount = Plan->NativeCount != Plan->GsubLookupCount ? Plan->SymbolCount : 0;
  if(Capacity > 0xFFFFFFFFu ||
     Plan->SymbolCount > 0xFFFFFFFFu / sizeof(kbts_u64) ||
     Plan->NativeCount > 0xFFFFFFFFu / sizeof(kbts_u64) ||
     !kbts__GsubStreamSlice(&Size, (kbts_un)Plan->StageCount / 64 + !!(Plan->StageCount & 63),
         sizeof(kbts_u64), KBTS_ALIGNOF(kbts_u64), &Offsets[0]) ||
     !kbts__GsubStreamSlice(&Size, Stride, SymbolCount * sizeof(kbts_u64), KBTS_ALIGNOF(kbts_u64), &Offsets[1]) ||
     !kbts__GsubStreamSlice(&Size, Stride, (kbts_un)Plan->NativeCount * sizeof(kbts_u64), KBTS_ALIGNOF(kbts_u64), &Offsets[2]) ||
     !kbts__GsubStreamSlice(&Size, SymbolCount ? Plan->SymbolWordCount : 0, sizeof(kbts_u32), KBTS_ALIGNOF(kbts_u32), &Offsets[3]) ||
     Size > 0xffffffffu - (KBTS_ALIGNOF(kbts_u64) - 1))
  {
    Scratchpad->Error = KBTS_SHAPE_ERROR_OUT_OF_MEMORY;
    return 0;
  }
  void *Memory = kbts__AllocatorAllocate(Scratchpad->Allocator, Scratchpad->AllocatorData,
      Size + KBTS_ALIGNOF(kbts_u64) - 1);
  if(!Memory)
  {
    Scratchpad->Error = KBTS_SHAPE_ERROR_OUT_OF_MEMORY;
    return 0;
  }
  kbts__gsub_stream Previous = *Stream;
  void *Data = KBTS__ALIGN_POINTER(void, Memory, KBTS_ALIGNOF(kbts_u64));
  Stream->Memory = Memory;
  Stream->Capacity = Capacity;
  Stream->SymbolStride = Stride;
  Stream->StageCandidates = KBTS__POINTER_OFFSET(kbts_u64, Data, Offsets[0]);
  Stream->SymbolCandidates = SymbolCount ? KBTS__POINTER_OFFSET(kbts_u64, Data, Offsets[1]) : 0;
  Stream->NativeCandidates = KBTS__POINTER_OFFSET(kbts_u64, Data, Offsets[2]);
  Stream->PresentSymbols = SymbolCount ? KBTS__POINTER_OFFSET(kbts_u32, Data, Offsets[3]) : 0;
  /* REVIEW[RUN01 PERF]: Growth zeros every symbol/native row before copying
   * admitted rows; first-use admission also clears previously unused rows.
   * How much of growth cost is this untouched capacity? Compare initializing
   * stage/presence metadata and copied-row tails only, retaining zero tails
   * and the lazy-row initialization invariant rather than rebuilding glyphs. */
  KBTS_MEMSET(Data, 0, Size);
  if(Previous.Memory && Stream->Indexing)
  {
    KBTS_MEMCPY(Stream->StageCandidates, Previous.StageCandidates,
        ((kbts_un)Plan->StageCount / 64 + !!(Plan->StageCount & 63)) * sizeof(kbts_u64));
    if(Stream->IndexSymbols)
    {
      KBTS_MEMCPY(Stream->PresentSymbols, Previous.PresentSymbols,
          (kbts_un)Plan->SymbolWordCount * sizeof(kbts_u32));
      for(kbts_u32 S = 0; S < Plan->SymbolCount; ++S)
        if(Previous.PresentSymbols[S / 32] & (1u << (S & 31)))
          KBTS_MEMCPY(Stream->SymbolCandidates + (kbts_un)S * Stride,
              Previous.SymbolCandidates + (kbts_un)S * Previous.SymbolStride,
              Previous.SymbolStride * sizeof(kbts_u64));
    }
    if(Stream->IndexNative)
      for(kbts_u32 I = 0; I < Plan->GsubLookupCount; ++I)
      {
        const kbts__gsub_node *Node = &Plan->Nodes[I];
        if(Node->Kind == KBTS__GSUB_NATIVE &&
           (Previous.StageCandidates[Node->Stage / 64] & ((kbts_u64)1 << (Node->Stage & 63))))
          KBTS_MEMCPY(Stream->NativeCandidates + (kbts_un)Node->NativeIndex * Stride,
              Previous.NativeCandidates + (kbts_un)Node->NativeIndex * Previous.SymbolStride,
              Previous.SymbolStride * sizeof(kbts_u64));
      }
  }
  kbts__AllocatorFree(Scratchpad->Allocator, Scratchpad->AllocatorData, Previous.Memory);
  return 1;
}


/* Config-owned stage words already aggregate every glyph's static lookup
 * eligibility. Overrides select the possible row; live flags, disabled
 * features, required features and nonbinary values are checked at execution. */
static void kbts__GsubAdmitStages(kbts_shape_config *Config, kbts__gsub_stream *Stream,
    const kbts_glyph *Glyph, kbts_u32 Slot, kbts_u32 FirstLookup, kbts_u32 LastLookup,
    kbts_u32 MinimumStage)
{
  const kbts__gsub_plan *Plan = Config->GsubPlan;
  if(FirstLookup >= LastLookup || MinimumStage >= Plan->StageCount) return;
  kbts_u32 FirstStage = KBTS__MAX(MinimumStage, Plan->Nodes[FirstLookup].Stage);
  kbts_u32 LastStage = Plan->Nodes[LastLookup - 1].Stage;
  if(FirstStage > LastStage) return;
  const kbts_u64 *Row = 0;
  if(Glyph->Id < Config->GlyphCount)
    Row = ((Glyph->Config && Glyph->Config->HasEnabledLookups) ?
        Plan->PossibleStages : Plan->DefaultStages) + (kbts_un)Glyph->Id * Plan->AdmissionStride;
  for(kbts_u32 W = FirstStage / 64; W <= LastStage / 64; ++W)
  {
    kbts_u64 Bits = Row ? Row[W] : ~(kbts_u64)0;
    if(Glyph->Config && Glyph->Config->DisabledGsubStages)
      Bits &= ~Glyph->Config->DisabledGsubStages[W];
    if(W == FirstStage / 64) Bits &= ~(kbts_u64)0 << (FirstStage & 63);
    if(W == LastStage / 64) Bits &= ~(kbts_u64)0 >> (63 - (LastStage & 63));
    kbts_u64 New = Bits & ~Stream->StageCandidates[W];
    Stream->StageCandidates[W] |= Bits;
    if(!Stream->IndexNative) continue;
    Bits &= Plan->NativeStages[W];
    while(Bits)
    {
      kbts_u32 Low = (kbts_u32)Bits;
      kbts_u32 B = Low ? kbts__LsbPositionOrBitWidth32(Low) :
          32 + kbts__LsbPositionOrBitWidth32((kbts_u32)(Bits >> 32));
      kbts_u32 Stage = W * 64 + B;
      const kbts__gsub_node *Node = &Plan->Nodes[Plan->Stages[Stage].FirstLookup];
      kbts_u64 *NativeRow = Stream->NativeCandidates + (kbts_un)Node->NativeIndex * Stream->SymbolStride;
      if(New & ((kbts_u64)1 << B)) KBTS_MEMSET(NativeRow, 0, Stream->SymbolStride * sizeof(*NativeRow));
      NativeRow[Slot / 64] |= (kbts_u64)1 << (Slot & 63);
      Bits &= Bits - 1;
    }
  }
}

/* Presence also marks initialized rows. Clear only rows used by this operation,
 * but clear their full stride so later growth cannot expose old position bits. */
KBTS_INLINE kbts_u64 *kbts__GsubSymbolRow(kbts__gsub_stream *Stream, kbts_u32 Symbol)
{
  kbts_u64 *Row = Stream->SymbolCandidates + (kbts_un)Symbol * Stream->SymbolStride;
  kbts_u32 Bit = (kbts_u32)1 << (Symbol & 31);
  if(!(Stream->PresentSymbols[Symbol / 32] & Bit))
  {
    Stream->PresentSymbols[Symbol / 32] |= Bit;
    KBTS_MEMSET(Row, 0, Stream->SymbolStride * sizeof(*Row));
  }
  return Row;
}

/* One initial pass per operation. Stable-slot edits never rebuild survivors. */
static void kbts__GsubBuildIndexes(kbts_shape_config *Config, kbts_glyph_storage *Storage, kbts__gsub_stream *Stream,
    kbts_u32 FirstLookup, kbts_u32 LastLookup, kbts_u32 MinimumStage)
{
  const kbts__gsub_plan *Plan = Config->GsubPlan;
  KBTS_MEMSET(Stream->StageCandidates, 0,
      ((kbts_un)Plan->StageCount / 64 + !!(Plan->StageCount & 63)) * sizeof(kbts_u64));
  if(Stream->IndexSymbols)
    KBTS_MEMSET(Stream->PresentSymbols, 0, (kbts_un)Plan->SymbolWordCount * sizeof(kbts_u32));
  Stream->HasIgnorables = 0;
  for(kbts_u32 Slot = Storage->First; Slot != Storage->End; Slot = kbts__NextGlyphSlot(Storage, Slot))
  {
    const kbts_glyph *Glyph = &Storage->Glyphs[Slot];
    if(Stream->IndexSymbols)
    {
      Stream->HasIgnorables |= !!(Glyph->UnicodeFlags & KBTS_UNICODE_FLAG_DEFAULT_IGNORABLE);
      kbts__GsubSymbolRow(Stream, Glyph->CompiledSymbol)[Slot / 64] |= (kbts_u64)1 << (Slot & 63);
    }
    kbts__GsubAdmitStages(Config, Stream, Glyph, Slot, FirstLookup, LastLookup, MinimumStage);
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
  if(!kbts__ReserveGsubStream(Scratchpad, Scratchpad->Storage->Count)) return;
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
  /* REVIEW[RUN03 PERF]: Each deletion clears this slot in every admitted native
   * stage, including stages already passed by MinimumStage. Ligature components
   * therefore repeat a stride-separated row walk despite stable-slot repair.
   * Can repair start at the current stage, given next-operation row clearing?
   * Measure deleted slots times visited rows; keep current-stage live updates. */
  if(Stream->IndexNative)
    for(kbts_u32 W = 0; W < (Plan->StageCount / 64 + !!(Plan->StageCount & 63)); ++W)
    {
      kbts_u64 Stages = Stream->StageCandidates[W] & Plan->NativeStages[W];
      while(Stages)
      {
        kbts_u32 Stage = W * 64 + kbts__GsubCandidateBit(Stages);
        const kbts__gsub_node *Node = &Plan->Nodes[Plan->Stages[Stage].FirstLookup];
        Stream->NativeCandidates[(kbts_un)Node->NativeIndex * Stream->SymbolStride + Slot / 64] &= ~Bit;
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
      /* REVIEW[RUN02 CLEANUP]: Logical traversal already supplies a live slot
       * and its current symbol, but this path also probes that symbol's exact
       * position row. Insert/remove/mutation hooks maintain precisely this bit.
       * Is the second membership check doing anything beyond rechecking that
       * invariant? Consider keeping only FirstSymbols here, not a new index. */
      if((FirstSymbols[Symbol / 32] & (1u << (Symbol & 31))) &&
         (Stream->SymbolCandidates[(kbts_un)Symbol * Stream->SymbolStride + Position / 64] &
          ((kbts_u64)1 << (Position & 63)))) return Position;
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
  const kbts_u32 *FirstSymbols = Plan->FirstSymbols + (kbts_un)LookupIndex * Plan->SymbolWordCount;
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
      if((Glyph->Flags & Filter) != Filter ||
         !kbts__GsubLookupEnabled(Config, Glyph->Id, Glyph->Config, LookupIndex, &FeatureValue)) continue;
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
               (Next->Flags & Sequential->GlyphFilter) != Sequential->GlyphFilter ||
               !kbts__GsubLookupEnabled(Config, Next->Id, Next->Config, LookupIndex, &NextFeature) ||
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
 * Structural edits repair just their own slot; capacity growth preserves rows,
 * and every continuation reloads the candidate pointer after possible growth. */
static kbts_b32 kbts__GsubNativeNode(kbts_shape_scratchpad *Scratchpad, kbts_shape_config *Config,
    kbts_glyph_storage *Storage, kbts_lookup_list *LookupList, kbts_u32 LookupIndex,
    kbts_u32 StageIndex, kbts_u32 LastLookup)
{
  kbts__gsub_stream *Stream = &Scratchpad->Stream;
  kbts_b32 IndexCandidates = Stream->IndexNative;
  const kbts__gsub_node *Node = &Config->GsubPlan->Nodes[LookupIndex];
  const kbts__sequential_lookup *Sequential = &Config->SequentialLookups[LookupIndex];
  kbts__lookup *Packed = kbts__GetLookup(LookupList, Sequential->LookupIndex);
  kbts_u32 Type = Packed->Type;
  /* REVIEW[RUN06 CLEANUP]: OpenType requires all extension subtables in a lookup
   * to share one non-extension subtype, so inspecting the first is sufficient.
   * Could font/config validation establish and cache that resolved direction,
   * rather than re-walking the wrapper per run? Keep the cheap runtime decode
   * unless measurement justifies metadata; malformed targets belong at loading. */
  if(Type == 7 && Packed->SubtableCount)
  {
    const kbts_u16 *Offsets = KBTS__POINTER_AFTER(kbts_u16, Packed);
    const kbts_u16 *Base = KBTS__POINTER_OFFSET(kbts_u16, Packed, Offsets[0]);
    while(Type == 7)
    {
      const kbts__extension *Extension = (const kbts__extension *)Base;
      Type = Extension->LookupType;
      Base = KBTS__POINTER_OFFSET(kbts_u16, Extension, kbts__ReadU32Unaligned(&Extension->Offset));
    }
  }
  kbts_b32 Reverse = Type == 8;
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
      const kbts_u64 *Candidates = Stream->NativeCandidates + (kbts_un)Node->NativeIndex * Stream->SymbolStride;
      Position = kbts__GsubNativeCandidate(Candidates, Storage, Position, Reverse);
      if(Position == KBTS__GLYPH_END) break;
    }
    const kbts_glyph *Current = &Storage->Glyphs[Position];
    kbts_glyph_ref Glyph = Current->Ref;
    kbts_u16 FeatureValue;
    if((Current->Flags & Filter) != Filter ||
       !kbts__GsubLookupEnabled(Config, Current->Id, Current->Config, LookupIndex, &FeatureValue))
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
      kbts__DoSubstitution(Scratchpad, Config, Storage, LookupList, LookupIndex, FeatureValue,
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
  Stream->IndexSymbols = NativeEnd - Plan->Nodes[FirstLookup].NativeIndex != LastLookup - FirstLookup;
  // Native-only intervals within one bitmap word are cheaper to scan directly.
  Stream->IndexNative = Storage->LiveCount > 64 || Stream->IndexSymbols;
  if(Stream->IndexNative)
  {
    if(!kbts__ReserveGsubStream(Scratchpad, Storage->Count)) return;
    kbts__GsubBuildIndexes(Config, Storage, Stream, FirstLookup, LastLookup, 0);
  }
  Stream->Indexing = Stream->IndexNative;
  Storage->IndexedScratchpad = Stream->Indexing ? Scratchpad : 0;
  kbts__gsub_gpos *Gsub = kbts__BlobTableDataType(Config->Font->Blob, KBTS_BLOB_TABLE_ID_GSUB, kbts__gsub_gpos);
  kbts_lookup_list *LookupList = kbts__GetLookupList(Gsub);
  for(kbts_u32 StageIndex = Plan->Nodes[FirstLookup].Stage;
      StageIndex <= Plan->Nodes[LastLookup - 1].Stage; ++StageIndex)
  {
    const kbts__gsub_stage *Stage = &Plan->Stages[StageIndex];
    if(Stage->OnePastLastLookup <= FirstLookup || Stage->FirstLookup >= LastLookup) continue;
    if(Stream->IndexNative && !(Stream->StageCandidates[StageIndex / 64] & ((kbts_u64)1 << (StageIndex & 63)))) continue;
    kbts_u32 NodeCount = Stage->NodeCount;
    for(kbts_u32 NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex)
    {
      /* Merged stages are contiguous lookup intervals. Live execution preserves
       * original order even when symbol-only proofs permit a DAG reorder. */
      kbts_u32 Lookup = Stage->FirstLookup + NodeIndex;
      if(Lookup < FirstLookup || Lookup >= LastLookup) continue;
      const kbts__sequential_lookup *Sequential = &Config->SequentialLookups[Lookup];
      kbts__lookup *Packed = kbts__GetLookup(LookupList, Sequential->LookupIndex);
      KB_PROFILE_LOOKUP_SCOPE(0, Sequential->LookupIndex, Packed->Type, KBL_TOTAL);
      KB_PROFILE_LOOKUP_SCOPE(0, Sequential->LookupIndex, Packed->Type, KBL_APPLY);
      (void)Packed;
      if(Plan->Nodes[Lookup].Kind == KBTS__GSUB_NATIVE)
      {
        KBTS_ASSERT(NodeCount == 1);
        if(!kbts__GsubNativeNode(Scratchpad, Config, Storage, LookupList, Lookup, StageIndex,
            LastLookup)) goto Finished;
      }
      else kbts__GsubConstantNode(Scratchpad, Config, Lookup, LastLookup);
    }
  }
Finished:
  Stream->Indexing = 0;
  Storage->IndexedScratchpad = 0;
}
