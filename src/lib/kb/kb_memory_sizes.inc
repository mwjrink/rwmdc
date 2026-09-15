/* Allocation-free construction bounds. Include after kb_context_fuse.inc and
 * kb_font_tables.inc: this uses their private types and native validation, but
 * never Font.Tables (which does not exist when these queries are first called).
 *
 * Every reservation below includes its own alignment slack. Repeated objects
 * are charged separately, not as one conveniently aligned array. Interning,
 * shared native offsets and equivalent predicates can only reduce these bounds.
 */
static kbts_b32 kbts__BoundRepeat(kbts_un *Total, kbts_un Times,
    kbts_un Count, kbts_un Width, kbts_un Align)
{
  kbts_un One = 0;
  return kbts__BoundAdd(&One, Count, Width, Align) &&
         kbts__BoundAdd(Total, Times, One, 1);
}

/* All generations survive in the construction arena. The source hash can grow
 * on a hit when full, so reserve through a capacity strictly greater than the
 * number of possible source misses. The pack hash grows only on a miss. */
static kbts_b32 kbts__BoundHash(kbts_un *Total, kbts_un Count,
    kbts_un Initial, kbts_un Families, kbts_un Width, kbts_un Align,
    kbts_b32 GrowOnHit)
{
  if(!Count) return 1;
  kbts_un Capacity = Initial;
  for(;;)
  {
    if(!kbts__BoundRepeat(Total, Families, Capacity, Width, Align)) return 0;
    if(GrowOnHit ? Capacity > Count : Capacity >= Count) return 1;
    if(Capacity > (kbts_u32)-1 / 2) return 0;
    Capacity *= 2;
  }
}

typedef struct kbts__compile_counts
{
  kbts_un Scratch, Rules, Tests, Sources, Programs, Records, Fusion;
  kbts_un MaxBacktrack, MaxAhead, Glyphs, Symbols;
} kbts__compile_counts;

#define KBTS__BOUND_ARRAY(Total, Type, Count) \
  kbts__BoundAdd((Total), (Count), sizeof(Type), KBTS_ALIGNOF(Type))
#define KBTS__BOUND_REPEAT(Total, Times, Type, Count) \
  kbts__BoundRepeat((Total), (Times), (Count), sizeof(Type), KBTS_ALIGNOF(Type))

/* Coverage groups normalize only format 2, and lazily allocate at most one
 * membership bitset. Charge once per reference here: sharing across programs
 * or repeated format-3 coverage offsets is deliberately not required. */
static kbts_b32 kbts__BoundCoverage(kbts__compile_counts *C,
    kbts__font_view *V, kbts_un At, kbts_un *Members)
{
  *Members = kbts__ValidateCoverage(V, At);
  if(V->Error || !KBTS__BOUND_ARRAY(&C->Scratch, kbts__context_group, 1) ||
     !KBTS__BOUND_ARRAY(&C->Scratch, kbts_u32, (C->Glyphs + 31) / 32)) return 0;
  return kbts__View16(V, At) != 2 ||
         KBTS__BOUND_ARRAY(&C->Scratch, kbts_u16, *Members);
}

/* Heads are indexed by the actual class value, not by glyph count or number
 * of class records. In particular class 65535 needs 65536 head entries. Values
 * outside the glyph domain do not matter to ContextGroup's glyph scan. */
static kbts_b32 kbts__BoundClasses(kbts__compile_counts *C,
    kbts__font_view *V, kbts_un At)
{
  kbts_un Values = 1;
  if(At)
  {
    kbts_u16 *Classes = (kbts_u16 *)(V->Data + At);
    for(kbts_u32 G = 0; G < C->Glyphs; ++G)
    {
      kbts_un Value = (kbts_un)kbts__GlyphClassFromTable(Classes, G).Class + 1;
      Values = KBTS__MAX(Values, Value);
    }
  }
  return KBTS__BOUND_ARRAY(&C->Scratch, kbts__context_group, 1) &&
         KBTS__BOUND_ARRAY(&C->Scratch, kbts_u32, C->Glyphs) &&
         KBTS__BOUND_ARRAY(&C->Scratch, kbts_u32, Values);
}

