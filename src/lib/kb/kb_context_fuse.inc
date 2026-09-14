/* Compile-time proofs only: the runtime still guards physical adjacency and
 * lookup/feature eligibility before consuming any compound rewrite prefix. */
typedef struct kbts__fusion_source
{
  kbts_u32 Program, Candidate;
  kbts_u32 *Symbols;
  kbts_u16 *Glyphs;
  int Seen, Ambiguous;
} kbts__fusion_source;

typedef struct kbts__fusion_domains
{
  kbts_u32 *Bits;
  kbts_s32 First, Last;
} kbts__fusion_domains;

static kbts__compiled_wide_test kbts__FusionTest(const kbts__compiled_contexts *Cache, const kbts__compiled_program *Program,
                                               kbts_u32 Candidate, kbts_u32 Stage)
{
  kbts_un Index = (kbts_un)Stage * Program->CandidateCount + Candidate;
  kbts__compiled_wide_test Result;
  if(Program->Wide) Result = ((kbts__compiled_wide_test *)Program->Tests)[Index];
  else
  {
    kbts__compiled_test Test = ((kbts__compiled_test *)Program->Tests)[Index];
    Result.Probe = Test.Probe;
    Result.Predicate = Test.Predicate;
  }
  if(Program->LocalPredicateCount) Result.Predicate = Cache->PredicateIds[Program - Cache->Programs][Result.Predicate];
  return Result;
}

static kbts_u16 *kbts__FusionSubtable(kbts__lookup *Lookup, kbts_u32 Index, kbts_u32 *Type)
{
  kbts_u16 *Offsets = KBTS__POINTER_AFTER(kbts_u16, Lookup);
  kbts_u16 *Base = KBTS__POINTER_OFFSET(kbts_u16, Lookup, Offsets[Index]);
  *Type = Lookup->Type;
  while(*Type == 7)
  {
    kbts__extension *Extension = (kbts__extension *)Base;
    *Type = Extension->LookupType;
    Base = KBTS__POINTER_OFFSET(kbts_u16, Extension, kbts__ReadU32Unaligned(&Extension->Offset));
  }
  return Base;
}

/* The zero symbol also represents a missing glyph. Never infer a substitution
 * from its real-glyph members while silently discarding that boundary case. */
static int kbts__FusionConstant(const kbts__compiled_contexts *Cache, kbts_lookup_list *List,
    const kbts__compiled_outcome *Outcome, const kbts_u32 *Domain,
    const kbts_u32 *GlyphHeads, const kbts_u32 *GlyphNext, kbts_u16 *Replacement)
{
  if(Outcome->InputCount != 1 || Outcome->RecordCount != 1 ||
     Outcome->Records[0].SequenceIndex || (Domain[0] & 1)) return 0;
  kbts_u32 Index = Outcome->Records[0].LookupListIndex;
  if(Index >= List->Count) return 0;
  kbts__lookup *Lookup = kbts__GetLookup(List, Index);
  if(Lookup->Flag || !Lookup->SubtableCount) return 0;
  for(kbts_u32 S = 0; S < Lookup->SubtableCount; ++S)
  {
    kbts_u32 Type;
    kbts_u16 *Base = kbts__FusionSubtable(Lookup, S, &Type);
    if(Type != 1 || (Base[0] != 1 && Base[0] != 2)) return 0;
  }
  int HaveReplacement = 0;
  for(kbts_u32 Symbol = 1; Symbol < Cache->SymbolCount; ++Symbol)
  {
    if(!(Domain[Symbol >> 5] & (1u << (Symbol & 31)))) continue;
    for(kbts_u32 Glyph = GlyphHeads[Symbol]; Glyph != (kbts_u32)-1; Glyph = GlyphNext[Glyph])
    {
      int Matched = 0;
      kbts_u16 NewGlyph = 0;
      for(kbts_u32 S = 0; S < Lookup->SubtableCount; ++S)
      {
        kbts_u32 Type;
        kbts__single_substitution *Subst = (kbts__single_substitution *)kbts__FusionSubtable(Lookup, S, &Type);
        kbts__coverage *Coverage = KBTS__POINTER_OFFSET(kbts__coverage, Subst, Subst->CoverageOffset);
        kbts__cover_glyph_result Cover = kbts__CoverGlyph(Coverage, (kbts_u16)Glyph);
        if(!Cover.Valid) continue;
        if(Subst->Format == 1) NewGlyph = (kbts_u16)(Glyph + (kbts_u32)Subst->DeltaOrCount.DeltaGlyphId);
        else
        {
          if(Cover.Index >= Subst->DeltaOrCount.GlyphCount) return 0;
          NewGlyph = KBTS__POINTER_AFTER(kbts_u16, Subst)[Cover.Index];
        }
        Matched = 1;
        break; /* Native GSUB stops at the first covered subtable. */
      }
      if(!Matched || (HaveReplacement && *Replacement != NewGlyph)) return 0;
      *Replacement = NewGlyph;
      HaveReplacement = 1;
    }
  }
  return HaveReplacement;
}

