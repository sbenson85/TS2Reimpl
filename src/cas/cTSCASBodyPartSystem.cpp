#include "cas/cTSCASBodyPartSystem.h"

#include "app/AppDebugger.h"
#include "app/AppInit.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/CommandParser.h"
#include "misc/HexParse.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/cGZAllocatorService.h"
#include "rz/cRZFile.h"
#include "rz/cRZFileName.h"

#include <cstring>

// The thread-safe-statics helpers of our CRT, used on the game's guard variable (as in rz/cGZAllocatorService.size.cpp).
extern "C" __declspec(thread) int _Init_thread_epoch;
extern "C" void __cdecl _Init_thread_header(int *target);
extern "C" void __cdecl _Init_thread_footer(int *target);

namespace nTSCASCore {

namespace {

// Copies 16 bytes like the original's movups load-then-store (the source is read completely first).
void CopyBlock16(void *dest, const void *src)
{
    uint8_t block[16];
    std::memcpy(block, src, sizeof(block));
    std::memcpy(dest, block, sizeof(block));
}

// The record type's own code (constructors 0xcddd40/0xcddc80, copy 0xcddbc0, destructor 0xcdf820, growing append 0xcda310)
// is still original: nobody has replaced it yet.
constexpr uint32_t kRvaCRZStringVTable = 0xd9b470; // the vtable of the embedded cRZString

// The embedded string of a record built by hand: empty, capacity 15, refcount 0 (what the compiler-inlined
// constructions in the original write).
void InitEmptyName(BodyPartEntry &e)
{
    e.mNameVTable = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress<void>(kRvaCRZStringVTable)));
    std::memset(e.mNameBuf, 0, sizeof(e.mNameBuf));
    e.mNameSize = 0;
    e.mNameCap = 0xf;
    e.mNameRef = 0;
}

// Appends a copy of the record to the stack: the copy constructor into spare capacity, else the growing path.
void AppendEntry(BodyPartEntryVector *stack, const BodyPartEntry &entry)
{
    BodyPartEntry *const last = stack->mLast;
    if (last == stack->mEnd) {
        OriginalFunction<void(__thiscall *)(BodyPartEntryVector *, BodyPartEntry *, const BodyPartEntry *), 0xcda310>()(stack, last, &entry); // grows and appends (ret 8)
    } else {
        OriginalFunction<void(__thiscall *)(BodyPartEntry *, const BodyPartEntry *), 0xcddbc0>()(last, &entry); // copy constructor (ret 4)
        stack->mLast = stack->mLast + 1;
    }
}

void DestroyEntry(BodyPartEntry &entry)
{
    OriginalFunction<void(__thiscall *)(BodyPartEntry *), 0xcdf820>()(&entry);
}

} // namespace

const char *BodyPartEntry::Name() const
{
    return mNameCap > 0xf ? *reinterpret_cast<const char *const *>(mNameBuf) : mNameBuf;
}

// Body: mov eax,[esp+4]; movups xmm0,[eax]; movups [ecx+0xb8],xmm0; ret 4.
void BodyPartSystemIface::StoreBlockB8(const void *src)
{
    CopyBlock16(mBlockB8, src);
}

// Body: the same copy to +0xc8.
void BodyPartSystemIface::StoreBlockC8(const void *src)
{
    CopyBlock16(mBlockC8, src);
}

// Body: the same copy to +0xd8.
void BodyPartSystemIface::StoreBlockD8(const void *src)
{
    CopyBlock16(mBlockD8, src);
}

void BodyPartSystemIface::StoreDword108(uint32_t value)
{
    mDword108 = value;
}

void BodyPartSystemIface::StoreByte10C(uint8_t value)
{
    mByte10C = value;
}

void BodyPartSystemIface::StoreByte129(uint8_t value)
{
    mByte129 = value;
}

// Returns eax whole: 1 for mode 0; for a positive mode the mode with its low byte replaced by ([+0x120] != 0);
// for a negative mode the mode with its low byte cleared. Callers read al.
uint32_t BodyPartSystemIface::ByteGate120(int32_t mode)
{
    const uint32_t bits = static_cast<uint32_t>(mode);
    if (mode == 0) return 1;
    if (mode > 0) return (bits & ~0xffu) | (mByte120 != 0 ? 1u : 0u);
    return bits & ~0xffu;
}

const char *BodyPartSystemIface::GetText()
{
    return mTextCap > 0xf ? *reinterpret_cast<const char *const *>(mText) : mText;
}

int32_t BodyPartSystemIface::RedoStackCount()
{
    return static_cast<int32_t>(reinterpret_cast<const uint8_t *>(mRedoStack->mLast) -
                                reinterpret_cast<const uint8_t *>(mRedoStack->mFirst)) >> 6;
}

int32_t BodyPartSystemIface::StackCount()
{
    return static_cast<int32_t>(reinterpret_cast<const uint8_t *>(mStack->mLast) -
                                reinterpret_cast<const uint8_t *>(mStack->mFirst)) >> 6;
}