/* Read one validated rule in native order. Counts are still checked here so
 * no arithmetic in the sizing walk depends on a truncating native integer. */
static kbts_b32 kbts__BoundRule(kbts__compile_counts *C, kbts__font_view *V,
    kbts_un At, kbts_un Parent, kbts_b32 Chain, kbts_b32 Coverages,
    kbts_un FirstMembers, kbts_un *Candidates, kbts_un *Stages,
    kbts_b32 Collect)
{
  kbts_un P = At, Back = 0, Input, Ahead = 0, Records = 0, Ignored;
  if(Chain)
  {
    Back = kbts__View16(V, P); P += 2;
    if(Coverages && Collect)
      for(kbts_un I = 0; I < Back; ++I)
        if(!kbts__BoundCoverage(C, V, kbts__ViewRef(V, Parent,
            kbts__View16(V, P + 2 * I)), &Ignored)) return 0;
    P += Back * 2;
  }
  Input = kbts__View16(V, P); P += 2;
  if(!Input && (!Coverages || Chain)) return 0;
  if(!Chain) { Records = kbts__View16(V, P); P += 2; }
  kbts_un Stored = Input - !Coverages;
  if(Coverages)
    for(kbts_un I = 0; I < Input; ++I)
    {
      kbts_un Cov = kbts__ViewRef(V, Parent, kbts__View16(V, P + 2 * I));
      kbts_un Members = kbts__ValidateCoverage(V, Cov);
      if(Collect && !kbts__BoundCoverage(C, V, Cov, &Members)) return 0;
      if(!I) FirstMembers = Members;
    }
  P += Stored * 2;
  if(Chain)
  {
    Ahead = kbts__View16(V, P); P += 2;
    if(Coverages && Collect)
      for(kbts_un I = 0; I < Ahead; ++I)
        if(!kbts__BoundCoverage(C, V, kbts__ViewRef(V, Parent,
            kbts__View16(V, P + 2 * I)), &Ignored)) return 0;
    P += Ahead * 2;
    Records = kbts__View16(V, P);
  }
  if(V->Error) return 0;
  kbts_un NativeTests = Back + Input + Ahead; /* three validated u16 counts */
  kbts_un Tests = KBTS__MAX(NativeTests, 1);
  *Stages = KBTS__MAX(*Stages, Tests > 1 ? Tests - 1 : 1);
  /* A nonzero first predicate excludes the boundary symbol. Every admitted
   * symbol has at least one distinct covered glyph, hence this cardinality
   * bound includes all per-symbol duplication, even for class-zero rules.
   * The nonchained zero-input format-3 rule instead tests padding predicate
   * zero, admitting all symbols including the boundary without a source. */
  if(!kbts__BoundAdd(Candidates,
      NativeTests ? KBTS__MIN(FirstMembers, C->Symbols) : C->Symbols, 1, 1)) return 0;
  if(!Collect) return 1;
  C->MaxBacktrack = KBTS__MAX(C->MaxBacktrack, Back);
  C->MaxAhead = KBTS__MAX(C->MaxAhead, Input + Ahead ? Input + Ahead - 1 : 0);
  return kbts__BoundAdd(&C->Rules, 1, 1, 1) &&
         kbts__BoundAdd(&C->Tests, Tests, 1, 1) &&
         kbts__BoundAdd(&C->Sources, NativeTests - !Coverages, 1, 1) &&
         kbts__BoundAdd(&C->Records, Records, 1, 1) &&
         kbts__BoundAdd(&C->Fusion, Input + Ahead, 1, 1) &&
         KBTS__BOUND_ARRAY(&C->Scratch, kbts__context_rule, 1) &&
         KBTS__BOUND_ARRAY(&C->Scratch, kbts__compiled_wide_test, Tests);
}

