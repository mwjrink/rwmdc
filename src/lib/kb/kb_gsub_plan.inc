/* Included before shape-config placement. All proof workspace belongs to the
 * caller's bump allocation; the resident plan only refers to existing programs. */
static kbts_b32 kbts__GsubPlanPush(kbts__pointer_bump_allocator *Bump,
    kbts_un Count, kbts_un Size, kbts_un Align, void **Result)
{
  kbts_uptr Limit = (kbts_uptr)-1;
  if(!Size || !Align || (Align & (Align - 1)) || Bump->At > Limit - (Align - 1)) return 0;
  kbts_uptr At = (Bump->At + Align - 1) & ~(Align - 1);
  if(Count > (Limit - At) / Size) return 0;
  *Result = (void *)At;
  Bump->At = At + Count * Size;
  return 1;
}

static kbts__compiled_wide_test kbts__GsubPlanTest(const kbts__compiled_program *Program,
    kbts_u32 Candidate, kbts_u32 Stage)
{
  kbts_un Index = (kbts_un)Stage * Program->CandidateCount + Candidate;
  kbts__compiled_wide_test Result;
  if(Program->Wide) Result = ((const kbts__compiled_wide_test *)Program->Tests)[Index];
  else
  {
    const kbts__compiled_test *Test = (const kbts__compiled_test *)Program->Tests + Index;
    Result.Probe = Test->Probe;
    Result.Predicate = Test->Predicate;
  }
  return Result;
}

static kbts_b32 kbts__GsubPlanLowerable(const kbts__compiled_contexts *Cache,
    kbts__lookup *Lookup, const kbts__gsub_node *Node)
{
  if(Lookup->Flag) return 0;
  for(kbts_u32 S = 0; S < Node->SubtableCount; ++S)
  {
    kbts_u32 Type = Lookup->Type;
    const kbts_u16 *Offsets = KBTS__POINTER_AFTER(kbts_u16, Lookup);
    kbts_u16 *Base = KBTS__POINTER_OFFSET(kbts_u16, Lookup, Offsets[S]);
    while(Type == 7)
    {
      kbts__extension *Extension = (kbts__extension *)Base;
      Type = Extension->LookupType;
      Base = KBTS__POINTER_OFFSET(kbts_u16, Extension, kbts__ReadU32Unaligned(&Extension->Offset));
    }
    if(Type != 5 && Type != 6) return 0;
    const kbts__compiled_program *Program = kbts__ProgramForSubtable(Cache, Node->FirstSubtable + S);
    if(Program->FirstFiltered) return 0;
    for(kbts_u32 C = 0; C < Program->CandidateCount; ++C)
    {
      kbts_u32 O = kbts__CompiledOutcomeIndex(Program, C);
      if(O == (kbts_u32)-1) continue;
      if(O >= Cache->OutcomeCount) return 0;
      const kbts__compiled_outcome *Outcome = &Cache->Outcomes[O];
      if(!Outcome->FusionCount)
      {
        if(Outcome->RecordCount) return 0;
      }
      else
      {
        /* Fusion is a prefix of repetitions of one single-anchor action, not
         * permission to lower arbitrary contextual action records. */
        if(Outcome->InputCount != 1 || Outcome->RecordCount != 1 ||
           Outcome->Records[0].SequenceIndex || !Cache->FusionGlyphs ||
           Outcome->FusionOffset > Cache->FusionBytes / sizeof(kbts_u16) ||
           Outcome->FusionCount > Cache->FusionBytes / sizeof(kbts_u16) - Outcome->FusionOffset) return 0;
      }
    }
  }
  return 1;
}

static void kbts__GsubPlanSetSymbol(kbts_u32 *Bits, kbts_u32 Symbol)
{
  Bits[Symbol >> 5] |= 1u << (Symbol & 31);
}

/* A later anchor can only see this lookup's earlier writes in backtrack.
 * Compare predicate membership, not merely overlapping symbol domains: changing
 * a glyph inside the same backtrack class is safe for a snapshot. */
