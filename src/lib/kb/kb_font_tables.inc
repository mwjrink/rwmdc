/* Checked integer offsets precede every read and pointer formation at the font
 * boundary. The same walker accepts big-endian SFNT tables and native blobs. */
typedef struct kbts__font_view
{
  const kbts_u8 *Data;
  kbts_un Size;
  int BigEndian, Error;
  kbts_u32 GlyphCount, LookupCount;
} kbts__font_view;

static int kbts__FontRange(kbts_un Size, kbts_un At, kbts_un Count, kbts_un Width)
{
  return At <= Size && (!Width || Count <= (Size - At) / Width);
}

static int kbts__ViewRange(kbts__font_view *V, kbts_un At, kbts_un Count, kbts_un Width)
{
  if(!kbts__FontRange(V->Size, At, Count, Width)) V->Error = 1;
  return !V->Error;
}

static kbts_u16 kbts__View16(kbts__font_view *V, kbts_un At)
{
  if(!kbts__ViewRange(V, At, 1, 2)) return 0;
  kbts_u16 X = kbts__ReadU16Unaligned(V->Data + At);
  return V->BigEndian ? kbts__ByteSwap16(X) : X;
}

static kbts_u32 kbts__View32(kbts__font_view *V, kbts_un At)
{
  if(!kbts__ViewRange(V, At, 1, 4)) return 0;
  kbts_u32 X = kbts__ReadU32Unaligned(V->Data + At);
  return V->BigEndian ? kbts__ByteSwap32(X) : X;
}

static kbts_un kbts__ViewRef(kbts__font_view *V, kbts_un Parent, kbts_un Offset)
{
  if(!Offset || (Offset & 1) || Parent > V->Size || Offset > V->Size - Parent)
  { V->Error = 1; return 0; }
  return Parent + Offset;
}

static kbts_un kbts__ValidateCoverage(kbts__font_view *V, kbts_un At)
{
  kbts_u32 Format = kbts__View16(V, At), Count = kbts__View16(V, At + 2);
  kbts_un Total = 0;
  if(Format != 1 && Format != 2) V->Error = 1;
  if(!kbts__ViewRange(V, At + 4, Count, Format == 1 ? 2 : 6)) return 0;
  kbts_u32 Previous = 0;
  for(kbts_u32 I = 0; I < Count; ++I)
  {
    kbts_un P = At + 4 + I * (Format == 1 ? 2 : 6);
    kbts_u32 First = kbts__View16(V, P), Last = Format == 1 ? First : kbts__View16(V, P + 2);
    if(First > Last || (I && First <= Previous) || Last >= V->GlyphCount) V->Error = 1;
    if(Format == 2 && kbts__View16(V, P + 4) != Total) V->Error = 1;
    Total += Last - First + 1;
    Previous = Last;
  }
  return Total;
}

static void kbts__ValidateClass(kbts__font_view *V, kbts_un At, kbts_u32 Limit)
{
  kbts_u32 Format = kbts__View16(V, At), Count;
  if(Format == 1)
  {
    kbts_u32 First = kbts__View16(V, At + 2);
    Count = kbts__View16(V, At + 4);
    if(First + Count > 65536 || !kbts__ViewRange(V, At + 6, Count, 2)) { V->Error = 1; return; }
    for(kbts_u32 I = 0; I < Count; ++I) if(kbts__View16(V, At + 6 + 2 * I) >= Limit) V->Error = 1;
  }
  else if(Format == 2)
  {
    Count = kbts__View16(V, At + 2);
    if(!kbts__ViewRange(V, At + 4, Count, 6)) return;
    kbts_u32 Previous = 0;
    for(kbts_u32 I = 0; I < Count; ++I)
    {
      kbts_un P = At + 4 + 6 * I;
      kbts_u32 First = kbts__View16(V, P), Last = kbts__View16(V, P + 2);
      if(First > Last || (I && First <= Previous) || kbts__View16(V, P + 4) >= Limit) V->Error = 1;
      Previous = Last;
    }
  }
  else V->Error = 1;
}

static void kbts__ValidateDevice(kbts__font_view *V, kbts_un At)
{
  kbts_u32 First = kbts__View16(V, At), Last = kbts__View16(V, At + 2), Format = kbts__View16(V, At + 4);
  if(Format == 0x8000) return;
  if(Format < 1 || Format > 3 || Last < First) { V->Error = 1; return; }
  kbts__ViewRange(V, At + 6, ((Last - First + 1) * (1u << Format) + 15) / 16, 2);
}

static void kbts__ValidateAnchor(kbts__font_view *V, kbts_un Parent, kbts_un Offset)
{
  if(!Offset) return;
  kbts_un At = kbts__ViewRef(V, Parent, Offset);
  kbts_u32 Format = kbts__View16(V, At);
  if(Format < 1 || Format > 3) V->Error = 1;
  kbts__ViewRange(V, At, Format == 1 ? 6 : Format == 2 ? 8 : 10, 1);
  if(Format == 3) for(kbts_un I = 6; I <= 8; I += 2)
  {
    kbts_un Off = kbts__View16(V, At + I);
    if(Off) kbts__ValidateDevice(V, kbts__ViewRef(V, At, Off));
  }
}