static kbts_b32 kbts__BoundContext(kbts__compile_counts *C,
    kbts__font_view *V, kbts_un At, kbts_b32 Chain, kbts_b32 Collect,
    kbts_un *Output)
{
  kbts_un Format = kbts__View16(V, At), Candidates = 0, Stages = 0;
  kbts_un RulesBefore = C->Rules;
  kbts_b32 HasRules = 0;
  if(Format == 3)
  {
    /* The chained compiler skips zero-input format 3 before constructing
     * rules or predicates; preserve that distinction if validation admits it. */
    if(Chain)
    {
      kbts_un Back = kbts__View16(V, At + 2);
      kbts_un Input = kbts__View16(V, At + 4 + Back * 2);
      if(V->Error) return 0;
      if(!Input) return 1;
    }
    if(!kbts__BoundRule(C, V, At + 2, At, Chain, 1, 0,
        &Candidates, &Stages, Collect)) return 0;
    HasRules = 1;
  }
  else
  {
    kbts_un Cov = kbts__ViewRef(V, At, kbts__View16(V, At + 2));
    kbts_un Covered = kbts__ValidateCoverage(V, Cov), P = At + 4;
    kbts_un InputClasses = 0, BackClasses = 0, AheadClasses = 0;
    if(Format == 2)
    {
      if(Chain)
      {
        kbts_un Off = kbts__View16(V, P); P += 2;
        if(Off) BackClasses = kbts__ViewRef(V, At, Off);
      }
      kbts_un Off = kbts__View16(V, P); P += 2;
      if(Off) InputClasses = kbts__ViewRef(V, At, Off);
      if(Chain)
      {
        Off = kbts__View16(V, P); P += 2;
        AheadClasses = Off ? kbts__ViewRef(V, At, Off) : InputClasses;
      }
    }
    else if(Format != 1) return 0;
    kbts_un Sets = kbts__View16(V, P); P += 2;
    kbts_b32 HaveSet = 0;
    for(kbts_un I = 0; I < Sets && !V->Error; ++I)
    {
      kbts_un Off = kbts__View16(V, P + 2 * I);
      if(!Off) continue;
      HaveSet = 1;
      kbts_un Set = kbts__ViewRef(V, At, Off), Rules = kbts__View16(V, Set);
      kbts_un FirstMembers = I < Covered ? 1 : 0;
      if(Format == 2)
      {
        FirstMembers = 0;
        kbts_u16 *Classes = InputClasses ? (kbts_u16 *)(V->Data + InputClasses) : 0;
        for(kbts_u32 G = 0; G < C->Glyphs; ++G)
          if((Classes ? kbts__GlyphClassFromTable(Classes, G).Class : 0) == I &&
             kbts__CoverGlyph((kbts__coverage *)(V->Data + Cov), (kbts_u16)G).Valid)
            ++FirstMembers;
      }
      if(Collect && !kbts__BoundAdd(&C->Sources, 1, 1, 1)) return 0;
      for(kbts_un R = 0; R < Rules && !V->Error; ++R)
      {
        kbts_un Rule = kbts__ViewRef(V, Set, kbts__View16(V, Set + 2 + 2 * R));
        if(!kbts__BoundRule(C, V, Rule, Rule, Chain, 0, FirstMembers,
            &Candidates, &Stages, Collect)) return 0;
        HasRules = 1;
      }
    }
    /* At most one coverage and three class families per subtable, even when
     * every set is a new source. A nonempty set with zero rules still creates
     * its first source and its coverage/input-class groups. */
    if(Collect && HaveSet)
    {
      if(!kbts__BoundCoverage(C, V, Cov, &Covered)) return 0;
      if(Format == 2 &&
         (!kbts__BoundClasses(C, V, InputClasses) ||
          (Chain && (!kbts__BoundClasses(C, V, BackClasses) ||
                     !kbts__BoundClasses(C, V, AheadClasses))))) return 0;
    }
  }
  if(V->Error) return 0;
  if(Collect)
    return C->Rules == RulesBefore || kbts__BoundAdd(&C->Programs, 1, 1, 1);
  if(!HasRules) return 1;
  if(!KBTS__BOUND_ARRAY(&C->Scratch, kbts__context_dispatch, C->Symbols) ||
     !KBTS__BOUND_REPEAT(&C->Scratch, Candidates, kbts__context_candidate, 1)) return 0;
  if(!Candidates) return 1;
  /* Candidate SIMD padding is per program, not per bucket. Wide test bytes
   * dominate compact bytes; one extra element covers compact final gathers.
   * All four emitted tables coexist with the builder and packed output. */
  kbts_un Padded = Candidates, TestCount = 0, Buckets = C->Symbols + 1;
  if(!kbts__BoundAdd(&Padded, 7, 1, 1)) return 0;
  Padded &= ~(kbts_un)7;
  if(!kbts__BoundAdd(&TestCount, Padded, Stages, 1) ||
     !kbts__BoundAdd(&TestCount, 1, 1, 1)) return 0;
  for(int Pass = 0; Pass < 2; ++Pass)
  {
    kbts_un *Bytes = Pass ? Output : &C->Scratch;
    if(!KBTS__BOUND_ARRAY(Bytes, kbts__compiled_wide_test, TestCount) ||
       !KBTS__BOUND_ARRAY(Bytes, kbts__compiled_bucket, Buckets) ||
       !KBTS__BOUND_ARRAY(Bytes, kbts_u32, C->Symbols) ||
       !KBTS__BOUND_ARRAY(Bytes, kbts_u32, C->Symbols)) return 0;
  }
  return KBTS__BOUND_ARRAY(Output, kbts_u32, Padded);
}