// 0xcf2160: indexes 0 and 1 give the keys at +0xe8 and +0xf8; anything else gives the shared zero GUID (0x4f8e50).
const void *BodyPartSystemIface::GetKey(uint32_t index)
{
    if (index > 1) return OriginalFunction<const uint8_t *(__cdecl *)(), 0x4f8e50>()();
    return mKeys[index];
}

// 0x267350 (cdecl, one argument): a jump table over selector values 0..3 giving 2, 4, 8 or 1, and 1 for larger ones.
uint32_t BodyPartSystemIface::AgeMask()
{
    return OriginalFunction<uint32_t(__cdecl *)(uint32_t), 0x267350>()(mAge);
}

// Walks the stack oldest first. A weight of NaN counts as non-zero (the compare skips only on an exact zero).
bool BodyPartSystemIface::HasActiveMorph(const char *name)
{
    for (const BodyPartEntry *e = mStack->mFirst; e != mStack->mLast; ++e) {
        if (e->mKind == 1 && e->mWeight != 0.0f && _stricmp(name, e->Name()) == 0) return true;
    }
    return false;
}

float BodyPartSystemIface::TopMorphWeight(const char *name)
{
    if (mStack->mFirst != mStack->mLast) {
        const BodyPartEntry &top = mStack->mLast[-1];
        if (top.mKind == 1 && _stricmp(name, top.Name()) == 0) return top.mWeight;
    }
    return 0.0f;
}

// Starts at the newest record and stops before index 0 (the oldest record is never looked at); a kind-1 record ends
// the walk. Kinds 2 and 7 whose name matches add their weight.
float BodyPartSystemIface::SumNamedWeights(const char *name, uint32_t)
{
    float sum = 0.0f;
    const BodyPartEntry *const first = mStack->mFirst;
    for (int32_t i = StackCount() - 1; i > 0; --i) {
        const BodyPartEntry &e = first[i];
        if (e.mKind == 1) break;
        if ((e.mKind == 7 || e.mKind == 2) && _stricmp(name, e.Name()) == 0) sum = sum + e.mWeight;
    }
    return sum;
}

bool BodyPartSystemIface::ClearPartWeight(int32_t value, int32_t category)
{
    bool found = false;
    for (BodyPartEntry *e = mStack->mLast; e != mStack->mFirst;) {
        --e;
        if (e->mKind != 3) continue;
        if (category != -1 && category != e->mShortA) continue;
        if (value != e->mValue) continue;
        if (e->mWeight == 0.0f) continue;
        e->mWeight = 0.0f;
        found = true;
    }
    return found;
}

// The GUID is written as text, in the form "%08x-%04x-%04x-%04x-%04x%08x" (the game string at 0xdce3cc), and compared
// with the names of the kind-3 records.
void BodyPartSystemIface::ClearGuidPartWeights(const uint32_t *guid)
{
    if (mStack->mFirst == mStack->mLast) return;
    cRZString text;
    text.Construct();
    text.Sprintf(GameAddress<const char>(0xdce3cc), guid[0], guid[1] >> 16, guid[1] & 0xffff, guid[2] >> 16,
                 guid[2] & 0xffff, guid[3]);
    for (BodyPartEntry *e = mStack->mLast; e != mStack->mFirst;) {
        --e;
        if (e->mKind != 3) continue;
        const char *entryName = e->Name();
        const size_t entryLength = std::strlen(entryName);
        if (text.mString.size() == entryLength && std::memcmp(text.mString.data(), entryName, entryLength) == 0)
            e->mWeight = 0.0f;
    }
    text.Destruct();
}

void BodyPartSystemIface::PushMarker()
{
    BodyPartEntry e;
    std::memset(&e, 0, sizeof(e));
    e.mKind = 8;
    InitEmptyName(e);
    AppendEntry(mStack, e);
    DestroyEntry(e);
}

void BodyPartSystemIface::PushKind5(const void *items)
{
    if (mAge == 0) return;
    BodyPartEntry e;
    std::memset(&e, 0, sizeof(e));
    e.mKind = 5;
    e.mWeight = 1.0f;
    OriginalFunction<void(__thiscall *)(void *, const void *), 0xcddae0>()(&e.mItemsFirst, items); // copies the item vector
    InitEmptyName(e);
    AppendEntry(mStack, e);
    DestroyEntry(e);
}

void BodyPartSystemIface::PushKind6(const NamedWeight *source)
{
    if (mAge == 0) return;
    BodyPartEntry e;
    OriginalFunction<void(__thiscall *)(BodyPartEntry *, const char *, int32_t, int32_t, float, float), 0xcddd40>()(
        &e, source->mName.ToChar(), 6, 0, source->mWeight, 0.0f);
    AppendEntry(mStack, e);
    mByte12A = 1;
    DestroyEntry(e);
}

namespace {

// The system object this subobject belongs to (the original passes `this - 0x1c` to helpers of the whole system).
void *SystemOf(BodyPartSystemIface *iface)
{
    return reinterpret_cast<uint8_t *>(iface) - 0x1c;
}

// Slots of the interface (the vtable at 0xeba198) that these dispatchers call.
using Slot3 = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);                 // slot 20
using Slot6 = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t); // slot 22
using Slot10 = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                  uint32_t, uint32_t, uint32_t);                          // slot 24
using Slot4 = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t);        // slot 25

} // namespace