static kbts_u32 *kbts__FusionDomain(const kbts__compiled_contexts *Cache,
                                   const kbts__fusion_domains *Domains, kbts_s32 Position)
{
  if(Position < Domains->First || Position > Domains->Last) return 0;
  return Domains->Bits + (kbts_un)(Position - Domains->First) * Cache->SymbolWordCount;
}

/* Three-valued membership: impossible, guaranteed, or unresolved. Unknown
 * positions include the boundary symbol, just like untouched tracked slots. */
static int kbts__FusionPredicate(const kbts__compiled_contexts *Cache,
                                 const kbts_u32 *Domain, kbts_u32 Predicate)
{
  if(!Predicate) return 1;
  const kbts_u32 *Mask = Cache->PredicateMasks + (kbts_un)Predicate * Cache->SymbolWordCount;
  kbts_u32 Inside = 0, Outside = 0;
  for(kbts_u32 W = 0; W < Cache->SymbolWordCount; ++W)
  {
    kbts_u32 Bits = Domain ? Domain[W] : (kbts_u32)-1;
    if(W + 1 == Cache->SymbolWordCount && (Cache->SymbolCount & 31))
      Bits &= (1u << (Cache->SymbolCount & 31)) - 1;
    Inside |= Bits & Mask[W];
    Outside |= Bits & ~Mask[W];
  }
  return !Inside ? 0 : !Outside ? 1 : -1;
}

/* Partition on the dispatch symbol instead of flattening bucket copies into
 * a new priority order. Every possible dispatch must prove the same winner. */
static kbts_u32 kbts__FusionWinner(const kbts__compiled_contexts *Cache,
    const kbts__fusion_domains *Domains, kbts_s32 Position, kbts_u32 FirstProgram,
    kbts_u32 ProgramCount)
{
  const kbts_u32 *Current = kbts__FusionDomain(Cache, Domains, Position);
  kbts_u32 Winner = (kbts_u32)-1;
  if(!Current || (Current[0] & 1)) return Winner;
  for(kbts_u32 Symbol = 1; Symbol < Cache->SymbolCount; ++Symbol)
  {
    if(!(Current[Symbol >> 5] & (1u << (Symbol & 31)))) continue;
    kbts_u32 Selected = (kbts_u32)-1;
    for(kbts_u32 P = FirstProgram; P < FirstProgram + ProgramCount && Selected == (kbts_u32)-1; ++P)
    {
      const kbts__compiled_program *Program = kbts__ProgramForSubtable(Cache, P);
      if(!Program->CandidateCount) continue;
      if(Program->FirstFiltered) return (kbts_u32)-1;
      kbts__compiled_bucket Bucket = Program->Buckets[kbts__CompiledBucketIndex(Program, Symbol)];
      for(kbts_u32 C = Bucket.First; C < Bucket.First + Bucket.Count; ++C)
      {
        if(kbts__CompiledOutcomeIndex(Program, C) == (kbts_u32)-1) continue;
        int Impossible = 0, Unresolved = 0;
        for(kbts_u32 T = 0; T < Program->StageCount; ++T)
        {
          kbts__compiled_wide_test Test = kbts__FusionTest(Cache, Program, C, T);
          int Match;
          if(!Test.Predicate) continue;
          if(!Test.Probe)
          {
            const kbts_u32 *Mask = Cache->PredicateMasks + (kbts_un)Test.Predicate * Cache->SymbolWordCount;
            Match = !!(Mask[Symbol >> 5] & (1u << (Symbol & 31)));
          }
          else
          {
            /* Native probes are bounded by 16-bit sequence counts. */
            kbts_s32 At = Position + Test.Probe;
            Match = kbts__FusionPredicate(Cache, kbts__FusionDomain(Cache, Domains, At), Test.Predicate);
          }
          if(!Match) {Impossible = 1; break;}
          if(Match < 0) Unresolved = 1;
        }
        if(Impossible) continue;
        if(Unresolved) return (kbts_u32)-1;
        Selected = kbts__CompiledOutcomeIndex(Program, C);
        break;
      }
    }
    if(Selected == (kbts_u32)-1 || (Winner != (kbts_u32)-1 && Winner != Selected)) return (kbts_u32)-1;
    Winner = Selected;
  }
  return Winner;
}

