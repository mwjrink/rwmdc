/* Canonical records occupy stable slots. A zero Ref is a tombstone; logical
 * order lives in slot links, not in a second glyph representation. Only backing
 * allocation growth copies records. Insertion, deletion and reordering do not. */
static void kbts__GsubInsertSlot(kbts_shape_scratchpad *Scratchpad, kbts_u32 Slot);
static void kbts__GsubRemoveSlot(kbts_shape_scratchpad *Scratchpad, kbts_u32 Slot);

static kbts_un kbts__GlyphStorageOverhead(void)
{
  return KBTS_ALIGNOF(kbts_glyph) - 1 + KBTS_ALIGNOF(kbts__glyph_link) - 1 +
         KBTS_ALIGNOF(kbts_u32) - 1;
}

static kbts_un kbts__GlyphStorageStride(void)
{
  return sizeof(kbts_glyph) + sizeof(kbts__glyph_link) + sizeof(kbts_u32);
}

static void kbts__BindGlyphStorage(kbts_glyph_storage *Storage, void *Memory, kbts_u32 Capacity)
{
  kbts__pointer_bump_allocator Bump = kbts__PointerBumpAllocator(Memory);
  Storage->Memory = Memory;
  Storage->Glyphs = kbts__PointerPushArray(&Bump, kbts_glyph, Capacity);
  Storage->Links = kbts__PointerPushArray(&Bump, kbts__glyph_link, Capacity);
  Storage->FreeSlots = kbts__PointerPushArray(&Bump, kbts_u32, Capacity);
  Storage->Capacity = Capacity;
}

static kbts_b32 kbts__ReserveGlyphs(kbts_glyph_storage *Storage, kbts_u32 Required)
{
  if(Storage->Error) return 0;
  Required = KBTS__MAX(Required, Storage->Count);
  if(Required <= Storage->Capacity) return 1;
  kbts_un Overhead = kbts__GlyphStorageOverhead();
  kbts_un Stride = kbts__GlyphStorageStride();
  kbts_u32 MaximumCapacity = (kbts_u32)(((kbts_un)(kbts_u32)-1 - Overhead) / Stride);
  if(Storage->FixedMemory || Required > MaximumCapacity)
  {
    Storage->Error = 1;
    return 0;
  }
  kbts_u32 Capacity = Storage->Capacity ? Storage->Capacity : KBTS__MIN(32, MaximumCapacity);
  while(Capacity < Required)
  {
    if(Capacity > MaximumCapacity / 2) { Capacity = MaximumCapacity; break; }
    Capacity *= 2;
  }
  kbts_un Size = Overhead + (kbts_un)Capacity * Stride;
  void *Memory = kbts__AllocatorAllocate(Storage->Allocator, Storage->AllocatorData, Size);
  if(!Memory)
  {
    Storage->Error = 1;
    return 0;
  }
  if((kbts_uptr)Memory > (kbts_uptr)-1 - Size)
  {
    kbts__AllocatorFree(Storage->Allocator, Storage->AllocatorData, Memory);
    Storage->Error = 1;
    return 0;
  }
  kbts_glyph_storage NewStorage = *Storage;
  kbts__BindGlyphStorage(&NewStorage, Memory, Capacity);
  if(Storage->Count)
  {
    KBTS_MEMCPY(NewStorage.Glyphs, Storage->Glyphs, (kbts_un)Storage->Count * sizeof(*Storage->Glyphs));
    KBTS_MEMCPY(NewStorage.Links, Storage->Links, (kbts_un)Storage->Count * sizeof(*Storage->Links));
  }
  if(Storage->FreeSlotCount)
    KBTS_MEMCPY(NewStorage.FreeSlots, Storage->FreeSlots,
                (kbts_un)Storage->FreeSlotCount * sizeof(*Storage->FreeSlots));
  void *OldMemory = Storage->Memory;
  *Storage = NewStorage;
  kbts__AllocatorFree(Storage->Allocator, Storage->AllocatorData, OldMemory);
  return 1;
}

