// Differential tests for the lane IG members that don't byte-match (src/misc/ClassesRound4.h): the triangle polygons'
// index emitters (operand order of the index sums only) and the "not installed" neighbourhood template filter.
#include "difftest.h"
#include "free_recorder.h"
#include "misc/ClassesRound4.h"

#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace {

// ---- fakes: every virtual call is logged (slot, receiver, arguments) and answered from globals ----

struct Call {
    int slot;
    const void *self;
    uint32_t a, b;
    bool operator==(const Call &o) const { return slot == o.slot && self == o.self && a == o.a && b == o.b; }
};
std::vector<Call> gCalls;

void __fastcall OwnerAdd(const void *self, void *, uint32_t index) { gCalls.push_back({35, self, index, 0}); }
void __fastcall OwnerAddSnow(const void *self, void *, uint32_t index) { gCalls.push_back({36, self, index, 0}); }
void __fastcall OwnerAddLayered(const void *self, void *, uint32_t index, uint32_t layer)
{
    gCalls.push_back({37, self, index, layer});
}

void *gOwnerVTable[40] = {};

void InstallOwner()
{
    gOwnerVTable[35] = reinterpret_cast<void *>(&OwnerAdd);
    gOwnerVTable[36] = reinterpret_cast<void *>(&OwnerAddSnow);
    gOwnerVTable[37] = reinterpret_cast<void *>(&OwnerAddLayered);
}

int32_t RandomSmall(std::mt19937 &rng)
{
    switch (rng() % 4) {
    case 0: return static_cast<int32_t>(rng() % 3);
    case 1: return -static_cast<int32_t>(rng() % 50);
    default: return static_cast<int32_t>(rng() % 20000) - 5000;
    }
}

// One member against its original: random polygons (flag 0, 1 or any value; strides, cells and first vertices
// including negatives), the owner calls (slot, receiver, index, layer) and the polygon compared.
template <typename T>
void RunTriangle(difftest::Context &ctx, uint32_t rva, void (T::*member)(), uint32_t seed)
{
    static_assert(sizeof(T) == sizeof(nTSSG::cTrianglePolygon), "the polygons add no fields");
    std::mt19937 rng(seed);
    InstallOwner();
    auto orig = ctx.Original<void(__thiscall *)(void *)>(rva);
    const void *owner[2] = {gOwnerVTable, nullptr};
    for (int i = 0; i < 5000; ++i) {
        T poly[2];
        auto *bytes = reinterpret_cast<uint8_t *>(&poly[0]);
        for (size_t k = 0; k < sizeof poly[0]; ++k) bytes[k] = static_cast<uint8_t>(rng());
        poly[0].mOwner = owner;
        poly[0].mFlipped = rng() % 3 == 0 ? static_cast<int32_t>(rng()) : static_cast<int32_t>(rng() % 2);
        poly[0].mStride = RandomSmall(rng);
        poly[0].mColumn = RandomSmall(rng);
        poly[0].mRow = RandomSmall(rng);
        poly[0].mFirstVertex = rng() % 4 == 0 ? static_cast<int32_t>(rng() % 0x1000000) : RandomSmall(rng);
        poly[1] = poly[0];
        std::vector<Call> calls[2];
        gCalls.clear();
        orig(&poly[0]);
        calls[0] = gCalls;
        gCalls.clear();
        (poly[1].*member)();
        calls[1] = gCalls;
        ++ctx.cases;
        if (calls[0] != calls[1] || std::memcmp(&poly[0], &poly[1], sizeof poly[0]) != 0) {
            ctx.Fail("flipped %d stride %d column %d row %d first %d: original %zu calls, ours %zu", poly[0].mFlipped,
                     poly[0].mStride, poly[0].mColumn, poly[0].mRow, poly[0].mFirstVertex, calls[0].size(),
                     calls[1].size());
            return;
        }
    }
}

} // namespace