static kbts_un kbts__ValidateValue(kbts__font_view *V, kbts_un Parent, kbts_un At, kbts_u32 Format)
{
  if(Format & ~255u) V->Error = 1;
  for(kbts_u32 Bit = 1; Bit <= 128; Bit <<= 1) if(Format & Bit)
  {
    kbts_un X = kbts__View16(V, At);
    if(Bit >= 16 && X) kbts__ValidateDevice(V, kbts__ViewRef(V, Parent, X));
    At += 2;
  }
  return At;
}

static void kbts__ValidateRecords(kbts__font_view *V, kbts_un At, kbts_un Count, kbts_un Inputs)
{
  if(!kbts__ViewRange(V, At, Count, 4)) return;
  for(kbts_un I = 0; I < Count; ++I)
    if(kbts__View16(V, At + 4 * I) >= Inputs || kbts__View16(V, At + 4 * I + 2) >= V->LookupCount) V->Error = 1;
}

static kbts_un kbts__ValidateContextRule(kbts__font_view *V, kbts_un At, int Chain, int Coverages, kbts_un Parent)
{
  kbts_un Inputs, Count, Back = 0, Ahead = 0, P = At;
  if(Chain)
  {
    Back = kbts__View16(V, P); P += 2;
    if(!kbts__ViewRange(V, P, Back, 2)) return 0;
    if(Coverages) for(kbts_un I = 0; I < Back; ++I) kbts__ValidateCoverage(V, kbts__ViewRef(V, Parent, kbts__View16(V, P + 2 * I)));
    P += 2 * Back;
  }
  Inputs = kbts__View16(V, P); P += 2;
  if(!Inputs) { V->Error = 1; return 0; }
  Count = 0;
  if(!Chain) { Count = kbts__View16(V, P); P += 2; }
  kbts_un Stored = Inputs - !Coverages;
  if(!kbts__ViewRange(V, P, Stored, 2)) return 0;
  if(Coverages) for(kbts_un I = 0; I < Stored; ++I) kbts__ValidateCoverage(V, kbts__ViewRef(V, Parent, kbts__View16(V, P + 2 * I)));
  P += 2 * Stored;
  if(Chain)
  {
    Ahead = kbts__View16(V, P); P += 2;
    if(!kbts__ViewRange(V, P, Ahead, 2)) return 0;
    if(Coverages) for(kbts_un I = 0; I < Ahead; ++I) kbts__ValidateCoverage(V, kbts__ViewRef(V, Parent, kbts__View16(V, P + 2 * I)));
    P += 2 * Ahead;
    Count = kbts__View16(V, P); P += 2;
  }
  kbts__ValidateRecords(V, P, Count, Inputs);
  return Back + Inputs + Ahead;
}

static kbts_un kbts__ValidateContext(kbts__font_view *V, kbts_un At, int Chain)
{
  kbts_u32 Format = kbts__View16(V, At);
  if(Format == 3) return kbts__ValidateContextRule(V, At + 2, Chain, 1, At);
  if(Format != 1 && Format != 2) { V->Error = 1; return 0; }
  kbts__ValidateCoverage(V, kbts__ViewRef(V, At, kbts__View16(V, At + 2)));
  kbts_un P = At + 4;
  if(Format == 2)
  {
    for(int I = 0; I < (Chain ? 3 : 1); ++I, P += 2)
    {
      kbts_un Offset = kbts__View16(V, P);
      if(Offset || !Chain) kbts__ValidateClass(V, kbts__ViewRef(V, At, Offset), 65536);
    }
  }
  kbts_un Count = kbts__View16(V, P), Window = 1; P += 2;
  if(!kbts__ViewRange(V, P, Count, 2)) return 0;
  for(kbts_un I = 0; I < Count && !V->Error; ++I)
  {
    kbts_un Offset = kbts__View16(V, P + 2 * I);
    if(!Offset) continue;
    kbts_un Set = kbts__ViewRef(V, At, Offset), N = kbts__View16(V, Set);
    if(!kbts__ViewRange(V, Set + 2, N, 2)) return 0;
    for(kbts_un J = 0; J < N && !V->Error; ++J)
    {
      kbts_un Rule = kbts__ViewRef(V, Set, kbts__View16(V, Set + 2 + 2 * J));
      kbts_un W = kbts__ValidateContextRule(V, Rule, Chain, 0, Rule);
      Window = KBTS__MAX(Window, W);
    }
  }
  return Window;
}

static void kbts__ValidateAnchorArray(kbts__font_view *V, kbts_un At, kbts_un Classes)
{
  kbts_un Count = kbts__View16(V, At);
  if(!kbts__ViewRange(V, At + 2, Count * Classes, 2)) return;
  for(kbts_un I = 0; I < Count * Classes; ++I) kbts__ValidateAnchor(V, At, kbts__View16(V, At + 2 + 2 * I));
}

