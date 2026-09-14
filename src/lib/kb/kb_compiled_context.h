#ifndef KBTS_COMPILED_CONTEXT_H
#define KBTS_COMPILED_CONTEXT_H

/* Internal resident representation. The compiler owns one allocation; glyph
 * classifications and padded candidate columns are immutable during shaping. */
typedef struct kbts__compiled_test
{
  kbts_s8 Probe;
  kbts_u8 Predicate;
} kbts__compiled_test;

typedef struct kbts__compiled_wide_test
{
  kbts_s32 Probe;
  kbts_u32 Predicate;
} kbts__compiled_wide_test;

typedef struct kbts__compiled_bucket
{
  kbts_u32 First;
  kbts_u32 Count;
  kbts_u32 Backtrack;
  kbts_u32 Ahead;
} kbts__compiled_bucket;

typedef struct kbts__compiled_outcome
{
  kbts__sequence_lookup_record *Records;
  kbts_u32 RecordCount;
  kbts_u32 InputCount;
  kbts_u32 FusionOffset;
  kbts_u32 FusionCount;
} kbts__compiled_outcome;

typedef struct kbts__compiled_program
{
  void *Tests;
  void *Outcomes;
  kbts__compiled_bucket *Buckets;
  void *SymbolBuckets;
  void *LocalMasks;
  kbts_u32 CandidateCount;
  kbts_u32 StageCount;
  kbts_u32 BucketCount;
  kbts_u32 MaxAhead;
  kbts_u16 MaxBacktrack;
  kbts_u8 LocalPredicateCount;
  kbts_u8 LocalMaskWidth; /* 0: packed nibbles; otherwise 1/2/4 bytes per symbol. */
  kbts_u8 Wide;
  kbts_u8 FirstFiltered;
  kbts_u8 SymbolBucketWidth;
  kbts_u8 OutcomeWidth;
} kbts__compiled_program;

/* Font-invariant planning facts. Configurations consume these domains and
 * direct child edges without decoding the compact matcher representation. */
typedef struct kbts__compiled_lookup
{
  kbts_u32 *ReadSymbols, *WriteSymbols, *FirstSymbols;
  kbts_u32 *Children;
  kbts_u32 ChildCount, WindowCapacity, Kind, MaxInserted;
} kbts__compiled_lookup;

struct kbts__compiled_contexts
{
  kbts_allocator_function *Allocator;
  void *AllocatorData;
  void *BaseAllocation;
  kbts_u32 GlyphCount;
  kbts_u32 SymbolCount;
  kbts_u32 SymbolWordCount;
  kbts_u32 PredicateCount;
  kbts_u32 ProgramCount;
  kbts_u32 FlatProgramCount;
  kbts_u32 WindowCapacity; // Font-static matching window including gather guards.
  void *ProgramIndices;
  kbts_u8 ProgramIndexWidth;
  kbts_u32 OutcomeCount;
  kbts_u16 GlyphPages[256];
  void *GlyphSymbols;
  kbts_u8 SymbolWidth;
  kbts_u32 *PredicateMasks;
  kbts__compiled_program *Programs;
  kbts__compiled_outcome *Outcomes;
  kbts_un AllocationSize;
  kbts_un TestBytes;
  kbts_un ClassifierBytes;
  kbts_un PredicateBytes;
  kbts_un BucketBytes;
  kbts_un OutcomeBytes;
  kbts_u16 *FusionGlyphs;
  kbts_un FusionBytes;
  kbts_u32 FusedOutcomeCount;
  kbts_u32 FusedChainCount;
  kbts_un ProgramBytes;
  kbts_un HotBytes;
  kbts_u32 LookupCount;
  kbts__compiled_lookup *LookupSummaries;
  kbts_un SummaryBytes;
};

static kbts__compiled_contexts *kbts__CompileFontData(kbts_font *Font,
    kbts_allocator_function *Allocator, void *AllocatorData,
    kbts_allocator_function *ScratchAllocator, void *ScratchAllocatorData);
