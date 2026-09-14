/* Included after kbts__sequence_lookup_result; the compiled cache owns all tests. */
#if defined(__AVX2__)
#include <immintrin.h>
#define KBTS__CONTEXT_LANES 8
#elif defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
#include <emmintrin.h>
#define KBTS__CONTEXT_LANES 4
#define KBTS__CONTEXT_SSE2 1
#else
#define KBTS__CONTEXT_LANES 4
#endif


/* On 32-bit x86, keep SSE2 local rather than requiring it for the entire TU. */
#if defined(KBTS__CONTEXT_SSE2) && !defined(__SSE2__) && (defined(__GNUC__) || defined(__clang__))
__attribute__((target("sse2")))
#endif
KBTS_INLINE kbts_u32 kbts__SelectCompiledCandidateKernel(const kbts__compiled_contexts *Cache,
    const kbts__compiled_program *Program, kbts__compiled_bucket Bucket, const kbts_u32 *Symbols, int Wide, int Local)
{
  kbts_u32 End = Bucket.First + Bucket.Count;
  /* Buckets need not start at a vector boundary. Mask both ends, so rounding the
   * program's candidate columns up to eight is sufficient for every load. */
  for(kbts_u32 Base = Bucket.First & ~(KBTS__CONTEXT_LANES - 1u); Base < End; Base += KBTS__CONTEXT_LANES)
  {
    kbts_u32 FirstLane = Base < Bucket.First ? Bucket.First - Base : 0;
    kbts_u32 LastLane = End - Base < KBTS__CONTEXT_LANES ? End - Base : KBTS__CONTEXT_LANES;
    kbts_u32 Active = ((1u << LastLane) - 1u) & ~((1u << FirstLane) - 1u);
    KB_PROFILE_WORK(KBW_RULE_VISITS, LastLane - FirstLane);
    KB_PROFILE_WORK(KBW_COMPARE_WORDS, (LastLane - FirstLane) * Program->StageCount);
    kbts_u32 Mask;
#if defined(__AVX2__)
    __m256i Lanes = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
    __m256i Valid = _mm256_and_si256(
        _mm256_cmpgt_epi32(Lanes, _mm256_set1_epi32((int)FirstLane - 1)),
        _mm256_cmpgt_epi32(_mm256_set1_epi32((int)LastLane), Lanes));
    __m256i Enabled = Valid;
    for(kbts_u32 Stage = 0; Stage < Program->StageCount; ++Stage)
    {
      kbts_un Index = (kbts_un)Stage * Program->CandidateCount + Base;
      __m256i Probes, Predicates;
      if(Wide)
      {
        const int *Tests = (const int *)((const kbts__compiled_wide_test *)Program->Tests + Index);
        __m256i Fields = _mm256_setr_epi32(0, 2, 4, 6, 8, 10, 12, 14);
        Probes = _mm256_mask_i32gather_epi32(_mm256_setzero_si256(), Tests, Fields, Enabled, 4);
        Predicates = _mm256_mask_i32gather_epi32(_mm256_setzero_si256(), Tests + 1, Fields, Enabled, 4);
      }
      else
      {
        __m128i Packed = _mm_loadu_si128((const __m128i *)((const kbts__compiled_test *)Program->Tests + Index));
        __m256i Tests = _mm256_cvtepu16_epi32(Packed);
        Probes = _mm256_srai_epi32(_mm256_slli_epi32(Tests, 24), 24);
        Predicates = _mm256_srli_epi32(Tests, 8);
      }
      __m256i Words = _mm256_mask_i32gather_epi32(_mm256_setzero_si256(), (const int *)Symbols, Probes, Enabled, 4);
      __m256i BitIndices = Predicates;
      if(!Local)
      {
        __m256i WordIndices = _mm256_add_epi32(
            _mm256_mullo_epi32(Predicates, _mm256_set1_epi32((int)Cache->SymbolWordCount)),
            _mm256_srli_epi32(Words, 5));
        BitIndices = _mm256_and_si256(Words, _mm256_set1_epi32(31));
        Words = _mm256_mask_i32gather_epi32(_mm256_setzero_si256(), (const int *)Cache->PredicateMasks, WordIndices, Enabled, 4);
      }
      __m256i Bits = _mm256_sllv_epi32(_mm256_set1_epi32(1), BitIndices);
      Valid = _mm256_and_si256(Valid, _mm256_cmpeq_epi32(_mm256_and_si256(Words, Bits), Bits));
    }
    Mask = (kbts_u32)_mm256_movemask_ps(_mm256_castsi256_ps(Valid)) & Active;
#elif defined(KBTS__CONTEXT_SSE2)
    __m128i Lanes = _mm_setr_epi32(0, 1, 2, 3);
    __m128i Valid = _mm_and_si128(
        _mm_cmpgt_epi32(Lanes, _mm_set1_epi32((int)FirstLane - 1)),
        _mm_cmpgt_epi32(_mm_set1_epi32((int)LastLane), Lanes));
    for(kbts_u32 Stage = 0; Stage < Program->StageCount; ++Stage)
    {
      kbts_un Index = (kbts_un)Stage * Program->CandidateCount + Base;
      kbts_s32 Probes[4];
      kbts_u32 Predicates[4], Pass[4];
      if(Wide)
      {
        const kbts__compiled_wide_test *Tests = (const kbts__compiled_wide_test *)Program->Tests + Index;
        for(kbts_u32 Lane = 0; Lane < 4; ++Lane)
        {
          Probes[Lane] = Tests[Lane].Probe;
          Predicates[Lane] = Tests[Lane].Predicate;
        }
      }
      else
      {
        __m128i Packed = _mm_loadl_epi64((const __m128i *)((const kbts__compiled_test *)Program->Tests + Index));
        __m128i Tests = _mm_unpacklo_epi16(Packed, _mm_setzero_si128());
        _mm_storeu_si128((__m128i *)Probes, _mm_srai_epi32(_mm_slli_epi32(Tests, 24), 24));
        _mm_storeu_si128((__m128i *)Predicates, _mm_srli_epi32(Tests, 8));
      }
      /* SSE2 has no gather or per-lane variable shift. Only those accesses are
       * scalar: conjunction and priority selection remain candidate SIMD. */
      for(kbts_u32 Lane = 0; Lane < 4; ++Lane)
      {
        Probes[Lane] &= -(kbts_s32)((Active >> Lane) & 1u);
        kbts_u32 Value = Symbols[Probes[Lane]];
        if(Local) Pass[Lane] = (Value >> Predicates[Lane]) & 1u;
        else
        {
          kbts_un Word = (kbts_un)Predicates[Lane] * Cache->SymbolWordCount + (Value >> 5);
          Pass[Lane] = (Cache->PredicateMasks[Word] >> (Value & 31)) & 1u;
        }
      }
      Valid = _mm_and_si128(Valid, _mm_cmpeq_epi32(_mm_loadu_si128((const __m128i *)Pass), _mm_set1_epi32(1)));
    }
    Mask = (kbts_u32)_mm_movemask_ps(_mm_castsi128_ps(Valid)) & Active;
#else
    Mask = Active;
    for(kbts_u32 Stage = 0; Stage < Program->StageCount; ++Stage)
    {
      kbts_un Index = (kbts_un)Stage * Program->CandidateCount + Base;
      kbts_u32 Pass = 0;
      for(kbts_u32 Lane = 0; Lane < KBTS__CONTEXT_LANES; ++Lane)
      {
        kbts_s32 Probe;
        kbts_u32 Predicate;
        if(Wide)
        {
          const kbts__compiled_wide_test *Test = (const kbts__compiled_wide_test *)Program->Tests + Index + Lane;
          Probe = Test->Probe;
          Predicate = Test->Predicate;
        }
        else
        {
          const kbts__compiled_test *Test = (const kbts__compiled_test *)Program->Tests + Index + Lane;
          Probe = Test->Probe;
          Predicate = Test->Predicate;
        }
        Probe &= -(kbts_s32)((Active >> Lane) & 1u);
        kbts_u32 Value = Symbols[Probe];
        if(Local) Pass |= ((Value >> Predicate) & 1u) << Lane;
        else
        {
          kbts_un Word = (kbts_un)Predicate * Cache->SymbolWordCount + (Value >> 5);
          Pass |= ((Cache->PredicateMasks[Word] >> (Value & 31)) & 1u) << Lane;
        }
      }
      Mask &= Pass;
    }
#endif
    /* Never short-circuit a failed lane or stage. Priority is resolved only
     * after the entire fixed-depth block has been evaluated. */
    if(Mask) return Base + kbts__LsbPositionOrBitWidth32(Mask);
  }
  return (kbts_u32)-1;
}

