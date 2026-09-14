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
  struct kbts__context_program *Owner;
  kbts_u32 *Symbols;
  kbts_u16 *FusionGlyphs;
  kbts_b32 Dispatched;
} kbts__context_rule;

typedef struct kbts__context_candidate
{
  struct kbts__context_candidate *Next;
  kbts__context_rule *Rule;
} kbts__context_candidate;

typedef struct kbts__context_dispatch
{
  kbts__context_candidate *First, *Last;
  kbts__compiled_bucket Bucket;
} kbts__context_dispatch;

typedef struct kbts__context_program
{
  kbts__compiled_program Program;
  kbts__context_rule *First, *Last;
  kbts__context_dispatch *Dispatch;
  kbts_u32 LocalPredicates[32];
  kbts_u32 DenseIndex, Parent;
} kbts__context_program;

typedef struct kbts__context_group
{
  struct kbts__context_group *Next;
  kbts__coverage *Coverage;
  kbts_u16 *Classes;
  kbts_u32 Kind, ValueCount;
  kbts_u32 *Heads, *NextGlyph;
  kbts_u32 *Bits;
  kbts_u16 *CoverageGlyphs;
} kbts__context_group;

typedef struct kbts__context_builder
{
  kbts_arena Arena;
  kbts__context_predicate *Predicates, *LastPredicate;
  kbts__context_predicate **PredicateHash;
  kbts__context_source **SourceHash;
  kbts_u32 HashCount, SourceCount, PredicateCount, OutcomeCount;
  kbts_u32 GlyphCount, GlyphWords;
  kbts_u32 *Bits;
  kbts__context_group *Groups;
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

/* Coverage indices borrow format 1 or normalize format 2 once. Materialize a
 * membership set only for whole-coverage/intersection consumers, not index-only
 * rules. Class families include implicit zero independently of intersections. */
static kbts__context_group *kbts__ContextGroup(kbts__context_builder *Builder,
    kbts_u32 Kind, kbts__coverage *Coverage, kbts_u16 *Classes)
{
  kbts__context_group *Group = Builder->Groups;
  while(Group && (Group->Kind != Kind || Group->Coverage != Coverage || Group->Classes != Classes)) Group = Group->Next;
  if(Group || Builder->Error) return Group;
  Group = kbts__ContextArray(Builder, kbts__context_group, 1);
  if(Builder->Error) return 0;
  Group->Kind = Kind;
  Group->Coverage = Coverage;
  Group->Classes = Classes;
  if(Coverage)
  {
    const kbts_u16 *Table = (const kbts_u16 *)Coverage;
    if(Table[0] == 1)
    {
      Group->ValueCount = Table[1];
      Group->CoverageGlyphs = (kbts_u16 *)(Table + 2);
    }
    else
    {
      for(kbts_u32 R = 0; R < Table[1]; ++R)
        Group->ValueCount += (kbts_u32)Table[2 + R * 3 + 1] - Table[2 + R * 3] + 1;
      Group->CoverageGlyphs = kbts__ContextArray(Builder, kbts_u16, Group->ValueCount);
      if(Builder->Error) return 0;
      for(kbts_u32 R = 0; R < Table[1]; ++R)
      {
        const kbts_u16 *Range = Table + 2 + R * 3;
        for(kbts_u32 Glyph = Range[0]; Glyph <= Range[1]; ++Glyph)
          Group->CoverageGlyphs[Range[2] + Glyph - Range[0]] = (kbts_u16)Glyph;
      }
    }
  }
  else
  {
    kbts_u32 *Next = kbts__ContextArray(Builder, kbts_u32, Builder->GlyphCount);
    if(Builder->Error) return 0;
    Group->ValueCount = 1;
    for(kbts_u32 Glyph = 0; Glyph < Builder->GlyphCount; ++Glyph)
    {
      kbts_u32 Class = Classes ? kbts__GlyphClassFromTable(Classes, Glyph).Class : 0;
      Next[Glyph] = Class;
      Group->ValueCount = KBTS__MAX(Group->ValueCount, Class + 1);
    }
    Group->Heads = kbts__ContextArray(Builder, kbts_u32, Group->ValueCount);
    if(Builder->Error) return 0;
    KBTS_MEMSET(Group->Heads, 0xff, Group->ValueCount * sizeof(kbts_u32));
    for(kbts_u32 Glyph = 0; Glyph < Builder->GlyphCount; ++Glyph)
    {
      kbts_u32 Class = Next[Glyph];
      Next[Glyph] = Group->Heads[Class];
      Group->Heads[Class] = Glyph;
    }
    Group->NextGlyph = Next;
  }
  Group->Next = Builder->Groups;
  Builder->Groups = Group;
  return Group;
}

static kbts_u32 *kbts__ContextCoverageBits(kbts__context_builder *Builder, kbts__context_group *Group)
{
  if(!Group->Bits)
  {
    Group->Bits = kbts__ContextArray(Builder, kbts_u32, Builder->GlyphWords);
    if(Builder->Error) return 0;
    for(kbts_u32 I = 0; I < Group->ValueCount; ++I)
    {
      kbts_u32 Glyph = Group->CoverageGlyphs[I];
      Group->Bits[Glyph >> 5] |= 1u << (Glyph & 31);
    }
  }
  return Group->Bits;
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

  kbts_b32 WholeCoverage = Kind == 1 || (Kind == 3 && !Coverage);
  if(!WholeCoverage) KBTS_MEMSET(Builder->Bits, 0, Builder->GlyphWords * sizeof(kbts_u32));
  if(Kind == 0)
  {
    if(Value < Builder->GlyphCount) Builder->Bits[Value >> 5] |= 1u << (Value & 31);
  }
  else
  {
    kbts__context_group *Covered = Coverage ? kbts__ContextGroup(Builder, 3, Coverage, 0) : 0;
    kbts__context_group *Classed = Kind == 2 ? kbts__ContextGroup(Builder, 2, 0, Classes) : 0;
    kbts_u32 *CoverageBits = Covered && Kind != 3 ? kbts__ContextCoverageBits(Builder, Covered) : 0;
    if(Builder->Error) return 0;
    if(WholeCoverage)
    {
      if(Covered) KBTS_MEMCPY(Builder->Bits, CoverageBits, Builder->GlyphWords * sizeof(kbts_u32));
      else
      {
        KBTS_MEMSET(Builder->Bits, 0xff, Builder->GlyphWords * sizeof(kbts_u32));
        if(Builder->GlyphCount & 31) Builder->Bits[Builder->GlyphWords - 1] &= (1u << (Builder->GlyphCount & 31)) - 1;
      }
    }
    else if(Kind == 3)
    {
      if(Value < Covered->ValueCount)
      {
        kbts_u32 Glyph = Covered->CoverageGlyphs[Value];
        Builder->Bits[Glyph >> 5] |= 1u << (Glyph & 31);
      }
    }
    else if(Value < Classed->ValueCount)
      for(kbts_u32 Glyph = Classed->Heads[Value]; Glyph != (kbts_u32)-1; Glyph = Classed->NextGlyph[Glyph])
        if(!Covered || (CoverageBits[Glyph >> 5] & (1u << (Glyph & 31))))
          Builder->Bits[Glyph >> 5] |= 1u << (Glyph & 31);
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
  Rule->Owner = Program;
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

/* One scratch model survives collection, proofs, and final-byte emission.
 * Logical rules retain their global predicates, parent, outcome, and priority;
 * dispatch nodes are non-owning views, never serialized runtime candidates. */
typedef struct kbts__context_model
{
  kbts_font *Font;
  kbts__context_program *Programs;
  kbts__context_rule **Rules;
  kbts__compiled_lookup *Lookups;
  kbts_u32 ProgramCount, DenseCount, SymbolCount, SymbolWordCount;
  kbts_u32 *Symbols, *PredicateMasks, *GlyphHeads, *GlyphNext;
  kbts_u16 *Pages, PageDirectory[256];
  kbts_u32 PageCount;
} kbts__context_model;

static int kbts__ContextMaskMember(const kbts__context_model *Model, kbts_u32 Predicate, kbts_u32 Symbol)
{
  return !!(Model->PredicateMasks[(kbts_un)Predicate * Model->SymbolWordCount + (Symbol >> 5)] & (1u << (Symbol & 31)));
}

static void kbts__CollectContexts(kbts__context_builder *Builder, kbts__context_model *Model, kbts_font *Font)
{
  Model->Font = Font;
  Builder->GlyphCount = Font->Blob->GlyphCount;
  Builder->GlyphWords = (Builder->GlyphCount + 31) / 32;
  Builder->PredicateCount = 1; /* Tautological padding is not a real glyph set. */
  if(Builder->GlyphCount > 65535 || !Font->Tables) {Builder->Error = 2; return;}
  Model->ProgramCount = Font->Tables->SubtableCount;
  Model->Programs = kbts__ContextArray(Builder, kbts__context_program, Model->ProgramCount);
  Model->Lookups = kbts__ContextArray(Builder, kbts__compiled_lookup, Font->Tables->LookupCount);
  Builder->Bits = kbts__ContextArray(Builder, kbts_u32, Builder->GlyphWords);
  if(Builder->Error) return;
  for(kbts_u32 L = 0; L < Font->Tables->LookupCount && !Builder->Error; ++L)
  {
    const kbts__font_lookup *Lookup = &Font->Tables->Lookups[L];
    kbts_u32 ContextType = L < Font->Blob->GposLookupIndexOffset ? 5 : 7;
    Model->Lookups[L].WindowCapacity = 9;
    for(kbts_u32 S = 0; S < Lookup->Packed->SubtableCount && !Builder->Error; ++S)
    {
      const kbts__font_subtable *Subtable = &Font->Tables->Subtables[Lookup->FirstSubtable + S];
      kbts__context_program *Program = &Model->Programs[Lookup->FirstSubtable + S];
      Program->Parent = L;
      // ANSWER Does this run before everything? Either way, a 4 way nested loop is a smell.
      // What is the source and result here? we loop through all tables in teh font, then all lists in each table then subtables? then lookup types within those?
      // Font compilation only: flat checked lookup/subtable views become retained logical rules.
      if(Subtable->Type == ContextType || Subtable->Type == ContextType + 1)
        kbts__ContextCompileSubtable(Builder, Program, (kbts_u16 *)Subtable->Data, Subtable->Type != ContextType);
    }
  }
  kbts_u32 Domain = Builder->GlyphCount + 1;
  kbts_u32 *Symbols = kbts__ContextArray(Builder, kbts_u32, Builder->GlyphCount);
  kbts_u32 *FalseSymbols = kbts__ContextArray(Builder, kbts_u32, Domain);
  kbts_u32 *TrueSymbols = kbts__ContextArray(Builder, kbts_u32, Domain);
  kbts_u32 *Representatives = kbts__ContextArray(Builder, kbts_u32, Domain);
  kbts__context_predicate **Predicates = kbts__ContextArray(Builder, kbts__context_predicate *, Builder->PredicateCount);
  kbts_u32 SymbolCount = 1;
  if(!Builder->Error)
  {
    /* Refine exact membership classes; keep the all-false boundary class at zero. */
    for(kbts__context_predicate *Predicate = Builder->Predicates; Predicate; Predicate = Predicate->Next)
    {
      Predicates[Predicate->Id] = Predicate;
      KBTS_MEMSET(FalseSymbols, 0xff, SymbolCount * sizeof(kbts_u32));
      KBTS_MEMSET(TrueSymbols, 0xff, SymbolCount * sizeof(kbts_u32));
      FalseSymbols[0] = 0;
      kbts_u32 NextCount = 1;
      for(kbts_u32 Glyph = 0; Glyph < Builder->GlyphCount; ++Glyph)
      {
        kbts_u32 *Map = ((Predicate->Bits[Glyph >> 5] >> (Glyph & 31)) & 1) ? TrueSymbols : FalseSymbols;
        kbts_u32 Old = Symbols[Glyph];
        if(Map[Old] == (kbts_u32)-1) Map[Old] = NextCount++;
        Symbols[Glyph] = Map[Old];
      }
      SymbolCount = NextCount;
    }
    for(kbts_u32 Glyph = 0; Glyph < Builder->GlyphCount; ++Glyph) Representatives[Symbols[Glyph]] = Glyph;
  }

  kbts_u16 *Pages = kbts__ContextArray(Builder, kbts_u16, 257 * 256);
  kbts_u16 PageDirectory[256] = {0};
  kbts_u32 PageCount = 1;
  if(!Builder->Error)
  {
    for(kbts_u32 Page = 0; Page < 256; ++Page)
    {
      kbts_u16 *Values = Pages + PageCount * 256;
      for(kbts_u32 Low = 0; Low < 256; ++Low)
      {
        kbts_u32 Glyph = Page * 256 + Low;
        Values[Low] = Glyph < Builder->GlyphCount ? (kbts_u16)Symbols[Glyph] : 0;
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

  if(Builder->Error) return;
  Model->SymbolCount = SymbolCount;
  Model->SymbolWordCount = (SymbolCount + 31) / 32;
  Model->Symbols = Symbols;
  Model->Pages = Pages;
  Model->PageCount = PageCount;
  KBTS_MEMCPY(Model->PageDirectory, PageDirectory, sizeof(PageDirectory));
  Model->Rules = kbts__ContextArray(Builder, kbts__context_rule *, Builder->OutcomeCount);
  Model->PredicateMasks = kbts__ContextArray(Builder, kbts_u32, (kbts_un)Builder->PredicateCount * Model->SymbolWordCount);
  Model->GlyphHeads = kbts__ContextArray(Builder, kbts_u32, SymbolCount);
  Model->GlyphNext = kbts__ContextArray(Builder, kbts_u32, Builder->GlyphCount);
  if(Builder->Error) return;
  for(kbts_u32 W = 0; W < Model->SymbolWordCount; ++W) Model->PredicateMasks[W] = (kbts_u32)-1;
  for(kbts_u32 P = 1; P < Builder->PredicateCount; ++P)
    for(kbts_u32 S = 1; S < SymbolCount; ++S)
      if(kbts__ContextMember(Predicates, P, S, Representatives))
        Model->PredicateMasks[(kbts_un)P * Model->SymbolWordCount + (S >> 5)] |= 1u << (S & 31);
  KBTS_MEMSET(Model->GlyphHeads, 0xff, SymbolCount * sizeof(kbts_u32));
  for(kbts_u32 G = 0; G < Builder->GlyphCount; ++G)
  {
    Model->GlyphNext[G] = Model->GlyphHeads[Symbols[G]];
    Model->GlyphHeads[Symbols[G]] = G;
  }
  Model->DenseCount = 1;
  kbts_u32 *TautologySymbols = 0;
  for(kbts_u32 P = 0; P < Model->ProgramCount && !Builder->Error; ++P)
  {
    kbts__context_program *Temp = &Model->Programs[P];
    kbts__compiled_program *Program = &Temp->Program;
    if(!Temp->First) continue;
    Temp->DenseIndex = Model->DenseCount++;
    Temp->Dispatch = kbts__ContextArray(Builder, kbts__context_dispatch, SymbolCount);
    if(Builder->Error) return;
    if(Program->StageCount > 1) --Program->StageCount;
    for(kbts__context_rule *Rule = Temp->First; Rule; Rule = Rule->Next)
    {
      Model->Rules[Rule->OutcomeIndex] = Rule;
      /* Dispatch membership is exactly the first global predicate. Share its
       * already-built domain instead of an outcome × symbol matrix. Padding
       * predicate zero needs only one bounded, shared dispatch domain. */
      kbts_u32 First = Rule->Tests[0].Predicate;
      Rule->Symbols = Model->PredicateMasks + (kbts_un)First * Model->SymbolWordCount;
      if(!First)
      {
        if(!TautologySymbols)
        {
          TautologySymbols = kbts__ContextArray(Builder, kbts_u32, Model->SymbolWordCount);
          if(Builder->Error) return;
          KBTS_MEMSET(TautologySymbols, 0xff, Model->SymbolWordCount * sizeof(kbts_u32));
          if(SymbolCount & 31) TautologySymbols[Model->SymbolWordCount - 1] &= (1u << (SymbolCount & 31)) - 1;
        }
        Rule->Symbols = TautologySymbols;
      }
    }
    if(Builder->Error) return;
    kbts_u32 Candidates = 0, Buckets = 1;
    for(kbts_u32 Symbol = 0; Symbol < SymbolCount && !Builder->Error; ++Symbol)
    {
      kbts__context_dispatch *Dispatch = &Temp->Dispatch[Symbol];
      Dispatch->Bucket.First = Candidates;
      for(kbts__context_rule *Rule = Temp->First; Rule; Rule = Rule->Next)
      {
        if(!kbts__ContextMaskMember(Model, Rule->Tests[0].Predicate, Symbol)) continue;
        if(Candidates == (kbts_u32)-1) {Builder->Error = 1; break;}
        kbts__context_candidate *Candidate = kbts__ContextArray(Builder, kbts__context_candidate, 1);
        if(Builder->Error) return;
        Candidate->Rule = Rule;
        if(Dispatch->Last) Dispatch->Last->Next = Candidate;
        else Dispatch->First = Candidate;
        Dispatch->Last = Candidate;
        Rule->Dispatched = 1;
        ++Candidates;
        ++Dispatch->Bucket.Count;
        for(kbts_u32 T = 1; T < Rule->TestCount; ++T)
        {
          kbts_s32 Probe = Rule->Tests[T].Probe;
          if(Probe < 0) Dispatch->Bucket.Backtrack = KBTS__MAX(Dispatch->Bucket.Backtrack, (kbts_u32)-Probe);
          else Dispatch->Bucket.Ahead = KBTS__MAX(Dispatch->Bucket.Ahead, (kbts_u32)Probe);
        }
      }
      if(Dispatch->Bucket.Count) ++Buckets;
    }
    if(Candidates > (kbts_u32)-1 - 7) {Builder->Error = 1; return;}
    Program->CandidateCount = (Candidates + 7) & ~7u;
    Program->BucketCount = Buckets;
    Program->SymbolBucketWidth = Buckets <= 256 ? 1 : Buckets <= 65536 ? 2 : 4;
    Program->OutcomeWidth = Builder->OutcomeCount <= 65535 ? 2 : 4;
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
    Program->LocalMaskWidth = (kbts_u8)(LocalFits ? (LocalCount <= 4 ? 0 : LocalCount <= 8 ? 1 : LocalCount <= 16 ? 2 : 4) : 0);
    for(kbts__context_rule *Rule = Temp->First; Rule; Rule = Rule->Next)
      for(kbts_u32 T = 1; T < Rule->TestCount; ++T)
        if((!LocalFits && Rule->Tests[T].Predicate > 255) || Rule->Tests[T].Probe < -128 || Rule->Tests[T].Probe > 127) Program->Wide = 1;
    kbts_un Window = (kbts_un)Program->MaxBacktrack + Program->MaxAhead + 9;
    if(Window > (kbts_u32)-1) {Builder->Error = 1; return;}
    Model->Lookups[Temp->Parent].WindowCapacity = KBTS__MAX(Model->Lookups[Temp->Parent].WindowCapacity, (kbts_u32)Window);
  }
}