DIFF_TEST(Triangle_EvenFirst_AddIndices, 0x413540)
{
    RunTriangle(ctx, 0x413540, &nTSSG::cBaseEvenTriangleFirst::AddIndices, 0x49470001);
}
DIFF_TEST(Triangle_EvenFirst_AddLayeredIndices, 0x413800)
{
    RunTriangle(ctx, 0x413800, &nTSSG::cBaseEvenTriangleFirst::AddLayeredIndices, 0x49470002);
}
DIFF_TEST(Triangle_SnowEvenFirst_AddSnowIndices, 0x413390)
{
    RunTriangle(ctx, 0x413390, &nTSSG::cSnowEvenTriangleFirst::AddSnowIndices, 0x49470003);
}
DIFF_TEST(Triangle_EvenSecond_AddIndices, 0x4135e0)
{
    RunTriangle(ctx, 0x4135e0, &nTSSG::cBaseEvenTriangleSecond::AddIndices, 0x49470004);
}
DIFF_TEST(Triangle_EvenSecond_AddLayeredIndices, 0x4138c0)
{
    RunTriangle(ctx, 0x4138c0, &nTSSG::cBaseEvenTriangleSecond::AddLayeredIndices, 0x49470005);
}
DIFF_TEST(Triangle_SnowEvenSecond_AddSnowIndices, 0x4133f0)
{
    RunTriangle(ctx, 0x4133f0, &nTSSG::cSnowEvenTriangleSecond::AddSnowIndices, 0x49470006);
}
DIFF_TEST(Triangle_OddFirst_AddIndices, 0x4136a0)
{
    RunTriangle(ctx, 0x4136a0, &nTSSG::cBaseOddTriangleFirst::AddIndices, 0x49470007);
}
DIFF_TEST(Triangle_OddFirst_AddLayeredIndices, 0x413990)
{
    RunTriangle(ctx, 0x413990, &nTSSG::cBaseOddTriangleFirst::AddLayeredIndices, 0x49470008);
}
DIFF_TEST(Triangle_SnowOddFirst_AddSnowIndices, 0x413450)
{
    RunTriangle(ctx, 0x413450, &nTSSG::cSnowOddTriangleFirst::AddSnowIndices, 0x49470009);
}
DIFF_TEST(Triangle_OddSecond_AddIndices, 0x413740)
{
    RunTriangle(ctx, 0x413740, &nTSSG::cBaseOddTriangleSecond::AddIndices, 0x4947000a);
}
DIFF_TEST(Triangle_OddSecond_AddLayeredIndices, 0x413a50)
{
    RunTriangle(ctx, 0x413a50, &nTSSG::cBaseOddTriangleSecond::AddLayeredIndices, 0x4947000b);
}
DIFF_TEST(Triangle_SnowOddSecond_AddSnowIndices, 0x4134b0)
{
    RunTriangle(ctx, 0x4134b0, &nTSSG::cSnowOddTriangleSecond::AddSnowIndices, 0x4947000c);
}
// The roof module's copies (size-built).
DIFF_TEST(Triangle_RoofEvenSecond_AddIndices, 0x7a2c06)
{
    RunTriangle(ctx, 0x7a2c06, &::cBaseEvenTriangleSecond::AddIndices, 0x4947000d);
}
DIFF_TEST(Triangle_RoofOddSecond_AddIndices, 0x7a2cb8)
{
    RunTriangle(ctx, 0x7a2cb8, &::cBaseOddTriangleSecond::AddIndices, 0x4947000e);
}

namespace {
// ---- the "not installed" template filter ----
int32_t gKind = 0;
uint32_t gHasFour = 0, gCategory = 0, gEntryId = 0;
uint32_t gInstalledAnswers = 0; // bit i: installed set i answers slot 11 with true
const void *gSets[8] = {};

uint32_t __fastcall EntryKind(const void *self, void *) { gCalls.push_back({18, self, 0, 0}); return static_cast<uint32_t>(gKind); }
uint32_t __fastcall EntryHas(const void *self, void *, uint32_t what)
{
    gCalls.push_back({14, self, what, 0});
    return gHasFour;
}
uint32_t __fastcall EntryCategory(const void *self, void *) { gCalls.push_back({22, self, 0, 0}); return gCategory; }
uint32_t __fastcall EntryId(const void *self, void *, uint32_t which)
{
    gCalls.push_back({8, self, which, 0});
    return gEntryId;
}
const void *__fastcall InstalledSet(const void *self, void *)
{
    gCalls.push_back({10, self, 0, 0});
    return gSets[reinterpret_cast<uintptr_t>(static_cast<const void *const *>(self)[1])];
}
uint32_t __fastcall SetContains(const void *self, void *, uint32_t id)
{
    gCalls.push_back({11, self, id, 0});
    uintptr_t index = reinterpret_cast<uintptr_t>(static_cast<const void *const *>(self)[1]);
    return (gInstalledAnswers >> index & 1) ? (rand() % 2 ? 1u : 0x101u) : (rand() % 2 ? 0u : 0xab00u);
}
void *gEntryVTable[24] = {};
void *gInstalledVTable[12] = {};
void *gSetVTable[12] = {};
} // namespace