static void kbts__LinkGlyphBefore(kbts_glyph_storage *Storage, kbts_u32 Slot, kbts_u32 Next)
{
  kbts_u32 Prev = Next == KBTS__GLYPH_END ? Storage->Tail : Storage->Links[Next].Prev;
  Storage->Links[Slot].Prev = Prev;
  Storage->Links[Slot].Next = Next;
  if(Prev == KBTS__GLYPH_END) Storage->Head = Slot;
  else Storage->Links[Prev].Next = Slot;
  if(Next == KBTS__GLYPH_END) Storage->Tail = Slot;
  else Storage->Links[Next].Prev = Slot;
  if(Storage->First == Next) Storage->First = Slot;
  if((Prev != KBTS__GLYPH_END && Prev > Slot) || (Next != KBTS__GLYPH_END && Next < Slot))
    Storage->Ordered = 0;
}

static void kbts__UnlinkGlyph(kbts_glyph_storage *Storage, kbts_u32 Slot)
{
  kbts_u32 Prev = Storage->Links[Slot].Prev, Next = Storage->Links[Slot].Next;
  if(Prev == KBTS__GLYPH_END) Storage->Head = Next;
  else Storage->Links[Prev].Next = Next;
  if(Next == KBTS__GLYPH_END) Storage->Tail = Prev;
  else Storage->Links[Next].Prev = Prev;
  if(Storage->First == Slot) Storage->First = Next;
}

/* Allocate a slot immediately before the logical boundary, not at its numeric
 * array position. Reusing a tombstone never changes a surviving slot identity. */
static kbts_glyph_ref kbts__AllocateGlyph(kbts_glyph_storage *Storage, kbts_u32 Before)
{
  /* Preserve the public zero-initialized-storage entry point. */
  if(!Storage->Count && !Storage->First && !Storage->End)
  {
    Storage->Head = Storage->Tail = Storage->First = Storage->End = KBTS__GLYPH_END;
    Storage->Ordered = 1;
    Before = KBTS__GLYPH_END;
  }
  if(Storage->Error) return 0;
  if(!Storage->FreeSlotCount && Storage->Count == (kbts_u32)-1)
  {
    Storage->Error = 1;
    return 0;
  }
  if(!kbts__ReserveGlyphs(Storage, Storage->Count + !Storage->FreeSlotCount)) return 0;
  kbts_u32 Slot = Storage->FreeSlotCount ? Storage->FreeSlots[--Storage->FreeSlotCount] : Storage->Count++;
  Storage->Glyphs[Slot].Ref = Slot + 1;
  Storage->Links[Slot].FirstAttached = 0;
  Storage->Links[Slot].NextAttached = 0;
  Storage->Links[Slot].PrevAttached = 0;
  kbts__LinkGlyphBefore(Storage, Slot, Before);
  ++Storage->LiveCount;
  return Slot + 1;
}

static kbts_glyph_ref kbts__InsertGlyph(kbts_glyph_storage *Storage, kbts_glyph_ref Anchor,
                                      kbts_b32 InsertBeforeAnchor, const kbts_glyph *BaseGlyph)
{
  if(Storage->Error) return 0;
  kbts_glyph Copy = BaseGlyph ? *BaseGlyph : KBTS__ZERO_TYPE(kbts_glyph);
  Copy.Bucketed = 0;
  kbts_u32 Before = Anchor ? (InsertBeforeAnchor ? Anchor - 1 : Storage->Links[Anchor - 1].Next) :
                            (InsertBeforeAnchor ? Storage->End : Storage->First);
  kbts_glyph_ref Ref = kbts__AllocateGlyph(Storage, Before);
  if(Ref)
  {
    Copy.Ref = Ref;
    Storage->Glyphs[Ref - 1] = Copy;
    if(Storage->IndexedScratchpad) kbts__GsubInsertSlot(Storage->IndexedScratchpad, Ref - 1);
  }
  return Ref;
}

static kbts_glyph_ref kbts__InsertGlyphBefore(kbts_glyph_storage *Storage, kbts_glyph_ref Anchor, const kbts_glyph *BaseGlyph)
{
  return kbts__InsertGlyph(Storage, Anchor, 1, BaseGlyph);
}

static kbts_glyph_ref kbts__InsertGlyphAfter(kbts_glyph_storage *Storage, kbts_glyph_ref Anchor, const kbts_glyph *BaseGlyph)
{
  return kbts__InsertGlyph(Storage, Anchor, 0, BaseGlyph);
}