static kbts_b32 kbts__GsubPlanChangesBacktrack(const kbts__compiled_contexts *Cache,
    const kbts__gsub_node *Node, const kbts__compiled_program *Reader,
    const kbts_u32 *BacktrackPredicates, kbts_un PredicateWords)
{
  kbts_b32 HasBacktrack = 0;
  kbts_un UsedWords = Reader->LocalMasks ? 1 : PredicateWords;
  for(kbts_un W = 0; W < UsedWords; ++W) HasBacktrack |= BacktrackPredicates[W] != 0;
  if(!HasBacktrack) return 0;
  for(kbts_u32 S = 0; S < Node->SubtableCount; ++S)
  {
    const kbts__compiled_program *Writer = kbts__ProgramForSubtable(Cache, Node->FirstSubtable + S);
    if(!Writer->CandidateCount) continue;
    for(kbts_u32 Symbol = 0; Symbol < Cache->SymbolCount; ++Symbol)
    {
      kbts__compiled_bucket Bucket = Writer->Buckets[kbts__CompiledBucketIndex(Writer, Symbol)];
      for(kbts_u32 C = Bucket.First; C < Bucket.First + Bucket.Count; ++C)
      {
        kbts_u32 O = kbts__CompiledOutcomeIndex(Writer, C);
        if(O == (kbts_u32)-1) continue;
        const kbts__compiled_outcome *Outcome = &Cache->Outcomes[O];
        if(!Outcome->FusionCount) continue;
        kbts_u32 Replacement = kbts__CompiledGlyphSymbol(Cache, Cache->FusionGlyphs[Outcome->FusionOffset]);
        if(Symbol == Replacement) continue;
        /* Dispatch proves the first predicate. Any remaining anchor tests
         * further restrict the possible source symbol of this action. */
        kbts_b32 Possible = 1;
        for(kbts_u32 T = 0; T < Writer->StageCount; ++T)
        {
          kbts__compiled_wide_test Test = kbts__GsubPlanTest(Writer, C, T);
          if(Test.Probe || !Test.Predicate) continue;
          kbts_u32 Member = Writer->LocalMasks ?
              (kbts__CompiledLocalMask(Writer, Symbol) >> Test.Predicate) & 1u :
              (Cache->PredicateMasks[(kbts_un)Test.Predicate * Cache->SymbolWordCount + (Symbol >> 5)] >> (Symbol & 31)) & 1u;
          if(!Member) {Possible = 0; break;}
        }
        if(!Possible) continue;
        if(Reader->LocalMasks)
        {
          if((kbts__CompiledLocalMask(Reader, Symbol) ^ kbts__CompiledLocalMask(Reader, Replacement)) & BacktrackPredicates[0]) return 1;
        }
        else
        {
          for(kbts_un W = 0; W < UsedWords; ++W)
          {
            kbts_u32 Predicates = BacktrackPredicates[W];
            while(Predicates)
            {
              kbts_u32 P = (kbts_u32)(W * 32) + kbts__LsbPositionOrBitWidth32(Predicates);
              const kbts_u32 *Mask = Cache->PredicateMasks + (kbts_un)P * Cache->SymbolWordCount;
              if(((Mask[Symbol >> 5] >> (Symbol & 31)) ^ (Mask[Replacement >> 5] >> (Replacement & 31))) & 1u) return 1;
              Predicates &= Predicates - 1;
            }
          }
        }
      }
    }
  }
  return 0;
}