/* A single candidate has no competing rule lanes. Use those lanes for its
 * stages instead, while still evaluating every predicate without early exit. */
#if defined(KBTS__CONTEXT_SSE2) && !defined(__SSE2__) && (defined(__GNUC__) || defined(__clang__))
__attribute__((target("sse2")))
#endif
KBTS_INLINE int kbts__MatchSingleCompiledCandidate(const kbts__compiled_contexts *Cache,
    const kbts__compiled_program *Program, kbts_u32 Candidate, const kbts_u32 *Symbols, int Wide, int Local)
{
#if defined(__AVX2__) || defined(KBTS__CONTEXT_SSE2)
  KB_PROFILE_WORK(KBW_RULE_VISITS, 1);
  KB_PROFILE_WORK(KBW_COMPARE_WORDS, Program->StageCount);
#endif
#if defined(__AVX2__)
  __m256i Valid = _mm256_set1_epi32(-1);
  for(kbts_u32 Stage = 0; Stage < Program->StageCount; Stage += 8)
  {
    __m256i Stages = _mm256_add_epi32(_mm256_set1_epi32((int)Stage), _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7));
    __m256i Active = _mm256_cmpgt_epi32(_mm256_set1_epi32((int)Program->StageCount), Stages);
    __m256i Indices = _mm256_add_epi32(_mm256_mullo_epi32(Stages, _mm256_set1_epi32((int)Program->CandidateCount)),
                                     _mm256_set1_epi32((int)Candidate));
    __m256i Probes, Predicates;
    if(Wide)
    {
      Indices = _mm256_slli_epi32(Indices, 1);
      Probes = _mm256_mask_i32gather_epi32(_mm256_setzero_si256(), (const int *)Program->Tests, Indices, Active, 4);
      Predicates = _mm256_mask_i32gather_epi32(_mm256_setzero_si256(), (const int *)Program->Tests + 1, Indices, Active, 4);
    }
    else
    {
      __m256i Packed = _mm256_mask_i32gather_epi32(_mm256_setzero_si256(), (const int *)Program->Tests, Indices, Active, 2);
      Probes = _mm256_srai_epi32(_mm256_slli_epi32(Packed, 24), 24);
      Predicates = _mm256_and_si256(_mm256_srli_epi32(Packed, 8), _mm256_set1_epi32(255));
    }
    __m256i Words = _mm256_mask_i32gather_epi32(_mm256_setzero_si256(), (const int *)Symbols, Probes, Active, 4);
    __m256i BitIndices = Predicates;
    if(!Local)
    {
      __m256i WordIndices = _mm256_add_epi32(
          _mm256_mullo_epi32(Predicates, _mm256_set1_epi32((int)Cache->SymbolWordCount)), _mm256_srli_epi32(Words, 5));
      BitIndices = _mm256_and_si256(Words, _mm256_set1_epi32(31));
      Words = _mm256_mask_i32gather_epi32(_mm256_setzero_si256(), (const int *)Cache->PredicateMasks, WordIndices, Active, 4);
    }
    __m256i Bits = _mm256_sllv_epi32(_mm256_set1_epi32(1), BitIndices);
    __m256i Passed = _mm256_cmpeq_epi32(_mm256_and_si256(Words, Bits), Bits);
    Valid = _mm256_and_si256(Valid, _mm256_or_si256(Passed, _mm256_xor_si256(Active, _mm256_set1_epi32(-1))));
  }
  return _mm256_movemask_ps(_mm256_castsi256_ps(Valid)) == 255;