static kbts_glyph_ref kbts__FreeGlyph(kbts_shape_scratchpad *Scratchpad, kbts_glyph_storage *Storage, kbts_glyph_ref Ref)
{
  if(!kbts__GlyphIsValid(Storage, Ref)) return 0;
  kbts_glyph_ref Next = kbts__NextGlyphRef(Storage, Ref);
  if(Scratchpad)
  {
    if(Ref == Scratchpad->LookupOnePastLastGlyph) Scratchpad->LookupOnePastLastGlyph = Next;
    kbts__UnbucketGlyph(Scratchpad, kbts__Glyph(Storage, Ref));
  }
  kbts_u32 Slot = Ref - 1;
  if(Storage->IndexedScratchpad) kbts__GsubRemoveSlot(Storage->IndexedScratchpad, Slot);
  kbts__UnlinkGlyph(Storage, Slot);
  Storage->Glyphs[Slot].Ref = 0;
  Storage->FreeSlots[Storage->FreeSlotCount++] = Slot;
  --Storage->LiveCount;
  return Next;
}

static kbts_glyph *kbts__SetGlyphPreserveIdentityAndUserId(kbts_glyph *Dest, const kbts_glyph *Source)
{
  kbts_glyph_ref Ref = Dest->Ref;
  int UserId = Dest->UserIdOrCodepointIndex;
  *Dest = *Source;
  Dest->Ref = Ref;
  Dest->UserIdOrCodepointIndex = UserId;
  return Dest;
}

static void kbts__MoveGlyph(kbts_glyph_storage *Storage, kbts_glyph_ref Ref,
                            kbts_glyph_ref Anchor, kbts_b32 Before)
{
  if(Ref == Anchor) return;
  KBTS_ASSERT(kbts__GlyphIsValid(Storage, Ref));
  KBTS_ASSERT(!Anchor || kbts__GlyphIsValid(Storage, Anchor));
  kbts_u32 Slot = Ref - 1;
  kbts_u32 Next = Anchor ? (Before ? Anchor - 1 : Storage->Links[Anchor - 1].Next) :
                          (Before ? Storage->End : Storage->First);
  if(Next == Slot || Storage->Links[Slot].Next == Next) return;
  kbts__UnlinkGlyph(Storage, Slot);
  kbts__LinkGlyphBefore(Storage, Slot, Next);
  Storage->Ordered = 0;
}

static void kbts__MoveGlyphBefore(kbts_glyph_storage *Storage, kbts_glyph_ref Ref, kbts_glyph_ref Anchor)
{
  kbts__MoveGlyph(Storage, Ref, Anchor, 1);
}

static void kbts__MoveGlyphAfter(kbts_glyph_storage *Storage, kbts_glyph_ref Ref, kbts_glyph_ref Anchor)
{
  kbts__MoveGlyph(Storage, Ref, Anchor, 0);
}

static void kbts__SwapGlyphs(kbts_glyph_storage *Storage, kbts_glyph_ref A, kbts_glyph_ref B)
{
  if(A == B) return;
  KBTS_ASSERT(kbts__GlyphIsValid(Storage, A) && kbts__GlyphIsValid(Storage, B));
  kbts_u32 ANext = Storage->Links[A - 1].Next, BNext = Storage->Links[B - 1].Next;
  if(ANext == B - 1) kbts__MoveGlyphBefore(Storage, B, A);
  else if(BNext == A - 1) kbts__MoveGlyphBefore(Storage, A, B);
  else
  {
    kbts__UnlinkGlyph(Storage, A - 1);
    kbts__UnlinkGlyph(Storage, B - 1);
    kbts__LinkGlyphBefore(Storage, A - 1, BNext);
    kbts__LinkGlyphBefore(Storage, B - 1, ANext);
    Storage->Ordered = 0;
  }
}

static void kbts__ReverseGlyphRange(kbts_glyph_storage *Storage, kbts_glyph_ref First, kbts_glyph_ref End)
{
  kbts_u32 At = First ? First - 1 : Storage->First;
  kbts_u32 After = End ? End - 1 : Storage->End;
  if(At == After) return;
  kbts_u32 Before = Storage->Links[At].Prev, OldFirst = At, Last = At;
  while(At != After)
  {
    kbts_u32 Next = Storage->Links[At].Next;
    Storage->Links[At].Next = Storage->Links[At].Prev;
    Storage->Links[At].Prev = Next;
    Last = At;
    At = Next;
  }
  Storage->Links[Last].Prev = Before;
  Storage->Links[OldFirst].Next = After;
  if(Before == KBTS__GLYPH_END) Storage->Head = Last;
  else Storage->Links[Before].Next = Last;
  if(After == KBTS__GLYPH_END) Storage->Tail = OldFirst;
  else Storage->Links[After].Prev = OldFirst;
  if(Storage->First == OldFirst) Storage->First = Last;
  Storage->Ordered = 0;
}