static kbts_un kbts__ValidateSubtable(kbts__font_view *V, kbts_un At, kbts_u32 Type, int Gpos)
{
  kbts_u32 F = kbts__View16(V, At);
  if(Type == (Gpos ? 7u : 5u) || Type == (Gpos ? 8u : 6u))
    return kbts__ValidateContext(V, At, Type == (Gpos ? 8u : 6u));
  kbts_un Covered = kbts__ValidateCoverage(V, kbts__ViewRef(V, At, kbts__View16(V, At + 2)));
  if(!Gpos)
  {
    kbts_un Count = kbts__View16(V, At + 4);
    if(Type == 1)
    {
      if(F == 2) { kbts__ViewRange(V, At + 6, Count, 2); if(Count < Covered) V->Error = 1; }
      else if(F != 1) V->Error = 1;
    }
    else if(Type >= 2 && Type <= 4)
    {
      if(F != 1 || Count < Covered) V->Error = 1;
      if(!kbts__ViewRange(V, At + 6, Count, 2)) return 0;
      for(kbts_un I = 0; I < Count && !V->Error; ++I)
      {
        kbts_un Set = kbts__ViewRef(V, At, kbts__View16(V, At + 6 + 2 * I)), N = kbts__View16(V, Set);
        if(!kbts__ViewRange(V, Set + 2, N, 2)) return 0;
        if(Type == 4) for(kbts_un J = 0; J < N && !V->Error; ++J)
        {
          kbts_un Lig = kbts__ViewRef(V, Set, kbts__View16(V, Set + 2 + 2 * J));
          kbts_un Components = kbts__View16(V, Lig + 2);
          if(!Components) V->Error = 1;
          else kbts__ViewRange(V, Lig + 4, Components - 1, 2);
        }
      }
    }
    else if(Type == 8)
    {
      if(F != 1) V->Error = 1;
      kbts_un P = At + 4, W = 1;
      for(int K = 0; K < 2; ++K)
      {
        Count = kbts__View16(V, P); P += 2; W += Count;
        if(!kbts__ViewRange(V, P, Count, 2)) return 0;
        for(kbts_un I = 0; I < Count; ++I) kbts__ValidateCoverage(V, kbts__ViewRef(V, At, kbts__View16(V, P + 2 * I)));
        P += 2 * Count;
      }
      Count = kbts__View16(V, P);
      if(Count < Covered) V->Error = 1;
      kbts__ViewRange(V, P + 2, Count, 2);
      return W;
    }
    else V->Error = 1;
  }
  else if(Type == 1)
  {
    kbts_u32 VF = kbts__View16(V, At + 4);
    kbts_un N = F == 1 ? 1 : kbts__View16(V, At + 6), P = At + (F == 1 ? 6 : 8);
    if(F != 1 && F != 2) V->Error = 1;
    if(F == 2 && N < Covered) V->Error = 1;
    for(kbts_un I = 0; I < N && !V->Error; ++I) P = kbts__ValidateValue(V, At, P, VF);
  }
  else if(Type == 2)
  {
    kbts_u32 VF1 = kbts__View16(V, At + 4), VF2 = kbts__View16(V, At + 6);
    if(F == 1)
    {
      kbts_un N = kbts__View16(V, At + 8);
      if(N < Covered || !kbts__ViewRange(V, At + 10, N, 2)) { V->Error = 1; return 0; }
      for(kbts_un I = 0; I < N && !V->Error; ++I)
      {
        kbts_un Set = kbts__ViewRef(V, At, kbts__View16(V, At + 10 + 2 * I)), Count = kbts__View16(V, Set), P = Set + 2;
        for(kbts_un J = 0; J < Count && !V->Error; ++J)
        {
          kbts__View16(V, P); P += 2;
          P = kbts__ValidateValue(V, Set, P, VF1); P = kbts__ValidateValue(V, Set, P, VF2);
        }
      }
    }
    else if(F == 2)
    {
      kbts_un N1 = kbts__View16(V, At + 12), N2 = kbts__View16(V, At + 14), P = At + 16;
      kbts__ValidateClass(V, kbts__ViewRef(V, At, kbts__View16(V, At + 8)), (kbts_u32)N1);
      kbts__ValidateClass(V, kbts__ViewRef(V, At, kbts__View16(V, At + 10)), (kbts_u32)N2);
      kbts_un Width = 2 * (kbts__PopCount32(VF1) + kbts__PopCount32(VF2));
      if(!kbts__ViewRange(V, P, N1 * N2, Width)) return 0;
      if(Width) for(kbts_un I = 0; I < N1 * N2 && !V->Error; ++I)
      { P = kbts__ValidateValue(V, At, P, VF1); P = kbts__ValidateValue(V, At, P, VF2); }
    }
    else V->Error = 1;
  }
  else if(Type == 3)
  {
    kbts_un N = kbts__View16(V, At + 4);
    if(F != 1 || N < Covered) V->Error = 1;
    if(!kbts__ViewRange(V, At + 6, N, 4)) return 0;
    for(kbts_un I = 0; I < N * 2; ++I) kbts__ValidateAnchor(V, At, kbts__View16(V, At + 6 + 2 * I));
  }
  else if(Type >= 4 && Type <= 6)
  {
    if(F != 1) V->Error = 1;
    kbts_un BaseCovered = kbts__ValidateCoverage(V, kbts__ViewRef(V, At, kbts__View16(V, At + 4)));
    kbts_un Classes = kbts__View16(V, At + 6), Marks = kbts__ViewRef(V, At, kbts__View16(V, At + 8));
    kbts_un N = kbts__View16(V, Marks);
    if(!Classes || N < Covered || !kbts__ViewRange(V, Marks + 2, N, 4)) { V->Error = 1; return 0; }
    for(kbts_un I = 0; I < N; ++I)
    {
      if(kbts__View16(V, Marks + 2 + 4 * I) >= Classes) V->Error = 1;
      kbts__ValidateAnchor(V, Marks, kbts__View16(V, Marks + 4 + 4 * I));
    }
    kbts_un Bases = kbts__ViewRef(V, At, kbts__View16(V, At + 10));
    N = kbts__View16(V, Bases);
    if(N < BaseCovered) V->Error = 1;
    if(Type != 5) kbts__ValidateAnchorArray(V, Bases, Classes);
    else if(kbts__ViewRange(V, Bases + 2, N, 2)) for(kbts_un I = 0; I < N && !V->Error; ++I)
      kbts__ValidateAnchorArray(V, kbts__ViewRef(V, Bases, kbts__View16(V, Bases + 2 + 2 * I)), Classes);
  }
  else V->Error = 1;
  return 1;
}

