/* Native-endian contextual tables are used only while constructing this cache. */
typedef struct kbts__context_predicate
{
  struct kbts__context_predicate *Next, *HashNext;
  kbts_u32 *Bits;
  kbts_u32 Id, Hash;
} kbts__context_predicate;

typedef struct kbts__context_source
{
  struct kbts__context_source *Next;
  kbts__coverage *Coverage;
  kbts_u16 *Classes;
  kbts_u32 Kind, Value, Predicate;
} kbts__context_source;

typedef struct kbts__context_rule
{
  struct kbts__context_rule *Next;
  kbts__compiled_wide_test *Tests;
  kbts__compiled_outcome Outcome;
  kbts_u32 TestCount, OutcomeIndex;
} kbts__context_rule;

typedef struct kbts__context_program
{
  kbts__compiled_program Program;
  kbts__context_rule *First, *Last;
  kbts_un TestsOffset, OutcomesOffset, BucketsOffset, SymbolsOffset;
  kbts_un LocalMasksOffset, PredicateIdsOffset;
  kbts_u32 LocalPredicates[32];
  kbts_u32 DenseIndex;
} kbts__context_program;

typedef struct kbts__context_builder
{
  kbts_arena Arena;
  kbts__context_predicate *Predicates, *LastPredicate;
  kbts__context_predicate **PredicateHash;
  kbts__context_source **SourceHash;
  kbts_u32 HashCount, SourceCount, PredicateCount, OutcomeCount;
  kbts_u32 GlyphCount, GlyphWords;
  kbts_u32 *Bits;
  int Error;
} kbts__context_builder;

static void *kbts__ContextMemory(kbts__context_builder *Builder, kbts_un Count, kbts_un Size, kbts_un Align)
{
  if(Builder->Error) return 0;
  kbts_un Limit = (kbts_u32)-1;
  kbts_un Overhead = sizeof(kbts__arena_block) + KBTS_ALIGNOF(kbts__arena_block) + Align - 1;
  if(!Size || Count > (Limit - Overhead) / Size)
  {
    Builder->Error = 1;
    return 0;
  }
  Size *= Count;
  kbts_arena *Arena = &Builder->Arena;
  kbts__EnsureArenaInitialized(Arena);
  kbts__arena_block *Block = (kbts__arena_block *)Arena->BlockSentinel.Prev;
  void *Result = 0;
  if(kbts__ArenaBlockIsValid(Arena, Block))
  {
    char *Memory = KBTS__POINTER_AFTER(char, Block);
    char *Aligned = KBTS__ALIGN_POINTER(char, Memory + Block->Used, Align);
    kbts_un Used = (kbts_un)(Aligned - Memory);
    if(Used <= Block->Size && Size <= Block->Size - Used) Result = Aligned;
  }
  if(!Result)
  {
    kbts_un BlockSize = Size + Overhead;
    if(BlockSize < KBTS_ARENA_MIN_BLOCK_SIZE) BlockSize = KBTS_ARENA_MIN_BLOCK_SIZE;
    void *Allocation = kbts__AllocatorAllocate(Arena->Allocator, Arena->AllocatorData, BlockSize);
    if(!Allocation) {Builder->Error = 1; return 0;}
    /* Link only successfully allocated blocks, so failure leaves every block freeable. */
    Block = KBTS__ALIGN_POINTER(kbts__arena_block, Allocation, KBTS_ALIGNOF(kbts__arena_block));
    KBTS_MEMSET(Block, 0, sizeof(*Block));
    Block->BaseAllocation = Allocation;
    Block->Size = (kbts_un)((char *)Allocation + BlockSize - KBTS__POINTER_AFTER(char, Block));
    KBTS__DLLIST_INSERT_BEFORE(&Block->Header, &Arena->BlockSentinel);
    Result = KBTS__ALIGN_POINTER(char, KBTS__POINTER_AFTER(char, Block), Align);
  }
  Block->Used = (kbts_un)((char *)Result + Size - KBTS__POINTER_AFTER(char, Block));
  KBTS_MEMSET(Result, 0, Size);
  return Result;
}
#define kbts__ContextArray(B, T, N) (T *)kbts__ContextMemory((B), (N), sizeof(T), KBTS_ALIGNOF(T))

static kbts_u32 kbts__ContextSourceHash(kbts_u32 Kind, kbts_u32 Value, kbts__coverage *Coverage, kbts_u16 *Classes)
{
  kbts_un A = (kbts_un)Coverage, B = (kbts_un)Classes;
  return ((kbts_u32)(A ^ (A >> 16)) * 16777619u) ^
         ((kbts_u32)(B ^ (B >> 16)) * 2246822519u) ^ (Value * 3266489917u) ^ Kind;
}