#elif defined(KBTS__CONTEXT_SSE2)
  __m128i Valid = _mm_set1_epi32(-1);
  for(kbts_u32 Stage = 0; Stage < Program->StageCount; Stage += 4)
  {
    kbts_u32 Pass[4];
    for(kbts_u32 Lane = 0; Lane < 4; ++Lane)
    {
      kbts_u32 At = Stage + Lane;
      kbts_u32 Safe = At < Program->StageCount ? At : Program->StageCount - 1;
      kbts_un Index = (kbts_un)Safe * Program->CandidateCount + Candidate;
      kbts_s32 Probe;
      kbts_u32 Predicate;
      if(Wide)
      {
        const kbts__compiled_wide_test *Test = (const kbts__compiled_wide_test *)Program->Tests + Index;
        Probe = Test->Probe;
        Predicate = Test->Predicate;
      }
      else
      {
        const kbts__compiled_test *Test = (const kbts__compiled_test *)Program->Tests + Index;
        Probe = Test->Probe;
        Predicate = Test->Predicate;
      }
      kbts_u32 Value = Symbols[Probe];
      if(Local) Pass[Lane] = (Value >> Predicate) & 1u;
      else
      {
        kbts_un Word = (kbts_un)Predicate * Cache->SymbolWordCount + (Value >> 5);
        Pass[Lane] = (Cache->PredicateMasks[Word] >> (Value & 31)) & 1u;
      }
      Pass[Lane] |= At >= Program->StageCount;
    }
    Valid = _mm_and_si128(Valid, _mm_cmpeq_epi32(_mm_loadu_si128((const __m128i *)Pass), _mm_set1_epi32(1)));
  }
  return _mm_movemask_ps(_mm_castsi128_ps(Valid)) == 15;