typedef struct kbts__packed_table
{
  struct kbts__packed_table *Next;
  const kbts_u8 *Data;
  kbts_un Size, Offset;
  kbts_u32 Hash, Align;
} kbts__packed_table;

/* Intern whole byte tables at construction time: runtime keeps a direct pointer
 * and one width-specific load, not an indirection through a compression dictionary. */
static kbts_un kbts__PackTable(kbts__context_builder *Builder, kbts__packed_table **Tables,
    kbts_un *Total, const void *Data, kbts_un Size, kbts_u32 Align, kbts_un *Bytes)
{
  if(Builder->Error || !Size) return 0;
  const kbts_u8 *Input = (const kbts_u8 *)Data;
  kbts_u32 Hash = 2166136261u;
  for(kbts_un I = 0; I < Size; ++I) Hash = (Hash ^ Input[I]) * 16777619u;
  /* REVIEW[COLD3 PERF]: Unlike predicate interning's growing hash, these tables
   * always use 256 chains, and every candidate is byte-hashed before probing.
   * Record distinct-table counts, chain lengths and bytes hashed on large fonts;
   * would sizing this cold index by program count reduce construction work
   * without changing the direct resident table pointers? */
  kbts_u32 Slot = Hash & 255u;
  for(kbts__packed_table *Table = Tables[Slot]; Table; Table = Table->Next)
  {
    if(Table->Hash != Hash || Table->Size != Size || Table->Align != Align) continue;
    kbts_un I = 0;
    while(I < Size && Table->Data[I] == Input[I]) ++I;
    if(I == Size) return Table->Offset;
  }
  kbts__packed_table *Table = (kbts__packed_table *)kbts__ContextMemory(Builder, 1, sizeof(*Table), KBTS_ALIGNOF(kbts__packed_table));
  if(Builder->Error) return 0;
  Table->Data = Input;
  Table->Size = Size;
  Table->Hash = Hash;
  Table->Align = Align;
  Table->Offset = kbts__ContextReserve(Builder, Total, Size, 1, Align);
  Table->Next = Tables[Slot];
  Tables[Slot] = Table;
  *Bytes += Size;
  return Table->Offset;
}

typedef struct kbts__packed_program
{
  kbts_un Tests, Masks, Symbols, Buckets, Outcomes;
  kbts_u8 MaskWidth;
} kbts__packed_program;