void BodyPartSystemIface::VisitIds(uint32_t a, uint32_t b)
{
    for (const uint32_t *id = mIdsFirst; id != mIdsLast; ++id) VirtualCall<Slot3, 20>(this, a, b, *id);
}

void BodyPartSystemIface::VisitIdsF7(uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6)
{
    for (const uint32_t *id = mIdsFirst; id != mIdsLast; ++id) {
        OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t), 0xcf7700>()(SystemOf(this), p1, p2, *id, p3, p4, p5, p6);
    }
}

void BodyPartSystemIface::VisitIdsEA(uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6)
{
    for (const uint32_t *id = mIdsFirst; id != mIdsLast; ++id) {
        OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t), 0xcecea0>()(SystemOf(this), p1, p2, *id, p3, p4, p5, p6);
    }
}

void BodyPartSystemIface::ApplyGuidText(const char *text, uint32_t a, uint32_t b)
{
    uint8_t guid[16];
    hexparse::ParseGuidText(guid, text);
    VirtualCall<void(__thiscall *)(void *, const void *, uint32_t, uint32_t), 37>(this, guid, a, b);
}

void BodyPartSystemIface::StoreKey(uint32_t index, const void *key)
{
    if (index > 1) return;
    if (std::memcmp(mKeys[index], key, 16) == 0) return;
    CopyBlock16(mKeys[index], key);
    OriginalFunction<void(__thiscall *)(void *, uint32_t), 0xcf69a0>()(SystemOf(this), index == 0 ? 3 : 4);
    OriginalFunction<void(__thiscall *)(void *), 0xcf8670>()(SystemOf(this));
}

void BodyPartSystemIface::ApplyAll(uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6)
{
    p6 |= mByte12A; // an OR into the argument's low byte
    mByte12A = 0;
    auto applyFirst = VirtualSlot<Slot10>(this, 24);
    auto applySecond = VirtualSlot<Slot4>(this, 25);
    if ((p5 & 0xff) != 0) {
        // Recurse through slot 22 for every pair of ids in the two lists.
        for (const uint32_t *a = mListAFirst; a != mListALast; ++a) {
            for (const uint32_t *b = mListBFirst; b != mListBLast; ++b) VirtualCall<Slot6, 22>(this, *a, *b, p3, p4, 0, p6);
        }
        return;
    }
    if (mAge == 0) {
        OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t), 0xcf8960>()(SystemOf(this), p1, p2);
        applyFirst(this, p1, p2, 0, 0, 0, 1, 1, 1, 1, p6);
        applySecond(this, p1, p2, p4, 1);
        if ((p3 & 0xff) != 0) VirtualCall<Slot3, 20>(this, p1, p2, 0);
        return;
    }
    OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t), 0xcf8960>()(SystemOf(this), p1, p2);
    for (const uint32_t *id = mIdsFirst; id != mIdsLast; ++id) applyFirst(this, p1, p2, *id, 0, 0, 1, 1, 0, 0, p6);
    OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), 0xcfa2d0>()(SystemOf(this), p1, p2, 0, 0);
    for (const uint32_t *id = mIdsFirst; id != mIdsLast; ++id) applyFirst(this, p1, p2, *id, 0, 0, 1, 0, 0, 1, 0);
    OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), 0xcfa6e0>()(SystemOf(this), p1, p2, 0, 0);
    for (const uint32_t *id = mIdsFirst; id != mIdsLast; ++id) applyFirst(this, p1, p2, *id, 0, 0, 1, 0, 1, 0, 0);
    applySecond(this, p1, p2, p4, 1);
    if ((p3 & 0xff) != 0) {
        for (const uint32_t *id = mIdsFirst; id != mIdsLast; ++id) VirtualCall<Slot3, 20>(this, p1, p2, *id);
    }
}

void BodyPartSystemIface::ApplyAllPasses(uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4)
{
    if (mByte12A != 0) {
        p4 = (p4 & ~0xffu) | 1u; // a byte store into the argument
        mByte12A = 0;
    }
    auto applyFirst = VirtualSlot<Slot10>(this, 24);
    for (const uint32_t *id = mIdsFirst; id != mIdsLast; ++id) applyFirst(this, p1, p2, *id, p3, p3, 1, 1, 0, 0, p4);
    OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), 0xcfa2d0>()(SystemOf(this), p1, p2, p3, p3);
    for (const uint32_t *id = mIdsFirst; id != mIdsLast; ++id) applyFirst(this, p1, p2, *id, p3, p3, 0, 0, 0, 1, 0);
    OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), 0xcfa6e0>()(SystemOf(this), p1, p2, p3, p3);
    for (const uint32_t *id = mIdsFirst; id != mIdsLast; ++id) applyFirst(this, p1, p2, *id, p3, p3, 1, 0, 1, 0, 0);
}

namespace {

// The newest record of the given kind, scanning from the end; null when there is none.
BodyPartEntry *FindNewestOfKind(const BodyPartEntryVector *stack, int32_t kind)
{
    for (BodyPartEntry *e = stack->mLast; e != stack->mFirst;) {
        --e;
        if (e->mKind == kind) return e;
    }
    return nullptr;
}

} // namespace

