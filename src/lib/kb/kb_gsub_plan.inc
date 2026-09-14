/* Included before shape-config placement. Shared font summaries own immutable
 * proof facts; configuration storage owns selected stages and admission rows. */
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

KBTS_INLINE void kbts__GsubPlanAdmit(kbts__gsub_plan *Plan, kbts_u32 Glyph,
    kbts_u32 Lookup, kbts_b32 Default, kbts_b32 Possible)
{
  kbts_u32 Stage = Plan->Nodes[Lookup].Stage;
  kbts_un At = (kbts_un)Glyph * Plan->AdmissionStride + Stage / 64;
  kbts_u64 Bit = (kbts_u64)1 << (Stage & 63);
  if(Default) Plan->DefaultStages[At] |= Bit;
  if(Possible) Plan->PossibleStages[At] |= Bit;
}

/* Config-dependent ordering consumes immutable font lookup summaries. No
 * candidate decoding or glyph-domain proof workspace survives font compilation. */
static kbts_b32 kbts__PlaceGsubPlan(kbts__pointer_bump_allocator *Bump,
    const kbts_shape_config *Config, kbts_un LookupCount, kbts_un GsubLookupCount,
    void *Memory, kbts__gsub_plan **Result)
{
  *Result = 0;
  if(!Config || !Config->Font || !Config->Font->Tables || !Config->Font->CompiledContexts ||
     LookupCount > (kbts_u32)-1 || GsubLookupCount > LookupCount) return 0;
  const kbts__compiled_contexts *Cache = Config->Font->CompiledContexts;
  if(!Cache->SymbolCount || Cache->SymbolWordCount != (Cache->SymbolCount - 1) / 32 + 1) return 0;
  kbts_un Words = Cache->SymbolWordCount;
  kbts__pointer_bump_allocator Placement = *Bump;
  void *Parts[8];
  if(!kbts__GsubPlanPush(&Placement, 1, sizeof(kbts__gsub_plan), KBTS_ALIGNOF(kbts__gsub_plan), &Parts[0]) ||
     !kbts__GsubPlanPush(&Placement, GsubLookupCount, sizeof(kbts__gsub_node), KBTS_ALIGNOF(kbts__gsub_node), &Parts[1]) ||
     !kbts__GsubPlanPush(&Placement, GsubLookupCount, sizeof(kbts__gsub_stage), KBTS_ALIGNOF(kbts__gsub_stage), &Parts[2]) ||
     !kbts__GsubPlanPush(&Placement, GsubLookupCount, sizeof(kbts_u32), KBTS_ALIGNOF(kbts_u32), &Parts[3]) ||
     !kbts__GsubPlanPush(&Placement, GsubLookupCount, sizeof(kbts_u32), KBTS_ALIGNOF(kbts_u32), &Parts[4])) return 0;
  kbts__gsub_plan *Plan = (kbts__gsub_plan *)Parts[0];
  kbts_un StageCount = GsubLookupCount;
  if(Memory)
  {
    KBTS_MEMSET(Plan, 0, sizeof(*Plan));
    Plan->LookupCount = (kbts_u32)LookupCount;
    Plan->GsubLookupCount = (kbts_u32)GsubLookupCount;
    Plan->Nodes = (kbts__gsub_node *)Parts[1];
    Plan->Stages = (kbts__gsub_stage *)Parts[2];
    Plan->Order = (kbts_u32 *)Parts[3];
    Plan->RequiredFlags = (kbts_u32 *)Parts[4];
    Plan->SymbolCount = Cache->SymbolCount;
    Plan->SymbolWordCount = Cache->SymbolWordCount;
  }
  if(Memory && GsubLookupCount)
  {
    if(!Config->SequentialLookups || !Config->FeatureStageFirstLookupIndices || !Config->OpList.FeatureStageCount ||
       Config->FeatureStageFirstLookupIndices[0] ||
       Config->FeatureStageFirstLookupIndices[Config->OpList.FeatureStageCount] != LookupCount ||
       Config->FeatureStageFirstLookupIndices[Config->OpList.FeatureStageCount - 1] != GsubLookupCount) return 0;
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
      Plan->RequiredFlags[I] = Config->SequentialLookups[I].GlyphFilter & FilterMask;
      kbts_u32 LookupIndex = Config->SequentialLookups[I].LookupIndex;
      if(LookupIndex >= Config->Font->Blob->GposLookupIndexOffset || LookupIndex >= Cache->LookupCount) return 0;
      const kbts__font_lookup *Lookup = kbts__FontLookup(Config->Font, KBTS_SHAPING_TABLE_GSUB, LookupIndex);
      const kbts__compiled_lookup *Summary = &Cache->LookupSummaries[LookupIndex];
      kbts__gsub_node *Node = &Plan->Nodes[I];
      Node->FirstSubtable = Lookup->FirstSubtable;
      Node->SubtableCount = Lookup->Filter.SubtableCount;
      Node->Kind = Summary->Kind;
      const kbts_u32 *Read = Summary->ReadSymbols, *Write = Summary->WriteSymbols;
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
          const kbts__compiled_lookup *Prior = &Cache->LookupSummaries[Config->SequentialLookups[J].LookupIndex];
          const kbts_u32 *PriorRead = Prior->ReadSymbols, *PriorWrite = Prior->WriteSymbols;
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
  }
  if(Memory) StageCount = Plan->StageCount;
  kbts_un AdmissionStride = StageCount / 64 + !!(StageCount & 63);
  if(AdmissionStride && Config->GlyphCount > (kbts_un)-1 / AdmissionStride) return 0;
  kbts_un AdmissionWords = (kbts_un)Config->GlyphCount * AdmissionStride;
  if(!kbts__GsubPlanPush(&Placement, AdmissionWords, sizeof(kbts_u64), KBTS_ALIGNOF(kbts_u64), &Parts[5]) ||
     !kbts__GsubPlanPush(&Placement, AdmissionWords, sizeof(kbts_u64), KBTS_ALIGNOF(kbts_u64), &Parts[6]) ||
     !kbts__GsubPlanPush(&Placement, AdmissionStride, sizeof(kbts_u64), KBTS_ALIGNOF(kbts_u64), &Parts[7])) return 0;
  if(Placement.At < (kbts_uptr)Memory || Placement.At - (kbts_uptr)Memory > 0x7fffffffu - KBTS_ALIGNOF(kbts_shape_config)) return 0;
  if(Memory)
  {
    Plan->AdmissionStride = AdmissionStride;
    Plan->DefaultStages = (kbts_u64 *)Parts[5];
    Plan->PossibleStages = (kbts_u64 *)Parts[6];
    Plan->NativeStages = (kbts_u64 *)Parts[7];
    KBTS_MEMSET(Plan->DefaultStages, 0, AdmissionWords * sizeof(kbts_u64));
    KBTS_MEMSET(Plan->PossibleStages, 0, AdmissionWords * sizeof(kbts_u64));
    KBTS_MEMSET(Plan->NativeStages, 0, AdmissionStride * sizeof(kbts_u64));
    for(kbts_u32 I = 0; I < Plan->GsubLookupCount; ++I)
    {
      const kbts__gsub_node *Node = &Plan->Nodes[I];
      if(Node->Kind == KBTS__GSUB_NATIVE)
        Plan->NativeStages[Node->Stage / 64] |= (kbts_u64)1 << (Node->Stage & 63);
    }
    *Result = Plan;
  }
  *Bump = Placement;
  return 1;
}