static kbts_un kbts__ValidateGdef(kbts__font_view *V)
{
  if(!V->Size) return 0;
  kbts_u32 Major = kbts__View16(V, 0), Minor = kbts__View16(V, 2);
  if(Major != 1 || !kbts__ViewRange(V, 0, Minor >= 3 ? 18 : Minor >= 2 ? 14 : 12, 1)) { V->Error = 1; return 0; }
  kbts_un Off = kbts__View16(V, 4);
  if(Off) kbts__ValidateClass(V, kbts__ViewRef(V, 0, Off), 5);
  Off = kbts__View16(V, 10);
  if(Off) kbts__ValidateClass(V, kbts__ViewRef(V, 0, Off), 65536);
  if(Minor >= 2 && (Off = kbts__View16(V, 12)) != 0)
  {
    kbts_un At = kbts__ViewRef(V, 0, Off), N = kbts__View16(V, At + 2);
    if(kbts__View16(V, At) != 1) V->Error = 1;
    if(!kbts__ViewRange(V, At + 4, N, 4)) return 0;
    for(kbts_un I = 0; I < N && !V->Error; ++I) kbts__ValidateCoverage(V, kbts__ViewRef(V, At, kbts__View32(V, At + 4 + 4 * I)));
    return N;
  }
  return 0;
}

static void kbts__ValidateFeature(kbts__font_view *V, kbts_un At)
{
  kbts_un N = kbts__View16(V, At + 2);
  if(!kbts__ViewRange(V, At, 2 + N, 2)) return;
  for(kbts_un I = 0; I < N; ++I) if(kbts__View16(V, At + 4 + 2 * I) >= V->LookupCount) V->Error = 1;
}

static void kbts__ValidateLangsys(kbts__font_view *V, kbts_un At, kbts_un Features)
{
  kbts_un Required = kbts__View16(V, At + 2), N = kbts__View16(V, At + 4);
  if(Required != 65535 && Required >= Features) V->Error = 1;
  if(!kbts__ViewRange(V, At, 3 + N, 2)) return;
  for(kbts_un I = 0; I < N; ++I) if(kbts__View16(V, At + 6 + 2 * I) >= Features) V->Error = 1;
}

static void kbts__ValidateLayoutLists(kbts__font_view *V)
{
  kbts_un F = kbts__View16(V, 6), S = kbts__View16(V, 4), Features = 0;
  if(F)
  {
    F = kbts__ViewRef(V, 0, F); Features = kbts__View16(V, F);
    if(!kbts__ViewRange(V, F + 2, Features, 6)) return;
    for(kbts_un I = 0; I < Features && !V->Error; ++I) kbts__ValidateFeature(V, kbts__ViewRef(V, F, kbts__View16(V, F + 6 + 6 * I)));
  }
  if(S)
  {
    S = kbts__ViewRef(V, 0, S); kbts_un N = kbts__View16(V, S);
    if(!kbts__ViewRange(V, S + 2, N, 6)) return;
    for(kbts_un I = 0; I < N && !V->Error; ++I)
    {
      kbts_un Script = kbts__ViewRef(V, S, kbts__View16(V, S + 6 + 6 * I));
      kbts_un Default = kbts__View16(V, Script), Count = kbts__View16(V, Script + 2);
      if(Default) kbts__ValidateLangsys(V, kbts__ViewRef(V, Script, Default), Features);
      if(!kbts__ViewRange(V, Script + 4, Count, 6)) return;
      for(kbts_un J = 0; J < Count && !V->Error; ++J) kbts__ValidateLangsys(V, kbts__ViewRef(V, Script, kbts__View16(V, Script + 8 + 6 * J)), Features);
    }
  }
  if(kbts__View16(V, 2) == 1)
  {
    kbts_un Off = kbts__View32(V, 10);
    if(Off)
    {
      kbts_un At = kbts__ViewRef(V, 0, Off), N = kbts__View32(V, At + 4);
      if(!kbts__ViewRange(V, At + 8, N, 8)) return;
      for(kbts_un I = 0; I < N && !V->Error; ++I)
      {
        kbts_un Set = kbts__ViewRef(V, At, kbts__View32(V, At + 8 + 8 * I)), C = kbts__View16(V, Set);
        if(!kbts__ViewRange(V, Set + 2, C, 4)) return;
        for(kbts_un J = 0; J < C; ++J) kbts__ViewRange(V, kbts__ViewRef(V, Set, kbts__View32(V, Set + 2 + 4 * J)), 4, 2);
        kbts_un Sub = kbts__ViewRef(V, At, kbts__View32(V, At + 12 + 8 * I));
        C = kbts__View16(V, Sub + 4);
        if(!kbts__ViewRange(V, Sub + 6, C, 6)) return;
        for(kbts_un J = 0; J < C && !V->Error; ++J)
        {
          if(kbts__View16(V, Sub + 6 + 6 * J) >= Features) V->Error = 1;
          kbts__ValidateFeature(V, kbts__ViewRef(V, Sub, kbts__View32(V, Sub + 8 + 6 * J)));
        }
      }
    }
  }
}