// Entries of kind 4 or others, slot 14 answering 0 / 1 / garbage above al, categories 0..31 against random masks, 0..5 installed templates whose sets answer true / false (with garbage above al):
// the result byte, the calls made (order, receivers, arguments) and the filter compared.
DIFF_TEST(UninstalledNeighborhoodTemplatesFilter_Accept, 0xc2f165)
{
    std::mt19937 rng(0x4947000f);
    gEntryVTable[18] = reinterpret_cast<void *>(&EntryKind);
    gEntryVTable[14] = reinterpret_cast<void *>(&EntryHas);
    gEntryVTable[22] = reinterpret_cast<void *>(&EntryCategory);
    gEntryVTable[8] = reinterpret_cast<void *>(&EntryId);
    gInstalledVTable[10] = reinterpret_cast<void *>(&InstalledSet);
    gSetVTable[11] = reinterpret_cast<void *>(&SetContains);
    // Installed templates and their sets: {vtable, index}.
    const void *installed[8][2], *sets[8][2];
    for (uint32_t i = 0; i < 8; ++i) {
        installed[i][0] = gInstalledVTable;
        installed[i][1] = reinterpret_cast<const void *>(static_cast<uintptr_t>(i));
        sets[i][0] = gSetVTable;
        sets[i][1] = reinterpret_cast<const void *>(static_cast<uintptr_t>(i));
        gSets[i] = sets[i];
    }
    void *list[8];
    for (int i = 0; i < 8; ++i) list[i] = installed[i];
    const void *entry[2] = {gEntryVTable, nullptr};
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0xc2f165);
    for (int i = 0; i < 20000; ++i) {
        int32_t kind = rng() % 4 ? 4 : static_cast<int32_t>(rng() % 8);
        uint32_t has = rng() % 4 ? 1 : (rng() % 2 ? 0 : (rng() & 0xffffff00u) | (rng() % 2));
        uint32_t category = rng() % 32;
        uint32_t mask = rng() % 3 ? rng() : 1u << (rng() % 32);
        uint32_t id = rng();
        uint32_t answers = rng() % 3 ? 0 : rng();
        uint32_t count = rng() % 6;
        uint32_t start = rng() % (8 - count + 1);
        bool nullEntry = rng() % 20 == 0;
        cTSUninstalledNeighborhoodTemplatesFilter filter[2];
        filter[0].mVTable = reinterpret_cast<void *>(static_cast<uintptr_t>(rng()));
        filter[0].mCategories = mask;
        filter[0].mInstalledBegin = list + start;
        filter[0].mInstalledEnd = list + start + count;
        filter[1] = filter[0];
        uint32_t results[2];
        std::vector<Call> calls[2];
        for (int side = 0; side < 2; ++side) {
            gKind = kind;
            gHasFour = has;
            gCategory = category;
            gEntryId = id;
            gInstalledAnswers = answers;
            srand(i);
            gCalls.clear();
            void *e = nullEntry ? nullptr : entry;
            results[side] = side == 0 ? orig(&filter[0], e) & 0xff : static_cast<uint32_t>(filter[1].Accept(e));
            calls[side] = gCalls;
        }
        ++ctx.cases;
        if (results[0] != results[1] || calls[0] != calls[1] ||
            std::memcmp(&filter[0], &filter[1], sizeof filter[0]) != 0) {
            ctx.Fail("kind %d has 0x%x category %u mask 0x%x answers 0x%x count %u: original %u (%zu calls), ours %u "
                     "(%zu calls)",
                     kind, has, category, mask, answers, count, results[0], calls[0].size(), results[1],
                     calls[1].size());
            return;
        }
    }
}