static kbts_b32 kbts__BoundNativeContexts(kbts_font *Font,
    const kbts_blob_header *Header, kbts__compile_counts *C,
    kbts_b32 Collect, kbts_un *Output)
{
  for(int Gpos = 0; Gpos < 2; ++Gpos)
  {
    kbts__font_view V = kbts__TableView(Font->Blob, Font->BlobSize,
        &Header->Tables[Gpos ? KBTS_BLOB_TABLE_ID_GPOS : KBTS_BLOB_TABLE_ID_GSUB],
        0, Header->GlyphCount);
    if(V.Error) return 0;
    if(!V.Size) continue;
    kbts_un List = kbts__ViewRef(&V, 0, kbts__View16(&V, 8));
    kbts_un Lookups = kbts__View16(&V, List);
    for(kbts_un L = 0; L < Lookups && !V.Error; ++L)
    {
      kbts_un Lookup = kbts__ViewRef(&V, List, kbts__View16(&V, List + 2 + 2 * L));
      kbts_un Type = kbts__View16(&V, Lookup), N = kbts__View16(&V, Lookup + 4);
      for(kbts_un S = 0; S < N && !V.Error; ++S)
      {
        kbts_un Sub = kbts__ViewRef(&V, Lookup, kbts__View16(&V, Lookup + 6 + 2 * S));
        kbts_un Native = Type;
        if(Type == (Gpos ? 9u : 7u))
        {
          Native = kbts__View16(&V, Sub + 2);
          Sub = kbts__ViewRef(&V, Sub, kbts__View32(&V, Sub + 4));
        }
        kbts_un ContextType = Gpos ? 7 : 5;
        if((Native == ContextType || Native == ContextType + 1) &&
           !kbts__BoundContext(C, &V, Sub, Native != ContextType, Collect, Output)) return 0;
      }
    }
    if(V.Error) return 0;
  }
  return 1;
}