BodyPartEntry *cTSCASBodyPartSystem::FindNewestKind5()
{
    return FindNewestOfKind(Iface()->mStack, 5);
}

BodyPartEntry *cTSCASBodyPartSystem::FindNewestKind6()
{
    return FindNewestOfKind(Iface()->mStack, 6);
}

bool BodyPartSystemIface::ItemsMatchNewestKind5(const BodyPartItemVector *items)
{
    const BodyPartEntry *record = reinterpret_cast<cTSCASBodyPartSystem *>(SystemOf(this))->FindNewestKind5();
    if (!record) return false;
    const int32_t count = static_cast<int32_t>(reinterpret_cast<const uint8_t *>(items->mLast) - reinterpret_cast<const uint8_t *>(items->mFirst)) / 0x44;
    const int32_t recordCount = static_cast<int32_t>(record->mItemsLast - record->mItemsFirst) / 0x44;
    if (recordCount != count) return false;
    const BodyPartItem *mine = items->mFirst;
    const BodyPartItem *theirs = reinterpret_cast<const BodyPartItem *>(record->mItemsFirst);
    for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
        if (mine[i].mFirst.mString != theirs[i].mFirst.mString) return false;
        if (mine[i].mSecond.mString != theirs[i].mSecond.mString) return false;
        if (mine[i].mValue != theirs[i].mValue) return false;
    }
    return true;
}

bool BodyPartSystemIface::MatchesNewestKind6(const NamedWeight *named)
{
    const BodyPartEntry *record = reinterpret_cast<cTSCASBodyPartSystem *>(SystemOf(this))->FindNewestKind6();
    if (!record) return false;
    const char *text = VirtualCall<const char *(__thiscall *)(const void *), 5>(named);
    const std::string copy(text);
    return std::strlen(record->Name()) == copy.size() && std::memcmp(record->Name(), copy.data(), copy.size()) == 0 &&
           record->mWeight == named->mWeight;
}

namespace {

// Shared body of slots 44 and 46: `matches(value)` picks the kind-3 records by their value field.
template <typename Matches> void CollectNames(BodyPartSystemIface *self, StringVector *out, Matches matches)
{
    if (self->mAge == 0) return;
    if (out->mFirst != out->mLast) {
        for (cRZString *s = out->mFirst; s != out->mLast; ++s) VirtualCall<void(__thiscall *)(void *, uint32_t), 33>(s, 0);
        out->mLast = out->mFirst;
    }
    for (const BodyPartEntry *e = self->mStack->mFirst; e != self->mStack->mLast; ++e) {
        if (e->mWeight == 0.0f) continue;
        if (e->mKind == 3 && matches(e->mValue)) {
            cRZString name;
            name.Construct(e->Name());
            OriginalFunction<void(__thiscall *)(StringVector *, const cRZString *), 0x3a05e0>()(out, &name);
            name.Destruct();
        }
    }
}

} // namespace

void BodyPartSystemIface::CollectNamesValue0B(StringVector *out)
{
    CollectNames(this, out, [](int32_t value) { return value == 0 || value == 0xb; });
}

void BodyPartSystemIface::CollectNamesValue5(StringVector *out)
{
    CollectNames(this, out, [](int32_t value) { return value == 5; });
}

namespace {

// Leaves a tree empty after its nodes were erased by the tree's own erase: the head points at itself, size 0.
void ResetTreeHeader(TreeHeader *tree)
{
    TreeHeader::Node *head = tree->mHead;
    head->mParent = head;
    head->mLeft = head;
    head->mRight = head;
    tree->mSize = 0;
}

} // namespace

void BodyPartSystemIface::ClearTreeAt14()
{
    TreeHeader *tree = mTree14;
    OriginalFunction<void(__thiscall *)(TreeHeader *, TreeHeader *, TreeHeader::Node *), 0xcdb400>()(tree, tree, tree->mHead->mParent);
    ResetTreeHeader(tree);
}

void BodyPartSystemIface::ResetState()
{
    const uint8_t *zeroGuid = OriginalFunction<const uint8_t *(__cdecl *)(), 0x4f8e50>()();
    CopyBlock16(mBlockB8, zeroGuid);
    zeroGuid = OriginalFunction<const uint8_t *(__cdecl *)(), 0x4f8e50>()();
    CopyBlock16(mBlockC8, zeroGuid);
    zeroGuid = OriginalFunction<const uint8_t *(__cdecl *)(), 0x4f8e50>()();
    CopyBlock16(mBlockD8, zeroGuid);
    using Erase = void(__thiscall *)(TreeHeader *, TreeHeader *, TreeHeader::Node *);
    TreeHeader *treeC = mTreeC;
    mDword108 = 1;
    OriginalFunction<Erase, 0xcdb5e0>()(treeC, treeC, treeC->mHead->mParent);
    ResetTreeHeader(treeC);
    VirtualCall<void(__thiscall *)(void *), 16>(this);
    if (mAge != 0) {
        TreeHeader *byAge = reinterpret_cast<TreeHeader *>(reinterpret_cast<uint8_t *>(this) + (mAge + 7) * 8);
        OriginalFunction<Erase, 0xcdc280>()(byAge, byAge, byAge->mHead->mParent);
        ResetTreeHeader(byAge);
    }
    TreeHeader *tree58 = reinterpret_cast<TreeHeader *>(reinterpret_cast<uint8_t *>(this) + 0x58);
    OriginalFunction<Erase, 0xcdc300>()(tree58, tree58, tree58->mHead->mParent);
    ResetTreeHeader(tree58);
    OriginalFunction<void(__thiscall *)(void *), 0xd0a9e0>()(reinterpret_cast<uint8_t *>(this) + 0x13c);
    OriginalFunction<void(__thiscall *)(void *), 0xcfebc0>()(reinterpret_cast<uint8_t *>(this) + 0x130);
    OriginalFunction<void(__thiscall *)(void *), 0xce9530>()(SystemOf(this));
    static_cast<cTSCASBodyPartSystem *>(SystemOf(this))->ResetKeys(); // 0xce9690, a tail jump in the original
}