static kbts__glyph_range kbts__PushGlyphRange(kbts_glyph_storage *Storage, kbts_glyph_ref First, kbts_glyph_ref Last)
{
  KBTS_ASSERT(!Storage->RangeActive);
  kbts__glyph_range Result = KBTS__ZERO;
  Result.Before = Storage->First == KBTS__GLYPH_END ? Storage->Tail : Storage->Links[Storage->First].Prev;
  Result.End = Storage->End;
  Result.OnePastLast = Last ? kbts__NextGlyphRef(Storage, Last) : 0;
  if(First && Last)
  {
    KBTS_ASSERT(kbts__GlyphIsValid(Storage, First) && kbts__GlyphIsValid(Storage, Last));
    Storage->First = First - 1;
    Storage->End = Storage->Links[Last - 1].Next;
  }
  else
  {
    KBTS_ASSERT(!First && !Last);
    Storage->First = Storage->End;
  }
  Storage->RangeActive = 1;
  return Result;
}

static void kbts__PopGlyphRange(kbts_glyph_storage *Storage, kbts__glyph_range *Range)
{
  Storage->First = Range->Before == KBTS__GLYPH_END ? Storage->Head : Storage->Links[Range->Before].Next;
  Storage->End = Range->End;
  Storage->RangeActive = 0;
  *Range = KBTS__ZERO_TYPE(kbts__glyph_range);
}

/* GPOS does not change slot identity or logical order. Rebuild its direct-child
 * links after GSUB, including existing attachments in the active range. */
static void kbts__RebuildAttachmentLinks(kbts_glyph_storage *Storage)
{
  for(kbts_u32 Slot = Storage->First; Slot != Storage->End; Slot = Storage->Links[Slot].Next)
  {
    kbts__glyph_link *Link = &Storage->Links[Slot];
    Link->FirstAttached = Link->NextAttached = Link->PrevAttached = 0;
    kbts_glyph_ref Parent = Storage->Glyphs[Slot].AttachGlyph;
    if(kbts__GlyphIsValid(Storage, Parent)) Storage->Links[Parent - 1].FirstAttached = 0;
  }
  for(kbts_u32 Slot = Storage->First; Slot != Storage->End; Slot = Storage->Links[Slot].Next)
  {
    kbts_glyph_ref Parent = Storage->Glyphs[Slot].AttachGlyph;
    if(!kbts__GlyphIsValid(Storage, Parent)) continue;
    kbts__glyph_link *ParentLink = &Storage->Links[Parent - 1];
    kbts_glyph_ref First = ParentLink->FirstAttached;
    Storage->Links[Slot].NextAttached = First;
    if(First) Storage->Links[First - 1].PrevAttached = Slot + 1;
    ParentLink->FirstAttached = Slot + 1;
  }
}

static void kbts__ResetGlyphSlots(kbts_glyph_storage *Storage)
{
  Storage->Count = Storage->LiveCount = Storage->FreeSlotCount = 0;
  Storage->Head = Storage->Tail = Storage->First = Storage->End = KBTS__GLYPH_END;
  Storage->RangeActive = 0;
  Storage->Ordered = 1;
  Storage->IndexedScratchpad = 0;
  Storage->Boundary = KBTS__ZERO_TYPE(kbts_glyph);
}

KBTS_EXPORT int kbts_InitializeGlyphStorage(kbts_glyph_storage *Storage, kbts_allocator_function *Allocator, void *AllocatorData)
{
  if(!Storage) return 0;
  KBTS_MEMSET(Storage, 0, sizeof(*Storage));
  Storage->Allocator = Allocator;
  Storage->AllocatorData = AllocatorData;
  kbts__ResetGlyphSlots(Storage);
  return 1;
}