static void kbts__GsubPlanDomains(const kbts__compiled_contexts *Cache, kbts__gsub_node *Node,
    kbts_u32 *Read, kbts_u32 *Write, kbts_u32 *Predicates, kbts_u32 *BacktrackPredicates,
    kbts_un PredicateWords)
{
  for(kbts_u32 S = 0; S < Node->SubtableCount; ++S)
  {
    const kbts__compiled_program *Program = kbts__ProgramForSubtable(Cache, Node->FirstSubtable + S);
    if(!Program->CandidateCount) continue;
    kbts_un UsedWords = Program->LocalMasks ? 1 : PredicateWords;
    KBTS_MEMSET(Predicates, 0, UsedWords * sizeof(kbts_u32));
    KBTS_MEMSET(BacktrackPredicates, 0, UsedWords * sizeof(kbts_u32));
    for(kbts_u32 C = 0; C < Program->CandidateCount; ++C)
    {
      kbts_u32 O = kbts__CompiledOutcomeIndex(Program, C);
      if(O == (kbts_u32)-1) continue;
      const kbts__compiled_outcome *Outcome = &Cache->Outcomes[O];
      for(kbts_u32 F = 0; F < Outcome->FusionCount; ++F)
        kbts__GsubPlanSetSymbol(Write, kbts__CompiledGlyphSymbol(Cache, Cache->FusionGlyphs[Outcome->FusionOffset + F]));
      for(kbts_u32 T = 0; T < Program->StageCount; ++T)
      {
        kbts__compiled_wide_test Test = kbts__GsubPlanTest(Program, C, T);
        if(!Test.Predicate) continue; /* Tautological padding reads no symbol. */
        kbts__GsubPlanSetSymbol(Predicates, Test.Predicate);
        if(Test.Probe < 0) kbts__GsubPlanSetSymbol(BacktrackPredicates, Test.Predicate);
      }
    }
    for(kbts_u32 Symbol = 0; Symbol < Cache->SymbolCount; ++Symbol)
    {
      kbts__compiled_bucket Bucket = Program->Buckets[kbts__CompiledBucketIndex(Program, Symbol)];
      if(Bucket.Count) kbts__GsubPlanSetSymbol(Read, Symbol);
      for(kbts_u32 C = Bucket.First; C < Bucket.First + Bucket.Count; ++C)
      {
        kbts_u32 O = kbts__CompiledOutcomeIndex(Program, C);
        if(O != (kbts_u32)-1 && Cache->Outcomes[O].FusionCount)
        {
          kbts__GsubPlanSetSymbol(Write, Symbol);
          break;
        }
      }
      if(Program->LocalMasks && (kbts__CompiledLocalMask(Program, Symbol) & Predicates[0]))
        kbts__GsubPlanSetSymbol(Read, Symbol);
    }
    if(!Program->LocalMasks)
    {
      for(kbts_un W = 0; W < UsedWords; ++W)
      {
        kbts_u32 Remaining = Predicates[W];
        while(Remaining)
        {
          kbts_u32 P = (kbts_u32)(W * 32) + kbts__LsbPositionOrBitWidth32(Remaining);
          const kbts_u32 *Mask = Cache->PredicateMasks + (kbts_un)P * Cache->SymbolWordCount;
          for(kbts_u32 B = 0; B < Cache->SymbolWordCount; ++B) Read[B] |= Mask[B];
          Remaining &= Remaining - 1;
        }
      }
    }
    if(Node->Kind == KBTS__GSUB_SNAPSHOT &&
       kbts__GsubPlanChangesBacktrack(Cache, Node, Program, BacktrackPredicates, PredicateWords))
      Node->Kind = KBTS__GSUB_ORDERED;
  }
}