#else
  kbts__compiled_bucket Bucket = {Candidate, 1};
  return kbts__SelectCompiledCandidateKernel(Cache, Program, Bucket, Symbols, Wide, Local) != (kbts_u32)-1;
#endif
}

#if defined(KBTS__CONTEXT_SSE2) && !defined(__SSE2__) && (defined(__GNUC__) || defined(__clang__))
__attribute__((target("sse2")))
#endif
static kbts_u32 kbts__SelectCompiledCandidate(const kbts__compiled_contexts *Cache,
    const kbts__compiled_program *Program, kbts__compiled_bucket Bucket, const kbts_u32 *Symbols)
{
  /* Select the encoding outside the stage loop. Constant arguments eliminate
   * format branches from each instantiated fixed-depth SIMD kernel. */
  if(Bucket.Count == 1)
  {
    int Matched;
    if(Program->Wide)
      Matched = Program->LocalMasks ? kbts__MatchSingleCompiledCandidate(Cache, Program, Bucket.First, Symbols, 1, 1) :
                                      kbts__MatchSingleCompiledCandidate(Cache, Program, Bucket.First, Symbols, 1, 0);
    else
      Matched = Program->LocalMasks ? kbts__MatchSingleCompiledCandidate(Cache, Program, Bucket.First, Symbols, 0, 1) :
                                      kbts__MatchSingleCompiledCandidate(Cache, Program, Bucket.First, Symbols, 0, 0);
    return Matched ? Bucket.First : (kbts_u32)-1;
  }
  if(Program->Wide)
    return Program->LocalMasks ? kbts__SelectCompiledCandidateKernel(Cache, Program, Bucket, Symbols, 1, 1) :
                                 kbts__SelectCompiledCandidateKernel(Cache, Program, Bucket, Symbols, 1, 0);
  return Program->LocalMasks ? kbts__SelectCompiledCandidateKernel(Cache, Program, Bucket, Symbols, 0, 1) :
                               kbts__SelectCompiledCandidateKernel(Cache, Program, Bucket, Symbols, 0, 0);
}