static int kbts__ContextGrowHash(kbts__context_builder *Builder)
{
  kbts_u32 OldCount = Builder->HashCount;
  if(OldCount > (kbts_u32)-1 / 2) {Builder->Error = 1; return 0;}
  kbts_u32 Count = OldCount ? OldCount * 2 : 256;
  kbts__context_source **Sources = kbts__ContextArray(Builder, kbts__context_source *, Count);
  kbts__context_predicate **Predicates = kbts__ContextArray(Builder, kbts__context_predicate *, Count);
  if(Builder->Error) return 0;
  for(kbts_u32 I = 0; I < OldCount; ++I)
  {
    for(kbts__context_source *Source = Builder->SourceHash[I]; Source; )
    {
      kbts__context_source *Next = Source->Next;
      kbts_u32 Slot = kbts__ContextSourceHash(Source->Kind, Source->Value, Source->Coverage, Source->Classes) & (Count - 1);
      Source->Next = Sources[Slot];
      Sources[Slot] = Source;
      Source = Next;
    }
  }
  for(kbts__context_predicate *Predicate = Builder->Predicates; Predicate; Predicate = Predicate->Next)
  {
    kbts_u32 Slot = Predicate->Hash & (Count - 1);
    Predicate->HashNext = Predicates[Slot];
    Predicates[Slot] = Predicate;
  }
  Builder->SourceHash = Sources;
  Builder->PredicateHash = Predicates;
  Builder->HashCount = Count;
  return 1;
}

/* Kind: singleton, coverage, class (optionally intersected with coverage), coverage index. */
static kbts_u32 kbts__ContextPredicate(kbts__context_builder *Builder, kbts_u32 Kind, kbts_u32 Value,
                                      kbts__coverage *Coverage, kbts_u16 *Classes)
{
  if(Builder->Error) return 0;
  if(!Builder->HashCount || Builder->SourceCount >= Builder->HashCount)
    if(!kbts__ContextGrowHash(Builder)) return 0;
  kbts_u32 Slot = kbts__ContextSourceHash(Kind, Value, Coverage, Classes) & (Builder->HashCount - 1);
  for(kbts__context_source *Source = Builder->SourceHash[Slot]; Source; Source = Source->Next)
    if(Source->Kind == Kind && Source->Value == Value && Source->Coverage == Coverage && Source->Classes == Classes)
      return Source->Predicate;

  KBTS_MEMSET(Builder->Bits, 0, Builder->GlyphWords * sizeof(kbts_u32));
  if(Kind == 0)
  {
    if(Value < Builder->GlyphCount) Builder->Bits[Value >> 5] |= 1u << (Value & 31);
  }
  else
  {
    /* REVIEW[COLD1 PERF]: Each new coverage-index/class source scans every glyph;
     * format-1 sets therefore revisit the same coverage once per SetIndex before
     * bitset interning can help. Measure this separately from symbol refinement:
     * can one coverage/class walk populate these source predicates together? */
    for(kbts_u32 Glyph = 0; Glyph < Builder->GlyphCount; ++Glyph)
    {
      int Member = 1;
      if(Coverage)
      {
        kbts__cover_glyph_result Cover = kbts__CoverGlyph(Coverage, (kbts_u16)Glyph);
        Member = Cover.Valid && (Kind != 3 || Cover.Index == Value);
      }
      if(Member && Kind == 2)
        Member = (Classes ? kbts__GlyphClassFromTable(Classes, Glyph).Class : 0) == Value;
      if(Member) Builder->Bits[Glyph >> 5] |= 1u << (Glyph & 31);
    }
  }
  kbts_u32 Hash = 2166136261u;
  for(kbts_u32 I = 0; I < Builder->GlyphWords; ++I) Hash = (Hash ^ Builder->Bits[I]) * 16777619u;
  kbts_u32 PredicateSlot = Hash & (Builder->HashCount - 1);
  kbts__context_predicate *Predicate;
  for(Predicate = Builder->PredicateHash[PredicateSlot]; Predicate; Predicate = Predicate->HashNext)
  {
    if(Predicate->Hash != Hash) continue;
    kbts_u32 I = 0;
    while(I < Builder->GlyphWords && Predicate->Bits[I] == Builder->Bits[I]) ++I;
    if(I == Builder->GlyphWords) break;
  }
  if(!Predicate)
  {
    if(Builder->PredicateCount == (kbts_u32)-1) {Builder->Error = 1; return 0;}
    Predicate = kbts__ContextArray(Builder, kbts__context_predicate, 1);
    kbts_u32 *Bits = kbts__ContextArray(Builder, kbts_u32, Builder->GlyphWords);
    if(Builder->Error) return 0;
    KBTS_MEMCPY(Bits, Builder->Bits, Builder->GlyphWords * sizeof(kbts_u32));
    Predicate->Bits = Bits;
    Predicate->Id = Builder->PredicateCount++;
    Predicate->Hash = Hash;
    Predicate->HashNext = Builder->PredicateHash[PredicateSlot];
    Builder->PredicateHash[PredicateSlot] = Predicate;
    if(Builder->LastPredicate) Builder->LastPredicate->Next = Predicate;
    else Builder->Predicates = Predicate;
    Builder->LastPredicate = Predicate;
  }
  kbts__context_source *Source = kbts__ContextArray(Builder, kbts__context_source, 1);
  if(!Source) return 0;
  Source->Kind = Kind;
  Source->Value = Value;
  Source->Coverage = Coverage;
  Source->Classes = Classes;
  Source->Predicate = Predicate->Id;
  Source->Next = Builder->SourceHash[Slot];
  Builder->SourceHash[Slot] = Source;
  ++Builder->SourceCount;
  return Predicate->Id;
}

static void kbts__ContextTest(kbts__context_builder *Builder, kbts__compiled_wide_test *Test, kbts_s32 Probe,
                              kbts_u32 Kind, kbts_u32 Value, kbts__coverage *Coverage, kbts_u16 *Classes)
{
  Test->Probe = Probe;
  Test->Predicate = kbts__ContextPredicate(Builder, Kind, Value, Coverage, Classes);
}

