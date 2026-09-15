/* Proofs consume the retained global-predicate rules and ordered dispatch
 * views. No compact matcher data is decoded during construction. */
typedef struct kbts__fusion_domains
{
  kbts_u32 *Bits;
  kbts_s32 First, Last;
} kbts__fusion_domains;

typedef struct kbts__fusion_workspace
{
  kbts_u32 **ChildSymbols;
  kbts_u32 *VerdictEpochs;
  kbts_s8 *Verdicts;
  kbts_u32 Epoch;
} kbts__fusion_workspace;

static kbts_u32 kbts__ContextGlyphSymbol(const kbts__context_model *Model, kbts_u32 Glyph)
{
  return Glyph < Model->Font->Blob->GlyphCount ? Model->Symbols[Glyph] : 0;
}

/* Cache only requested child/symbol mappings. The first request proves native
 * first-success substitution over that symbol's real glyph members; subsequent
 * outcomes reuse it. Boundary symbol zero remains explicitly ineligible. */
static int kbts__FusionConstant(kbts__context_builder *Builder, const kbts__context_model *Model,
    kbts__fusion_workspace *Work, const kbts__compiled_outcome *Outcome,
    const kbts_u32 *Domain, kbts_u16 *Replacement)
{
  if(Outcome->InputCount != 1 || Outcome->RecordCount != 1 ||
     Outcome->Records[0].SequenceIndex || (Domain[0] & 1)) return 0;
  kbts_u32 Child = Outcome->Records[0].LookupListIndex;
  if(Child >= Model->Font->Blob->GposLookupIndexOffset) return 0;
  const kbts__font_lookup *Lookup = &Model->Font->Tables->Lookups[Child];
  if(Lookup->Packed->Flag || !Lookup->Packed->SubtableCount || Lookup->Filter.Type != 1) return 0;
  kbts_u32 *Mapped = Work->ChildSymbols[Child];
  if(!Mapped)
  {
    Mapped = kbts__ContextArray(Builder, kbts_u32, Model->SymbolCount);
    if(Builder->Error) return 0;
    KBTS_MEMSET(Mapped, 0xff, Model->SymbolCount * sizeof(kbts_u32));
    Work->ChildSymbols[Child] = Mapped;
  }
  int HaveReplacement = 0;
  for(kbts_u32 Symbol = 1; Symbol < Model->SymbolCount; ++Symbol)
  {
    if(!(Domain[Symbol >> 5] & (1u << (Symbol & 31)))) continue;
    if(Mapped[Symbol] == (kbts_u32)-1)
    {
      kbts_u32 Value = (kbts_u32)-1;
      for(kbts_u32 Glyph = Model->GlyphHeads[Symbol]; Glyph != (kbts_u32)-1; Glyph = Model->GlyphNext[Glyph])
      {
        kbts_u32 NewGlyph = (kbts_u32)-2;
        for(kbts_u32 S = 0; S < Lookup->Packed->SubtableCount; ++S)
        {
          const kbts__font_subtable *View = &Model->Font->Tables->Subtables[Lookup->FirstSubtable + S];
          kbts__single_substitution *Subst = (kbts__single_substitution *)View->Data;
          kbts__coverage *Coverage = KBTS__POINTER_OFFSET(kbts__coverage, Subst, Subst->CoverageOffset);
          kbts__cover_glyph_result Cover = kbts__CoverGlyph(Coverage, (kbts_u16)Glyph);
          if(!Cover.Valid) continue;
          if(Subst->Format == 1) NewGlyph = (kbts_u16)(Glyph + (kbts_u32)Subst->DeltaOrCount.DeltaGlyphId);
          else if(Subst->Format == 2 && Cover.Index < Subst->DeltaOrCount.GlyphCount)
            NewGlyph = KBTS__POINTER_AFTER(kbts_u16, Subst)[Cover.Index];
          break;
        }
        if(NewGlyph == (kbts_u32)-2 || (Value != (kbts_u32)-1 && Value != NewGlyph)) {Value = (kbts_u32)-2; break;}
        Value = NewGlyph;
      }
      Mapped[Symbol] = Value == (kbts_u32)-1 ? (kbts_u32)-2 : Value;
    }
    if(Mapped[Symbol] == (kbts_u32)-2 || (HaveReplacement && *Replacement != Mapped[Symbol])) return 0;
    *Replacement = (kbts_u16)Mapped[Symbol];
    HaveReplacement = 1;
  }
  return HaveReplacement;
}

static kbts_u32 *kbts__FusionDomain(const kbts__context_model *Cache,
                                   const kbts__fusion_domains *Domains, kbts_s32 Position)
{
  if(Position < Domains->First || Position > Domains->Last) return 0;
  return Domains->Bits + (kbts_un)(Position - Domains->First) * Cache->SymbolWordCount;
}

/* Three-valued membership: impossible, guaranteed, or unresolved. Unknown
 * positions include the boundary symbol, just like untouched tracked slots. */