void BodyPartSystemIface::PopEntry(uint32_t moveToRedo)
{
    BodyPartEntryVector *stack = mStack;
    if (stack->mFirst == stack->mLast) return;
    BodyPartEntry removed;
    OriginalFunction<void(__thiscall *)(BodyPartEntry *, const BodyPartEntry *), 0xcddbc0>()(&removed, stack->mLast - 1); // copy constructor
    stack = mStack;
    DestroyEntry(stack->mLast[-1]);
    stack->mLast = stack->mLast - 1;
    if ((moveToRedo & 0xff) != 0) AppendEntry(mRedoStack, removed);
    OriginalFunction<void(__thiscall *)(void *), 0xce9530>()(SystemOf(this));
    OriginalFunction<void(__thiscall *)(void *), 0xcebb70>()(SystemOf(this));
    DestroyEntry(removed);
}

void BodyPartSystemIface::RedoEntry()
{
    BodyPartEntryVector *redo = mRedoStack;
    if (redo->mFirst == redo->mLast) return;
    BodyPartEntry record;
    OriginalFunction<void(__thiscall *)(BodyPartEntry *, const BodyPartEntry *), 0xcddbc0>()(&record, redo->mLast - 1); // copy constructor
    redo = mRedoStack;
    DestroyEntry(redo->mLast[-1]);
    redo->mLast = redo->mLast - 1;
    const char *name = record.Name();
    uint32_t weightBits;
    std::memcpy(&weightBits, &record.mWeight, 4);
    using AddNamedWeight = void(__thiscall *)(void *, const char *, uint32_t, uint32_t);
    switch (static_cast<uint32_t>(record.mKind)) {
    case 0: VirtualCall<void(__thiscall *)(void *, const char *), 47>(this, name); break;
    case 1: VirtualCall<void(__thiscall *)(void *, const char *, uint32_t, uint32_t), 48>(this, name, weightBits, 1); break;
    case 2: OriginalFunction<AddNamedWeight, 0xcf5930>()(SystemOf(this), name, weightBits, 1); break;
    case 3:
        VirtualCall<void(__thiscall *)(void *, const char *, uint32_t, uint32_t), 38>(this, name, record.mWeight > 0.5f ? 1 : 0, 0);
        break;
    case 5: VirtualCall<void(__thiscall *)(void *, const void *), 33>(this, &record.mItemsFirst); break;
    case 7: OriginalFunction<AddNamedWeight, 0xcf5930>()(SystemOf(this), name, weightBits, 0); break;
    case 8: VirtualCall<void(__thiscall *)(void *), 32>(this); break;
    default: break;
    }
    DestroyEntry(record);
}

void BodyPartSystemIface::ClearStacks()
{
    BodyPartEntryVector *redo = mRedoStack;
    if (redo->mFirst != redo->mLast) {
        for (BodyPartEntry *e = redo->mFirst; e != redo->mLast; ++e) DestroyEntry(*e);
        redo->mLast = redo->mFirst;
    }
    BodyPartEntryVector *stack = mStack;
    if (stack->mFirst != stack->mLast) {
        for (BodyPartEntry *e = stack->mFirst; e != stack->mLast; ++e) DestroyEntry(*e);
        stack->mLast = stack->mFirst;
    }
    OriginalFunction<void(__thiscall *)(void *), 0xce9530>()(SystemOf(this));
    OriginalFunction<void(__thiscall *)(void *), 0xcebb70>()(SystemOf(this));
}

void BodyPartSystemIface::PushKind0(const char *name)
{
    BodyPartEntry e;
    using NamedCtor = void(__thiscall *)(BodyPartEntry *, const char *, int32_t, int32_t, float);
    OriginalFunction<NamedCtor, 0xcddc80>()(&e, name, 0, 0, 1.0f);
    AppendEntry(mStack, e);
    DestroyEntry(e);
}

// ---- The +0 face and the system's start-up helpers ----------------------------------------------------------------