static kbts__compiled_contexts *kbts__PackCompiledContexts(kbts__context_builder *Builder,
    const kbts__compiled_contexts *Raw, const kbts__fusion_source *Sources,
    kbts_allocator_function *Allocator, void *AllocatorData,
    kbts_u32 FusionCount, kbts_u32 FusedOutcomes, kbts_u32 FusedChains)
{
  kbts__packed_program *Layout = (kbts__packed_program *)kbts__ContextMemory(Builder, Raw->ProgramCount,
      sizeof(*Layout), KBTS_ALIGNOF(kbts__packed_program));
  kbts__packed_table *MaskTables[256] = KBTS__ZERO, *SymbolTables[256] = KBTS__ZERO;
  kbts__packed_table *TestTables[256] = KBTS__ZERO, *BucketTables[256] = KBTS__ZERO;
  kbts_un Total = sizeof(kbts__compiled_contexts);
  kbts_un ProgramOffset = kbts__ContextReserve(Builder, &Total, Raw->ProgramCount,
      sizeof(kbts__compiled_program), KBTS_ALIGNOF(kbts__compiled_program));
  kbts_un IndexOffset = kbts__ContextReserve(Builder, &Total, Raw->FlatProgramCount, Raw->ProgramIndexWidth, Raw->ProgramIndexWidth);
  kbts_un GlyphBytes = Raw->ClassifierBytes - sizeof(Raw->GlyphPages);
  kbts_un GlyphOffset = kbts__ContextReserve(Builder, &Total, GlyphBytes, 1, Raw->SymbolWidth);
  kbts_un PredicateBytes = (kbts_un)Raw->PredicateCount * Raw->SymbolWordCount * sizeof(kbts_u32);
  kbts_un PredicateOffset = kbts__ContextReserve(Builder, &Total, PredicateBytes, 1, KBTS_ALIGNOF(kbts_u32));
  kbts_un TestBytes = 0, BucketBytes = 0;
  kbts_un WindowCapacity = 1;
  for(kbts_u32 P = 1; P < Raw->ProgramCount && !Builder->Error; ++P)
  {
    const kbts__compiled_program *Program = &Raw->Programs[P];
    WindowCapacity = KBTS__MAX(WindowCapacity, (kbts_un)Program->MaxBacktrack + Program->MaxAhead + 1);
    if(!Program->CandidateCount) continue;
    kbts_un Bytes = (kbts_un)Program->CandidateCount * Program->StageCount *
        (Program->Wide ? sizeof(kbts__compiled_wide_test) : sizeof(kbts__compiled_test));
    /* Preserve the last compact test's 32-bit gather guard after repacking. */
    if(!Program->Wide) Bytes += sizeof(kbts__compiled_test);
    Layout[P].Tests = kbts__PackTable(Builder, TestTables, &Total, Program->Tests,
        Bytes, KBTS_ALIGNOF(kbts__compiled_wide_test), &TestBytes);
    Layout[P].Buckets = kbts__PackTable(Builder, BucketTables, &Total, Program->Buckets,
        (kbts_un)Program->BucketCount * sizeof(kbts__compiled_bucket), KBTS_ALIGNOF(kbts__compiled_bucket), &BucketBytes);
    Layout[P].Symbols = kbts__PackTable(Builder, SymbolTables, &Total, Program->SymbolBuckets,
        (kbts_un)Raw->SymbolCount * Program->SymbolBucketWidth, Program->SymbolBucketWidth, &BucketBytes);
    if(Program->LocalMasks)
    {
      const void *Masks = Program->LocalMasks;
      kbts_un MaskBytes = (kbts_un)Raw->SymbolCount * Program->LocalMaskWidth;
      Layout[P].MaskWidth = Program->LocalMaskWidth;
      if(Program->LocalPredicateCount <= 4)
      {
        MaskBytes = ((kbts_un)Raw->SymbolCount + 1) / 2;
        kbts_u8 *Packed = (kbts_u8 *)kbts__ContextMemory(Builder, MaskBytes, 1, 1);
        if(Builder->Error) break;
        for(kbts_u32 Symbol = 0; Symbol < Raw->SymbolCount; ++Symbol)
          Packed[Symbol >> 1] |= (kbts_u8)(kbts__CompiledLocalMask(Program, Symbol) << ((Symbol & 1u) * 4));
        Masks = Packed;
        Layout[P].MaskWidth = 0;
      }
      Layout[P].Masks = kbts__PackTable(Builder, MaskTables, &Total, Masks, MaskBytes,
          Layout[P].MaskWidth ? Layout[P].MaskWidth : 1, &PredicateBytes);
    }
  }
  kbts_un HotBytes = Total;
  /* Outcome references are needed only after a complete match. Keep cold action
   * records out of the matching prefix along with their fused replacement data. */
  kbts_un OutcomeBytes = 0;
  for(kbts_u32 P = 1; P < Raw->ProgramCount && !Builder->Error; ++P)
  {
    const kbts__compiled_program *Program = &Raw->Programs[P];
    if(!Program->CandidateCount) continue;
    Layout[P].Outcomes = kbts__ContextReserve(Builder, &Total, Program->CandidateCount, Program->OutcomeWidth, Program->OutcomeWidth);
    OutcomeBytes += (kbts_un)Program->CandidateCount * Program->OutcomeWidth;
  }
  kbts_un OutcomeOffset = kbts__ContextReserve(Builder, &Total, Raw->OutcomeCount,
      sizeof(kbts__compiled_outcome), KBTS_ALIGNOF(kbts__compiled_outcome));
  OutcomeBytes += (kbts_un)Raw->OutcomeCount * sizeof(kbts__compiled_outcome);
  kbts_un FusionOffset = kbts__ContextReserve(Builder, &Total, FusionCount, sizeof(kbts_u16), KBTS_ALIGNOF(kbts_u16));
  kbts_un AllocationSize = Total;
  kbts__ContextReserve(Builder, &AllocationSize, KBTS_ALIGNOF(kbts__compiled_contexts) - 1, 1, 1);
  if(WindowCapacity > (kbts_u32)-1 - 8) Builder->Error = 1;
  if(Builder->Error) return 0;
  void *Allocation = kbts__AllocatorAllocate(Allocator, AllocatorData, AllocationSize);
  if(!Allocation) {Builder->Error = 1; return 0;}
  kbts__compiled_contexts *Result = KBTS__ALIGN_POINTER(kbts__compiled_contexts, Allocation, KBTS_ALIGNOF(kbts__compiled_contexts));
  KBTS_MEMSET(Result, 0, Total);
  *Result = *Raw;
  Result->Allocator = Allocator;
  Result->AllocatorData = AllocatorData;
  Result->BaseAllocation = Allocation;
  Result->AllocationSize = AllocationSize;
  Result->HotBytes = HotBytes;
  Result->WindowCapacity = (kbts_u32)WindowCapacity + 8;
  Result->PredicateIds = 0;
  Result->TestBytes = TestBytes;
  Result->PredicateBytes = PredicateBytes;
  Result->BucketBytes = BucketBytes;
  Result->OutcomeBytes = OutcomeBytes;
  Result->FusionBytes = (kbts_un)FusionCount * sizeof(kbts_u16);
  Result->FusedOutcomeCount = FusedOutcomes;
  Result->FusedChainCount = FusedChains;
  Result->Programs = KBTS__POINTER_OFFSET(kbts__compiled_program, Result, ProgramOffset);
  Result->ProgramIndices = KBTS__POINTER_OFFSET(void, Result, IndexOffset);
  Result->GlyphSymbols = KBTS__POINTER_OFFSET(void, Result, GlyphOffset);
  Result->PredicateMasks = KBTS__POINTER_OFFSET(kbts_u32, Result, PredicateOffset);
  Result->Outcomes = KBTS__POINTER_OFFSET(kbts__compiled_outcome, Result, OutcomeOffset);
  Result->FusionGlyphs = FusionCount ? KBTS__POINTER_OFFSET(kbts_u16, Result, FusionOffset) : 0;
  KBTS_MEMCPY(Result->ProgramIndices, Raw->ProgramIndices, (kbts_un)Raw->FlatProgramCount * Raw->ProgramIndexWidth);
  KBTS_MEMCPY(Result->GlyphSymbols, Raw->GlyphSymbols, GlyphBytes);
  KBTS_MEMCPY(Result->PredicateMasks, Raw->PredicateMasks, (kbts_un)Raw->PredicateCount * Raw->SymbolWordCount * sizeof(kbts_u32));
  KBTS_MEMCPY(Result->Outcomes, Raw->Outcomes, (kbts_un)Raw->OutcomeCount * sizeof(kbts__compiled_outcome));
  for(kbts_u32 P = 1; P < Raw->ProgramCount; ++P)
  {
    kbts__compiled_program *Program = &Result->Programs[P];
    const kbts__compiled_program *Old = &Raw->Programs[P];
    *Program = *Old;
    Program->Tests = Program->Outcomes = Program->SymbolBuckets = Program->LocalMasks = 0;
    Program->Buckets = 0;
    if(!Program->CandidateCount) continue;
    Program->Tests = KBTS__POINTER_OFFSET(void, Result, Layout[P].Tests);
    Program->Outcomes = KBTS__POINTER_OFFSET(void, Result, Layout[P].Outcomes);
    Program->Buckets = KBTS__POINTER_OFFSET(kbts__compiled_bucket, Result, Layout[P].Buckets);
    Program->SymbolBuckets = KBTS__POINTER_OFFSET(void, Result, Layout[P].Symbols);
    Program->LocalMasks = Old->LocalMasks ? KBTS__POINTER_OFFSET(void, Result, Layout[P].Masks) : 0;
    Program->LocalMaskWidth = Layout[P].MaskWidth;
    KBTS_MEMCPY(Program->Outcomes, Old->Outcomes, (kbts_un)Program->CandidateCount * Program->OutcomeWidth);
  }
  /* Copy each shared table once; duplicate program pointers already share its offset. */
  for(kbts_u32 Slot = 0; Slot < 256; ++Slot)
  {
    for(kbts__packed_table *Table = MaskTables[Slot]; Table; Table = Table->Next)
      KBTS_MEMCPY(KBTS__POINTER_OFFSET(void, Result, Table->Offset), Table->Data, Table->Size);
    for(kbts__packed_table *Table = SymbolTables[Slot]; Table; Table = Table->Next)
      KBTS_MEMCPY(KBTS__POINTER_OFFSET(void, Result, Table->Offset), Table->Data, Table->Size);
    for(kbts__packed_table *Table = TestTables[Slot]; Table; Table = Table->Next)
      KBTS_MEMCPY(KBTS__POINTER_OFFSET(void, Result, Table->Offset), Table->Data, Table->Size);
    for(kbts__packed_table *Table = BucketTables[Slot]; Table; Table = Table->Next)
      KBTS_MEMCPY(KBTS__POINTER_OFFSET(void, Result, Table->Offset), Table->Data, Table->Size);
  }
  for(kbts_u32 O = 0; O < Result->OutcomeCount && FusionCount; ++O)
  {
    const kbts__compiled_outcome *Outcome = &Result->Outcomes[O];
    if(Outcome->FusionCount)
      KBTS_MEMCPY(Result->FusionGlyphs + Outcome->FusionOffset, Sources[O].Glyphs,
          Outcome->FusionCount * sizeof(kbts_u16));
  }
  return Result;
}