static int kbts__ValidateLayout(kbts__font_view *V, int Gpos, kbts_un MarkSets,
    kbts_u32 *LookupCount, kbts_u32 *SubtableCount, kbts__font_tables *Tables,
    kbts_u32 FirstLookup, kbts_u32 FirstSubtable, kbts__font_view *Gdef)
{
  if(!V->Size) { *LookupCount = *SubtableCount = 0; return 1; }
  if(kbts__View16(V, 0) != 1 || kbts__View16(V, 2) > 1) V->Error = 1;
  kbts_un List = kbts__ViewRef(V, 0, kbts__View16(V, 8)), N = kbts__View16(V, List), Total = 0;
  if(Tables && (FirstLookup > Tables->LookupCount || N > Tables->LookupCount - FirstLookup)) return 0;
  V->LookupCount = (kbts_u32)N;
  kbts__ValidateLayoutLists(V);
  if(!kbts__ViewRange(V, List + 2, N, 2)) return 0;
  for(kbts_un I = 0; I < N && !V->Error; ++I)
  {
    kbts_un At = kbts__ViewRef(V, List, kbts__View16(V, List + 2 + 2 * I));
    kbts_u32 Type = kbts__View16(V, At), Flags = kbts__View16(V, At + 2), Count = kbts__View16(V, At + 4), Resolved = 0;
    if(!Type || Type > (Gpos ? 9u : 8u) || (Flags & 0x00e0)) V->Error = 1;
    if(!kbts__ViewRange(V, At + 6, Count + !!(Flags & 16), 2)) return 0;
    if(Tables && ((kbts_u64)FirstSubtable + Total + Count > Tables->SubtableCount)) return 0;
    kbts_un MarkSet = Flags & 16 ? kbts__View16(V, At + 6 + 2 * Count) : 0;
    if((Flags & 16) && MarkSet >= MarkSets) return 0;
    kbts__font_lookup *L = Tables ? &Tables->Lookups[FirstLookup + I] : 0;
    if(L)
    {
      L->Packed = (kbts__lookup *)(V->Data + At); L->FirstSubtable = FirstSubtable + (kbts_u32)Total;
      L->Filter.Flags = (kbts_u16)Flags; L->Filter.SubtableCount = (kbts_u16)Count;
      if(Flags & 16)
      {
        kbts_un Sets = kbts__View16(Gdef, 12);
        kbts_un Cov = kbts__ViewRef(Gdef, Sets, kbts__View32(Gdef, Sets + 4 + 4 * MarkSet));
        if(Gdef->Error) return 0;
        L->Filter.MarkFilteringSet = (kbts__coverage *)(Gdef->Data + Cov);
      }
    }
    for(kbts_un J = 0; J < Count && !V->Error; ++J)
    {
      kbts_un Sub = kbts__ViewRef(V, At, kbts__View16(V, At + 6 + 2 * J));
      kbts_u32 Native = Type;
      if(Type == (Gpos ? 9u : 7u))
      {
        Native = kbts__View16(V, Sub + 2);
        kbts_un Off = kbts__View32(V, Sub + 4);
        if(kbts__View16(V, Sub) != 1 || !Native || Native > (Gpos ? 8u : 8u) || Native == (Gpos ? 9u : 7u) || Off < 8) V->Error = 1;
        Sub = kbts__ViewRef(V, Sub, Off);
      }
      if(J && Native != Resolved) V->Error = 1;
      Resolved = Native;
      kbts_un Window = kbts__ValidateSubtable(V, Sub, Native, Gpos);
      if(L && !V->Error)
      {
        kbts__font_subtable *D = &Tables->Subtables[FirstSubtable + Total + J];
        D->Data = (kbts_u16 *)(V->Data + Sub); D->Length = (kbts_u32)(V->Size - Sub); D->Type = (kbts_u16)Native;
        L->WindowCapacity = (kbts_u32)KBTS__MAX(L->WindowCapacity, Window);
      }
    }
    if(L) { L->Filter.Type = (kbts_u16)(Count ? Resolved : Type); L->Reverse = !Gpos && Resolved == 8; }
    Total += Count;
    if(Total > 0xffffffffu) V->Error = 1;
  }
  *LookupCount = (kbts_u32)N; *SubtableCount = (kbts_u32)Total;
  return !V->Error;
}

/* Selection is shared by validation and loading. Only the selected nominal
 * cmap payload is native-endian in a blob; unselected payloads remain SFNT
 * bytes, including blobs made by earlier versions of the loader. */
static kbts_un kbts__SelectCmapView(kbts__font_view *V, kbts_u16 *SelectedFormat, kbts_un *Uvs)
{
  kbts_un Selected = V->Size, Count = kbts__View16(V, 2);
  *Uvs = V->Size;
  if(!kbts__ViewRange(V, 4, Count, 8)) return Selected;
  for(kbts_un I = 0; I < Count && !V->Error; ++I)
  {
    kbts_un At = kbts__View32(V, 8 + 8 * I);
    kbts_u16 Format = kbts__View16(V, At);
    if(Format > 255 && (Format >> 8) <= 14) Format = kbts__ByteSwap16(Format);
    if(Format == 14) *Uvs = At;
    else if(Selected == V->Size ||
        (Format < KBTS__ARRAY_LENGTH(kbts__CmapFormatPrecedence) &&
         (kbts__CmapFormatPrecedence[Format] > kbts__CmapFormatPrecedence[1] ||
          (kbts__CmapFormatPrecedence[Format] == kbts__CmapFormatPrecedence[1] &&
           kbts__View16(V, 4 + 8 * I) == 3))))
    {
      Selected = At;
      *SelectedFormat = Format;
    }
  }
  return Selected;
}