namespace {

constexpr uint32_t kIID_cITSCASBodyPartSystem = 0xcb7d64f8;

// Slots of the cITSCASBodyPartSystem face these functions call on the system's own +0x1c subobject.
constexpr int kSlotLoadBodyPartData = 4; // vf28_4 0xcf3750 (ret 0)
constexpr int kSlotResetState = 9;       // 0xcf7110
constexpr int kSlotClearTreeAt14 = 10;   // 0xcf71f0
constexpr int kSlotStoreKey = 58;        // 0xcf8140 (ret 8)

void CallFaceSlot(cTSCASBodyPartSystem *self, int slot)
{
    void *face = self->Iface();
    using Fn = void(__thiscall *)(void *);
    (*reinterpret_cast<Fn *const *>(face))[slot](face);
}

// The game's resource-type ids and groups these registrations use.
constexpr uint32_t kBodyPartDataType = 0x6ccef9f2;
constexpr uint32_t kBodyPartDataGroup = 0xcccef852;
constexpr uint32_t kLockedObjectType = 0xaba91d51;
constexpr uint32_t kLockedObjectGroup = 0x11db585b;

// The persist resource manager: the cached service object (0xb390c0), then its slot 3.
void *PersistResourceManager()
{
    void *service = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    return VirtualCall<void *(__thiscall *)(void *), 3>(service);
}

// Slot 20 of the persist resource manager: registers `factory` for a type and group (null: none).
void RegisterFactory(void *manager, uint32_t type, uint32_t group, void *factory)
{
    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void *), 20>(manager, type, group, factory);
}

// A cheat command built as the original builds it inline: a 16-byte cCommandT (the 12-byte cCommand from its constructor
// 0xaf7820, the fourth word left as allocated), then the class's own game vtable. The cheat classes are not ours.
void *NewCheatCommand(uint32_t vtableRva)
{
    auto *command = static_cast<nGZCommandParser::cCommand *>(::operator new(0x10));
    command->Construct();
    command->mVTable = GameAddress<void>(vtableRva);
    return command;
}
constexpr uint32_t kBlendLimitCheatVTable = 0xeba440;
constexpr uint32_t kPurgeFaceSystemMemoryCheatVTable = 0xeba3b4;

// nTSCASCore::cTSBodyPartDataFactory, built as Init builds it inline (12 bytes): the cIGZPersistResourceFactory vtable
// (a dead store), the cRZUnknown part at +4 (0x1b9052: vtable, zero count), then the class's two game vtables. Not ours.
struct BodyPartDataFactory {
    const void *mVTable;  // +0 0xeba300
    RZUnknownLayout mUnknown; // +4 0xeba320, count +8
};
static_assert(sizeof(BodyPartDataFactory) == 0xc, "BodyPartDataFactory layout");
constexpr uint32_t kPersistResourceFactoryVTable = 0xda6718;
constexpr uint32_t kBodyPartDataFactoryVTable = 0xeba300;
constexpr uint32_t kBodyPartDataFactoryVTable4 = 0xeba320;

// The cheat system (the framework's slot 5 with these ids) and its slot 4, which registers a command.
constexpr uint32_t kCheatSystemClsid = 0x2a4915ea;
constexpr uint32_t kCheatSystemIid = 0x4a490e00;

} // namespace

bool cTSCASBodyPartSystem::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kIID_cITSCASBodyPartSystem) {
        return rz::cRZSystemServiceAdapters::QueryInterface(reinterpret_cast<rz::cRZSystemService *>(this), nullptr, riid, out);
    }
    *out = Iface();
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef through the +0 vtable
    return true;
}

bool cTSCASBodyPartSystem::Init()
{
    // The base's Init (shared 0x144910) answers true without side effects: the original's early return is dead.
    void *manager = PersistResourceManager();
    auto *factory = static_cast<BodyPartDataFactory *>(::operator new(sizeof(BodyPartDataFactory)));
    factory->mVTable = GameAddress<void>(kPersistResourceFactoryVTable);
    factory->mUnknown.Construct();
    factory->mVTable = GameAddress<void>(kBodyPartDataFactoryVTable);
    factory->mUnknown.mVTable = GameAddress<void>(kBodyPartDataFactoryVTable4);
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(factory); // AddRef
    RegisterFactory(manager, kBodyPartDataType, kBodyPartDataGroup, factory);
    mLockedObjects.RegisterNoFactory();
    CallFaceSlot(this, kSlotLoadBodyPartData);

    void *blendLimit = NewCheatCommand(kBlendLimitCheatVTable);
    ScopedRef cheats{nullptr};
    if (void *framework = GetServiceObject()) {
        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **), 5>(framework, kCheatSystemClsid, kCheatSystemIid,
                                                                                       &cheats.mObject);
    }
    if (cheats.mObject) VirtualCall<uint32_t(__thiscall *)(void *, void *), 4>(cheats.mObject, blendLimit);
    cheats.ReleaseObject();

    void *purge = NewCheatCommand(kPurgeFaceSystemMemoryCheatVTable);
    OriginalFunction<void(__cdecl *)(void *), 0xb39640>()(purge); // registers the command with the cheat system
    void *director = GetDirectorFrameworkObject();
    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t *), 7>(director, &mDirectorCookie);
    VirtualCall<uint32_t(__thiscall *)(void *), 2>(factory); // Release
    return true;
}