static kbts__context_rule *kbts__ContextRule(kbts__context_builder *Builder, kbts__context_program *Program,
                                            kbts_u32 Backtrack, kbts_u32 Input, kbts_u32 Lookahead,
                                            kbts__sequence_lookup_record *Records, kbts_u32 RecordCount)
{
  if(Builder->OutcomeCount == (kbts_u32)-1) {Builder->Error = 1; return 0;}
  if(Backtrack > 65535) {Builder->Error = 2; return 0;}
  kbts_u32 Count = Backtrack + Input + Lookahead;
  if(!Count) Count = 1; /* A zero-input context still evaluates its padding stage. */
  kbts__context_rule *Rule = kbts__ContextArray(Builder, kbts__context_rule, 1);
  kbts__compiled_wide_test *Tests = kbts__ContextArray(Builder, kbts__compiled_wide_test, Count);
  if(Builder->Error) return 0;
  Rule->Tests = Tests;
  Rule->TestCount = Count;
  Rule->Outcome.Records = Records;
  Rule->Outcome.RecordCount = RecordCount;
  Rule->Outcome.InputCount = Input;
  Rule->OutcomeIndex = Builder->OutcomeCount++;
  if(Program->Last) Program->Last->Next = Rule;
  else Program->First = Rule;
  Program->Last = Rule;
  if(Program->Program.StageCount < Count) Program->Program.StageCount = Count;
  if(Program->Program.MaxBacktrack < Backtrack) Program->Program.MaxBacktrack = (kbts_u16)Backtrack;
  kbts_u32 Ahead = Input + Lookahead;
  if(Ahead) --Ahead;
  if(Program->Program.MaxAhead < Ahead) Program->Program.MaxAhead = Ahead;
  return Rule;
}

static void kbts__ContextCompileSubtable(kbts__context_builder *Builder, kbts__context_program *Program,
                                         kbts_u16 *Base, int Chained)
{
  kbts_u32 Format = Base[0];
  if(Format == 3)
  {
    kbts_u16 *Backtrack = 0, *Input, *Lookahead = 0;
    kbts_u32 BacktrackCount = 0, InputCount, LookaheadCount = 0, RecordCount;
    kbts__sequence_lookup_record *Records;
    if(Chained)
    {
      kbts__unpacked_chained_sequence_context_3 Table = kbts__UnpackChainedSequenceContext3((kbts__chained_sequence_context_3 *)Base, 0);
      Backtrack = Table.BacktrackCoverageOffsets;
      Input = Table.InputCoverageOffsets;
      Lookahead = Table.LookaheadCoverageOffsets;
      BacktrackCount = Table.BacktrackCount;
      InputCount = Table.InputCount;
      LookaheadCount = Table.LookaheadCount;
      RecordCount = Table.RecordCount;
      Records = Table.Records;
      /* The original chained matcher requires its unfiltered first input glyph. */
      if(!InputCount) return;
    }
    else
    {
      kbts__sequence_context_3 *Table = (kbts__sequence_context_3 *)Base;
      InputCount = Table->GlyphCount;
      Input = KBTS__POINTER_AFTER(kbts_u16, Table);
      RecordCount = Table->SequenceLookupCount;
      Records = (kbts__sequence_lookup_record *)(Input + InputCount);
      Program->Program.FirstFiltered = 1;
    }
    kbts__context_rule *Rule = kbts__ContextRule(Builder, Program, BacktrackCount, InputCount, LookaheadCount, Records, RecordCount);
    if(!Rule) return;
    kbts_u32 At = 0;
    for(kbts_u32 I = 0; I < InputCount; ++I)
      kbts__ContextTest(Builder, &Rule->Tests[At++], (kbts_s32)I, 1, 0, KBTS__POINTER_OFFSET(kbts__coverage, Base, Input[I]), 0);
    for(kbts_u32 I = 0; I < BacktrackCount; ++I)
      kbts__ContextTest(Builder, &Rule->Tests[At++], -(kbts_s32)I - 1, 1, 0, KBTS__POINTER_OFFSET(kbts__coverage, Base, Backtrack[I]), 0);
    for(kbts_u32 I = 0; I < LookaheadCount; ++I)
      kbts__ContextTest(Builder, &Rule->Tests[At++], (kbts_s32)(InputCount + I), 1, 0, KBTS__POINTER_OFFSET(kbts__coverage, Base, Lookahead[I]), 0);
    return;
  }
  if(Format != 1 && Format != 2) {Builder->Error = 2; return;}
  kbts__coverage *Coverage = KBTS__POINTER_OFFSET(kbts__coverage, Base, Base[1]);
  kbts_u16 *InputClasses = 0, *BacktrackClasses = 0, *LookaheadClasses = 0;
  kbts_u32 SetCount;
  kbts_u16 *SetOffsets;
  if(Format == 1)
  {
    SetCount = Base[2];
    SetOffsets = Base + 3;
  }
  else if(Chained)
  {
    if(Base[2]) BacktrackClasses = KBTS__POINTER_OFFSET(kbts_u16, Base, Base[2]);
    if(Base[3]) InputClasses = KBTS__POINTER_OFFSET(kbts_u16, Base, Base[3]);
    LookaheadClasses = Base[4] ? KBTS__POINTER_OFFSET(kbts_u16, Base, Base[4]) : InputClasses;
    SetCount = Base[5];
    SetOffsets = Base + 6;
  }
  else
  {
    if(Base[2]) InputClasses = KBTS__POINTER_OFFSET(kbts_u16, Base, Base[2]);
    SetCount = Base[3];
    SetOffsets = Base + 4;
  }
  for(kbts_u32 SetIndex = 0; SetIndex < SetCount && !Builder->Error; ++SetIndex)
  {
    if(!SetOffsets[SetIndex]) continue;
    kbts_u16 *Set = KBTS__POINTER_OFFSET(kbts_u16, Base, SetOffsets[SetIndex]);
    kbts_u32 First = kbts__ContextPredicate(Builder, Format == 1 ? 3 : 2, SetIndex, Coverage, InputClasses);
    for(kbts_u32 RuleIndex = 0; RuleIndex < Set[0] && !Builder->Error; ++RuleIndex)
    {
      kbts_u16 *Raw = KBTS__POINTER_OFFSET(kbts_u16, Set, Set[1 + RuleIndex]);
      kbts_u16 *Input, *Backtrack = 0, *Lookahead = 0;
      kbts_u32 InputCount, BacktrackCount = 0, LookaheadCount = 0, RecordCount;
      kbts__sequence_lookup_record *Records;
      if(Chained)
      {
        /* Check before unpacking: its variable layout subtracts one from InputCount. */
        if(!Raw[1 + Raw[0]]) {Builder->Error = 2; return;}
        kbts__unpacked_chained_sequence_rule Unpacked = kbts__UnpackChainedSequenceRule((kbts__chained_sequence_rule *)Raw, 0);
        Input = Unpacked.Input;
        InputCount = Unpacked.InputCount;
        Backtrack = Unpacked.Backtrack;
        BacktrackCount = Unpacked.BacktrackCount;
        Lookahead = Unpacked.Lookahead;
        LookaheadCount = Unpacked.LookaheadCount;
        Records = Unpacked.Records;
        RecordCount = Unpacked.RecordCount;
      }
      else
      {
        InputCount = Raw[0];
        if(!InputCount) {Builder->Error = 2; return;}
        RecordCount = Raw[1];
        Input = Raw + 2;
        Records = (kbts__sequence_lookup_record *)(Input + InputCount - 1);
      }
      kbts__context_rule *Rule = kbts__ContextRule(Builder, Program, BacktrackCount, InputCount, LookaheadCount, Records, RecordCount);
      if(!Rule) return;
      Rule->Tests[0].Predicate = First;
      kbts_u32 At = 1, Kind = Format == 1 ? 0 : 2;
      for(kbts_u32 I = 1; I < InputCount; ++I)
        kbts__ContextTest(Builder, &Rule->Tests[At++], (kbts_s32)I, Kind, Input[I - 1], 0, InputClasses);
      for(kbts_u32 I = 0; I < BacktrackCount; ++I)
        kbts__ContextTest(Builder, &Rule->Tests[At++], -(kbts_s32)I - 1, Kind, Backtrack[I], 0, BacktrackClasses);
      for(kbts_u32 I = 0; I < LookaheadCount; ++I)
        kbts__ContextTest(Builder, &Rule->Tests[At++], (kbts_s32)(InputCount + I), Kind, Lookahead[I], 0, LookaheadClasses);
    }
  }
}