static int kbts__ValidateCmap(kbts__font_view *V)
{
  if(!V->Size) return 1;
  kbts_u16 SelectedFormat = 0;
  kbts_un Uvs, Selected = kbts__SelectCmapView(V, &SelectedFormat, &Uvs);
  if(V->Error) return 0;
  kbts_un Offsets[2] = {Selected, Uvs};
  for(kbts_un I = 0; I < 2 && !V->Error; ++I)
  {
    kbts_un At = Offsets[I];
    if(At == V->Size) continue;
    if(!kbts__ViewRange(V, At, 1, 2)) return 0;
    kbts__font_view S = *V;
    S.Data += At; S.Size -= At;
    kbts_u32 F = kbts__View16(&S, 0);
    if(!S.BigEndian && F > 255) { S.BigEndian = 1; F = kbts__View16(&S, 0); }
    if((At & 1) && (F == 2 || F == 4 || F == 6 || F == 12 || F == 13)) return 0;
    if(F == 0 || F == 2 || F == 4 || F == 6 || F == 12 || F == 13 || F == 14)
    {
      if(F == 14) S.BigEndian = 1;
      kbts_un Length = F == 14 ? kbts__View32(&S, 2) :
          F >= 12 ? kbts__View32(&S, 4) : kbts__View16(&S, 2);
      if(!Length || !kbts__ViewRange(&S, 0, Length, 1)) return 0;
      S.Size = Length;
    }
    if(F == 0) kbts__ViewRange(&S, 0, 262, 1);
    else if(F == 2)
    {
      if(!kbts__ViewRange(&S, 0, 518, 1)) return 0;
      for(kbts_un J = 0; J < 256 && !S.Error; ++J)
      {
        kbts_un Key = kbts__View16(&S, 6 + 2 * J);
        if(Key & 7) S.Error = 1;
        kbts_un P = 518 + Key, Count = kbts__View16(&S, P + 2), Off = kbts__View16(&S, P + 6);
        kbts__ViewRange(&S, P, 8, 1);
        if(Count) kbts__ViewRange(&S, P + 6 + Off, Count, 2);
      }
    }
    else if(F == 4)
    {
      kbts_un Twice = kbts__View16(&S, 6), Count = Twice / 2;
      if(!Count || (Twice & 1)) S.Error = 1;
      if(!kbts__ViewRange(&S, 14, 4 * Count + 1, 2)) return 0;
      kbts_u32 Previous = 0;
      for(kbts_un J = 0; J < Count && !S.Error; ++J)
      {
        kbts_u32 Last = kbts__View16(&S, 14 + 2 * J), First = kbts__View16(&S, 16 + 2 * Count + 2 * J);
        kbts_un P = 16 + 6 * Count + 2 * J, Off = kbts__View16(&S, P);
        if(First > Last || (J && Last < Previous) || (Off & 1)) S.Error = 1;
        if(Off) kbts__ViewRange(&S, P + Off, Last - First + 1, 2);
        Previous = Last;
      }
    }
    else if(F == 6) kbts__ViewRange(&S, 10, kbts__View16(&S, 8), 2);
    else if(F == 12 || F == 13) kbts__ViewRange(&S, 16, kbts__View32(&S, 12), 12);
    else if(F == 14)
    {
      /* Format is normalized by selection; UVS payload remains big-endian. */
      S.BigEndian = 1;
      kbts_un Count = kbts__View32(&S, 6);
      if(!kbts__ViewRange(&S, 10, Count, 11)) return 0;
      for(kbts_un J = 0; J < Count && !S.Error; ++J)
        for(kbts_un K = 0; K < 2; ++K)
        {
          kbts_un Off = kbts__View32(&S, 13 + 11 * J + 4 * K);
          if(Off) kbts__ViewRange(&S, Off + 4, kbts__View32(&S, Off), K ? 5 : 4);
        }
    }
    V->Error |= S.Error;
  }
  return !V->Error;
}

static kbts__font_view kbts__TableView(const void *Data, kbts_un Size,
    const kbts_blob_table *Table, int BigEndian, kbts_u32 GlyphCount)
{
  kbts__font_view V = KBTS__ZERO;
  V.BigEndian = BigEndian; V.GlyphCount = GlyphCount;
  if(!kbts__FontRange(Size, Table->OffsetFromStartOfFile, Table->Length, 1)) V.Error = 1;
  else { V.Data = (const kbts_u8 *)Data + Table->OffsetFromStartOfFile; V.Size = Table->Length; }
  return V;
}