bool cTSCASBodyPartSystem::Shutdown()
{
    CallFaceSlot(this, kSlotClearTreeAt14);
    CallFaceSlot(this, kSlotResetState);
    mRecords->clear();
    using Erase = void(__thiscall *)(TreeHeader *, TreeHeader *, TreeHeader::Node *);
    TreeHeader *tree20 = mTree20;
    OriginalFunction<Erase, 0xcdbf10>()(tree20, tree20, tree20->mHead->mParent);
    ResetTreeHeader(tree20);
    TreeHeader *tree4C = &mResources;
    OriginalFunction<Erase, 0xcdbc10>()(tree4C, tree4C, tree4C->mHead->mParent);
    ResetTreeHeader(tree4C);
    OriginalFunction<void(__thiscall *)(void *), 0xcfebc0>()(&mLockedObjects); // the locked-object manager's clear
    return true; // the base's Shutdown (shared 0x144910, a tail jump in the original)
}

void cTSCASBodyPartSystem::InitCategoryLists()
{
    mListB.clear();
    mListA.clear();
    mListC.clear();
    if (mAge == 0) {
        mListB.push_back(0);
        mListB.push_back(1);
        for (uint32_t id : {5u, 0u, 1u, 2u, 3u, 4u}) mListA.push_back(id);
        mListC.push_back(0);
    } else {
        mListB.push_back(0);
        for (uint32_t id : {5u, 3u, 4u}) mListA.push_back(id);
        for (uint32_t id : {2u, 1u, 4u}) mListC.push_back(id);
        if (mAge != 3) mListC.push_back(3);
    }
}

void cTSCASBodyPartSystem::InitTargetSurfaceNames()
{
    mSurfaceSets.clear();
    mSurfaceMap.clear();
    mSurfaceSets.resize(5);
    if (mAge == 0) return;

    // The target surfaces, the set each joins (2 the snout's, 3 the cranium's, 1 the body's) and the region it maps to.
    struct Target {
        const char *surface;
        const char *region;
        size_t regionLength;
        uint32_t set;
    };
    const char *const kSnout = "snout";
    const char *const kCranium = "cranium";
    const char *const kBody = GameAddress<const char>(0xdbcf58); // a 4-character region name in the image
    const Target kTargets[] = {
        {"jaw", kSnout, 5, 2},
        {"jaw_grip", kSnout, 5, 2},
        {"mouthtarget_surface", kSnout, 5, 2},
        {"nosetarget_surface", kSnout, 5, 2},
        {"tonguebase_grip", kSnout, 5, 2},
        {"behindears_surface", kCranium, 7, 3},
        {"headtarget_surface", kCranium, 7, 3},
        {"l_earbase_trans", kCranium, 7, 3},
        {"r_earbase_trans", kCranium, 7, 3},
        {"bellytarget_surface", kBody, 4, 1},
        {"rumptarget_surface", kBody, 4, 1},
        {"shouldertarget_surface", kBody, 4, 1},
    };
    for (const Target &t : kTargets) mSurfaceSets[t.set].insert(SurfaceSetName(t.surface, std::strlen(t.surface)));
    for (const Target &t : kTargets) {
        mSurfaceMap[SurfaceName(t.surface, std::strlen(t.surface))].mText.mString.assign(t.region, t.regionLength);
    }
}

void cTSCASBodyPartSystem::ResetKeys()
{
    // A function-local static in the image (guard 0x10d9f64): a copy of the GUID at 0x10d9f44 taken on first use.
    int *guard = GameAddressOpaque<int>(0x10d9f64);
    uint8_t *key = GameAddress<uint8_t>(0x10d9f54);
    BodyPartSystemIface *face = Iface();
    for (uint32_t index = 0; index < 2; ++index) {
        if (*guard > _Init_thread_epoch) {
            _Init_thread_header(guard);
            if (*guard == -1) {
                CopyBlock16(key, GameAddress<uint8_t>(0x10d9f44));
                _Init_thread_footer(guard);
            }
        }
        VirtualCall<void(__thiscall *)(void *, uint32_t, const void *), kSlotStoreKey>(face, index, key);
    }
}

// `this` is not read.
void cTSCASLockedObjectManager::RegisterNoFactory()
{
    RegisterFactory(PersistResourceManager(), kLockedObjectType, kLockedObjectGroup, nullptr);
}

// ---- Slot 4: the FACE resource load (lane IU) -------------------------------------------------------------------------

namespace {

// A node of the game's std::list<std::string> (allocator-service storage, 0x1e7190).
struct DirectoryNode {
    DirectoryNode *mNext;
    DirectoryNode *mPrev;
    std::string mText; // +0x08
};
static_assert(sizeof(DirectoryNode) == 0x20, "DirectoryNode layout");

constexpr uint32_t kFaceDirectoriesQuery = 0x8ded6280;  // cTSAppCommonImpl slot 5: the directories to search
constexpr uint32_t kPersistOpenSegment = 21;            // the persist service: open a package as a segment
constexpr uint32_t kSegmentInterface = 0x3b2271bb;
constexpr uint32_t kSegmentGroup = 4;                   // the segment's group id
constexpr uint32_t kManagerGetResource = 39;            // the resource manager: fetch a resource by key
constexpr uint32_t kFaceResourceType = 0x46414345;      // 'FACE'
constexpr uint32_t kResourceStreamIid = 0xc56b8f08;     // the resource's input stream
constexpr uint32_t kResourceDone = 6;                   // the resource's slot 6, called once it has been read
constexpr uint32_t kStreamReadUint32 = 13;
constexpr uint32_t kStreamReadFloat = 21;
constexpr uint32_t kStreamReadString = 24;
constexpr int kSlotAddRegionMorph = 64;                 // the face's slot 64 (0x100)

template <typename R = uint32_t, typename... A> R CallSlot(void *object, uint32_t slot, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return (*reinterpret_cast<Fn *const *>(object))[slot](object, args...);
}

void ReleaseIf(void *object)
{
    if (object) CallSlot(object, 2);
}

} // namespace