static kbts_b32 kbts__BoundCmap(kbts_font *Font,
    kbts_un *Output, kbts_un *Scratch)
{
  /* Like CompileCmap, this consumes the trusted native Font.Cmap field, which
   * may borrow a separate table rather than the selected blob payload. */
  if(Font->CmapLookup || !Font->Cmap ||
     kbts__ReadU16Unaligned(Font->Cmap) != 12) return 1;
  const kbts__cmap_12_13 *Cmap = (const kbts__cmap_12_13 *)Font->Cmap;
  const kbts__sequential_map_group *Ranges = KBTS__POINTER_AFTER(kbts__sequential_map_group, Cmap);
  kbts_un Groups = Cmap->GroupCount;
  if(Groups < 4096) return 1;
  kbts_u32 Last = 0;
  for(kbts_un I = 0; I < Groups; ++I)
  {
    const kbts__sequential_map_group *Group = Ranges + I;
    kbts_u32 First = Group->StartCharacterCode, End = Group->EndCharacterCode;
    kbts_u32 Glyph = Group->StartGlyphId;
    /* These are the optional cmap compiler's eligibility rules, not extra
     * font validation: ineligible tables remain borrowed and allocate zero. */
    if(First > End || End > 0x10ffff || (I && First <= Last) ||
       Glyph > 0xffff || End - First > 0xffff - Glyph) return 1;
    Last = End;
  }
  kbts_un Pages = (Last >> 5) + 1, Hash = 1, Bytes = 0;
  while(Hash < Pages * 2) Hash *= 2; /* Unicode caps Hash at 131072. */
  if(!KBTS__BOUND_ARRAY(&Bytes, kbts_u32, Hash) ||
     !KBTS__BOUND_ARRAY(&Bytes, kbts_u16, Pages * 33) ||
     !KBTS__BOUND_ARRAY(Scratch, kbts_u8, Bytes)) return 0;
  /* The compiler only retains the cache when its full allocation fits the
   * native cmap length. It additionally aligns the external MemoryPush. */
  return kbts__BoundAdd(Output, Cmap->Length, 1,
      KBTS_ALIGNOF(kbts__cmap_lookup));
}