KBTS_INLINE kbts_u32 kbts__CompiledGlyphSymbol(const kbts__compiled_contexts *Cache, kbts_u32 GlyphId)
{
  if(GlyphId >= Cache->GlyphCount || GlyphId > 0xFFFF) return 0;
  kbts_un Index = ((kbts_un)Cache->GlyphPages[GlyphId >> 8] << 8) + (GlyphId & 255);
  return Cache->SymbolWidth == 1 ? ((const kbts_u8 *)Cache->GlyphSymbols)[Index] :
                                   ((const kbts_u16 *)Cache->GlyphSymbols)[Index];
}

KBTS_INLINE kbts_u32 kbts__CompiledIndex(const void *Values, kbts_u32 Width, kbts_un Index)
{
  if(Width == 1) return ((const kbts_u8 *)Values)[Index];
  if(Width == 2) return ((const kbts_u16 *)Values)[Index];
  return ((const kbts_u32 *)Values)[Index];
}

KBTS_INLINE void kbts__SetCompiledIndex(void *Values, kbts_u32 Width, kbts_un Index, kbts_u32 Value)
{
  if(Width == 1) ((kbts_u8 *)Values)[Index] = (kbts_u8)Value;
  else if(Width == 2) ((kbts_u16 *)Values)[Index] = (kbts_u16)Value;
  else ((kbts_u32 *)Values)[Index] = Value;
}

KBTS_INLINE const kbts__compiled_program *kbts__ProgramForSubtable(const kbts__compiled_contexts *Cache,
                                                                kbts_un FlatSubtable)
{
  KBTS_ASSERT(FlatSubtable < Cache->FlatProgramCount);
  kbts_u32 Index = kbts__CompiledIndex(Cache->ProgramIndices, Cache->ProgramIndexWidth, FlatSubtable);
  KBTS_ASSERT(Index < Cache->ProgramCount);
  return &Cache->Programs[Index];
}

KBTS_INLINE kbts_u32 kbts__CompiledLocalMask(const kbts__compiled_program *Program, kbts_u32 Symbol)
{
  if(!Program->LocalMaskWidth)
    return (((const kbts_u8 *)Program->LocalMasks)[Symbol >> 1] >> ((Symbol & 1u) * 4)) & 15u;
  return kbts__CompiledIndex(Program->LocalMasks, Program->LocalMaskWidth, Symbol);
}

KBTS_INLINE kbts_u32 kbts__CompiledBucketIndex(const kbts__compiled_program *Program, kbts_u32 Symbol)
{
  if(Program->SymbolBucketWidth == 1) return ((const kbts_u8 *)Program->SymbolBuckets)[Symbol];
  if(Program->SymbolBucketWidth == 2) return ((const kbts_u16 *)Program->SymbolBuckets)[Symbol];
  return ((const kbts_u32 *)Program->SymbolBuckets)[Symbol];
}

KBTS_INLINE void kbts__SetCompiledBucketIndex(kbts__compiled_program *Program, kbts_u32 Symbol, kbts_u32 Bucket)
{
  if(Program->SymbolBucketWidth == 1) ((kbts_u8 *)Program->SymbolBuckets)[Symbol] = (kbts_u8)Bucket;
  else if(Program->SymbolBucketWidth == 2) ((kbts_u16 *)Program->SymbolBuckets)[Symbol] = (kbts_u16)Bucket;
  else ((kbts_u32 *)Program->SymbolBuckets)[Symbol] = Bucket;
}

KBTS_INLINE kbts_u32 kbts__CompiledOutcomeIndex(const kbts__compiled_program *Program, kbts_u32 Candidate)
{
  if(Program->OutcomeWidth == 2)
  {
    kbts_u32 Id = ((const kbts_u16 *)Program->Outcomes)[Candidate];
    return Id == 0xFFFFu ? (kbts_u32)-1 : Id;
  }
  return ((const kbts_u32 *)Program->Outcomes)[Candidate];
}

KBTS_INLINE void kbts__SetCompiledOutcomeIndex(kbts__compiled_program *Program, kbts_u32 Candidate, kbts_u32 Outcome)
{
  if(Program->OutcomeWidth == 2) ((kbts_u16 *)Program->Outcomes)[Candidate] = (kbts_u16)Outcome;
  else ((kbts_u32 *)Program->Outcomes)[Candidate] = Outcome;
}

#endif