namespace {
void __fastcall ChildDestroy(const void *self, void *, uint32_t flag) { gCalls.push_back({33, self, flag, 0}); }
void *gChildVTable[34] = {};
} // namespace

// cHTMLLink's deleting destructor (its frame differs: the original keeps ebp): flags 0..3 and random, each child null or
// a fake (slot 33 logged): the result, the frees (FreeRecorder), the calls, and the object when it is not freed (the
// base vtable stored last). Our side runs our destructor 0x988ba0 (byte match); the original side the original's.
DIFF_TEST(HTMLLink_DeletingDestructor, 0x988df3)
{
    gChildVTable[33] = reinterpret_cast<void *>(&ChildDestroy);
    const void *children[3][2] = {{gChildVTable, nullptr}, {gChildVTable, nullptr}, {gChildVTable, nullptr}};
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x988df3);
    std::mt19937 rng(0x49470010);
    for (int i = 0; i < 4000; ++i) {
        uint32_t flags = i < 4 ? static_cast<uint32_t>(i) : rng();
        uint32_t fill[11];
        for (uint32_t &w : fill) w = rng();
        void *kids[3];
        for (int k = 0; k < 3; ++k) kids[k] = rng() % 3 == 0 ? nullptr : children[k];
        uint32_t after[2][11];
        bool freed[2];
        std::vector<Call> calls[2];
        for (int side = 0; side < 2; ++side) {
            auto *obj = static_cast<cHTMLLink *>(::operator new(sizeof(cHTMLLink)));
            std::memcpy(obj, fill, sizeof fill);
            obj->mChild1c = kids[0];
            obj->mChild24 = kids[1];
            obj->mChild28 = kids[2];
            gCalls.clear();
            difftest::FreeRecorder rec;
            void *ret = side == 0 ? orig(obj, flags) : obj->DeletingDestructor(flags);
            rec.Stop();
            calls[side] = gCalls;
            freed[side] = rec.Freed(obj);
            ++ctx.cases;
            if (ret != obj) ctx.Fail("side %d returned %p, not this", side, ret);
            if (freed[side] != ((flags & 1) != 0) || rec.Count() != (freed[side] ? 1u : 0u))
                ctx.Fail("side %d flags %08x: freed %d, %zu frees", side, flags, freed[side], rec.Count());
            if (!freed[side]) {
                std::memcpy(after[side], obj, sizeof after[side]);
                ::operator delete(obj);
            }
        }
        if (calls[0] != calls[1]) ctx.Fail("flags %08x: original %zu child calls, ours %zu", flags, calls[0].size(), calls[1].size());
        if (!freed[0] && !freed[1] && std::memcmp(after[0], after[1], sizeof after[0]) != 0)
            ctx.Fail("flags %08x: the objects differ after the destructor", flags);
    }
}

namespace {
uint32_t __fastcall FakeAddRef(const void *self, void *) { gCalls.push_back({1, self, 0, 0}); return 7; }
void *gUnknownVTable[4] = {};
} // namespace