static int kbts__ValidateFontTables(const void *Data, kbts_un Size, const kbts_blob_table *Directory,
    int BigEndian, kbts_u32 GlyphCount, kbts_u32 *Lookups, kbts_u32 *Subtables, kbts__font_tables *Tables)
{
  kbts__font_view Head = kbts__TableView(Data, Size, &Directory[KBTS_BLOB_TABLE_ID_HEAD], BigEndian, GlyphCount);
  if(Head.Error || (Head.Size && !kbts__ViewRange(&Head, 0, sizeof(kbts__head), 1))) return 0;
  kbts__font_view Maxp = kbts__TableView(Data, Size, &Directory[KBTS_BLOB_TABLE_ID_MAXP], BigEndian, GlyphCount);
  if(Maxp.Error || (Maxp.Size && (kbts__View16(&Maxp, 4) != GlyphCount || Maxp.Error))) return 0;
  for(int Vertical = 0; Vertical < 2; ++Vertical)
  {
    kbts__font_view Hea = kbts__TableView(Data, Size, &Directory[Vertical ? KBTS_BLOB_TABLE_ID_VHEA : KBTS_BLOB_TABLE_ID_HHEA], BigEndian, GlyphCount);
    kbts__font_view Mtx = kbts__TableView(Data, Size, &Directory[Vertical ? KBTS_BLOB_TABLE_ID_VMTX : KBTS_BLOB_TABLE_ID_HMTX], BigEndian, GlyphCount);
    if(Hea.Error || Mtx.Error || (Hea.Size && !kbts__ViewRange(&Hea, 0, sizeof(kbts__hea), 1))) return 0;
    if(Hea.Size && Mtx.Size)
    {
      kbts_un Metrics = kbts__View16(&Hea, 34);
      if(!Metrics || !kbts__ViewRange(&Mtx, 0, Metrics, sizeof(kbts__long_mtx))) return 0;
    }
  }
  kbts__font_view Gdef = kbts__TableView(Data, Size, &Directory[KBTS_BLOB_TABLE_ID_GDEF], BigEndian, GlyphCount);
  kbts_un MarkSets = kbts__ValidateGdef(&Gdef);
  if(Gdef.Error) return 0;
  kbts__font_view Cmap = kbts__TableView(Data, Size, &Directory[KBTS_BLOB_TABLE_ID_CMAP], BigEndian, GlyphCount);
  if(!kbts__ValidateCmap(&Cmap)) return 0;
  kbts_u32 FlatLookup = 0, FlatSubtable = 0;
  for(int Gpos = 0; Gpos < 2; ++Gpos)
  {
    kbts__font_view V = kbts__TableView(Data, Size, &Directory[Gpos ? KBTS_BLOB_TABLE_ID_GPOS : KBTS_BLOB_TABLE_ID_GSUB], BigEndian, GlyphCount);
    if(!kbts__ValidateLayout(&V, Gpos, MarkSets, &Lookups[Gpos], &Subtables[Gpos], Tables, FlatLookup, FlatSubtable, &Gdef)) return 0;
    FlatLookup += Lookups[Gpos]; FlatSubtable += Subtables[Gpos];
    if(FlatSubtable < Subtables[Gpos]) return 0;
  }
  return 1;
}

static int kbts__ValidateNativeDirectory(const void *Data, kbts_un Size, kbts_blob_header *Header)
{
  if(!Data || Size < sizeof(*Header)) return 0;
  KBTS_MEMCPY(Header, Data, sizeof(*Header));
  if(Header->Magic != KBTS_FOURCC('k', 'b', 't', 's') || Header->Version != KBTS_BLOB_VERSION_CURRENT ||
     Header->GlyphCount > 65535 || Header->LookupCount > 131070 ||
     Header->LookupSubtableCount > Size / 2) return 0;
  for(kbts_un I = 0; I < KBTS_BLOB_TABLE_ID_COUNT; ++I)
  {
    kbts_blob_table *T = &Header->Tables[I];
    if(!kbts__FontRange(Size, T->OffsetFromStartOfFile, T->Length, 1) ||
       (T->Length && (T->OffsetFromStartOfFile < sizeof(*Header) || (T->OffsetFromStartOfFile & 3)))) return 0;
  }
  kbts_u64 LookupWords = ((kbts_u64)Header->LookupCount * Header->GlyphCount + 31) / 32;
  kbts_u64 SubtableWords = ((kbts_u64)Header->LookupSubtableCount * Header->GlyphCount + 31) / 32;
  if(LookupWords > Size / 4 || SubtableWords > Size / 4 ||
     (Header->GlyphLookupMatrixOffsetFromStartOfFile & 3) ||
     (Header->GlyphLookupSubtableMatrixOffsetFromStartOfFile & 3) ||
     (Header->LookupSubtableIndexOffsetsOffsetFromStartOfFile & 3) ||
     !kbts__FontRange(Size, Header->GlyphLookupMatrixOffsetFromStartOfFile, (kbts_un)LookupWords, 4) ||
     !kbts__FontRange(Size, Header->GlyphLookupSubtableMatrixOffsetFromStartOfFile, (kbts_un)SubtableWords, 4) ||
     !kbts__FontRange(Size, Header->LookupSubtableIndexOffsetsOffsetFromStartOfFile, Header->LookupCount, 4)) return 0;
  return 1;
}

static int kbts__ValidateNativeBlob(const void *Data, kbts_un Size, kbts_blob_header *Header,
    kbts__font_tables *Tables)
{
  if(!kbts__ValidateNativeDirectory(Data, Size, Header)) return 0;
  kbts_u32 L[2], S[2];
  if(!kbts__ValidateFontTables(Data, Size, Header->Tables, 0, Header->GlyphCount, L, S, Tables) ||
     L[0] != Header->GposLookupIndexOffset || L[0] + L[1] != Header->LookupCount ||
     (kbts_u64)S[0] + S[1] != Header->LookupSubtableCount) return 0;
  kbts_u32 Flat = 0, Sub = 0;
  for(int Gpos = 0; Gpos < 2; ++Gpos)
  {
    kbts__font_view V = kbts__TableView(Data, Size, &Header->Tables[Gpos ? KBTS_BLOB_TABLE_ID_GPOS : KBTS_BLOB_TABLE_ID_GSUB], 0, Header->GlyphCount);
    if(!V.Size) continue;
    kbts_un List = kbts__View16(&V, 8);
    for(kbts_u32 I = 0; I < L[Gpos]; ++I, ++Flat)
    {
      kbts_un At = List + kbts__View16(&V, List + 2 + 2 * I);
      if(kbts__ReadU32Unaligned((const kbts_u8 *)Data + Header->LookupSubtableIndexOffsetsOffsetFromStartOfFile + 4 * Flat) != Sub) return 0;
      Sub += kbts__View16(&V, At + 4);
    }
  }
  return 1;
}