static kbts_b32 kbts__FontCompileBounds(kbts_font *Font,
    kbts_un *Output, kbts_un *Scratch)
{
  *Output = *Scratch = 0;
  kbts_blob_header Header;
  if(!Font || Font->Error || !Font->Blob ||
     ((kbts_un)Font->Blob & (KBTS_ALIGNOF(kbts_blob_header) - 1)) ||
     !kbts__ValidateNativeBlob(Font->Blob, Font->BlobSize, &Header, 0)) return 0;
  kbts_un Out = 0, CmapScratch = 0;
  kbts_un G = Header.GlyphCount, L = Header.LookupCount;
  kbts_un S = Header.LookupSubtableCount, Lg = Header.GposLookupIndexOffset;
  /* CompileFontTables reserves this whole byte count, including its explicit
   * three alignment allowances, rather than only its final used fields. */
  if(!KBTS__BOUND_ARRAY(&Out, kbts__font_tables, 1) ||
     !kbts__BoundAdd(&Out, L, sizeof(kbts__font_lookup), 1) ||
     !kbts__BoundAdd(&Out, S, sizeof(kbts__font_subtable), 1) ||
     !kbts__BoundAdd(&Out, G, sizeof(kbts_glyph_classes), 1) ||
     !kbts__BoundAdd(&Out, 3, KBTS_ALIGNOF(kbts__font_lookup), 1) ||
     !kbts__BoundCmap(Font, &Out, &CmapScratch)) return 0;

  kbts__compile_counts C = KBTS__ZERO;
  C.Glyphs = G; C.Symbols = G + 1;
  if(!kbts__BoundNativeContexts(Font, &Header, &C, 1, &Out)) return 0;
  kbts_un Predicates = C.Sources;
  if(!kbts__BoundAdd(&Predicates, 1, 1, 1)) return 0;
  /* Each source can introduce at most one predicate. Membership refinement
   * creates at most 2^Sources real-glyph signatures plus boundary symbol zero.
   * Saturate at the semantic G+1 limit without shifting an oversized count. */
  kbts_un Signatures = 1;
  for(kbts_un I = 0; I < C.Sources && Signatures < G; ++I) Signatures *= 2;
  C.Symbols = C.Sources ? KBTS__MIN(G + 1, Signatures + 1) : 1;
  kbts_un K = C.Symbols, Wg = (G + 31) / 32, Ws = (K + 31) / 32;
  kbts_un Dense = C.Programs, PredicateWords = (Predicates - 1) / 32 + 1;
  if(!kbts__BoundAdd(&Dense, 1, 1, 1)) return 0;

  /* Collection: programs, rules/tests (charged during traversal), source and
   * predicate objects/bitsets, both hash generations, refinement and page maps,
   * global predicate masks, glyph membership chains, and shared tautology. */
  if(!KBTS__BOUND_ARRAY(&C.Scratch, kbts__context_program, S) ||
     !KBTS__BOUND_ARRAY(&C.Scratch, kbts__compiled_lookup, L) ||
     !KBTS__BOUND_ARRAY(&C.Scratch, kbts_u32, Wg) ||
     !KBTS__BOUND_REPEAT(&C.Scratch, C.Sources, kbts__context_source, 1) ||
     !KBTS__BOUND_REPEAT(&C.Scratch, C.Sources, kbts__context_predicate, 1) ||
     !KBTS__BOUND_REPEAT(&C.Scratch, C.Sources, kbts_u32, Wg) ||
     !kbts__BoundHash(&C.Scratch, C.Sources, 256, 2, sizeof(void *), KBTS_ALIGNOF(void *), 1) ||
     !KBTS__BOUND_REPEAT(&C.Scratch, 2, kbts_u32, G) ||
     !KBTS__BOUND_REPEAT(&C.Scratch, 3, kbts_u32, G + 1) ||
     !KBTS__BOUND_ARRAY(&C.Scratch, kbts__context_predicate *, Predicates) ||
     !KBTS__BOUND_ARRAY(&C.Scratch, kbts_u16, 257 * 256) ||
     !KBTS__BOUND_ARRAY(&C.Scratch, kbts__context_rule *, C.Rules) ||
     !KBTS__BOUND_ARRAY(&C.Scratch, kbts_u32, K) ||
     !KBTS__BOUND_ARRAY(&C.Scratch, kbts_u32, Ws) ||
     !KBTS__BOUND_REPEAT(&C.Scratch, Predicates, kbts_u32, Ws)) return 0;

  /* Fusion: one reusable position-domain rectangle, verdict arrays and output
   * row; at most one child symbol map per referenced GSUB child. Each rule's
   * fused chain fits its own input+lookahead interval, so sum those intervals
   * rather than multiplying every rule by the largest rule in the font. */
  kbts_un Positions = C.MaxBacktrack + C.MaxAhead + 1;
  if(!KBTS__BOUND_ARRAY(&C.Scratch, kbts_u32 *, Lg) ||
     !KBTS__BOUND_ARRAY(&C.Scratch, kbts_u32, C.Rules) ||
     !KBTS__BOUND_ARRAY(&C.Scratch, kbts_s8, C.Rules) ||
     !KBTS__BOUND_REPEAT(&C.Scratch, Positions, kbts_u32, Ws) ||
     !KBTS__BOUND_ARRAY(&C.Scratch, kbts_u16, C.MaxAhead + 1) ||
     !KBTS__BOUND_REPEAT(&C.Scratch, KBTS__MIN(Lg, C.Records), kbts_u32, K) ||
     !kbts__BoundAdd(&C.Scratch, C.Fusion, sizeof(kbts_u16), 1) ||
     !kbts__BoundAdd(&C.Scratch, C.Rules, KBTS_ALIGNOF(kbts_u16) - 1, 1)) return 0;

  /* Lookup analysis and expansion. Child edges cannot exceed native record
   * occurrences. Three symbol sets per lookup dominate lowerable summaries;
   * retain separate alignment slack for each of their construction arrays. */
  if(!KBTS__BOUND_REPEAT(&C.Scratch, 2, kbts_u32, L) ||
     !KBTS__BOUND_REPEAT(&C.Scratch, 2, kbts_u32, PredicateWords) ||
     !KBTS__BOUND_REPEAT(&C.Scratch, 2, kbts_u32, Lg) ||
     !kbts__BoundAdd(&C.Scratch, C.Records, sizeof(kbts_u32), 1) ||
     !kbts__BoundAdd(&C.Scratch, L, KBTS_ALIGNOF(kbts_u32) - 1, 1) ||
     !KBTS__BOUND_REPEAT(&C.Scratch, L, kbts_u32, Ws) ||
     !KBTS__BOUND_REPEAT(&C.Scratch, L, kbts_u32, Ws) ||
     !KBTS__BOUND_REPEAT(&C.Scratch, L, kbts_u32, Ws)) return 0;

  /* Pack bookkeeping: up to four interned tables per nonempty program, one
   * node per table and every slot generation (16,32,...), plus layout and
   * two summary offsets per lookup. Hash collisions never add extra nodes. */
  kbts_un Tables = 0;
  if(!kbts__BoundAdd(&Tables, C.Programs, 4, 1) ||
     !KBTS__BOUND_ARRAY(&C.Scratch, kbts__packed_program, Dense) ||
     !KBTS__BOUND_REPEAT(&C.Scratch, 2, kbts_un, L) ||
     !KBTS__BOUND_REPEAT(&C.Scratch, Tables, kbts__packed_table, 1) ||
     !kbts__BoundHash(&C.Scratch, Tables, 16, 1,
         sizeof(kbts__packed_table *), KBTS_ALIGNOF(kbts__packed_table *), 0)) return 0;

  /* Retained context header/directory, classifier, masks, outcomes, fusion and
   * lookup summaries. Index and symbol widths use their widest legal types.
   * 257 pages includes the all-zero page and every possible glyph page. */
  if(!KBTS__BOUND_ARRAY(&Out, kbts__compiled_contexts, 1) ||
     !KBTS__BOUND_ARRAY(&Out, kbts__compiled_program, Dense) ||
     !KBTS__BOUND_ARRAY(&Out, kbts_u32, S) ||
     !KBTS__BOUND_ARRAY(&Out, kbts_u16, 257 * 256) ||
     !KBTS__BOUND_REPEAT(&Out, Predicates, kbts_u32, Ws) ||
     !KBTS__BOUND_ARRAY(&Out, kbts__compiled_outcome, C.Rules) ||
     !KBTS__BOUND_ARRAY(&Out, kbts_u16, C.Fusion) ||
     !KBTS__BOUND_ARRAY(&Out, kbts__compiled_lookup, L) ||
     !KBTS__BOUND_REPEAT(&Out, L, kbts_u32, Ws) ||
     !KBTS__BOUND_REPEAT(&Out, L, kbts_u32, Ws) ||
     !KBTS__BOUND_REPEAT(&Out, L, kbts_u32, Ws) ||
     !kbts__BoundAdd(&Out, C.Records, sizeof(kbts_u32), 1) ||
     !kbts__BoundAdd(&Out, L, KBTS_ALIGNOF(kbts_u32) - 1, 1) ||
     !kbts__BoundNativeContexts(Font, &Header, &C, 0, &Out)) return 0;

  /* Cmap construction restores scratch before context construction begins. */
  *Output = Out;
  *Scratch = KBTS__MAX(CmapScratch, C.Scratch);
  return 1;
}

#undef KBTS__BOUND_ARRAY
#undef KBTS__BOUND_REPEAT