KBTS_EXPORT int kbts_InitializeGlyphStorageFixedMemory(kbts_glyph_storage *Storage, void *Memory, int MemorySize)
{
  if(!Storage) return 0;
  KBTS_MEMSET(Storage, 0, sizeof(*Storage));
  kbts__ResetGlyphSlots(Storage);
  Storage->FixedMemory = 1;
  kbts_un Overhead = kbts__GlyphStorageOverhead(), Stride = kbts__GlyphStorageStride();
  if(!Memory || MemorySize <= 0 || (kbts_un)MemorySize < Overhead + Stride ||
     (kbts_uptr)Memory > (kbts_uptr)-1 - (kbts_un)MemorySize)
  {
    Storage->Error = 1;
    return 0;
  }
  kbts__BindGlyphStorage(Storage, Memory, (kbts_u32)(((kbts_un)MemorySize - Overhead) / Stride));
  return 1;
}

KBTS_EXPORT kbts_glyph_iterator kbts_ActiveGlyphIterator(kbts_glyph_storage *Storage)
{
  kbts_glyph_iterator Result = KBTS__ZERO;
  if(Storage && !Storage->Error)
  {
    Result.GlyphStorage = Storage;
    Result.CurrentIndex = Storage->First;
  }
  return Result;
}

KBTS_EXPORT void kbts_ClearActiveGlyphs(kbts_glyph_storage *Storage)
{
  if(!Storage || Storage->Error) return;
  if(!Storage->RangeActive && Storage->First == Storage->Head && Storage->End == KBTS__GLYPH_END)
  {
    kbts__ResetGlyphSlots(Storage);
    return;
  }
  while(Storage->First != Storage->End)
    kbts__FreeGlyph(Storage->IndexedScratchpad, Storage, Storage->First + 1);
  Storage->Boundary = KBTS__ZERO_TYPE(kbts_glyph);
}

KBTS_EXPORT void kbts_FreeAllGlyphs(kbts_glyph_storage *Storage)
{
  if(!Storage) return;
  if(!Storage->FixedMemory)
  {
    kbts__AllocatorFree(Storage->Allocator, Storage->AllocatorData, Storage->Memory);
    Storage->Memory = 0;
    Storage->Glyphs = 0;
    Storage->Links = 0;
    Storage->FreeSlots = 0;
    Storage->Capacity = 0;
  }
  kbts__ResetGlyphSlots(Storage);
}

static kbts_glyph *kbts__PushMappedGlyph(kbts_glyph_storage *Storage, kbts_font *Font, int Codepoint, kbts_glyph_config *Config, int UserId, kbts_u16 Id)
{
  KB_PROFILE_SCOPE(KBP_GLYPH_PUSH);
  if(!Storage || Storage->Error) return 0;
  kbts_glyph_ref Ref = kbts__AllocateGlyph(Storage, Storage->End);
  if(!Ref) return 0;
  kbts_glyph *Result = kbts__Glyph(Storage, Ref);
  if(Font) kbts__InitializeMappedGlyph(Result, Font, Codepoint, Config, UserId, Id);
  else
  {
    *Result = KBTS__ZERO_TYPE(kbts_glyph);
    Result->Codepoint = (kbts_u32)Codepoint;
    Result->Config = Config;
  }
  Result->Ref = Ref;
  if(Storage->IndexedScratchpad) kbts__GsubInsertSlot(Storage->IndexedScratchpad, Ref - 1);
  return Result;
}

KBTS_EXPORT kbts_glyph *kbts_PushGlyph(kbts_glyph_storage *Storage, kbts_font *Font, int Codepoint, kbts_glyph_config *Config, int UserId)
{
  if(!Storage || Storage->Error) return 0;
  kbts_u16 Id = Font ? (kbts_u16)kbts_CodepointToGlyphId(Font, Codepoint) : 0;
  return kbts__PushMappedGlyph(Storage, Font, Codepoint, Config, UserId, Id);
}

KBTS_EXPORT int kbts_GlyphIteratorIsValid(kbts_glyph_iterator *It)
{
  return It && It->GlyphStorage && !It->GlyphStorage->Error &&
         It->CurrentIndex != It->GlyphStorage->End &&
         It->CurrentIndex < It->GlyphStorage->Count && It->GlyphStorage->Glyphs[It->CurrentIndex].Ref;
}

KBTS_EXPORT int kbts_GlyphIteratorNext(kbts_glyph_iterator *It, kbts_glyph **Glyph)
{
  KB_PROFILE_SCOPE(KBP_GLYPH_READ);
  if(!kbts_GlyphIteratorIsValid(It)) return 0;
  *Glyph = &It->GlyphStorage->Glyphs[It->CurrentIndex];
  It->CurrentIndex = It->GlyphStorage->Links[It->CurrentIndex].Next;
  return 1;
}