static void kbts__FillGlyphClasses(kbts_glyph_classes *Classes, kbts_u32 GlyphCount,
    kbts__gdef *Gdef, kbts_u16 Offset, int Attachment)
{
  if(!Offset) return;
  kbts_u16 *Definition = KBTS__POINTER_OFFSET(kbts_u16, Gdef, Offset);
  kbts_u32 Count = Definition[Definition[0] == 1 ? 2 : 1];
  for(kbts_u32 I = 0; I < Count; ++I)
  {
    kbts_u32 First, End;
    kbts_u16 Class;
    if(Definition[0] == 1)
    {
      First = Definition[1] + I; End = First + 1; Class = Definition[3 + I];
    }
    else
    {
      First = Definition[2 + 3 * I]; End = (kbts_u32)Definition[3 + 3 * I] + 1; Class = Definition[4 + 3 * I];
    }
    End = KBTS__MIN(End, GlyphCount);
    for(kbts_u32 Glyph = First; Glyph < End; ++Glyph)
    {
      if(!Attachment) Classes[Glyph].Class = Class;
      else if(Classes[Glyph].Class == KBTS__GLYPH_CLASS_MARK) Classes[Glyph].MarkAttachmentClass = Class;
    }
  }
}

static kbts_load_font_error kbts__CompileFontTables(kbts_font *Font,
    kbts_allocator_function *Allocator, void *AllocatorData)
{
  if(Font->Tables) return KBTS_LOAD_FONT_ERROR_NONE;
  kbts_blob_header Header;
  if(((kbts_un)Font->Blob & (KBTS_ALIGNOF(kbts_blob_header) - 1)) ||
     !kbts__ValidateNativeDirectory(Font->Blob, Font->BlobSize, &Header)) return KBTS_LOAD_FONT_ERROR_INVALID_FONT;
  kbts_u64 Bytes = sizeof(kbts__font_tables) + KBTS_ALIGNOF(kbts__font_tables) - 1 +
      (kbts_u64)Header.LookupCount * sizeof(kbts__font_lookup) +
      (kbts_u64)Header.LookupSubtableCount * sizeof(kbts__font_subtable) +
      (kbts_u64)Header.GlyphCount * sizeof(kbts_glyph_classes) + 3 * KBTS_ALIGNOF(kbts__font_lookup);
  if(Bytes > (kbts_un)-1) return KBTS_LOAD_FONT_ERROR_INVALID_FONT;
  if(!Allocator) Allocator = kbts__DefaultAllocator;
  void *Allocation = kbts__AllocatorAllocate(Allocator, AllocatorData, (kbts_un)Bytes);
  if(!Allocation) return KBTS_LOAD_FONT_ERROR_OUT_OF_MEMORY;
  kbts__pointer_bump_allocator Bump = kbts__PointerBumpAllocator(Allocation);
  kbts__font_tables *T = kbts__PointerPushType(&Bump, kbts__font_tables);
  *T = KBTS__ZERO_TYPE(kbts__font_tables);
  T->Allocator = Allocator; T->AllocatorData = AllocatorData; T->BaseAllocation = Allocation; T->AllocationSize = (kbts_un)Bytes;
  T->LookupCount = Header.LookupCount; T->SubtableCount = Header.LookupSubtableCount;
  T->Lookups = kbts__PointerPushArray(&Bump, kbts__font_lookup, Header.LookupCount);
  T->Subtables = kbts__PointerPushArray(&Bump, kbts__font_subtable, Header.LookupSubtableCount);
  T->GlyphClasses = kbts__PointerPushArray(&Bump, kbts_glyph_classes, Header.GlyphCount);
  KBTS_MEMSET(T->Lookups, 0, Header.LookupCount * sizeof(*T->Lookups));
  KBTS_MEMSET(T->GlyphClasses, 0, Header.GlyphCount * sizeof(*T->GlyphClasses));
  if(!kbts__ValidateNativeBlob(Font->Blob, Font->BlobSize, &Header, T))
  { kbts__AllocatorFree(Allocator, AllocatorData, Allocation); return KBTS_LOAD_FONT_ERROR_INVALID_FONT; }
  kbts__gdef *Gdef = kbts__BlobTableDataType(Font->Blob, KBTS_BLOB_TABLE_ID_GDEF, kbts__gdef);
  if(Gdef)
  {
    kbts__FillGlyphClasses(T->GlyphClasses, Header.GlyphCount, Gdef, Gdef->ClassDefinitionOffset, 0);
    kbts__FillGlyphClasses(T->GlyphClasses, Header.GlyphCount, Gdef, Gdef->MarkAttachmentClassDefinitionOffset, 1);
  }
  Font->Tables = T;
  return KBTS_LOAD_FONT_ERROR_NONE;
}