static kbts_un kbts__ContextReserve(kbts__context_builder *Builder, kbts_un *Total, kbts_un Count, kbts_un Size, kbts_un Align)
{
  if(Builder->Error) return 0;
  kbts_un Limit = (kbts_u32)-1;
  if(*Total > Limit - (Align - 1)) {Builder->Error = 1; return 0;}
  kbts_un Offset = (*Total + Align - 1) & ~(Align - 1);
  if(Count > (Limit - Offset) / Size) {Builder->Error = 1; return 0;}
  *Total = Offset + Count * Size;
  return Offset;
}

static int kbts__ContextMember(kbts__context_predicate **Predicates, kbts_u32 Predicate, kbts_u32 Symbol, kbts_u32 *Representatives)
{
  if(!Predicate) return 1;
  if(!Symbol) return 0;
  kbts_u32 Glyph = Representatives[Symbol];
  return (Predicates[Predicate]->Bits[Glyph >> 5] >> (Glyph & 31)) & 1;
}

static kbts__compiled_contexts *kbts__CompileContexts(kbts_font *Font, kbts_allocator_function *Allocator, void *AllocatorData,
                                                    kbts_allocator_function *ScratchAllocator, void *ScratchAllocatorData)
{
  kbts__context_builder Builder = KBTS__ZERO;
  Builder.Arena.Allocator = ScratchAllocator;
  Builder.Arena.AllocatorData = ScratchAllocatorData;
  Builder.GlyphCount = Font->Blob->GlyphCount;
  Builder.GlyphWords = (Builder.GlyphCount + 31) / 32;
  Builder.PredicateCount = 1; /* Padding is not interned with a real all-glyph predicate. */
  kbts__compiled_contexts *Result = 0;
  if(Builder.GlyphCount > 65535) {Font->Error = KBTS_LOAD_FONT_ERROR_INVALID_FONT; return 0;}
  kbts_u32 ProgramCount = Font->Blob->LookupSubtableCount;
  kbts__context_program *Programs = kbts__ContextArray(&Builder, kbts__context_program, ProgramCount);
  Builder.Bits = kbts__ContextArray(&Builder, kbts_u32, Builder.GlyphWords);
  if(!Builder.Error)
  {
    kbts_u32 *FlatOffsets = KBTS__POINTER_OFFSET(kbts_u32, Font->Blob, Font->Blob->LookupSubtableIndexOffsetsOffsetFromStartOfFile);
    kbts_blob_table_id TableIds[2] = {KBTS_BLOB_TABLE_ID_GSUB, KBTS_BLOB_TABLE_ID_GPOS};
    for(kbts_u32 TableIndex = 0; TableIndex < 2 && !Builder.Error; ++TableIndex)
    {
      kbts__gsub_gpos *Table = kbts__BlobTableDataType(Font->Blob, TableIds[TableIndex], kbts__gsub_gpos);
      kbts_lookup_list *List = kbts__GetLookupList(Table);
      if(!List) continue;
      kbts_u32 LookupBase = TableIndex ? Font->Blob->GposLookupIndexOffset : 0;
      for(kbts_u32 LookupIndex = 0; LookupIndex < List->Count && !Builder.Error; ++LookupIndex)
      {
        kbts__lookup *Lookup = kbts__GetLookup(List, LookupIndex);
        kbts_u16 *Offsets = KBTS__POINTER_AFTER(kbts_u16, Lookup);
        for(kbts_u32 Subtable = 0; Subtable < Lookup->SubtableCount && !Builder.Error; ++Subtable)
        {
          kbts_u32 Type = Lookup->Type;
          kbts_u16 *Base = KBTS__POINTER_OFFSET(kbts_u16, Lookup, Offsets[Subtable]);
          /* REVIEW[COLD2 SUSPECT]: Native-blob loading checks the version, not
           * extension targets; this walk has neither bounds nor a progress guard.
           * A surviving type-7/9 extension with zero offset would loop here.
           * Where is the validated-table invariant established before compilation,
           * including the equivalent fusion and admission-plan walks? */
          // ANSWER Does this run before everything? Either way, a 4 way nested loop is a smell.
          // What is the source and result here? we loop through all tables in teh font, then all lists in each table then subtables? then lookup types within those?
          while(Type == (TableIndex ? 9u : 7u))
          {
            kbts__extension *Extension = (kbts__extension *)Base;
            Type = Extension->LookupType;
            Base = KBTS__POINTER_OFFSET(kbts_u16, Extension, kbts__ReadU32Unaligned(&Extension->Offset));
          }
          kbts_u32 ContextType = TableIndex ? 7 : 5;
          if(Type == ContextType || Type == ContextType + 1)
          {
            kbts_u32 Flat = FlatOffsets[LookupBase + LookupIndex];
            if(Flat >= ProgramCount || Subtable >= ProgramCount - Flat) {Builder.Error = 2; break;}
            kbts__ContextCompileSubtable(&Builder, &Programs[Flat + Subtable], Base, Type != ContextType);
          }
        }
      }
    }
  }

  kbts_u32 Domain = Builder.GlyphCount + 1;
  kbts_u32 *Symbols = kbts__ContextArray(&Builder, kbts_u32, Builder.GlyphCount);
  kbts_u32 *FalseSymbols = kbts__ContextArray(&Builder, kbts_u32, Domain);
  kbts_u32 *TrueSymbols = kbts__ContextArray(&Builder, kbts_u32, Domain);
  kbts_u32 *Representatives = kbts__ContextArray(&Builder, kbts_u32, Domain);
  kbts__context_predicate **Predicates = kbts__ContextArray(&Builder, kbts__context_predicate *, Builder.PredicateCount);
  kbts_u32 SymbolCount = 1;
  if(!Builder.Error)
  {
    /* Refine exact membership classes; keep the all-false boundary class at zero. */
    for(kbts__context_predicate *Predicate = Builder.Predicates; Predicate; Predicate = Predicate->Next)
    {
      Predicates[Predicate->Id] = Predicate;
      KBTS_MEMSET(FalseSymbols, 0xff, SymbolCount * sizeof(kbts_u32));
      KBTS_MEMSET(TrueSymbols, 0xff, SymbolCount * sizeof(kbts_u32));
      FalseSymbols[0] = 0;
      kbts_u32 NextCount = 1;
      for(kbts_u32 Glyph = 0; Glyph < Builder.GlyphCount; ++Glyph)
      {
        kbts_u32 *Map = ((Predicate->Bits[Glyph >> 5] >> (Glyph & 31)) & 1) ? TrueSymbols : FalseSymbols;
        kbts_u32 Old = Symbols[Glyph];
        if(Map[Old] == (kbts_u32)-1) Map[Old] = NextCount++;
        Symbols[Glyph] = Map[Old];
      }
      SymbolCount = NextCount;
    }
    for(kbts_u32 Glyph = 0; Glyph < Builder.GlyphCount; ++Glyph) Representatives[Symbols[Glyph]] = Glyph;
  }

  kbts_u16 *Pages = kbts__ContextArray(&Builder, kbts_u16, 257 * 256);
  kbts_u16 PageDirectory[256] = {0};
  kbts_u32 PageCount = 1;
  if(!Builder.Error)
  {
    for(kbts_u32 Page = 0; Page < 256; ++Page)
    {
      kbts_u16 *Values = Pages + PageCount * 256;
      for(kbts_u32 Low = 0; Low < 256; ++Low)
      {
        kbts_u32 Glyph = Page * 256 + Low;
        Values[Low] = Glyph < Builder.GlyphCount ? (kbts_u16)Symbols[Glyph] : 0;
      }
      kbts_u32 Match = 0;
      for(; Match < PageCount; ++Match)
      {
        kbts_u32 Low = 0;
        while(Low < 256 && Pages[Match * 256 + Low] == Values[Low]) ++Low;
        if(Low == 256) break;
      }
      PageDirectory[Page] = (kbts_u16)Match;
      if(Match == PageCount) ++PageCount;
    }
  }

  kbts_u32 DenseCount = 1; /* Every noncontext/empty native subtable shares slot zero. */
  for(kbts_u32 P = 0; P < ProgramCount && !Builder.Error; ++P)
    if(Programs[P].First) Programs[P].DenseIndex = DenseCount++;
  kbts_u32 ProgramIndexWidth = DenseCount <= 256 ? 1 : DenseCount <= 65536 ? 2 : 4;
  kbts_un Total = sizeof(kbts__compiled_contexts);
  kbts_u32 SymbolWidth = SymbolCount <= 256 ? 1 : 2;
  kbts_u32 SymbolWords = (SymbolCount + 31) / 32;
  kbts_un GlyphOffset = kbts__ContextReserve(&Builder, &Total, PageCount * 256, SymbolWidth, SymbolWidth);
  kbts_un MaskOffset = kbts__ContextReserve(&Builder, &Total, Builder.PredicateCount, (kbts_un)SymbolWords * sizeof(kbts_u32), KBTS_ALIGNOF(kbts_u32));
  kbts_un ProgramOffset = kbts__ContextReserve(&Builder, &Total, DenseCount, sizeof(kbts__compiled_program), KBTS_ALIGNOF(kbts__compiled_program));
  kbts_un ProgramIndexOffset = kbts__ContextReserve(&Builder, &Total, ProgramCount, ProgramIndexWidth, ProgramIndexWidth);
  kbts_un ProofOffset = kbts__ContextReserve(&Builder, &Total, DenseCount, sizeof(kbts_u32 *), KBTS_ALIGNOF(kbts_u32 *));
  kbts_un OutcomeOffset = kbts__ContextReserve(&Builder, &Total, Builder.OutcomeCount, sizeof(kbts__compiled_outcome), KBTS_ALIGNOF(kbts__compiled_outcome));
  kbts_un TestBytes = 0, BucketBytes = 0, OutcomeBytes = (kbts_un)Builder.OutcomeCount * sizeof(kbts__compiled_outcome);
  kbts_un LocalPredicateBytes = 0;
  for(kbts_u32 P = 0; P < ProgramCount && !Builder.Error; ++P)
  {
    kbts__context_program *Temp = &Programs[P];
    kbts__compiled_program *Program = &Temp->Program;
    if(!Temp->First) continue;
    /* The bucket dispatch has already proved the first input predicate.
     * Remaining columns still execute a uniform number of stages. */
    if(Program->StageCount > 1) --Program->StageCount;
    kbts_u32 Candidates = 0, Buckets = 1;
    for(kbts_u32 Symbol = 0; Symbol < SymbolCount && !Builder.Error; ++Symbol)
    {
      kbts_u32 Count = 0;
      for(kbts__context_rule *Rule = Temp->First; Rule; Rule = Rule->Next)
        if(kbts__ContextMember(Predicates, Rule->Tests[0].Predicate, Symbol, Representatives))
        {
          if(Candidates == (kbts_u32)-1) {Builder.Error = 1; break;}
          ++Candidates;
          ++Count;
        }
      if(Count) ++Buckets;
    }
    if(Candidates > (kbts_u32)-1 - 7) {Builder.Error = 1; break;}
    Program->CandidateCount = (Candidates + 7) & ~7u;
    Program->BucketCount = Buckets;
    Program->SymbolBucketWidth = Buckets <= 256 ? 1 : Buckets <= 65536 ? 2 : 4;
    Program->OutcomeWidth = Builder.OutcomeCount <= 65535 ? 2 : 4;
    kbts_u32 LocalCount = 1;
    int LocalFits = 1;
    for(kbts__context_rule *Rule = Temp->First; Rule && LocalFits; Rule = Rule->Next)
      for(kbts_u32 T = 1; T < Rule->TestCount; ++T)
      {
        kbts_u32 Id = Rule->Tests[T].Predicate, Local = 0;
        while(Local < LocalCount && Temp->LocalPredicates[Local] != Id) ++Local;
        if(Local == LocalCount)
        {
          if(LocalCount == KBTS__ARRAY_LENGTH(Temp->LocalPredicates)) {LocalFits = 0; break;}
          Temp->LocalPredicates[LocalCount++] = Id;
        }
      }
    Program->LocalPredicateCount = (kbts_u8)(LocalFits ? LocalCount : 0);
    Program->LocalMaskWidth = (kbts_u8)(LocalFits ? (LocalCount <= 8 ? 1 : LocalCount <= 16 ? 2 : 4) : 0);
    for(kbts__context_rule *Rule = Temp->First; Rule; Rule = Rule->Next)
      for(kbts_u32 T = 1; T < Rule->TestCount; ++T)
        if((!LocalFits && Rule->Tests[T].Predicate > 255) || Rule->Tests[T].Probe < -128 || Rule->Tests[T].Probe > 127) Program->Wide = 1;
    kbts_un TestSize = Program->Wide ? sizeof(kbts__compiled_wide_test) : sizeof(kbts__compiled_test);
    /* Reserve one stage at a time arithmetically, without overflowing the product. */
    if(Program->CandidateCount > (kbts_u32)-1 / TestSize) {Builder.Error = 1; break;}
    kbts_un StageBytes = (kbts_un)Program->CandidateCount * TestSize;
    Temp->TestsOffset = kbts__ContextReserve(&Builder, &Total, StageBytes ? Program->StageCount : 0,
                                           StageBytes ? StageBytes : 1, KBTS_ALIGNOF(kbts__compiled_wide_test));
    if(Builder.Error) break;
    TestBytes += Program->StageCount * StageBytes;
    if(!Program->Wide && Program->CandidateCount)
    {
      /* A 32-bit gather of the final compact test may read its padding neighbor. */
      kbts__ContextReserve(&Builder, &Total, 1, sizeof(kbts__compiled_test), KBTS_ALIGNOF(kbts__compiled_test));
      TestBytes += sizeof(kbts__compiled_test);
    }
    Temp->OutcomesOffset = kbts__ContextReserve(&Builder, &Total, Program->CandidateCount, Program->OutcomeWidth, Program->OutcomeWidth);
    Temp->BucketsOffset = kbts__ContextReserve(&Builder, &Total, Buckets, sizeof(kbts__compiled_bucket), KBTS_ALIGNOF(kbts__compiled_bucket));
    Temp->SymbolsOffset = kbts__ContextReserve(&Builder, &Total, SymbolCount, Program->SymbolBucketWidth, Program->SymbolBucketWidth);
    if(LocalFits)
    {
      Temp->LocalMasksOffset = kbts__ContextReserve(&Builder, &Total, SymbolCount, Program->LocalMaskWidth, Program->LocalMaskWidth);
      Temp->PredicateIdsOffset = kbts__ContextReserve(&Builder, &Total, LocalCount, sizeof(kbts_u32), KBTS_ALIGNOF(kbts_u32));
      LocalPredicateBytes += (kbts_un)SymbolCount * Program->LocalMaskWidth + (kbts_un)LocalCount * sizeof(kbts_u32);
    }
    BucketBytes += (kbts_un)Buckets * sizeof(kbts__compiled_bucket) + (kbts_un)SymbolCount * Program->SymbolBucketWidth;
    OutcomeBytes += (kbts_un)Program->CandidateCount * Program->OutcomeWidth;
  }

  kbts_un AllocationSize = Total;
  kbts__ContextReserve(&Builder, &AllocationSize, KBTS_ALIGNOF(kbts__compiled_contexts) - 1, 1, 1);
  if(!Builder.Error)
  {
    void *Allocation = kbts__AllocatorAllocate(Allocator, AllocatorData, AllocationSize);
    if(Allocation)
    {
      Result = KBTS__ALIGN_POINTER(kbts__compiled_contexts, Allocation, KBTS_ALIGNOF(kbts__compiled_contexts));
      KBTS_MEMSET(Result, 0, Total);
      Result->BaseAllocation = Allocation;
      Result->Allocator = Allocator;
      Result->AllocatorData = AllocatorData;
      Result->GlyphCount = Builder.GlyphCount;
      Result->SymbolCount = SymbolCount;
      Result->SymbolWordCount = SymbolWords;
      Result->PredicateCount = Builder.PredicateCount;
      Result->ProgramCount = DenseCount;
      Result->FlatProgramCount = ProgramCount;
      Result->ProgramIndexWidth = (kbts_u8)ProgramIndexWidth;
      Result->ProgramIndices = KBTS__POINTER_OFFSET(void, Result, ProgramIndexOffset);
      Result->PredicateIds = KBTS__POINTER_OFFSET(kbts_u32 *, Result, ProofOffset);
      Result->ProgramBytes = (kbts_un)DenseCount * sizeof(kbts__compiled_program) + (kbts_un)ProgramCount * ProgramIndexWidth;
      Result->OutcomeCount = Builder.OutcomeCount;
      Result->SymbolWidth = (kbts_u8)SymbolWidth;
      Result->AllocationSize = AllocationSize;
      Result->TestBytes = TestBytes;
      Result->ClassifierBytes = sizeof(Result->GlyphPages) + PageCount * 256 * SymbolWidth;
      Result->PredicateBytes = (kbts_un)Builder.PredicateCount * SymbolWords * sizeof(kbts_u32) + LocalPredicateBytes;
      Result->BucketBytes = BucketBytes;
      Result->OutcomeBytes = OutcomeBytes;
      KBTS_MEMCPY(Result->GlyphPages, PageDirectory, sizeof(PageDirectory));
      Result->GlyphSymbols = KBTS__POINTER_OFFSET(void, Result, GlyphOffset);
      for(kbts_u32 I = 0; I < PageCount * 256; ++I)
      {
        if(SymbolWidth == 1) ((kbts_u8 *)Result->GlyphSymbols)[I] = (kbts_u8)Pages[I];
        else ((kbts_u16 *)Result->GlyphSymbols)[I] = Pages[I];
      }
      Result->PredicateMasks = KBTS__POINTER_OFFSET(kbts_u32, Result, MaskOffset);
      for(kbts_u32 I = 0; I < SymbolWords; ++I) Result->PredicateMasks[I] = (kbts_u32)-1;
      for(kbts_u32 Predicate = 1; Predicate < Builder.PredicateCount; ++Predicate)
        for(kbts_u32 Symbol = 1; Symbol < SymbolCount; ++Symbol)
          if(kbts__ContextMember(Predicates, Predicate, Symbol, Representatives))
            Result->PredicateMasks[(kbts_un)Predicate * SymbolWords + (Symbol >> 5)] |= 1u << (Symbol & 31);
      Result->Programs = KBTS__POINTER_OFFSET(kbts__compiled_program, Result, ProgramOffset);
      Result->Outcomes = KBTS__POINTER_OFFSET(kbts__compiled_outcome, Result, OutcomeOffset);
      for(kbts_u32 P = 0; P < ProgramCount; ++P)
      {
        kbts__context_program *Temp = &Programs[P];
        kbts__SetCompiledIndex(Result->ProgramIndices, ProgramIndexWidth, P, Temp->DenseIndex);
        if(!Temp->First) continue;
        kbts__compiled_program *Program = &Result->Programs[Temp->DenseIndex];
        *Program = Temp->Program;
        Program->Tests = KBTS__POINTER_OFFSET(void, Result, Temp->TestsOffset);
        Program->Outcomes = KBTS__POINTER_OFFSET(void, Result, Temp->OutcomesOffset);
        Program->Buckets = KBTS__POINTER_OFFSET(kbts__compiled_bucket, Result, Temp->BucketsOffset);
        Program->SymbolBuckets = KBTS__POINTER_OFFSET(void, Result, Temp->SymbolsOffset);
        if(Program->LocalPredicateCount)
        {
          Program->LocalMasks = KBTS__POINTER_OFFSET(void, Result, Temp->LocalMasksOffset);
          kbts_u32 *Ids = KBTS__POINTER_OFFSET(kbts_u32, Result, Temp->PredicateIdsOffset);
          Result->PredicateIds[Temp->DenseIndex] = Ids;
          KBTS_MEMCPY(Ids, Temp->LocalPredicates, Program->LocalPredicateCount * sizeof(kbts_u32));
          for(kbts_u32 Symbol = 0; Symbol < SymbolCount; ++Symbol)
          {
            kbts_u32 Mask = 1;
            for(kbts_u32 Local = 1; Local < Program->LocalPredicateCount; ++Local)
              Mask |= (kbts_u32)kbts__ContextMember(Predicates, Temp->LocalPredicates[Local], Symbol, Representatives) << Local;
            kbts__SetCompiledIndex(Program->LocalMasks, Program->LocalMaskWidth, Symbol, Mask);
          }
        }
        KBTS_MEMSET(Program->Outcomes, 0xff, Program->CandidateCount * Program->OutcomeWidth);
        for(kbts__context_rule *Rule = Temp->First; Rule; Rule = Rule->Next) Result->Outcomes[Rule->OutcomeIndex] = Rule->Outcome;
        kbts_u32 Candidate = 0, Bucket = 1;
        for(kbts_u32 Symbol = 0; Symbol < SymbolCount; ++Symbol)
        {
          kbts_u32 First = Candidate;
          for(kbts__context_rule *Rule = Temp->First; Rule; Rule = Rule->Next)
          {
            if(!kbts__ContextMember(Predicates, Rule->Tests[0].Predicate, Symbol, Representatives)) continue;
            kbts__SetCompiledOutcomeIndex(Program, Candidate, Rule->OutcomeIndex);
            for(kbts_u32 Stage = 1; Stage < Rule->TestCount; ++Stage)
            {
              kbts_un Index = (kbts_un)(Stage - 1) * Program->CandidateCount + Candidate;
              kbts__compiled_wide_test Test = Rule->Tests[Stage];
              if(Test.Probe < 0)
                Program->Buckets[Bucket].Backtrack = KBTS__MAX(Program->Buckets[Bucket].Backtrack, (kbts_u32)-Test.Probe);
              else
                Program->Buckets[Bucket].Ahead = KBTS__MAX(Program->Buckets[Bucket].Ahead, (kbts_u32)Test.Probe);
              if(Program->LocalPredicateCount)
              {
                kbts_u32 Local = 0;
                while(Temp->LocalPredicates[Local] != Test.Predicate) ++Local;
                Test.Predicate = Local;
              }
              if(Program->Wide) ((kbts__compiled_wide_test *)Program->Tests)[Index] = Test;
              else
              {
                kbts__compiled_test *Packed = &((kbts__compiled_test *)Program->Tests)[Index];
                Packed->Probe = (kbts_s8)Test.Probe;
                Packed->Predicate = (kbts_u8)Test.Predicate;
              }
            }
            ++Candidate;
          }
          if(Candidate != First)
          {
            kbts__SetCompiledBucketIndex(Program, Symbol, Bucket);
            Program->Buckets[Bucket].First = First;
            Program->Buckets[Bucket].Count = Candidate - First;
            ++Bucket;
          }
        }
      }
    }
  }
  kbts__FreeArena(&Builder.Arena);
  if(!Result) Font->Error = Builder.Error == 2 ? KBTS_LOAD_FONT_ERROR_INVALID_FONT : KBTS_LOAD_FONT_ERROR_OUT_OF_MEMORY;
  return Result;
}

#undef kbts__ContextArray