static kbts__compiled_contexts *kbts__CompileFontData(kbts_font *Font,
    kbts_allocator_function *Allocator, void *AllocatorData,
    kbts_allocator_function *ScratchAllocator, void *ScratchAllocatorData)
{
  /* REVIEW[COLD4 CLEANUP]: CompileContexts first materializes a contiguous raw
   * cache; packing later copies its classifier, masks, tests and outcomes into
   * another allocation while raw data and fusion workspace remain live.
   * Fusion needs a queryable temporary view, but does it need this full layout?
   * Compare peak scratch bytes and build time before changing that boundary. */
  kbts__compiled_contexts *Raw = kbts__CompileContexts(Font, ScratchAllocator, ScratchAllocatorData,
                                                      ScratchAllocator, ScratchAllocatorData);
  if(!Raw) return 0;
  kbts__context_builder Builder = KBTS__ZERO;
  Builder.Arena.Allocator = ScratchAllocator;
  Builder.Arena.AllocatorData = ScratchAllocatorData;
  kbts__compiled_contexts *Result = 0;
  kbts_lookup_list *List = kbts__GetLookupList(kbts__BlobTableDataType(Font->Blob, KBTS_BLOB_TABLE_ID_GSUB, kbts__gsub_gpos));
  kbts__fusion_source *Sources = 0;
  kbts_u32 FusionCount = 0, FusedOutcomes = 0, FusedChains = 0;
  if(List && Raw->OutcomeCount)
  {
    Sources = (kbts__fusion_source *)kbts__ContextMemory(&Builder, Raw->OutcomeCount, sizeof(*Sources), KBTS_ALIGNOF(kbts__fusion_source));
    kbts_u32 *SourceBits = (kbts_u32 *)kbts__ContextMemory(&Builder, Raw->OutcomeCount,
        (kbts_un)Raw->SymbolWordCount * sizeof(kbts_u32), KBTS_ALIGNOF(kbts_u32));
    kbts__lookup **Parents = (kbts__lookup **)kbts__ContextMemory(&Builder, Raw->ProgramCount, sizeof(*Parents), KBTS_ALIGNOF(kbts__lookup *));
    kbts_u32 *ParentFirst = (kbts_u32 *)kbts__ContextMemory(&Builder, Raw->ProgramCount, sizeof(kbts_u32), KBTS_ALIGNOF(kbts_u32));
    kbts_u32 *GlyphHeads = (kbts_u32 *)kbts__ContextMemory(&Builder, Raw->SymbolCount, sizeof(kbts_u32), KBTS_ALIGNOF(kbts_u32));
    kbts_u32 *GlyphNext = (kbts_u32 *)kbts__ContextMemory(&Builder, Raw->GlyphCount, sizeof(kbts_u32), KBTS_ALIGNOF(kbts_u32));
    kbts_u32 MaxBacktrack = 0, MaxAhead = 0;
    if(!Builder.Error)
    {
      kbts_u32 *FlatOffsets = KBTS__POINTER_OFFSET(kbts_u32, Font->Blob, Font->Blob->LookupSubtableIndexOffsetsOffsetFromStartOfFile);
      for(kbts_u32 L = 0; L < List->Count; ++L)
      {
        kbts__lookup *Lookup = kbts__GetLookup(List, L);
        if(Lookup->Flag || !Lookup->SubtableCount) continue;
        kbts_u32 First = FlatOffsets[L];
        if(First >= Raw->FlatProgramCount || Lookup->SubtableCount > Raw->FlatProgramCount - First) {Builder.Error = 2; break;}
        int Eligible = 1;
        for(kbts_u32 S = 0; S < Lookup->SubtableCount; ++S)
        {
          kbts_u32 Type;
          kbts__FusionSubtable(Lookup, S, &Type);
          if(Type != 5 && Type != 6) {Eligible = 0; break;}
        }
        if(!Eligible) continue;
        for(kbts_u32 S = 0; S < Lookup->SubtableCount; ++S)
        {
          const kbts__compiled_program *Program = kbts__ProgramForSubtable(Raw, First + S);
          kbts_un Dense = (kbts_un)(Program - Raw->Programs);
          Parents[Dense] = Lookup;
          ParentFirst[Dense] = First;
          if(Program->FirstFiltered) continue;
          if(MaxBacktrack < Program->MaxBacktrack) MaxBacktrack = Program->MaxBacktrack;
          if(MaxAhead < Program->MaxAhead) MaxAhead = Program->MaxAhead;
        }
      }
      KBTS_MEMSET(GlyphHeads, 0xff, Raw->SymbolCount * sizeof(kbts_u32));
      for(kbts_u32 G = 0; G < Raw->GlyphCount; ++G)
      {
        kbts_u32 Symbol = kbts__CompiledGlyphSymbol(Raw, G);
        GlyphNext[G] = GlyphHeads[Symbol];
        GlyphHeads[Symbol] = G;
      }
      for(kbts_u32 O = 0; O < Raw->OutcomeCount; ++O)
        Sources[O].Symbols = SourceBits + (kbts_un)O * Raw->SymbolWordCount;
      for(kbts_u32 P = 0; P < Raw->ProgramCount; ++P)
      {
        kbts__compiled_program *Program = &Raw->Programs[P];
        for(kbts_u32 C = 0; C < Program->CandidateCount; ++C)
        {
          kbts_u32 O = kbts__CompiledOutcomeIndex(Program, C);
          if(O == (kbts_u32)-1) continue;
          if(O >= Raw->OutcomeCount) {Builder.Error = 2; break;}
          kbts__fusion_source *Source = &Sources[O];
          if(!Source->Seen)
          {
            Source->Seen = 1;
            Source->Program = P;
            Source->Candidate = C;
          }
          else if(Source->Program != P) Source->Ambiguous = 1;
          else if(!Source->Ambiguous)
          {
            for(kbts_u32 T = 0; T < Program->StageCount; ++T)
            {
              kbts__compiled_wide_test A = kbts__FusionTest(Raw, Program, C, T);
              kbts__compiled_wide_test B = kbts__FusionTest(Raw, Program, Source->Candidate, T);
              if(A.Probe != B.Probe || A.Predicate != B.Predicate) {Source->Ambiguous = 1; break;}
            }
          }
        }
        if(Builder.Error) break;
        if(!Program->CandidateCount) continue;
        for(kbts_u32 Symbol = 0; Symbol < Raw->SymbolCount; ++Symbol)
        {
          kbts__compiled_bucket Bucket = Program->Buckets[kbts__CompiledBucketIndex(Program, Symbol)];
          for(kbts_u32 C = Bucket.First; C < Bucket.First + Bucket.Count; ++C)
          {
            kbts_u32 O = kbts__CompiledOutcomeIndex(Program, C);
            if(O != (kbts_u32)-1) Sources[O].Symbols[Symbol >> 5] |= 1u << (Symbol & 31);
          }
        }
      }
    }
    kbts_u32 *DomainBits = (kbts_u32 *)kbts__ContextMemory(&Builder, (kbts_un)MaxBacktrack + MaxAhead + 1,
        (kbts_un)Raw->SymbolWordCount * sizeof(kbts_u32), KBTS_ALIGNOF(kbts_u32));
    kbts_u16 *Outputs = (kbts_u16 *)kbts__ContextMemory(&Builder, (kbts_un)MaxAhead + 1, sizeof(kbts_u16), KBTS_ALIGNOF(kbts_u16));
    for(kbts_u32 O = 0; O < Raw->OutcomeCount && !Builder.Error; ++O)
    {
      kbts__fusion_source *Source = &Sources[O];
      if(!Source->Seen || Source->Ambiguous) continue;
      kbts__compiled_program *Program = &Raw->Programs[Source->Program];
      kbts__lookup *Parent = Parents[Source->Program];
      kbts__compiled_outcome *Outcome = &Raw->Outcomes[O];
      if(!Parent || Program->FirstFiltered || Outcome->InputCount != 1 || Outcome->RecordCount != 1) continue;
      kbts__fusion_domains Domains = KBTS__ZERO;
      Domains.Bits = DomainBits;
      for(kbts_u32 T = 0; T < Program->StageCount; ++T)
      {
        kbts__compiled_wide_test Test = kbts__FusionTest(Raw, Program, Source->Candidate, T);
        if(!Test.Predicate) continue;
        if(Domains.First > Test.Probe) Domains.First = Test.Probe;
        if(Domains.Last < Test.Probe) Domains.Last = Test.Probe;
      }
      kbts_un Positions = (kbts_un)(Domains.Last - Domains.First) + 1;
      KBTS_MEMSET(DomainBits, 0xff, Positions * Raw->SymbolWordCount * sizeof(kbts_u32));
      if(Raw->SymbolCount & 31)
        for(kbts_un P = 0; P < Positions; ++P)
          DomainBits[(P + 1) * Raw->SymbolWordCount - 1] &= (1u << (Raw->SymbolCount & 31)) - 1;
      KBTS_MEMCPY(kbts__FusionDomain(Raw, &Domains, 0), Source->Symbols, Raw->SymbolWordCount * sizeof(kbts_u32));
      for(kbts_u32 T = 0; T < Program->StageCount; ++T)
      {
        kbts__compiled_wide_test Test = kbts__FusionTest(Raw, Program, Source->Candidate, T);
        if(!Test.Predicate) continue;
        kbts_u32 *Domain = kbts__FusionDomain(Raw, &Domains, Test.Probe);
        const kbts_u32 *Mask = Raw->PredicateMasks + (kbts_un)Test.Predicate * Raw->SymbolWordCount;
        for(kbts_u32 W = 0; W < Raw->SymbolWordCount; ++W) Domain[W] &= Mask[W];
      }
      kbts_u32 Count = 0, Selected = O;
      for(kbts_s32 Position = 0; Position <= Domains.Last; ++Position)
      {
        kbts_u32 *Domain = kbts__FusionDomain(Raw, &Domains, Position);
        kbts_u16 Replacement = 0;
        if(!kbts__FusionConstant(Raw, List, &Raw->Outcomes[Selected], Domain, GlyphHeads, GlyphNext, &Replacement)) break;
        Outputs[Count++] = Replacement;
        KBTS_MEMSET(Domain, 0, Raw->SymbolWordCount * sizeof(kbts_u32));
        kbts_u32 Symbol = kbts__CompiledGlyphSymbol(Raw, Replacement);
        Domain[Symbol >> 5] = 1u << (Symbol & 31);
        if(Position == Domains.Last) break;
        Selected = kbts__FusionWinner(Raw, &Domains, Position + 1, ParentFirst[Source->Program], Parent->SubtableCount);
        if(Selected == (kbts_u32)-1 || Sources[Selected].Ambiguous) break;
      }
      if(!Count) continue;
      if(Count > (kbts_u32)-1 - FusionCount) {Builder.Error = 1; break;}
      Source->Glyphs = (kbts_u16 *)kbts__ContextMemory(&Builder, Count, sizeof(kbts_u16), KBTS_ALIGNOF(kbts_u16));
      if(Builder.Error) break;
      KBTS_MEMCPY(Source->Glyphs, Outputs, Count * sizeof(kbts_u16));
      Outcome->FusionOffset = FusionCount;
      Outcome->FusionCount = Count;
      FusionCount += Count;
      ++FusedOutcomes;
      if(Count > 1) ++FusedChains;
    }
  }

  if(!Builder.Error)
    Result = kbts__PackCompiledContexts(&Builder, Raw, Sources, Allocator, AllocatorData,
        FusionCount, FusedOutcomes, FusedChains);
  if(Raw) kbts__AllocatorFree(Raw->Allocator, Raw->AllocatorData, Raw->BaseAllocation);
  kbts__FreeArena(&Builder.Arena);
  if(!Result) Font->Error = Builder.Error == 2 ? KBTS_LOAD_FONT_ERROR_INVALID_FONT : KBTS_LOAD_FONT_ERROR_OUT_OF_MEMORY;
  return Result;
}