static kbts__sequence_lookup_result kbts__MatchCompiledContext(kbts_shape_scratchpad *Scratchpad,
    kbts_glyph_storage *Storage, kbts__unpacked_lookup *Lookup, const kbts__compiled_program *Program,
    kbts_glyph_ref AtGlyph, kbts__skip_flags SkipFlags, kbts_u32 SkipUnicodeFlags)
{
  KB_PROFILE_SCOPE(KBP_SEQUENCE);
  kbts__sequence_lookup_result Result = KBTS__ZERO;
  KBTS_ASSERT(Scratchpad && Storage && Lookup && Program && AtGlyph);
  Result.OnePastLastGlyphMatched = kbts__NextGlyphRef(Storage, AtGlyph);
  if(!Program->StageCount || !Program->CandidateCount) return Result;
  const kbts__compiled_contexts *Cache = Scratchpad->Config->Font->CompiledContexts;
  KBTS_ASSERT(Cache && Program->Tests && Program->Outcomes && Program->Buckets && Program->SymbolBuckets);

  kbts_un WindowCount = (kbts_un)Program->MaxBacktrack + Program->MaxAhead + 1;
  KBTS_ASSERT(WindowCount <= Scratchpad->CompiledWindowCapacity);
  KBTS_ASSERT(Scratchpad->CompiledSymbols && Scratchpad->CompiledGlyphs && Scratchpad->CompiledOffsets);
  kbts_u32 *Symbols = Scratchpad->CompiledSymbols;
  kbts_glyph_ref *Glyphs = Scratchpad->CompiledGlyphs;
  kbts_u32 *Offsets = Scratchpad->CompiledOffsets;
  kbts_u32 Center = Program->MaxBacktrack;
  kbts_u32 AnchorPosition = AtGlyph - 1;
  kbts_u32 Position = AnchorPosition, Distance = 0;
  /* REVIEW[RUN05 SUSPECT]: FirstFiltered can move the matched center beyond
   * AtGlyph, but GSUB child frames retain AtGlyph and record index zero targets
   * that original glyph. Passing the GDEF filter does not exclude a Unicode
   * ignorable skipped here. Can admission guarantee Distance stays zero, or
   * should matching and record targeting share the same filtered anchor? */
  if(Program->FirstFiltered)
  {
    while(Position != Storage->End &&
          kbts__SkipGlyph(&Storage->Glyphs[Position], Lookup, SkipFlags, SkipUnicodeFlags))
    {
      Position = kbts__NextGlyphSlot(Storage, Position);
      ++Distance;
    }
  }
  kbts_u32 FirstSymbol = Position != Storage->End ? Storage->Glyphs[Position].CompiledSymbol : 0;
  Symbols[Center] = Program->LocalMasks ? kbts__CompiledLocalMask(Program, FirstSymbol) : FirstSymbol;
  Glyphs[Center] = Position != Storage->End ? Position + 1 : 0;
  Offsets[Center] = Distance;
  kbts_u32 BucketIndex = kbts__CompiledBucketIndex(Program, FirstSymbol);
  KBTS_ASSERT(BucketIndex < Program->BucketCount);
  kbts__compiled_bucket Bucket = Program->Buckets[BucketIndex];
  if(!Bucket.Count) return Result;
  KBTS_ASSERT(Bucket.First < Program->CandidateCount && Bucket.Count <= Program->CandidateCount - Bucket.First);
  KBTS_ASSERT(Bucket.Backtrack <= Center && Bucket.Ahead <= Program->MaxAhead);
  WindowCount = (kbts_un)Center + Bucket.Ahead + 1;

  {
    KB_PROFILE_SCOPE(KBP_INPUT_GATHER);
    /* Logical input/lookahead positions share one forward traversal, but the
     * saved logical distance lets the winning outcome consume only its input. */
    for(kbts_un Index = (kbts_un)Center + 1; Index < WindowCount; ++Index)
    {
      if(Position != Storage->End)
      {
        Position = kbts__NextGlyphSlot(Storage, Position);
        ++Distance;
      }
      while(Position != Storage->End)
      {
        KB_PROFILE_WORK(KBW_INPUT_GLYPHS, 1);
        if(!kbts__SkipGlyph(&Storage->Glyphs[Position], Lookup, SkipFlags, SkipUnicodeFlags)) break;
        Position = kbts__NextGlyphSlot(Storage, Position);
        ++Distance;
      }
      kbts_u32 Symbol = Position != Storage->End ? Storage->Glyphs[Position].CompiledSymbol : 0;
      Symbols[Index] = Program->LocalMasks ? kbts__CompiledLocalMask(Program, Symbol) : Symbol;
      Glyphs[Index] = Position != Storage->End ? Position + 1 : 0;
      Offsets[Index] = Distance;
    }
  }
  {
    KB_PROFILE_SCOPE(KBP_BACKTRACK_GATHER);
    /* REVIEW[RUN04 CLEANUP]: Only Symbols is read below Center: result
     * consumption reads the last forward input, and ApplyCompiledFusion reads
     * forward Glyphs. Backtrack nevertheless maintains Distance and writes
     * Glyphs/Offsets for every probe. Can those unused writes and distance
     * arithmetic go while retaining the same filtered symbol traversal? */
    Position = AnchorPosition;
    Distance = 0;
    for(kbts_u32 Index = Center; Index > Center - Bucket.Backtrack;)
    {
      --Index;
      if(Position != KBTS__GLYPH_END)
      {
        Position = Position == Storage->First ? KBTS__GLYPH_END : kbts__PrevGlyphSlot(Storage, Position);
        --Distance;
      }
      while(Position != KBTS__GLYPH_END)
      {
        KB_PROFILE_WORK(KBW_BACKTRACK_GLYPHS, 1);
        if(!kbts__SkipGlyph(&Storage->Glyphs[Position], Lookup, SkipFlags, SkipUnicodeFlags)) break;
        Position = Position == Storage->First ? KBTS__GLYPH_END : kbts__PrevGlyphSlot(Storage, Position);
        --Distance;
      }
      kbts_b32 Valid = Position != KBTS__GLYPH_END;
      kbts_u32 Symbol = Valid ? Storage->Glyphs[Position].CompiledSymbol : 0;
      Symbols[Index] = Program->LocalMasks ? kbts__CompiledLocalMask(Program, Symbol) : Symbol;
      Glyphs[Index] = Valid ? Position + 1 : 0;
      /* Backtrack distances use unsigned two's complement. Actions consume
       * only forward logical distances, never physical slot differences. */
      Offsets[Index] = Distance;
    }
  }

  kbts_u32 Candidate = kbts__SelectCompiledCandidate(Cache, Program, Bucket, Symbols + Center);
  if(Candidate == (kbts_u32)-1) return Result;
  kbts_u32 OutcomeIndex = kbts__CompiledOutcomeIndex(Program, Candidate);
  KBTS_ASSERT(OutcomeIndex < Cache->OutcomeCount);
  const kbts__compiled_outcome *Outcome = Cache->Outcomes + OutcomeIndex;
  if(Outcome->InputCount)
  {
    kbts_un LastInput = (kbts_un)Center + Outcome->InputCount - 1;
    KBTS_ASSERT(LastInput < WindowCount && kbts__GlyphIsValid(Storage, Glyphs[LastInput]));
    Result.InputSequenceCountIncludingSkippedGlyphs = Offsets[LastInput] + 1;
    Result.OnePastLastGlyphMatched = kbts__NextGlyphRef(Storage, Glyphs[LastInput]);
  }
  else
  {
    Result.OnePastLastGlyphMatched = AtGlyph;
  }
  Result.Records = Outcome->Records;
  Result.Outcome = Outcome;
  Result.RecordCount = Outcome->RecordCount;
  Result.Matched = 1;
  KB_PROFILE_WORK(KBW_SEQUENCE_MATCHES, 1);
  return Result;
}

#undef KBTS__CONTEXT_LANES
#ifdef KBTS__CONTEXT_SSE2
#undef KBTS__CONTEXT_SSE2
#endif