static kbts_b32 kbts__PlaceGsubPlan(kbts__pointer_bump_allocator *Bump,
    const kbts_shape_config *Config, kbts_un LookupCount, kbts_un GsubLookupCount,
    void *Memory, kbts__gsub_plan **Result)
{
  *Result = 0;
  if(!Config || !Config->Font || !Config->Font->Blob || !Config->Font->CompiledContexts ||
     LookupCount > (kbts_u32)-1 || GsubLookupCount > LookupCount) return 0;
  const kbts__compiled_contexts *Cache = Config->Font->CompiledContexts;
  if(!Cache->SymbolCount || Cache->SymbolWordCount != (Cache->SymbolCount - 1) / 32 + 1) return 0;
  kbts_un Words = Cache->SymbolWordCount;
  kbts_un PredicateWords = Cache->PredicateCount ? ((kbts_un)Cache->PredicateCount - 1) / 32 + 1 : 1;
  if(GsubLookupCount > (kbts_un)-1 / Words) return 0;
  kbts_un DomainWords = GsubLookupCount * Words;
  kbts_un AdmissionStride = GsubLookupCount / 64 + !!(GsubLookupCount & 63);
  if(AdmissionStride && Config->GlyphCount > (kbts_un)-1 / AdmissionStride) return 0;
  kbts_un AdmissionWords = (kbts_un)Config->GlyphCount * AdmissionStride;
  kbts__pointer_bump_allocator Placement = *Bump;
  /* REVIEW[COLD5 CLEANUP]: Parts[5..7] are proof-only write/predicate workspace,
   * yet Placement retains them in the config allocation after planning. The
   * lookup-count admission upper bound also survives the stage-stride shrink.
   * Can these share a temporary tail with final admission storage, preserving
   * the sizing API's peak requirement but reusing dead proof bytes? */
  void *Parts[12];
  if(!kbts__GsubPlanPush(&Placement, 1, sizeof(kbts__gsub_plan), KBTS_ALIGNOF(kbts__gsub_plan), &Parts[0]) ||
     !kbts__GsubPlanPush(&Placement, GsubLookupCount, sizeof(kbts__gsub_node), KBTS_ALIGNOF(kbts__gsub_node), &Parts[1]) ||
     !kbts__GsubPlanPush(&Placement, GsubLookupCount, sizeof(kbts__gsub_stage), KBTS_ALIGNOF(kbts__gsub_stage), &Parts[2]) ||
     !kbts__GsubPlanPush(&Placement, GsubLookupCount, sizeof(kbts_u32), KBTS_ALIGNOF(kbts_u32), &Parts[3]) ||
     !kbts__GsubPlanPush(&Placement, DomainWords, sizeof(kbts_u32), KBTS_ALIGNOF(kbts_u32), &Parts[4]) ||
     !kbts__GsubPlanPush(&Placement, DomainWords, sizeof(kbts_u32), KBTS_ALIGNOF(kbts_u32), &Parts[5]) ||
     !kbts__GsubPlanPush(&Placement, GsubLookupCount ? PredicateWords : 0, sizeof(kbts_u32), KBTS_ALIGNOF(kbts_u32), &Parts[6]) ||
     !kbts__GsubPlanPush(&Placement, GsubLookupCount ? PredicateWords : 0, sizeof(kbts_u32), KBTS_ALIGNOF(kbts_u32), &Parts[7]) ||
     !kbts__GsubPlanPush(&Placement, GsubLookupCount, sizeof(kbts_u32), KBTS_ALIGNOF(kbts_u32), &Parts[8]) ||
     !kbts__GsubPlanPush(&Placement, AdmissionWords, sizeof(kbts_u64), KBTS_ALIGNOF(kbts_u64), &Parts[9]) ||
     !kbts__GsubPlanPush(&Placement, AdmissionWords, sizeof(kbts_u64), KBTS_ALIGNOF(kbts_u64), &Parts[10]) ||
     !kbts__GsubPlanPush(&Placement, AdmissionStride, sizeof(kbts_u64), KBTS_ALIGNOF(kbts_u64), &Parts[11])) return 0;
  /* The public sizing API returns int. Reserve its caller-alignment slack too. */
  kbts_uptr Base = (kbts_uptr)Memory;
  if(Placement.At < Base || Placement.At - Base > 0x7fffffffu - KBTS_ALIGNOF(kbts_shape_config)) return 0;
  if(!Memory)
  {
    /* Sequential arrays and feature boundaries are virtual in sizing mode.
     * Reserve the same worst-case nodes, levels, and proof workspace only. */
    *Bump = Placement;
    return 1;
  }
  kbts__gsub_plan *Plan = (kbts__gsub_plan *)Parts[0];
  KBTS_MEMSET(Plan, 0, sizeof(*Plan));
  Plan->LookupCount = (kbts_u32)LookupCount;
  Plan->GsubLookupCount = (kbts_u32)GsubLookupCount;
  Plan->Nodes = (kbts__gsub_node *)Parts[1];
  Plan->Stages = (kbts__gsub_stage *)Parts[2];
  Plan->Order = (kbts_u32 *)Parts[3];
  Plan->SymbolCount = Cache->SymbolCount;
  Plan->SymbolWordCount = Cache->SymbolWordCount;
  Plan->FirstSymbols = (kbts_u32 *)Parts[4];
  Plan->RequiredFlags = (kbts_u32 *)Parts[8];
  Plan->DefaultStages = (kbts_u64 *)Parts[9];
  Plan->PossibleStages = (kbts_u64 *)Parts[10];
  Plan->NativeStages = (kbts_u64 *)Parts[11];
  if(GsubLookupCount)
  {
    if(!Config->SequentialLookups || !Config->FeatureStageFirstLookupIndices || !Config->OpList.FeatureStageCount) return 0;
    if(Config->FeatureStageFirstLookupIndices[0] ||
       Config->FeatureStageFirstLookupIndices[Config->OpList.FeatureStageCount] != LookupCount ||
       Config->FeatureStageFirstLookupIndices[Config->OpList.FeatureStageCount - 1] != GsubLookupCount) return 0;
    for(kbts_un F = 0; F < Config->OpList.FeatureStageCount; ++F)
      if(Config->FeatureStageFirstLookupIndices[F] > Config->FeatureStageFirstLookupIndices[F + 1]) return 0;
    kbts_lookup_list *List = kbts__GetLookupList(kbts__BlobTableDataType(Config->Font->Blob, KBTS_BLOB_TABLE_ID_GSUB, kbts__gsub_gpos));
    if(!List) return 0;
    const kbts_u32 *FlatOffsets = KBTS__POINTER_OFFSET(kbts_u32, Config->Font->Blob,
        Config->Font->Blob->LookupSubtableIndexOffsetsOffsetFromStartOfFile);
    kbts_u32 *Reads = (kbts_u32 *)Parts[4], *Writes = (kbts_u32 *)Parts[5];
    KBTS_MEMSET(Reads, 0, DomainWords * sizeof(kbts_u32));
    KBTS_MEMSET(Writes, 0, DomainWords * sizeof(kbts_u32));
    kbts_u32 Feature = 0, SegmentFirst = 0, Floor = 0;
    kbts_u32 FilterMask = Config->Shaper == KBTS_SHAPER_USE ? KBTS__USE_GLYPH_FEATURE_MASK : KBTS__GLYPH_FEATURE_MASK;
    for(kbts_u32 I = 0; I < Plan->GsubLookupCount; ++I)
    {
      while(Config->FeatureStageFirstLookupIndices[Feature + 1] <= I)
      {
        ++Feature;
        SegmentFirst = I;
        Floor = Plan->StageCount;
      }
      kbts_u32 Required = Config->SequentialLookups[I].GlyphFilter & FilterMask;
      Plan->RequiredFlags[I] = Required;
      kbts_u32 LookupIndex = Config->SequentialLookups[I].LookupIndex;
      if(LookupIndex >= List->Count) return 0;
      kbts__lookup *Lookup = kbts__GetLookup(List, LookupIndex);
      kbts__gsub_node *Node = &Plan->Nodes[I];
      Node->FirstSubtable = FlatOffsets[LookupIndex];
      Node->SubtableCount = Lookup->SubtableCount;
      if(Node->FirstSubtable > Cache->FlatProgramCount || Node->SubtableCount > Cache->FlatProgramCount - Node->FirstSubtable) return 0;
      Node->Kind = kbts__GsubPlanLowerable(Cache, Lookup, Node) ? KBTS__GSUB_SNAPSHOT : KBTS__GSUB_NATIVE;
      kbts_u32 *Read = Reads + (kbts_un)I * Words, *Write = Writes + (kbts_un)I * Words;
      if(Node->Kind == KBTS__GSUB_SNAPSHOT)
        kbts__GsubPlanDomains(Cache, Node, Read, Write, (kbts_u32 *)Parts[6], (kbts_u32 *)Parts[7], PredicateWords);
      // Also a native-prefix count on constant nodes, for interval admission.
      Node->NativeIndex = Plan->NativeCount;
      Plan->NativeCount += Node->Kind == KBTS__GSUB_NATIVE;
      if(Node->Kind != KBTS__GSUB_SNAPSHOT)
      {
        Node->Stage = Plan->StageCount++;
        SegmentFirst = I + 1;
        Floor = Plan->StageCount;
      }
      else
      {
        kbts_u32 Level = Floor;
        for(kbts_u32 J = SegmentFirst; J < I; ++J)
        {
          const kbts_u32 *PriorRead = Reads + (kbts_un)J * Words, *PriorWrite = Writes + (kbts_un)J * Words;
          kbts_b32 Depends = 0;
          for(kbts_un W = 0; W < Words; ++W)
          {
            /* RAW enforces forward observations. WAR also prevents a later
             * writer from moving ahead of an earlier, delayed DAG level. */
            if((PriorWrite[W] & Read[W]) || (Write[W] & PriorRead[W]) || (Write[W] & PriorWrite[W]))
            {Depends = 1; break;}
          }
          if(Depends && Level <= Plan->Nodes[J].Stage) Level = Plan->Nodes[J].Stage + 1;
        }
        Node->Stage = Level;
        if(Plan->StageCount <= Level) Plan->StageCount = Level + 1;
      }
    }
    KBTS_MEMSET(Plan->Stages, 0, Plan->StageCount * sizeof(kbts__gsub_stage));
    for(kbts_u32 I = 0; I < Plan->GsubLookupCount; ++I)
      ++Plan->Stages[Plan->Nodes[I].Stage].NodeCount;
    kbts_u32 First = 0;
    for(kbts_u32 S = 0; S < Plan->StageCount; ++S)
    {
      kbts__gsub_stage *Stage = &Plan->Stages[S];
      Stage->FirstNode = First;
      First += Stage->NodeCount;
      Stage->NodeCount = 0;
      Stage->FirstLookup = Plan->GsubLookupCount;
    }
    /* Counting placement preserves original lookup order within each level. */
    for(kbts_u32 I = 0; I < Plan->GsubLookupCount; ++I)
    {
      kbts__gsub_stage *Stage = &Plan->Stages[Plan->Nodes[I].Stage];
      Plan->Order[Stage->FirstNode + Stage->NodeCount++] = I;
      if(Stage->FirstLookup > I) Stage->FirstLookup = I;
      Stage->OnePastLastLookup = I + 1;
    }
    /* Constant actions can forward writes through a whole topologically ordered
     * chain. Dependency levels constrain execution order, not mandatory buffer
     * copies. Native actions and baked feature boundaries still flush the view. */
    kbts_u32 StageCount = 0, FeatureIndex = 0;
    kbts_b32 PreviousNative = 1;
    for(kbts_u32 S = 0; S < Plan->StageCount; ++S)
    {
      kbts__gsub_stage Stage = Plan->Stages[S];
      kbts_b32 NewFeature = 0;
      while(Config->FeatureStageFirstLookupIndices[FeatureIndex + 1] <= Stage.FirstLookup)
      {
        ++FeatureIndex;
        NewFeature = 1;
      }
      kbts_b32 Native = Plan->Nodes[Plan->Order[Stage.FirstNode]].Kind == KBTS__GSUB_NATIVE;
      if(!StageCount || Native || PreviousNative || NewFeature)
        Plan->Stages[StageCount++] = Stage;
      else
      {
        kbts__gsub_stage *Chain = &Plan->Stages[StageCount - 1];
        Chain->NodeCount += Stage.NodeCount;
        Chain->FirstLookup = KBTS__MIN(Chain->FirstLookup, Stage.FirstLookup);
        Chain->OnePastLastLookup = KBTS__MAX(Chain->OnePastLastLookup, Stage.OnePastLastLookup);
      }
      for(kbts_u32 N = 0; N < Stage.NodeCount; ++N)
        Plan->Nodes[Plan->Order[Stage.FirstNode + N]].Stage = StageCount - 1;
      PreviousNative = Native;
    }
    Plan->StageCount = StageCount;
    /* Sizing reserves the lookup-count upper bound, but only actual merged
     * stage words belong in the hot rows. Do not clear or traverse slack. */
    AdmissionStride = StageCount / 64 + !!(StageCount & 63);
    AdmissionWords = (kbts_un)Config->GlyphCount * AdmissionStride;
    Plan->AdmissionStride = AdmissionStride;
    Plan->PossibleStages = Plan->DefaultStages + AdmissionWords;
    Plan->NativeStages = Plan->PossibleStages + AdmissionWords;
    KBTS_MEMSET(Plan->DefaultStages, 0, AdmissionWords * sizeof(kbts_u64));
    KBTS_MEMSET(Plan->PossibleStages, 0, AdmissionWords * sizeof(kbts_u64));
    KBTS_MEMSET(Plan->NativeStages, 0, AdmissionStride * sizeof(kbts_u64));
    /* REVIEW[COLD6 PERF]: Both input matrices and output admission rows are
     * glyph-major, but this lookup-major traversal revisits one bit in every
     * glyph row for each lookup. The cold aggregation is useful at runtime;
     * compare a glyph-major or tiled build for config time and cache misses
     * rather than moving this work back into live admission. */
    for(kbts_u32 I = 0; I < Plan->GsubLookupCount; ++I)
    {
      const kbts__gsub_node *Node = &Plan->Nodes[I];
      kbts_u64 Bit = (kbts_u64)1 << (Node->Stage & 63);
      if(Node->Kind == KBTS__GSUB_NATIVE) Plan->NativeStages[Node->Stage / 64] |= Bit;
      for(kbts_u32 G = 0; G < Config->GlyphCount; ++G)
      {
        kbts__matrix_index Index = kbts__IdSequentialLookupMatrixIndex(I, G, LookupCount);
        kbts_un At = (kbts_un)G * AdmissionStride + Node->Stage / 64;
        kbts_u32 LookupBit = 1u << Index.BitIndex;
        if(Config->IdSequentialLookupMatrix[Index.WordIndex] & LookupBit) Plan->DefaultStages[At] |= Bit;
        if(Config->IdAllLookupMatrix[Index.WordIndex] & LookupBit) Plan->PossibleStages[At] |= Bit;
      }
    }
    /* Reuse the consumed read-domain workspace for anchor dispatch. Runtime
     * candidates are indexed by shared symbols, not by font lookup queues. */
    KBTS_MEMSET(Plan->FirstSymbols, 0, DomainWords * sizeof(kbts_u32));
    for(kbts_u32 I = 0; I < Plan->GsubLookupCount; ++I)
    {
      const kbts__gsub_node *Node = &Plan->Nodes[I];
      if(Node->Kind == KBTS__GSUB_NATIVE) continue;
      kbts_u32 *FirstSymbols = Plan->FirstSymbols + (kbts_un)I * Words;
      for(kbts_u32 S = 0; S < Node->SubtableCount; ++S)
      {
        const kbts__compiled_program *Program = kbts__ProgramForSubtable(Cache, Node->FirstSubtable + S);
        if(!Program->CandidateCount) continue;
        for(kbts_u32 Symbol = 0; Symbol < Cache->SymbolCount; ++Symbol)
          if(Program->Buckets[kbts__CompiledBucketIndex(Program, Symbol)].Count)
            kbts__GsubPlanSetSymbol(FirstSymbols, Symbol);
      }
    }
  }
  *Bump = Placement;
  *Result = Plan;
  return 1;
}