static int kbts__FusionPredicate(const kbts__context_model *Cache,
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

/* Distinct proof dimensions are live position, possible dispatch symbol,
 * native subtable/rule priority, and global predicates. Nonanchor membership
 * depends only on position/domain/rule, so evaluate it once per winner query,
 * not again for every dispatch-symbol copy of the same logical rule. */
static kbts_u32 kbts__FusionWinner(const kbts__context_model *Model,
    kbts__fusion_workspace *Work, const kbts__fusion_domains *Domains,
    kbts_s32 Position, kbts_u32 FirstProgram, kbts_u32 ProgramCount)
{
  const kbts_u32 *Current = kbts__FusionDomain(Model, Domains, Position);
  kbts_u32 Winner = (kbts_u32)-1;
  if(!Current || (Current[0] & 1)) return Winner;
  if(!++Work->Epoch)
  {
    /* No stale verdict survives epoch wrap. */
    for(kbts_u32 P = 0; P < Model->ProgramCount; ++P)
      for(kbts__context_rule *R = Model->Programs[P].First; R; R = R->Next) Work->VerdictEpochs[R->OutcomeIndex] = 0;
    Work->Epoch = 1;
  }
  for(kbts_u32 Symbol = 1; Symbol < Model->SymbolCount; ++Symbol)
  {
    if(!(Current[Symbol >> 5] & (1u << (Symbol & 31)))) continue;
    kbts_u32 Selected = (kbts_u32)-1;
    for(kbts_u32 P = FirstProgram; P < FirstProgram + ProgramCount && Selected == (kbts_u32)-1; ++P)
    {
      const kbts__context_program *Program = &Model->Programs[P];
      if(!Program->Program.CandidateCount) continue;
      if(Program->Program.FirstFiltered) return (kbts_u32)-1;
      for(kbts__context_candidate *C = Program->Dispatch[Symbol].First; C; C = C->Next)
      {
        kbts__context_rule *Rule = C->Rule;
        kbts_u32 O = Rule->OutcomeIndex;
        if(Work->VerdictEpochs[O] != Work->Epoch)
        {
          int Verdict = 1;
          for(kbts_u32 T = 1; T < Rule->TestCount; ++T)
          {
            kbts__compiled_wide_test Test = Rule->Tests[T];
            if(!Test.Predicate || !Test.Probe) continue;
            int Match = kbts__FusionPredicate(Model, kbts__FusionDomain(Model, Domains, Position + Test.Probe), Test.Predicate);
            if(!Match) {Verdict = 0; break;}
            if(Match < 0) Verdict = -1;
          }
          Work->Verdicts[O] = (kbts_s8)Verdict;
          Work->VerdictEpochs[O] = Work->Epoch;
        }
        int Match = Work->Verdicts[O];
        if(!Match) continue;
        for(kbts_u32 T = 1; T < Rule->TestCount; ++T)
          if(!Rule->Tests[T].Probe && !kbts__ContextMaskMember(Model, Rule->Tests[T].Predicate, Symbol)) {Match = 0; break;}
        if(!Match) continue;
        if(Match < 0) return (kbts_u32)-1;
        Selected = O;
        break;
      }
    }
    if(Selected == (kbts_u32)-1 || (Winner != (kbts_u32)-1 && Winner != Selected)) return (kbts_u32)-1;
    Winner = Selected;
  }
  return Winner;
}

static void kbts__FuseContexts(kbts__context_builder *Builder, kbts__context_model *Model,
    kbts_u32 *FusionCount, kbts_u32 *FusedOutcomes, kbts_u32 *FusedChains)
{
  kbts__fusion_workspace Work = KBTS__ZERO;
  Work.ChildSymbols = kbts__ContextArray(Builder, kbts_u32 *, Model->Font->Blob->GposLookupIndexOffset);
  Work.VerdictEpochs = kbts__ContextArray(Builder, kbts_u32, Builder->OutcomeCount);
  Work.Verdicts = kbts__ContextArray(Builder, kbts_s8, Builder->OutcomeCount);
  kbts_u32 MaxBacktrack = 0, MaxAhead = 0;
  for(kbts_u32 P = 0; P < Model->ProgramCount; ++P)
  {
    const kbts__context_program *Temp = &Model->Programs[P];
    const kbts__font_lookup *Parent = &Model->Font->Tables->Lookups[Temp->Parent];
    if(Temp->Parent >= Model->Font->Blob->GposLookupIndexOffset || Parent->Packed->Flag ||
       (Parent->Filter.Type != 5 && Parent->Filter.Type != 6) || Temp->Program.FirstFiltered) continue;
    MaxBacktrack = KBTS__MAX(MaxBacktrack, Temp->Program.MaxBacktrack);
    MaxAhead = KBTS__MAX(MaxAhead, Temp->Program.MaxAhead);
  }
  kbts_u32 *DomainBits = kbts__ContextArray(Builder, kbts_u32, ((kbts_un)MaxBacktrack + MaxAhead + 1) * Model->SymbolWordCount);
  kbts_u16 *Outputs = kbts__ContextArray(Builder, kbts_u16, (kbts_un)MaxAhead + 1);
  for(kbts_u32 O = 0; O < Builder->OutcomeCount && !Builder->Error; ++O)
  {
    kbts__context_rule *Rule = Model->Rules[O];
    kbts__context_program *Temp = Rule->Owner;
    const kbts__font_lookup *Parent = &Model->Font->Tables->Lookups[Temp->Parent];
    kbts__compiled_outcome *Outcome = &Rule->Outcome;
    if(!Rule->Dispatched || Temp->Parent >= Model->Font->Blob->GposLookupIndexOffset ||
       Parent->Packed->Flag || (Parent->Filter.Type != 5 && Parent->Filter.Type != 6) ||
       Temp->Program.FirstFiltered || Outcome->InputCount != 1 || Outcome->RecordCount != 1) continue;
    kbts__fusion_domains Domains = KBTS__ZERO;
    Domains.Bits = DomainBits;
    for(kbts_u32 T = 1; T < Rule->TestCount; ++T)
    {
      kbts__compiled_wide_test Test = Rule->Tests[T];
      if(!Test.Predicate) continue;
      Domains.First = KBTS__MIN(Domains.First, Test.Probe);
      Domains.Last = KBTS__MAX(Domains.Last, Test.Probe);
    }
    kbts_un Positions = (kbts_un)(Domains.Last - Domains.First) + 1;
    KBTS_MEMSET(DomainBits, 0xff, Positions * Model->SymbolWordCount * sizeof(kbts_u32));
    if(Model->SymbolCount & 31)
      for(kbts_un P = 0; P < Positions; ++P)
        DomainBits[(P + 1) * Model->SymbolWordCount - 1] &= (1u << (Model->SymbolCount & 31)) - 1;
    KBTS_MEMCPY(kbts__FusionDomain(Model, &Domains, 0), Rule->Symbols, Model->SymbolWordCount * sizeof(kbts_u32));
    for(kbts_u32 T = 1; T < Rule->TestCount; ++T)
    {
      kbts__compiled_wide_test Test = Rule->Tests[T];
      if(!Test.Predicate) continue;
      kbts_u32 *Domain = kbts__FusionDomain(Model, &Domains, Test.Probe);
      const kbts_u32 *Mask = Model->PredicateMasks + (kbts_un)Test.Predicate * Model->SymbolWordCount;
      for(kbts_u32 W = 0; W < Model->SymbolWordCount; ++W) Domain[W] &= Mask[W];
    }
    kbts_u32 Count = 0, Selected = O;
    for(kbts_s32 Position = 0; Position <= Domains.Last && !Builder->Error; ++Position)
    {
      kbts_u32 *Domain = kbts__FusionDomain(Model, &Domains, Position);
      kbts_u16 Replacement = 0;
      if(!kbts__FusionConstant(Builder, Model, &Work, &Model->Rules[Selected]->Outcome, Domain, &Replacement)) break;
      Outputs[Count++] = Replacement;
      KBTS_MEMSET(Domain, 0, Model->SymbolWordCount * sizeof(kbts_u32));
      kbts_u32 Symbol = kbts__ContextGlyphSymbol(Model, Replacement);
      Domain[Symbol >> 5] = 1u << (Symbol & 31);
      if(Position == Domains.Last) break;
      Selected = kbts__FusionWinner(Model, &Work, &Domains, Position + 1, Parent->FirstSubtable, Parent->Packed->SubtableCount);
      if(Selected == (kbts_u32)-1) break;
    }
    if(!Count || Builder->Error) continue;
    if(Count > (kbts_u32)-1 - *FusionCount) {Builder->Error = 1; return;}
    Rule->FusionGlyphs = kbts__ContextArray(Builder, kbts_u16, Count);
    if(Builder->Error) return;
    KBTS_MEMCPY(Rule->FusionGlyphs, Outputs, Count * sizeof(kbts_u16));
    Outcome->FusionOffset = *FusionCount;
    Outcome->FusionCount = Count;
    *FusionCount += Count;
    ++*FusedOutcomes;
    if(Count > 1) ++*FusedChains;
  }
}

static void kbts__ContextSetSymbol(kbts_u32 *Bits, kbts_u32 Symbol)
{
  Bits[Symbol >> 5] |= 1u << (Symbol & 31);
}

static void kbts__AnalyzeLookups(kbts__context_builder *Builder, kbts__context_model *Model)
{
  kbts_u32 LookupCount = Model->Font->Tables->LookupCount;
  kbts_u32 *ChildEpochs = kbts__ContextArray(Builder, kbts_u32, LookupCount);
  kbts_u32 *Children = kbts__ContextArray(Builder, kbts_u32, LookupCount);
  kbts_u32 PredicateWords = (Builder->PredicateCount - 1u) / 32u + 1u;
  kbts_u32 *Backtrack = kbts__ContextArray(Builder, kbts_u32, PredicateWords);
  kbts_u32 *ReadPredicates = kbts__ContextArray(Builder, kbts_u32, PredicateWords);
  if(Builder->Error) return;
  for(kbts_u32 L = 0; L < LookupCount && !Builder->Error; ++L)
  {
    const kbts__font_lookup *Lookup = &Model->Font->Tables->Lookups[L];
    kbts__compiled_lookup *Summary = &Model->Lookups[L];
    kbts_b32 IsGpos = L >= Model->Font->Blob->GposLookupIndexOffset;
    kbts_u32 TableBase = IsGpos ? Model->Font->Blob->GposLookupIndexOffset : 0;
    kbts_u32 TableEnd = IsGpos ? LookupCount : Model->Font->Blob->GposLookupIndexOffset;
    kbts_u32 ChildCount = 0;
    kbts_b32 Lowerable = !IsGpos && !Lookup->Packed->Flag;
    for(kbts_u32 S = 0; S < Lookup->Packed->SubtableCount; ++S)
    {
      const kbts__context_program *Program = &Model->Programs[Lookup->FirstSubtable + S];
      kbts_u32 Type = Model->Font->Tables->Subtables[Lookup->FirstSubtable + S].Type;
      if((Type != 5 && Type != 6) || Program->Program.FirstFiltered) Lowerable = 0;
      for(kbts__context_rule *Rule = Program->First; Rule; Rule = Rule->Next)
      {
        const kbts__compiled_outcome *Outcome = &Rule->Outcome;
        for(kbts_u32 R = 0; R < Outcome->RecordCount; ++R)
        {
          kbts_u32 Child = Outcome->Records[R].LookupListIndex;
          if(Child >= TableEnd - TableBase) {Builder->Error = 2; return;}
          Child += TableBase;
          if(ChildEpochs[Child] == L + 1) continue;
          ChildEpochs[Child] = L + 1;
          Children[ChildCount++] = Child;
        }
        if(!Rule->Dispatched) continue;
        if(!Outcome->FusionCount ? Outcome->RecordCount != 0 :
            (Outcome->InputCount != 1 || Outcome->RecordCount != 1 || Outcome->Records[0].SequenceIndex || !Rule->FusionGlyphs)) Lowerable = 0;
      }
    }
    Summary->ChildCount = ChildCount;
    if(ChildCount)
    {
      Summary->Children = kbts__ContextArray(Builder, kbts_u32, ChildCount);
      if(Builder->Error) return;
      KBTS_MEMCPY(Summary->Children, Children, ChildCount * sizeof(kbts_u32));
    }
    Summary->Kind = Lowerable ? KBTS__GSUB_SNAPSHOT : KBTS__GSUB_NATIVE;
    if(!Lowerable) continue;
    Summary->ReadSymbols = kbts__ContextArray(Builder, kbts_u32, Model->SymbolWordCount);
    Summary->WriteSymbols = kbts__ContextArray(Builder, kbts_u32, Model->SymbolWordCount);
    Summary->FirstSymbols = kbts__ContextArray(Builder, kbts_u32, Model->SymbolWordCount);
    if(Builder->Error) return;
    KBTS_MEMSET(Backtrack, 0, PredicateWords * sizeof(kbts_u32));
    KBTS_MEMSET(ReadPredicates, 0, PredicateWords * sizeof(kbts_u32));
    for(kbts_u32 S = 0; S < Lookup->Packed->SubtableCount; ++S)
    {
      const kbts__context_program *Program = &Model->Programs[Lookup->FirstSubtable + S];
      for(kbts__context_rule *Rule = Program->First; Rule; Rule = Rule->Next)
      {
        if(!Rule->Dispatched) continue;
        for(kbts_u32 W = 0; W < Model->SymbolWordCount; ++W)
        {
          Summary->FirstSymbols[W] |= Rule->Symbols[W];
          Summary->ReadSymbols[W] |= Rule->Symbols[W];
          if(Rule->Outcome.FusionCount) Summary->WriteSymbols[W] |= Rule->Symbols[W];
        }
        for(kbts_u32 F = 0; F < Rule->Outcome.FusionCount; ++F)
          kbts__ContextSetSymbol(Summary->WriteSymbols, kbts__ContextGlyphSymbol(Model, Rule->FusionGlyphs[F]));
        for(kbts_u32 T = 1; T < Rule->TestCount; ++T)
        {
          kbts__compiled_wide_test Test = Rule->Tests[T];
          if(!Test.Predicate) continue;
          kbts__ContextSetSymbol(ReadPredicates, Test.Predicate);
          if(Test.Probe < 0) kbts__ContextSetSymbol(Backtrack, Test.Predicate);
        }
      }
    }
    for(kbts_u32 W = 0; W < PredicateWords; ++W)
    {
      kbts_u32 Remaining = ReadPredicates[W];
      while(Remaining)
      {
        kbts_u32 P = W * 32 + kbts__LsbPositionOrBitWidth32(Remaining);
        const kbts_u32 *Mask = Model->PredicateMasks + (kbts_un)P * Model->SymbolWordCount;
        for(kbts_u32 B = 0; B < Model->SymbolWordCount; ++B) Summary->ReadSymbols[B] |= Mask[B];
        Remaining &= Remaining - 1;
      }
    }
    /* A source/replacement pair may change a predicate without leaving the
     * aggregate read domain. Preserve the original predicate-membership proof,
     * now once per lookup and distinct backtrack predicate, not per config. */
    for(kbts_u32 S = 0; S < Lookup->Packed->SubtableCount && Summary->Kind == KBTS__GSUB_SNAPSHOT; ++S)
      for(kbts__context_rule *Rule = Model->Programs[Lookup->FirstSubtable + S].First;
          Rule && Summary->Kind == KBTS__GSUB_SNAPSHOT; Rule = Rule->Next)
      {
        if(!Rule->Outcome.FusionCount) continue;
        kbts_u32 Replacement = kbts__ContextGlyphSymbol(Model, Rule->FusionGlyphs[0]);
        for(kbts_u32 Symbol = 0; Symbol < Model->SymbolCount && Summary->Kind == KBTS__GSUB_SNAPSHOT; ++Symbol)
        {
          if(Symbol == Replacement || !(Rule->Symbols[Symbol >> 5] & (1u << (Symbol & 31)))) continue;
          kbts_b32 Possible = 1;
          for(kbts_u32 T = 1; T < Rule->TestCount; ++T)
            if(!Rule->Tests[T].Probe && !kbts__ContextMaskMember(Model, Rule->Tests[T].Predicate, Symbol)) {Possible = 0; break;}
          if(!Possible) continue;
          for(kbts_u32 W = 0; W < PredicateWords; ++W)
          {
            kbts_u32 Remaining = Backtrack[W];
            while(Remaining)
            {
              kbts_u32 P = W * 32 + kbts__LsbPositionOrBitWidth32(Remaining);
              if(kbts__ContextMaskMember(Model, P, Symbol) != kbts__ContextMaskMember(Model, P, Replacement))
              {Summary->Kind = KBTS__GSUB_ORDERED; break;}
              Remaining &= Remaining - 1;
            }
            if(Summary->Kind == KBTS__GSUB_ORDERED) break;
          }
        }
      }
  }
}

/* locl construction reserves the largest legal insertion count before its
 * probe executes. Two depth rows mirror the bounded native action stack; cycles
 * are finite, and saturation lets the public sizing boundary reject overflow. */
static void kbts__AnalyzeExpansion(kbts__context_builder *Builder, kbts__context_model *Model)
{
  kbts_u32 Count = Model->Font->Blob->GposLookupIndexOffset;
  kbts_u32 *Previous = kbts__ContextArray(Builder, kbts_u32, Count);
  kbts_u32 *Current = kbts__ContextArray(Builder, kbts_u32, Count);
  if(Builder->Error) return;
  for(kbts_u32 L = 0; L < Count; ++L)
  {
    const kbts__font_lookup *Lookup = &Model->Font->Tables->Lookups[L];
    if(Lookup->Filter.Type != 2) continue;
    for(kbts_u32 S = 0; S < Lookup->Packed->SubtableCount; ++S)
    {
      const kbts__multiple_substitution *Subst = (const kbts__multiple_substitution *)Model->Font->Tables->Subtables[Lookup->FirstSubtable + S].Data;
      const kbts_u16 *Offsets = KBTS__POINTER_AFTER(kbts_u16, Subst);
      for(kbts_u32 I = 0; I < Subst->SequenceCount; ++I)
      {
        const kbts__sequence *Sequence = KBTS__POINTER_OFFSET(kbts__sequence, Subst, Offsets[I]);
        kbts_u32 Inserted = Sequence->GlyphCount ? Sequence->GlyphCount - 1 : 0;
        Model->Lookups[L].MaxInserted = KBTS__MAX(Model->Lookups[L].MaxInserted, Inserted);
      }
    }
  }
  for(kbts_u32 Depth = 0; Depth < KBTS_LOOKUP_STACK_SIZE; ++Depth)
  {
    kbts_b32 Changed = 0;
    for(kbts_u32 L = 0; L < Count; ++L)
    {
      const kbts__font_lookup *Lookup = &Model->Font->Tables->Lookups[L];
      kbts_u32 Maximum = Model->Lookups[L].MaxInserted;
      for(kbts_u32 S = 0; S < Lookup->Packed->SubtableCount; ++S)
        for(kbts__context_rule *Rule = Model->Programs[Lookup->FirstSubtable + S].First; Rule; Rule = Rule->Next)
        {
          kbts_u32 Inserted = 0;
          for(kbts_u32 R = 0; R < Rule->Outcome.RecordCount; ++R)
          {
            kbts_u32 Child = Rule->Outcome.Records[R].LookupListIndex;
            if(Child >= Count) {Builder->Error = 2; return;}
            kbts_u32 Add = Previous[Child];
            if(Add > (kbts_u32)-1 - Inserted) {Inserted = (kbts_u32)-1; break;}
            Inserted += Add;
          }
          Maximum = KBTS__MAX(Maximum, Inserted);
        }
      Current[L] = Maximum;
      Changed |= Maximum != Previous[L];
    }
    kbts_u32 *Swap = Previous;
    Previous = Current;
    Current = Swap;
    if(!Changed) break;
  }
  for(kbts_u32 L = 0; L < Count; ++L) Model->Lookups[L].MaxInserted = Previous[L];
}

typedef struct kbts__packed_table
{
  struct kbts__packed_table *Next, *HashNext;
  const kbts_u8 *Data;
  kbts_un Size, Offset;
  kbts_u32 Hash, Align;
} kbts__packed_table;

typedef struct kbts__table_interner
{
  kbts__packed_table **Slots, *First;
  kbts_u32 Capacity, Count;
} kbts__table_interner;

/* Hash the exact final bytes as they are emitted, including zero padding.
 * The index keeps this hash through growth; collision equality remains bytes,
 * size, and alignment. Runtime only sees direct resident pointers. */
static void kbts__ContextEmit(void *Destination, const void *Source, kbts_un Size, kbts_u32 *Hash)
{
  kbts_u8 *To = (kbts_u8 *)Destination;
  const kbts_u8 *From = (const kbts_u8 *)Source;
  for(kbts_un I = 0; I < Size; ++I)
  {
    kbts_u8 Byte = From[I];
    To[I] = Byte;
    *Hash = (*Hash ^ Byte) * 16777619u;
  }
}

static kbts_un kbts__PackTable(kbts__context_builder *Builder, kbts__table_interner *Tables,
    kbts_un *Total, const void *Data, kbts_un Size, kbts_u32 Align, kbts_u32 Hash, kbts_un *Bytes)
{
  if(Builder->Error || !Size) return 0;
  kbts_u32 Slot = Tables->Capacity ? Hash & (Tables->Capacity - 1) : 0;
  const kbts_u8 *Input = (const kbts_u8 *)Data;
  for(kbts__packed_table *Table = Tables->Capacity ? Tables->Slots[Slot] : 0; Table; Table = Table->HashNext)
  {
    if(Table->Hash != Hash || Table->Size != Size || Table->Align != Align) continue;
    kbts_un I = 0;
    while(I < Size && Table->Data[I] == Input[I]) ++I;
    if(I == Size) return Table->Offset;
  }
  if(!Tables->Capacity || Tables->Count >= Tables->Capacity)
  {
    if(Tables->Capacity > (kbts_u32)-1 / 2) {Builder->Error = 1; return 0;}
    kbts_u32 Capacity = Tables->Capacity ? Tables->Capacity * 2 : 16;
    kbts__packed_table **Slots = kbts__ContextArray(Builder, kbts__packed_table *, Capacity);
    if(Builder->Error) return 0;
    for(kbts__packed_table *Table = Tables->First; Table; Table = Table->Next)
    {
      kbts_u32 Slot = Table->Hash & (Capacity - 1);
      Table->HashNext = Slots[Slot];
      Slots[Slot] = Table;
    }
    Tables->Slots = Slots;
    Tables->Capacity = Capacity;
  }
  Slot = Hash & (Tables->Capacity - 1);
  kbts__packed_table *Table = kbts__ContextArray(Builder, kbts__packed_table, 1);
  if(Builder->Error) return 0;
  Table->Data = Input;
  Table->Size = Size;
  Table->Hash = Hash;
  Table->Align = Align;
  Table->Offset = kbts__ContextReserve(Builder, Total, Size, 1, Align);
  Table->HashNext = Tables->Slots[Slot];
  Tables->Slots[Slot] = Table;
  Table->Next = Tables->First;
  Tables->First = Table;
  ++Tables->Count;
  *Bytes += Size;
  return Table->Offset;
}

typedef struct kbts__packed_program
{
  kbts_un Tests, Masks, Symbols, Buckets, Outcomes;
} kbts__packed_program;

static kbts_u32 kbts__ContextLocalMask(const kbts__context_model *Model,
    const kbts__context_program *Program, kbts_u32 Symbol)
{
  kbts_u32 Mask = 1;
  for(kbts_u32 P = 1; P < Program->Program.LocalPredicateCount; ++P)
    Mask |= (kbts_u32)kbts__ContextMaskMember(Model, Program->LocalPredicates[P], Symbol) << P;
  return Mask;
}

static kbts__compiled_contexts *kbts__PackCompiledContexts(kbts__context_builder *Builder,
    kbts__context_model *Model, kbts__memory *Persistent,
    kbts_u32 FusionCount, kbts_u32 FusedOutcomes, kbts_u32 FusedChains)
{
  kbts__packed_program *Layout = kbts__ContextArray(Builder, kbts__packed_program, Model->DenseCount);
  kbts__table_interner Tables = KBTS__ZERO;
  kbts_un Total = sizeof(kbts__compiled_contexts);
  kbts_u32 SymbolWidth = Model->SymbolCount <= 256 ? 1 : 2;
  kbts_u32 IndexWidth = Model->DenseCount <= 256 ? 1 : Model->DenseCount <= 65536 ? 2 : 4;
  kbts_un ProgramOffset = kbts__ContextReserve(Builder, &Total, Model->DenseCount, sizeof(kbts__compiled_program), KBTS_ALIGNOF(kbts__compiled_program));
  kbts_un IndexOffset = kbts__ContextReserve(Builder, &Total, Model->ProgramCount, IndexWidth, IndexWidth);
  kbts_un GlyphBytes = (kbts_un)Model->PageCount * 256 * SymbolWidth;
  kbts_un GlyphOffset = kbts__ContextReserve(Builder, &Total, GlyphBytes, 1, SymbolWidth);
  kbts_un PredicateBytes = (kbts_un)Builder->PredicateCount * Model->SymbolWordCount * sizeof(kbts_u32);
  kbts_un PredicateOffset = kbts__ContextReserve(Builder, &Total, PredicateBytes, 1, KBTS_ALIGNOF(kbts_u32));
  kbts_un TestBytes = 0, BucketBytes = 0;
  kbts_u32 WindowCapacity = 9;
  for(kbts_u32 P = 0; P < Model->ProgramCount && !Builder->Error; ++P)
  {
    const kbts__context_program *Temp = &Model->Programs[P];
    const kbts__compiled_program *Program = &Temp->Program;
    if(!Temp->First) continue;
    WindowCapacity = KBTS__MAX(WindowCapacity, Model->Lookups[Temp->Parent].WindowCapacity);
    if(!Program->CandidateCount) continue;
    kbts__packed_program *Packed = &Layout[Temp->DenseIndex];
    kbts_un TestSize = Program->Wide ? sizeof(kbts__compiled_wide_test) : sizeof(kbts__compiled_test);
    kbts_un TestCount = (kbts_un)Program->CandidateCount * Program->StageCount;
    if(!Program->Wide) ++TestCount; /* Final compact gather reads four bytes. */
    void *Tests = kbts__ContextMemory(Builder, TestCount, TestSize, KBTS_ALIGNOF(kbts__compiled_wide_test));
    if(Builder->Error) break;
    kbts_u32 Hash = 2166136261u;
    kbts_un At = 0;
    for(kbts_u32 Stage = 0; Stage < Program->StageCount; ++Stage)
    {
      kbts_u32 Count = 0;
      for(kbts_u32 Symbol = 0; Symbol < Model->SymbolCount; ++Symbol)
        for(kbts__context_candidate *C = Temp->Dispatch[Symbol].First; C; C = C->Next)
        {
          kbts__compiled_wide_test Test = KBTS__ZERO;
          if(Stage + 1 < C->Rule->TestCount) Test = C->Rule->Tests[Stage + 1];
          if(Program->LocalPredicateCount)
          {
            kbts_u32 Local = 0;
            while(Temp->LocalPredicates[Local] != Test.Predicate) ++Local;
            Test.Predicate = Local;
          }
          if(Program->Wide) kbts__ContextEmit((kbts_u8 *)Tests + At, &Test, sizeof(Test), &Hash);
          else
          {
            kbts__compiled_test Narrow;
            Narrow.Probe = (kbts_s8)Test.Probe;
            Narrow.Predicate = (kbts_u8)Test.Predicate;
            kbts__ContextEmit((kbts_u8 *)Tests + At, &Narrow, sizeof(Narrow), &Hash);
          }
          At += TestSize;
          ++Count;
        }
      kbts__compiled_wide_test Zero = KBTS__ZERO;
      while(Count++ < Program->CandidateCount)
      {
        kbts__ContextEmit((kbts_u8 *)Tests + At, &Zero, TestSize, &Hash);
        At += TestSize;
      }
    }
    if(!Program->Wide)
    {
      kbts__compiled_test Zero = KBTS__ZERO;
      kbts__ContextEmit((kbts_u8 *)Tests + At, &Zero, sizeof(Zero), &Hash);
    }
    Packed->Tests = kbts__PackTable(Builder, &Tables, &Total, Tests, TestCount * TestSize,
        KBTS_ALIGNOF(kbts__compiled_wide_test), Hash, &TestBytes);
    kbts__compiled_bucket *Buckets = kbts__ContextArray(Builder, kbts__compiled_bucket, Program->BucketCount);
    void *Symbols = kbts__ContextMemory(Builder, Model->SymbolCount, Program->SymbolBucketWidth, Program->SymbolBucketWidth);
    if(Builder->Error) break;
    kbts_u32 BucketHash = 2166136261u, SymbolHash = 2166136261u, Bucket = 1;
    kbts__compiled_bucket Empty = KBTS__ZERO;
    kbts__ContextEmit(Buckets, &Empty, sizeof(Empty), &BucketHash);
    for(kbts_u32 Symbol = 0; Symbol < Model->SymbolCount; ++Symbol)
    {
      const kbts__context_dispatch *Dispatch = &Temp->Dispatch[Symbol];
      kbts_u32 Index = 0;
      if(Dispatch->Bucket.Count)
      {
        Index = Bucket++;
        kbts__ContextEmit(Buckets + Index, &Dispatch->Bucket, sizeof(Dispatch->Bucket), &BucketHash);
      }
      kbts_u32 Narrow = 0;
      kbts__SetCompiledIndex(&Narrow, Program->SymbolBucketWidth, 0, Index);
      kbts__ContextEmit((kbts_u8 *)Symbols + (kbts_un)Symbol * Program->SymbolBucketWidth,
          &Narrow, Program->SymbolBucketWidth, &SymbolHash);
    }
    Packed->Buckets = kbts__PackTable(Builder, &Tables, &Total, Buckets,
        (kbts_un)Program->BucketCount * sizeof(*Buckets), KBTS_ALIGNOF(kbts__compiled_bucket), BucketHash, &BucketBytes);
    Packed->Symbols = kbts__PackTable(Builder, &Tables, &Total, Symbols,
        (kbts_un)Model->SymbolCount * Program->SymbolBucketWidth, Program->SymbolBucketWidth, SymbolHash, &BucketBytes);
    if(Program->LocalPredicateCount)
    {
      kbts_un Bytes = Program->LocalMaskWidth ? (kbts_un)Model->SymbolCount * Program->LocalMaskWidth : ((kbts_un)Model->SymbolCount + 1) / 2;
      kbts_u32 Align = Program->LocalMaskWidth ? Program->LocalMaskWidth : 1;
      void *Masks = kbts__ContextMemory(Builder, Bytes, 1, Align);
      if(Builder->Error) break;
      Hash = 2166136261u;
      for(kbts_u32 Symbol = 0; Symbol < Model->SymbolCount; ++Symbol)
      {
        kbts_u32 Mask = kbts__ContextLocalMask(Model, Temp, Symbol);
        if(!Program->LocalMaskWidth)
        {
          if(Symbol + 1 < Model->SymbolCount) Mask |= kbts__ContextLocalMask(Model, Temp, Symbol + 1) << 4;
          kbts_u8 Byte = (kbts_u8)Mask;
          kbts__ContextEmit((kbts_u8 *)Masks + Symbol / 2, &Byte, 1, &Hash);
          ++Symbol;
        }
        else
        {
          kbts_u32 Value = 0;
          kbts__SetCompiledIndex(&Value, Program->LocalMaskWidth, 0, Mask);
          kbts__ContextEmit((kbts_u8 *)Masks + (kbts_un)Symbol * Program->LocalMaskWidth, &Value, Program->LocalMaskWidth, &Hash);
        }
      }
      Packed->Masks = kbts__PackTable(Builder, &Tables, &Total, Masks, Bytes, Align, Hash, &PredicateBytes);
    }
  }
  kbts_un HotBytes = Total;
  kbts_un OutcomeBytes = (kbts_un)Builder->OutcomeCount * sizeof(kbts__compiled_outcome);
  for(kbts_u32 P = 0; P < Model->ProgramCount && !Builder->Error; ++P)
  {
    const kbts__context_program *Temp = &Model->Programs[P];
    if(!Temp->Program.CandidateCount) continue;
    Layout[Temp->DenseIndex].Outcomes = kbts__ContextReserve(Builder, &Total,
        Temp->Program.CandidateCount, Temp->Program.OutcomeWidth, Temp->Program.OutcomeWidth);
    OutcomeBytes += (kbts_un)Temp->Program.CandidateCount * Temp->Program.OutcomeWidth;
  }
  kbts_un OutcomeOffset = kbts__ContextReserve(Builder, &Total, Builder->OutcomeCount, sizeof(kbts__compiled_outcome), KBTS_ALIGNOF(kbts__compiled_outcome));
  kbts_un FusionOffset = kbts__ContextReserve(Builder, &Total, FusionCount, sizeof(kbts_u16), KBTS_ALIGNOF(kbts_u16));
  kbts_u32 LookupCount = Model->Font->Tables->LookupCount;
  kbts_un SummaryOffset = kbts__ContextReserve(Builder, &Total, LookupCount, sizeof(kbts__compiled_lookup), KBTS_ALIGNOF(kbts__compiled_lookup));
  kbts_un SummaryBytes = (kbts_un)LookupCount * sizeof(kbts__compiled_lookup);
  kbts_un *SummaryData = kbts__ContextArray(Builder, kbts_un, (kbts_un)LookupCount * 2);
  for(kbts_u32 L = 0; L < LookupCount && !Builder->Error; ++L)
  {
    const kbts__compiled_lookup *Summary = &Model->Lookups[L];
    if(Summary->ReadSymbols)
    {
      SummaryData[L * 2] = kbts__ContextReserve(Builder, &Total, (kbts_un)Model->SymbolWordCount * 3, sizeof(kbts_u32), KBTS_ALIGNOF(kbts_u32));
      SummaryBytes += (kbts_un)Model->SymbolWordCount * 3 * sizeof(kbts_u32);
    }
    SummaryData[L * 2 + 1] = kbts__ContextReserve(Builder, &Total, Summary->ChildCount, sizeof(kbts_u32), KBTS_ALIGNOF(kbts_u32));
    SummaryBytes += (kbts_un)Summary->ChildCount * sizeof(kbts_u32);
  }
  kbts_un AllocationSize = Total;
  kbts__ContextReserve(Builder, &AllocationSize, KBTS_ALIGNOF(kbts__compiled_contexts) - 1, 1, 1);
  if(Builder->Error) return 0;
  void *Allocation = kbts__MemoryPush(Persistent, AllocationSize, 1);
  if(!Allocation) {Builder->Error = 1; return 0;}
  kbts__compiled_contexts *Result = KBTS__ALIGN_POINTER(kbts__compiled_contexts, Allocation, KBTS_ALIGNOF(kbts__compiled_contexts));
  KBTS_MEMSET(Result, 0, Total);
  Result->AllocationSize = (kbts_un)((kbts_uptr)Result - (kbts_uptr)Allocation) + Total;
  Persistent->Used -= AllocationSize - Result->AllocationSize;
  Result->GlyphCount = Builder->GlyphCount;
  Result->SymbolCount = Model->SymbolCount;
  Result->SymbolWordCount = Model->SymbolWordCount;
  Result->PredicateCount = Builder->PredicateCount;
  Result->ProgramCount = Model->DenseCount;
  Result->FlatProgramCount = Model->ProgramCount;
  Result->ProgramIndexWidth = (kbts_u8)IndexWidth;
  Result->SymbolWidth = (kbts_u8)SymbolWidth;
  Result->OutcomeCount = Builder->OutcomeCount;
  Result->WindowCapacity = WindowCapacity;
  Result->ProgramBytes = (kbts_un)Model->DenseCount * sizeof(kbts__compiled_program) + (kbts_un)Model->ProgramCount * IndexWidth;
  Result->ClassifierBytes = sizeof(Result->GlyphPages) + GlyphBytes;
  Result->PredicateBytes = PredicateBytes;
  Result->TestBytes = TestBytes;
  Result->BucketBytes = BucketBytes;
  Result->OutcomeBytes = OutcomeBytes;
  Result->HotBytes = HotBytes;
  Result->FusionBytes = (kbts_un)FusionCount * sizeof(kbts_u16);
  Result->FusedOutcomeCount = FusedOutcomes;
  Result->FusedChainCount = FusedChains;
  Result->SummaryBytes = SummaryBytes;
  Result->LookupCount = LookupCount;
  Result->Programs = KBTS__POINTER_OFFSET(kbts__compiled_program, Result, ProgramOffset);
  Result->ProgramIndices = KBTS__POINTER_OFFSET(void, Result, IndexOffset);
  Result->GlyphSymbols = KBTS__POINTER_OFFSET(void, Result, GlyphOffset);
  Result->PredicateMasks = KBTS__POINTER_OFFSET(kbts_u32, Result, PredicateOffset);
  Result->Outcomes = KBTS__POINTER_OFFSET(kbts__compiled_outcome, Result, OutcomeOffset);
  Result->FusionGlyphs = FusionCount ? KBTS__POINTER_OFFSET(kbts_u16, Result, FusionOffset) : 0;
  Result->LookupSummaries = KBTS__POINTER_OFFSET(kbts__compiled_lookup, Result, SummaryOffset);
  KBTS_MEMCPY(Result->GlyphPages, Model->PageDirectory, sizeof(Result->GlyphPages));
  for(kbts_u32 I = 0; I < Model->PageCount * 256; ++I)
    kbts__SetCompiledIndex(Result->GlyphSymbols, SymbolWidth, I, Model->Pages[I]);
  KBTS_MEMCPY(Result->PredicateMasks, Model->PredicateMasks, (kbts_un)Builder->PredicateCount * Model->SymbolWordCount * sizeof(kbts_u32));
  for(kbts__packed_table *Table = Tables.First; Table; Table = Table->Next)
    KBTS_MEMCPY(KBTS__POINTER_OFFSET(void, Result, Table->Offset), Table->Data, Table->Size);
  for(kbts_u32 P = 0; P < Model->ProgramCount; ++P)
  {
    const kbts__context_program *Temp = &Model->Programs[P];
    kbts__SetCompiledIndex(Result->ProgramIndices, IndexWidth, P, Temp->DenseIndex);
    if(!Temp->First) continue;
    kbts__compiled_program *Program = &Result->Programs[Temp->DenseIndex];
    *Program = Temp->Program;
    if(!Program->CandidateCount) continue;
    const kbts__packed_program *Packed = &Layout[Temp->DenseIndex];
    Program->Tests = KBTS__POINTER_OFFSET(void, Result, Packed->Tests);
    Program->Buckets = KBTS__POINTER_OFFSET(kbts__compiled_bucket, Result, Packed->Buckets);
    Program->SymbolBuckets = KBTS__POINTER_OFFSET(void, Result, Packed->Symbols);
    Program->LocalMasks = Program->LocalPredicateCount ? KBTS__POINTER_OFFSET(void, Result, Packed->Masks) : 0;
    Program->Outcomes = KBTS__POINTER_OFFSET(void, Result, Packed->Outcomes);
    KBTS_MEMSET(Program->Outcomes, 0xff, (kbts_un)Program->CandidateCount * Program->OutcomeWidth);
    kbts_u32 Candidate = 0;
    for(kbts_u32 Symbol = 0; Symbol < Model->SymbolCount; ++Symbol)
      for(kbts__context_candidate *C = Temp->Dispatch[Symbol].First; C; C = C->Next)
        kbts__SetCompiledOutcomeIndex(Program, Candidate++, C->Rule->OutcomeIndex);
  }
  for(kbts_u32 O = 0; O < Builder->OutcomeCount; ++O)
  {
    const kbts__context_rule *Rule = Model->Rules[O];
    Result->Outcomes[O] = Rule->Outcome;
    if(Rule->Outcome.FusionCount)
      KBTS_MEMCPY(Result->FusionGlyphs + Rule->Outcome.FusionOffset, Rule->FusionGlyphs, Rule->Outcome.FusionCount * sizeof(kbts_u16));
  }
  for(kbts_u32 L = 0; L < LookupCount; ++L)
  {
    const kbts__compiled_lookup *From = &Model->Lookups[L];
    kbts__compiled_lookup *To = &Result->LookupSummaries[L];
    To->Kind = From->Kind;
    To->MaxInserted = From->MaxInserted;
    To->WindowCapacity = From->WindowCapacity;
    To->ChildCount = From->ChildCount;
    if(From->ReadSymbols)
    {
      To->ReadSymbols = KBTS__POINTER_OFFSET(kbts_u32, Result, SummaryData[L * 2]);
      To->WriteSymbols = To->ReadSymbols + Model->SymbolWordCount;
      To->FirstSymbols = To->WriteSymbols + Model->SymbolWordCount;
      KBTS_MEMCPY(To->ReadSymbols, From->ReadSymbols, Model->SymbolWordCount * sizeof(kbts_u32));
      KBTS_MEMCPY(To->WriteSymbols, From->WriteSymbols, Model->SymbolWordCount * sizeof(kbts_u32));
      KBTS_MEMCPY(To->FirstSymbols, From->FirstSymbols, Model->SymbolWordCount * sizeof(kbts_u32));
    }
    if(From->ChildCount)
    {
      To->Children = KBTS__POINTER_OFFSET(kbts_u32, Result, SummaryData[L * 2 + 1]);
      KBTS_MEMCPY(To->Children, From->Children, From->ChildCount * sizeof(kbts_u32));
    }
  }
  return Result;
}

static kbts__compiled_contexts *kbts__CompileFontData(kbts_font *Font,
    kbts__memory *Persistent, kbts__memory *Scratch)
{
  kbts__context_builder Builder = KBTS__ZERO;
  kbts__context_model Model = KBTS__ZERO;
  kbts_un ScratchMark = Scratch->Used;
  Builder.Arena.Memory = Scratch;
  kbts_u32 FusionCount = 0, FusedOutcomes = 0, FusedChains = 0;
  kbts__compiled_contexts *Result = 0;
  kbts__CollectContexts(&Builder, &Model, Font);
  if(!Builder.Error) kbts__FuseContexts(&Builder, &Model, &FusionCount, &FusedOutcomes, &FusedChains);
  if(!Builder.Error) kbts__AnalyzeLookups(&Builder, &Model);
  if(!Builder.Error) kbts__AnalyzeExpansion(&Builder, &Model);
  if(!Builder.Error) Result = kbts__PackCompiledContexts(&Builder, &Model, Persistent,
      FusionCount, FusedOutcomes, FusedChains);
  Scratch->Used = ScratchMark;
  if(!Result) Font->Error = Builder.Error == 2 ? KBTS_LOAD_FONT_ERROR_INVALID_FONT : KBTS_LOAD_FONT_ERROR_OUT_OF_MEMORY;
  return Result;
}

#undef kbts__ContextArray