void BodyPartSystemIface::LoadFaceData()
{
    auto *system = static_cast<cTSCASBodyPartSystem *>(SystemOf(this));
    if (auto *records = system->mRecords) records->clear();
    if (TreeHeader *names = system->mTree20) {
        using Erase = void(__thiscall *)(TreeHeader *, TreeHeader *, TreeHeader::Node *);
        OriginalFunction<Erase, 0xcdbf10>()(names, names, names->mHead->mParent);
        ResetTreeHeader(names);
    }
    void *persist = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    CallSlot<void *>(persist, 3); // the resource manager, asked for and not used

    appdebugger::ListHeader directories{nullptr, 0};
    auto *sentinel = static_cast<DirectoryNode *>(appdebugger::AllocateListNodes(1, 0));
    sentinel->mNext = sentinel;
    sentinel->mPrev = sentinel;
    directories.mHead = sentinel;
    appinit::CommonImplSlot5(kFaceDirectoriesQuery, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&directories)), 1, 1,
                             0xffffffffu);
    for (DirectoryNode *node = sentinel->mNext; node != sentinel; node = node->mNext) {
        cRZString path;
        path.Construct(node->mText.c_str());
        void *segment = nullptr;
        if (!cRZFile::PathExists(*path.AsInterface())) {
            path.Destruct();
            continue;
        }
        cRZFileName fileName;
        fileName.Construct(&path);
        cRZString drivePath;
        fileName.GetDrivePath(&drivePath);
        const cRZString &namePart = *fileName.GetName();
        cRZString name;
        name.Construct();
        name.mString.assign(namePart.mString.data(), namePart.mString.size());
        CallSlot(persist, kPersistOpenSegment, kSegmentInterface, &segment, 2u, drivePath.ToChar(), name.ToChar(), 1u, 1u);
        // The original asks the segment for its group without checking that it opened.
        const uint32_t key[4] = {kFaceResourceType, CallSlot(segment, kSegmentGroup), 1, 0};
        void *resource = nullptr;
        void *manager = CallSlot<void *>(persist, 3);
        const bool found = CallSlot<uint8_t>(manager, kManagerGetResource, key, &resource, 0u) != 0;
        if (found) {
            void *stream = nullptr;
            CallSlot(resource, 0, kResourceStreamIid, &stream);
            uint32_t version = 0; // read and not used
            CallSlot(stream, kStreamReadUint32, &version);
            uint32_t count = 0;
            CallSlot(stream, kStreamReadUint32, &count);
            for (uint32_t i = 0; i < count; ++i) {
                cRZString text[4];
                for (cRZString &t : text) t.Construct();
                for (cRZString &t : text) CallSlot(stream, kStreamReadString, t.AsInterface());
                OriginalFunction<void(__thiscall *)(void *, const char *, const char *, const char *, const char *), 0xce0b30>()(
                    system, text[0].ToChar(), text[1].ToChar(), text[2].ToChar(), text[3].ToChar());
                for (int k = 3; k >= 0; --k) text[k].Destruct();
            }
            uint32_t morphs = 0;
            CallSlot(stream, kStreamReadUint32, &morphs);
            // As in the original, the numbers live across iterations: a read that fails leaves the previous value.
            uint32_t a = 0, b = 0;
            float x = 0, y = 0;
            for (uint32_t i = 0; i < morphs; ++i) {
                cRZString first, second;
                first.Construct();
                second.Construct();
                CallSlot(stream, kStreamReadUint32, &a);
                CallSlot(stream, kStreamReadUint32, &b);
                CallSlot(stream, kStreamReadString, first.AsInterface());
                CallSlot(stream, kStreamReadString, second.AsInterface());
                CallSlot(stream, kStreamReadFloat, &x);
                CallSlot(stream, kStreamReadFloat, &y);
                VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t, const char *, const char *, float, float),
                            kSlotAddRegionMorph>(this, a, b, first.ToChar(), second.ToChar(), x, y);
                second.Destruct();
                first.Destruct();
            }
            CallSlot(resource, kResourceDone);
            ReleaseIf(stream);
            ReleaseIf(resource);
        } else {
            ReleaseIf(resource);
        }
        name.Destruct();
        drivePath.Destruct();
        fileName.Destruct();
        ReleaseIf(segment);
        path.Destruct();
        if (found) break; // the first package that has the resource is the only one read
    }
    appdebugger::DestroyStringListNodes(&directories, directories.mHead);
    FreeThroughAllocatorService(directories.mHead, 1);
}

} // namespace nTSCASCore