// cGZBufferFontRenderer::QueryInterface (the original keeps two copies of the hand-out; no byte match): ids 1, the
// renderer's, near misses and random ones, the result byte, the out pointer and the AddRef calls compared.
DIFF_TEST(BufferFontRenderer_QueryInterface, 0x90e210)
{
    gUnknownVTable[1] = reinterpret_cast<void *>(&FakeAddRef);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, void **)>(0x90e210);
    std::mt19937 rng(0x49470011);
    static const uint32_t kIds[] = {1, 0xa8bc2a75, 0, 2, 0xa8bc2a74, 0xa8bc2a76, 0x7ec0006};
    for (int i = 0; i < 2000; ++i) {
        uint32_t riid = i < 7 ? kIds[i] : (rng() % 2 ? kIds[rng() % 7] : rng());
        cGZBufferFontRenderer renderer[2];
        renderer[0].mVTable = gUnknownVTable;
        renderer[0].mRefCount = static_cast<int32_t>(rng());
        renderer[1] = renderer[0];
        void *out[2] = {reinterpret_cast<void *>(0x1234), reinterpret_cast<void *>(0x1234)};
        uint32_t result[2];
        std::vector<Call> calls[2];
        gCalls.clear();
        result[0] = orig(&renderer[0], riid, &out[0]) & 0xff;
        calls[0] = gCalls;
        gCalls.clear();
        result[1] = renderer[1].QueryInterface(riid, &out[1]);
        calls[1] = gCalls;
        ++ctx.cases;
        bool sameOut = out[0] == out[1] || (out[0] == &renderer[0] && out[1] == &renderer[1]);
        bool sameCalls = calls[0].size() == calls[1].size();
        if (result[0] != result[1] || !sameOut || !sameCalls)
            ctx.Fail("riid 0x%08x: original %u (%zu calls), ours %u (%zu calls)", riid, result[0], calls[0].size(),
                     result[1], calls[1].size());
    }
}

namespace {
// ---- the drag-and-drop message filter ----
using DragFilter = cTSWinDragAndDropMessageFilter;
const uint32_t kDragIds[] = {0x6c085f45, 0x8c085f48, 0x8c0a1fcc, 0xac085f43, 0xac085f47, 0xac0fe76d, 0x14, 0x13, 0x77};
uint32_t gHandlerAnswers = 0; // consumed one bit per handler call (bit 0 first); garbage above al
const void *gWindow = nullptr;

bool __cdecl FakeHandler(void *window, DragFilter::Message *message, void *context)
{
    gCalls.push_back({100, window, message->mId, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context))});
    gCalls.push_back({101, message->mData, message->mArg[0], message->mArg[1]});
    uint32_t bit = gHandlerAnswers & 1;
    gHandlerAnswers >>= 1;
    return bit != 0;
}
void __fastcall WindowMessage(const void *self, void *, DragFilter::Message *message)
{
    gCalls.push_back({3, self, message->mId, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(message->mData))});
    gCalls.push_back({4, nullptr, message->mArg[0], message->mArg[1]});
}
void *gWindowVTable[4] = {};

// A filter built with our constructor-less layout (placement new: the map is ours, the same layout as the game's);
// handlers for the ids whose bit is set in `mask`, contexts 0x100 + index.
DragFilter *NewFilter(void *buffer, uint32_t mask)
{
    auto *f = new (buffer) DragFilter;
    f->mVTable = nullptr;
    f->mRefCount = 1;
    for (uint32_t k = 0; k < 9; ++k)
        if (mask >> k & 1)
            f->mHandlers[kDragIds[k]] = {&FakeHandler, reinterpret_cast<void *>(static_cast<uintptr_t>(0x100 + k))};
    return f;
}
} // namespace

// DoMessage and Dispatch: every drag id and others, handlers present or not for each, handlers answering true / false
// in scripted order (Dispatch's double call for the plain drag ids shows up as two calls), the drag data's +0x18 / +0x24
// pointing at the window or elsewhere: the result byte and the full call log (handlers with their message, the
// window's slot 3 with the message it is sent) compared.
DIFF_TEST(DragAndDropFilter_DoMessage, 0x7452a6)
{
    gWindowVTable[3] = reinterpret_cast<void *>(&WindowMessage);
    const void *window[2] = {gWindowVTable, nullptr};
    auto doMessage = ctx.Original<uint32_t(__thiscall *)(void *, void *, void *)>(0x7452a6);
    auto dispatch = ctx.Original<uint32_t(__thiscall *)(void *, void *, void *)>(0x74537f);
    std::mt19937 rng(0x49470012);
    alignas(8) uint8_t buffer[2][sizeof(DragFilter)];
    for (int i = 0; i < 6000; ++i) {
        uint32_t mask = rng() % 4 == 0 ? 0x1ff : rng() & 0x1ff;
        uint32_t answers = rng();
        uint32_t id = rng() % 10 == 0 ? rng() : kDragIds[rng() % 9];
        void *data[10];
        for (void *&d : data) d = reinterpret_cast<void *>(static_cast<uintptr_t>(rng()));
        if (rng() % 2) data[6] = window;  // +0x18
        if (rng() % 2) data[9] = window;  // +0x24
        bool direct = rng() % 4 == 0;     // Dispatch alone
        DragFilter::Message message = {id, data, {rng(), rng()}};
        uint32_t result[2];
        std::vector<Call> calls[2];
        for (int side = 0; side < 2; ++side) {
            DragFilter *f = NewFilter(buffer[side], mask);
            DragFilter::Message m = message;
            gHandlerAnswers = answers;
            gCalls.clear();
            if (side == 0)
                result[0] = (direct ? dispatch(f, window, &m) : doMessage(f, window, &m)) & 0xff;
            else
                result[1] = direct ? f->Dispatch(window, &m) : f->DoMessage(window, &m);
            calls[side] = gCalls;
            if (std::memcmp(&m, &message, sizeof m) != 0) ctx.Fail("side %d changed the message", side);
            f->mHandlers.~unordered_map();
        }
        ++ctx.cases;
        if (result[0] != result[1] || calls[0] != calls[1]) {
            ctx.Fail("%s id 0x%08x mask 0x%03x answers 0x%08x: original %u (%zu calls), ours %u (%zu calls)",
                     direct ? "Dispatch" : "DoMessage", id, mask, answers, result[0], calls[0].size(), result[1],
                     calls[1].size());
            return;
        }
    }
}

