#ifndef KB_GSUB_STREAM_H
#define KB_GSUB_STREAM_H

/* Native actions and compiled contextual actions share canonical live glyphs.
 * Snapshot classifications retain the plan's dependency proofs, not a mirror. */
enum
{
  KBTS__GSUB_NATIVE,
  KBTS__GSUB_SNAPSHOT,
  KBTS__GSUB_ORDERED,
};

typedef struct kbts__gsub_node
{
  kbts_u32 FirstSubtable;
  kbts_u32 SubtableCount;
  kbts_u32 Stage;
  kbts_u32 Kind;
  kbts_u32 NativeIndex;
} kbts__gsub_node;

typedef struct kbts__gsub_stage
{
  kbts_u32 FirstNode;
  kbts_u32 NodeCount;
  kbts_u32 FirstLookup;
  kbts_u32 OnePastLastLookup;
} kbts__gsub_stage;

typedef struct kbts__gsub_plan
{
  kbts_u32 LookupCount;
  kbts_u32 GsubLookupCount;
  kbts_u32 StageCount;
  kbts_u32 NativeCount;
  kbts_u32 SymbolCount;
  kbts_u32 SymbolWordCount;
  kbts__gsub_node *Nodes;
  kbts__gsub_stage *Stages;
  kbts_u32 *Order;
  kbts_u32 *FirstSymbols;
  kbts_u32 *RequiredFlags;
  /* Cold glyph-to-stage aggregation. Possible includes feature overrides;
   * live lookup checks remain authoritative for flags and feature values. */
  kbts_un AdmissionStride;
  kbts_u64 *DefaultStages;
  kbts_u64 *PossibleStages;
  kbts_u64 *NativeStages;
} kbts__gsub_plan;


typedef struct kbts__gsub_stream
{
  void *Memory;
  kbts_un Capacity;
  kbts_un SymbolStride;
  /* One bit per stage; native/symbol rows use absolute physical glyph slots.
   * Stage/native admission is conservative; symbol positions remain exact. */
  kbts_u64 *StageCandidates;
  kbts_u64 *NativeCandidates;
  kbts_u64 *SymbolCandidates;
  kbts_u32 *PresentSymbols;
  /* Stable slot insert/remove hooks maintain indexes throughout execution. */
  kbts_u32 FirstLookup, LastLookup, MinimumStage;
  kbts_b32 Indexing;
  kbts_b32 IndexNative, IndexSymbols;
  kbts_b32 HasIgnorables;
} kbts__gsub_stream;

#endif