// SetHandler: random sequences of registrations (new ids and overwrites) on both sides, then every id's handler and
// context, and the number of entries, compared. The original inserts through its own (non-library) try_emplace.
DIFF_TEST(DragAndDropFilter_SetHandler, 0x7453d9)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, void *, void *)>(0x7453d9);
    std::mt19937 rng(0x49470013);
    alignas(8) uint8_t buffer[2][sizeof(DragFilter)];
    for (int i = 0; i < 500; ++i) {
        DragFilter *f[2] = {NewFilter(buffer[0], 0), NewFilter(buffer[1], 0)};
        int n = static_cast<int>(rng() % 40);
        for (int k = 0; k < n; ++k) {
            uint32_t id = rng() % 3 ? rng() % 50 : rng();
            auto fn = reinterpret_cast<DragFilter::HandlerFn>(static_cast<uintptr_t>(rng()));
            void *context = reinterpret_cast<void *>(static_cast<uintptr_t>(rng()));
            orig(f[0], id, reinterpret_cast<void *>(fn), context);
            f[1]->SetHandler(id, fn, context);
        }
        ++ctx.cases;
        bool same = f[0]->mHandlers.size() == f[1]->mHandlers.size();
        for (auto &entry : f[1]->mHandlers) {
            auto it = f[0]->mHandlers.find(entry.first);
            if (it == f[0]->mHandlers.end() || it->second.mFn != entry.second.mFn ||
                it->second.mContext != entry.second.mContext)
                same = false;
        }
        if (!same) ctx.Fail("case %d: %d registrations give different maps", i, n);
        f[0]->mHandlers.~unordered_map();
        f[1]->mHandlers.~unordered_map();
    }
}

// The destructor: filters with 0..9 handlers; the frees (FreeRecorder: count; the map's nodes and bucket vector) and
// the vtable word compared.
DIFF_TEST(DragAndDropFilter_Destruct, 0x74526d)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0x74526d);
    std::mt19937 rng(0x49470014);
    alignas(8) uint8_t buffer[sizeof(DragFilter)];
    for (int i = 0; i < 500; ++i) {
        uint32_t mask = rng() & 0x1ff;
        size_t frees[2];
        const void *vtable[2];
        for (int side = 0; side < 2; ++side) {
            DragFilter *f = NewFilter(buffer, mask);
            difftest::FreeRecorder rec;
            if (side == 0) orig(f);
            else f->Destruct();
            rec.Stop();
            frees[side] = rec.Count();
            vtable[side] = f->mVTable;
        }
        ++ctx.cases;
        if (frees[0] != frees[1] || vtable[0] != vtable[1])
            ctx.Fail("mask 0x%03x: original %zu frees, ours %zu; vtables %p / %p", mask, frees[0], frees[1], vtable[0],
                     vtable[1]);
    }
}
