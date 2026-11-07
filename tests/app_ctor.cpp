#define _CRT_SECURE_NO_WARNINGS
// Differential tests for src/app/AppCtor.*: the application object's constructor (0xc26147),
// destructor (0xc266b9), deleting destructor (0xc267d9) and its two vtable thunks, and the pieces the
// destructor uses (cGZApp's destructor body 0xc59f91, cTSAppCommonImpl's 0xca4de0 with the tree
// erase 0xca49f0, the list teardown 0x1d5e50 and the element teardown 0x1d8450).
//
// The constructor and the reporter call imports (SetUnhandledExceptionFilter, GetModuleFileNameA,
// RegisterWindowMessageA, CoInitialize, CoUninitialize); the tests point those slots at scripted fakes
// for the life of an Env (restored afterwards), and the command line is a double whose switch lookups
// are scripted and logged. The two single-instance mutexes are real ("Global\The Sims 2" and "... Body
// Shop": the names are fixed in the constructor): the Env wraps the cRZMutex vtable entries (Delete,
// Unlock, TryLock) so that both sides log the calls made through them, and a helper thread can hold
// either name to reach the "already running" branches. The application built by the original
// constructor for the destructor tests has its real mutex replaced by a recording double.
#include "rz/RZMessageServer.h"
#include "app_fixture.h"
#include "gz/cGZPropertyManager.h"
#include "rz/cRZNetSSLManager.h"
#include "free_recorder.h"
#include "ts/cTSAppFWHelpers.h"
#include "ts/cTSAppPropertyFiles.h"
#include "ts/cTSGlobals.h"
#include "world/cWorldDB.h"

// ---- 0xc26147 ----

DIFF_TEST(AppCtor_Construct, kAppCtor)
{
    Env env(ctx);
    SetUpCommandLine(ctx);
    LearnOurVTables();
    std::mt19937 rng(kAppCtor);
    auto orig = ctx.Original<cTSAppFW *(__thiscall *)(void *, uint32_t, const char *, void *)>(kAppCtor);
    static const char *const kNames[] = {"", "The Sims 2", "exactly15chars!!", "exactly 15 char", "a considerably longer application name than fits in the small buffer"};
    static const char *const kValues[] = {"on", "enabled", "off", "disabled", "ON", "", "onn", "enable", "other", "off "};
    static const uint8_t kAnswers[] = {0, 1, 2, 3, 0x80, 0xfe, 0xff};
    for (int run = 0; run < 400; ++run) {
        uint32_t id = run % 3 == 0 ? 0x5afdaae5u : static_cast<uint32_t>(rng());
        const char *name = kNames[rng() % 5];
        for (int i = 0; i < 5; ++i) {
            gSwitches[i].al = run % 4 == 0 ? (i == 4 ? 0 : static_cast<uint8_t>(rng() % 2)) : kAnswers[rng() % 7];
            if (rng() % 3 == 0) gSwitches[i].al = 0;
            gSwitches[i].value = kValues[rng() % 10];
        }
        gSwitches[4].al = 0; // crashTest writes to address 4: not run (the faulting write is the point of the switch)
        if (run < 10) { // the interesting values of the first switch, all answered
            gSwitches[0].al = 1;
            gSwitches[0].value = kValues[run];
        }
        bool hold1 = (run / 5) % 2 == 1, hold2 = (run / 10) % 2 == 1;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            std::unique_ptr<Holder> h1, h2;
            if (hold1) h1 = std::make_unique<Holder>("Global\\The Sims 2");
            if (hold2) h2 = std::make_unique<Holder>("Global\\The Sims 2 Body Shop");
            gLog.clear();
            *ctx.image.At<int32_t>(kSystemCodePageCache) = -3;
            alignas(8) uint8_t raw[cTSAppFW::kSize + 8];
            std::mt19937 fill(run);
            for (auto &b : raw) b = static_cast<uint8_t>(fill());
            CmdLineDouble cmd;
            // Ours: the adapter of 0xc26147, our constructor building our class in place (our vtables).
            void *r = side == 0 ? orig(raw, id, name, &cmd) : static_cast<void *>(ts::cTSAppFWAdapters::Construct(raw, nullptr, id, name, reinterpret_cast<cIGZCmdLine *>(&cmd)));
            auto *app = reinterpret_cast<cTSAppFW *>(raw);
            std::string log = Joined();
            uint8_t copy[sizeof(raw)];
            std::memcpy(copy, raw, sizeof(raw));
            std::string codes = TakeVtCodes(ctx, side == 0, copy);
            MaskHeapWords(copy, true);
            std::memset(copy + 0x74, 0, 4); // the mutex object
            for (uint32_t offset = 0; offset <= 0x80; offset += 0x20) {
                auto *string = reinterpret_cast<const cRZString *>(raw + 0x98 + offset);
                if (string->mString.capacity() > 15) std::memset(copy + 0x98 + offset + 4, 0, 4);
            }
            if (app->mApp.mName.mString.capacity() > 15) std::memset(copy + 0x24, 0, 4);
            std::string ids;
            for (uint32_t v : app->mServiceIds) ids += std::to_string(v) + ",";
            std::string state = codes + (r == raw ? " this " : " other ") + Hex(copy, sizeof(copy)) + " name [" + app->mApp.mName.mString + "] ids " + ids + " idsize " + std::to_string(app->mServiceIds.size()) + " list " +
                                std::to_string(app->mList.size()) + " mutex ";
            cRZMutex *mutex = app->mMutex;
            state += std::string(mutex->mData->mValid ? "valid " : "invalid ") + "[" + mutex->mData->mName.mString + "] open " + std::to_string(HandleOpen(mutex->mData->mHandle)) + " vt " + std::to_string(mutex->mVTable == ctx.image.At<void>(kMutexVTable));
            state += "\n" + log;
            // Clean up with the original's own code: the kept mutex, the reporter, the containers.
            gLog.clear();
            reinterpret_cast<uint32_t(__thiscall *)(void *)>(gRealMutexSlots[0])(mutex);
            ctx.Original<void(__thiscall *)(void *)>(kReporterDtor)(raw + 0x98);
            ctx.Original<void(__thiscall *)(void *)>(kCommonImplDtor)(raw + 0x44);
            app->mServiceIds.~set();
            app->mList.~list();
            app->mString244.Destruct();
            app->mApp.mName.Destruct();
            results[side] = state;
        }
        ExpectSame(ctx, ("app ctor " + std::to_string(run)).c_str(), results[0], results[1]);
    }
}

// ---- the teardown helpers ----

namespace {

struct Blocks {
    void *p[1024];
    size_t n = 0;
    void Add(void *x)
    {
        if (x && n < 1024) p[n++] = x;
    }
    // Which blocks the call under test freed, from a FreeRecorder that ran across it (exact: an allocation
    // that reuses a freed block can't make it look live).
    char live[1032];
    void Measure(const difftest::FreeRecorder &rec)
    {
        for (size_t i = 0; i < n; ++i) live[i] = rec.Freed(p[i]) ? '0' : '1';
        live[n] = 0;
    }
    std::string Live() const { return live; }
};

const char *RandomText(std::mt19937 &rng, std::string &store)
{
    static const size_t kLengths[] = {0, 1, 7, 15, 16, 17, 40, 200, 700};
    store.assign(kLengths[rng() % 9], 'x');
    for (char &c : store) c = static_cast<char>('a' + rng() % 26);
    return store.c_str();
}

void *HeapBuffer(cRZString &s)
{
    return s.mString.capacity() > 15 ? *reinterpret_cast<void **>(&s.mString) : nullptr;
}

// A list as the game keeps them: a head node, then `count` nodes with strings.
ListNode *MakeList(std::mt19937 &rng, Blocks &blocks, int count)
{
    auto *head = static_cast<ListNode *>(::operator new(ListNode::kSize));
    std::memset(head, 0xA5, ListNode::kSize);
    head->mNext = head->mPrevious = head;
    ListNode *previous = head;
    for (int i = 0; i < count; ++i) {
        auto *node = static_cast<ListNode *>(::operator new(ListNode::kSize));
        std::memset(node, 0xA6, ListNode::kSize);
        std::string store;
        node->mText.Construct(RandomText(rng, store));
        node->mNext = head;
        node->mPrevious = previous;
        previous->mNext = node;
        head->mPrevious = node;
        previous = node;
        blocks.Add(node);
        blocks.Add(HeapBuffer(node->mText));
    }
    blocks.Add(head);
    return head;
}

TreeNode *MakeTree(std::mt19937 &rng, Blocks &blocks, TreeNode *nil, int depth)
{
    if (depth == 0 || rng() % 4 == 0) return nil;
    auto *node = static_cast<TreeNode *>(::operator new(TreeNode::kSize));
    std::memset(node, 0xB7, TreeNode::kSize);
    node->mIsNil = 0;
    node->mColor = static_cast<uint8_t>(rng() % 2);
    node->mList = MakeList(rng, blocks, rng() % 4);
    TreeNode *left = MakeTree(rng, blocks, nil, depth - 1);
    TreeNode *right = MakeTree(rng, blocks, nil, depth - 1);
    node->mLeft = left;
    node->mRight = right;
    node->mParent = nil;
    blocks.Add(node);
    return node;
}

TreeNode *MakeNil()
{
    auto *nil = static_cast<TreeNode *>(::operator new(TreeNode::kSize));
    std::memset(nil, 0xC8, TreeNode::kSize);
    nil->mLeft = nil->mParent = nil->mRight = nil;
    nil->mColor = 1;
    nil->mIsNil = 1;
    return nil;
}

} // namespace

// ---- 0x1d8450 ----

DIFF_TEST(AppCtor_DestroyElement, kDestroyElement)
{
    std::mt19937 rng(kDestroyElement);
    auto orig = ctx.Original<void(__cdecl *)(void *, void *)>(kDestroyElement);
    for (int run = 0; run < 300; ++run) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 text(run);
            alignas(8) uint8_t raw[0x28];
            std::memset(raw, 0x6D, sizeof(raw));
            auto *s = reinterpret_cast<cRZString *>(raw);
            std::string store;
            s->Construct(RandomText(text, store));
            s->mRefCount = run;
            s->mVTable = reinterpret_cast<const void *>(0x1234 + run); // the teardown must put the string's own vtable back
            Blocks blocks;
            blocks.Add(HeapBuffer(*s));
            uint8_t copy[sizeof(raw)];
            difftest::FreeRecorder rec;
            if (side == 0) orig(nullptr, s);
            else appctor::DestroyElement(nullptr, s);
            rec.Stop();
            blocks.Measure(rec);
            std::memcpy(copy, raw, sizeof(raw));
            bool big = blocks.n != 0; // the buffer pointer stays in the object
            if (big) std::memset(copy + 4, 0, 4);
            results[side] = Hex(copy, sizeof(copy)) + " live " + blocks.Live();
        }
        ExpectSame(ctx, "destroy element", results[0], results[1]);
    }
}

// ---- 0x1d5e50 ----

DIFF_TEST(AppCtor_DestroyListNodes, kDestroyList)
{
    auto orig = ctx.Original<void(__cdecl *)(void *, void *)>(kDestroyList);
    for (int run = 0; run < 300; ++run) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(run);
            Blocks blocks;
            ListNode *head = MakeList(rng, blocks, run % 6);
            difftest::FreeRecorder rec;
            if (side == 0) orig(nullptr, head);
            else appctor::DestroyListNodes(nullptr, head);
            rec.Stop();
            blocks.Measure(rec);
            // The head stays: its next is null; the previous pointer is where the last node was.
            std::string headState = std::string(head->mNext ? "next set " : "next null ") + std::to_string(head->mPrevious == head);
            results[side] = headState + " live " + blocks.Live();
            ::operator delete(head, ListNode::kSize);
        }
        ExpectSame(ctx, "destroy list", results[0], results[1]);
    }
}

// ---- 0xca49f0 ----

DIFF_TEST(AppCtor_EraseSubtree, kEraseSubtree)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(kEraseSubtree);
    for (int run = 0; run < 300; ++run) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(run);
            Blocks blocks;
            TreeNode *nil = MakeNil();
            TreeNode *root = run == 0 ? nil : MakeTree(rng, blocks, nil, 2 + run % 4);
            void *container = &results; // never dereferenced
            difftest::FreeRecorder rec;
            if (side == 0) orig(container, container, root);
            else appctor::EraseSubtree(container, nullptr, container, root);
            rec.Stop();
            blocks.Measure(rec);
            uint8_t nilCopy[TreeNode::kSize];
            std::memcpy(nilCopy, nil, sizeof(nilCopy));
            std::memset(nilCopy, 0, 12); // the three pointers to itself
            results[side] = "live " + blocks.Live() + " nil " + Hex(nilCopy, TreeNode::kSize);
            ::operator delete(nil, TreeNode::kSize);
        }
        ExpectSame(ctx, "erase subtree", results[0], results[1]);
    }
}

// ---- 0xc266b9, 0xc267d9, 0xc26780, 0xc26788 ----

namespace {

// Doubles for the mutex at +0x74 and the object at +0x80: slots log the calls.
uint32_t __fastcall FakeMutexUnlock(void *, void *)
{
    Log("mutex.Unlock");
    return 0x1234;
}
uint32_t __fastcall FakeMutexDelete(void *, void *)
{
    Log("mutex.Delete");
    return 0x5678;
}
uint32_t __fastcall FakeRelease80(void *, void *)
{
    Log("object80.Release");
    return 0x9abc;
}
void *gMutexVt[8], *gObj80Vt[8];
struct VtableObject {
    void **vtable;
};
VtableObject gElements[4];
uint32_t __fastcall FakeElementRelease(void *self, void *)
{
    Log("element[%d].Release", static_cast<int>(static_cast<VtableObject *>(self) - gElements));
    return 0;
}
VtableObject gFakeMutex = {gMutexVt}, gFakeObj80 = {gObj80Vt};

enum class Entry { Body, Deleting, ThunkUnknownMT, ThunkCommonImpl };


// Builds an application (the original side always with the original constructor; ours with the original constructor
// on the Adapter route, with ours on the OwnVTable route), gives it content from the seed, runs one destructor entry
// (original or ours) and describes the outcome.
std::string RunDestructor(difftest::Context &ctx, Entry entry, bool original, int run, uint32_t flags, Route route)
{
    std::mt19937 rng(run);
    gLog.clear();
    *ctx.image.At<int32_t>(kSystemCodePageCache) = -3;
    for (int i = 0; i < 5; ++i) gSwitches[i].al = 0;
    CmdLineDouble cmd;
    // The application is allocated like the game does it (operator new, 0x268 bytes).
    auto *app = static_cast<cTSAppFW *>(::operator new(cTSAppFW::kSize));
    std::memset(app, 0x99, cTSAppFW::kSize);
    const char *name = run % 2 ? "The Sims 2" : "a considerably longer application name than fits in the small buffer";
    if (original || route == Route::Adapter)
        ctx.Original<void *(__thiscall *)(void *, uint32_t, const char *, void *)>(kAppCtor)(app, 0x1234, name, &cmd);
    else
        ts::cTSAppFWAdapters::Construct(app, nullptr, 0x1234, name, reinterpret_cast<cIGZCmdLine *>(&cmd));
    // The real mutex is deleted now and replaced by the double.
    reinterpret_cast<uint32_t(__thiscall *)(void *)>(gRealMutexSlots[0])(app->mMutex);
    app->mMutex = reinterpret_cast<cRZMutex *>(&gFakeMutex);
    app->mObject80 = run % 3 == 0 ? nullptr : reinterpret_cast<cIGZUnknown *>(&gFakeObj80);

    Blocks blocks;
    blocks.Add(app->mCommonImpl.mHeadA);
    blocks.Add(app->mCommonImpl.mHeadB);
    auto *headA = static_cast<TreeNode *>(app->mCommonImpl.mHeadA);
    auto *headB = static_cast<TreeNode *>(app->mCommonImpl.mHeadB);
    if (run % 4 != 0) headA->mParent = MakeTree(rng, blocks, headA, 2 + run % 3);
    if (run % 5 != 0) headB->mParent = MakeTree(rng, blocks, headB, 2 + run % 3);
    app->mCommonImpl.mSizeA = run;
    app->mCommonImpl.mSizeB = run * 3;
    for (uint32_t i = 0; i < rng() % 6; ++i) appbase::InsertId(&app->mServiceIds, nullptr, rng());
    for (uint32_t i = 0; i < rng() % 5; ++i) app->mList.emplace_back(rng() % 3 == 0 ? nullptr : &gElements[rng() % 4]);
    std::string longText(200 + run, 'q');
    app->mString244.mString = run % 2 ? longText : "short";
    blocks.Add(HeapBuffer(app->mString244));
    // Heads of the set and the list, and their nodes.
    blocks.Add(*reinterpret_cast<void **>(&app->mServiceIds));
    blocks.Add(*reinterpret_cast<void **>(&app->mList));
    for (auto it = app->mServiceIds.begin(); it != app->mServiceIds.end(); ++it) blocks.Add(const_cast<char *>(reinterpret_cast<const char *>(&*it)) - 0x10);
    for (auto it = app->mList.begin(); it != app->mList.end(); ++it) blocks.Add(reinterpret_cast<char *>(&*it) - 8);
    blocks.Add(app);

    gLog.clear();
    void *result = nullptr;
    uint8_t *raw = reinterpret_cast<uint8_t *>(app);
    difftest::FreeRecorder rec;
    if (original) {
        switch (entry) {
        case Entry::Body: ctx.Original<void(__thiscall *)(void *)>(kAppDtor)(app); break;
        case Entry::Deleting: result = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kAppDeleting)(app, flags); break;
        case Entry::ThunkUnknownMT: result = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kThunkUnknownMT)(raw + 4, flags); break;
        case Entry::ThunkCommonImpl: result = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kThunkCommonImpl)(raw + 0x44, flags); break;
        }
    } else if (route == Route::Adapter) {
        auto *self = reinterpret_cast<ts::cTSAppFW *>(app);
        switch (entry) {
        case Entry::Body: ts::cTSAppFWAdapters::Destruct(self, nullptr); break;
        case Entry::Deleting: result = ts::cTSAppFWAdapters::DeletingDestructor(self, nullptr, flags); break;
        default: break; // the thunks have no adapter (not hooked): OwnVTable route only
        }
    } else {
        auto *self = reinterpret_cast<ts::cTSAppFW *>(app);
        switch (entry) {
        case Entry::Body: self->ts::cTSAppFW::~cTSAppFW(); break;
        case Entry::Deleting: // the deleting destructor is reached through the vtables only: the +4 one's slot 3
        case Entry::ThunkUnknownMT: result = CallSlot<void *>(raw + 4, 3, flags); break;
        case Entry::ThunkCommonImpl: result = CallSlot<void *>(raw + 0x44, 10, flags); break;
        }
    }
    rec.Stop();
    blocks.Measure(rec);
    std::string state = std::string(result == nullptr ? "void " : result == app ? "this " : "other ") + "log:\n" + Joined();
    state += "global " + std::to_string(*ctx.image.At<void *>(appbase::ExceptionReporterObject::kInstanceGlobal) == nullptr);
    bool freed = entry != Entry::Body && (flags & 1);
    state += " live " + blocks.Live();
    if (!freed) {
        uint8_t copy[0x268];
        std::memcpy(copy, raw, sizeof(copy));
        state += " " + TakeVtCodes(ctx, original, copy);
        MaskHeapWords(copy, false);
        // The reporter's strings carry stale heap pointers after the teardown; its bytes are not
        // compared here (0xc266b9 calls the reporter's destructor, tested elsewhere).
        std::memset(copy + 0x98, 0, 0x244 - 0x98);
        state += " bytes " + Hex(copy, sizeof(copy));
        // The block is freed by the test (the object is not the game's).
        ::operator delete(app, cTSAppFW::kSize);
    }
    return state;
}

void RunDestructorTest(difftest::Context &ctx, Entry entry, Route route)
{
    Env env(ctx);
    SetUpCommandLine(ctx);
    LearnOurVTables();
    gMutexVt[0] = reinterpret_cast<void *>(&FakeMutexDelete);
    gMutexVt[2] = reinterpret_cast<void *>(&FakeMutexUnlock);
    gObj80Vt[2] = reinterpret_cast<void *>(&FakeRelease80);
    static void *elementVt[8];
    elementVt[2] = reinterpret_cast<void *>(&FakeElementRelease);
    for (VtableObject &e : gElements) e.vtable = elementVt;
    for (int run = 0; run < 60; ++run) {
        uint32_t flags = entry == Entry::Body ? 0 : run % 4;
        std::string results[2];
        for (int side = 0; side < 2; ++side) results[side] = RunDestructor(ctx, entry, side == 0, run, flags, route);
        ExpectSame(ctx, ("app destructor " + std::to_string(run)).c_str(), results[0], results[1]);
    }
}

} // namespace

DIFF_TEST(AppCtor_Destruct, kAppDtor)
{
    RunDestructorTest(ctx, Entry::Body, Route::Adapter);
    RunDestructorTest(ctx, Entry::Body, Route::OwnVTable);
}

DIFF_TEST(AppCtor_DeletingDestructor, kAppDeleting)
{
    RunDestructorTest(ctx, Entry::Deleting, Route::Adapter);
    RunDestructorTest(ctx, Entry::Deleting, Route::OwnVTable);
}

// 0xc26780: the compiler's adjustor thunk in our +4 vtable (slot 3), against the original thunk.
DIFF_TEST(AppCtor_DeletingThunkUnknownMT, kThunkUnknownMT)
{
    RunDestructorTest(ctx, Entry::ThunkUnknownMT, Route::OwnVTable);
}

// 0xc26788: the compiler's adjustor thunk in our +0x44 vtable (slot 10).
DIFF_TEST(AppCtor_DeletingThunkCommonImpl, kThunkCommonImpl)
{
    RunDestructorTest(ctx, Entry::ThunkCommonImpl, Route::OwnVTable);
}

// =====================================================================================================================
// ts::cTSAppFW's slots (lane FG), on both routes (Route above):
//  - OwnVTable: each side's own object (the original's built by 0xc26147, ours by our constructor), called through its
//    own vtables: the original side reaches the game's bodies and thunks, ours the compiler's thunks and our members.
//  - Adapter: objects built by the original constructor on both sides (the game's live application); the original side
//    calls the original function, ours the hook adapter, with the adapter's own slot trapped in a copy of the game's
//    vtable (an adapter that dispatched virtually would reach the trap; in the game: its own hook).
// The objects are compared whole after every call (heap words masked as in AppCtor_Construct, the six vtable words as
// codes), with the fakes' call logs (who was called on which face: offsets into the object).
// =====================================================================================================================

namespace {

// ---- the small slots: QueryInterface 0xc297fa (and the compiler's QueryInterface / AddRef / Release thunks), SetAppName
// 0xc299da, InitOptions 0xc2a7bb, GetWord70 (folded 0x1457d0)

uint8_t gFindAnswer;
uint32_t __fastcall FakeFindProductDirectories(void *self, void *)
{
    Log("FindProductDirectories@%d", AppOff(self));
    return 0x3300u | gFindAnswer;
}

// No slot is forwarded to a game body any more: Shutdown, OnTick, PreFrameworkInit and SetupUserDataDirectories have
// bodies (lane FM: tests/app_lifecycle.cpp, tests/app_dirs.cpp), AddDynamicLibrariesHere and Init too (lane FW:
// tests/app_dlls.cpp, tests/app_init.cpp).

const uint32_t kQiIds[] = {0x452294aa, 0x9af89af3, 0x66, 0x287fb697, 1, 0xebabd35c, 0, 0xffffffff, 0x26c63345, 0xa5c5b6a7};
const char *const kAppNames[] = {"", "x", "exactly15chars!", "sixteen chars!!!", "an application name well beyond the small buffer"};

std::string RunSlotsCase(difftest::Context &ctx, bool original, Route route, int run)
{
    std::mt19937 r(run * 7919 + static_cast<int>(route));
    LiveApp app(ctx, original, route, kAppNames[run % 5]);
    app.SetSlot(4, 14, reinterpret_cast<const void *>(&FakeFindProductDirectories));
    if (route == Route::Adapter) {
        app.SetSlot(0, 0, reinterpret_cast<const void *>(&TrapTwoArgs)); // QueryInterface's own slot
        app.SetSlot(4, 13, reinterpret_cast<const void *>(&TrapNoArgs)); // InitOptions's own slot
    }
    std::string out = app.Digest() + "\n";
    for (int i = 0; i < 14; ++i) {
        int op = i < 2 ? 1 : static_cast<int>(r() % 7);
        if (route == Route::Adapter) op = r() % 2 ? 0 : 4;
        int face = static_cast<int>(r() % 6);
        uint32_t result = 0;
        char what[96];
        gLog.clear();
        switch (op) {
        case 0: { // QueryInterface
            uint32_t iid = r() % 4 == 0 ? static_cast<uint32_t>(r()) : kQiIds[r() % 10];
            void *got = reinterpret_cast<void *>(0x7a7a7a7a);
            if (route == Route::Adapter) {
                face = 0;
                result = original ? ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, void **)>(0xc297fa)(app.raw, iid, &got)
                                  : ts::cTSAppFWAdapters::QueryInterface(reinterpret_cast<ts::cTSAppFW *>(app.raw), nullptr, iid, &got);
            } else {
                result = CallSlot<uint32_t>(app.Face(face), 0, iid, &got);
            }
            std::snprintf(what, sizeof(what), "QI@%d(%x) -> %d", kFaceOffsets[face], iid, got == reinterpret_cast<void *>(0x7a7a7a7a) ? -9 : AppOff(got));
            break;
        }
        case 1: // AddRef
            result = CallSlot<uint32_t>(app.Face(face), 1);
            std::snprintf(what, sizeof(what), "AddRef@%d -> %u", kFaceOffsets[face], result);
            break;
        case 2: // Release, never the last reference
            if (*reinterpret_cast<uint32_t *>(app.raw + 8) < 2) continue;
            result = CallSlot<uint32_t>(app.Face(face), 2);
            std::snprintf(what, sizeof(what), "Release@%d -> %u", kFaceOffsets[face], result);
            break;
        case 3: // SetAppName (cITSAppCommon slot 9)
            CallSlot<void>(app.Face(4), 9, kAppNames[r() % 5]);
            std::snprintf(what, sizeof(what), "SetAppName");
            break;
        case 4: // InitOptions (cITSAppCommon slot 13; slot 14 faked)
            gFindAnswer = static_cast<uint8_t>(r() % 3 == 0 ? 0 : r());
            if (route == Route::Adapter)
                result = original ? ctx.Original<uint32_t(__thiscall *)(void *)>(0xc2a7bb)(app.Face(4))
                                  : ts::cTSAppFWAdapters::InitOptions(reinterpret_cast<ts::cTSAppCommonImpl *>(app.Face(4)), nullptr);
            else
                result = CallSlot<uint32_t>(app.Face(4), 13);
            std::snprintf(what, sizeof(what), "InitOptions");
            break;
        default: // GetWord70 (cITSAppFW slot 4)
            *reinterpret_cast<uint32_t *>(app.raw + 0x70) = r();
            result = CallSlot<uint32_t>(app.Face(3), 4);
            std::snprintf(what, sizeof(what), "GetWord70 -> %x", result);
            break;
        }
        out += what + std::string(" ") + Outcome(result, app.Digest());
    }
    return out;
}

} // namespace

// QueryInterface 0xc297fa; the thunks QueryInterface 0xc29852 / 0xc2985a / 0xc29862 / 0xc2986a / 0xc29872, AddRef 0xc274b6
// (primary) and 0xc274bb / 0xc274c3 / 0xc274cb / 0xc274d3 / 0xc274db, Release 0xc29892 (primary), 0xb359c9 (its target, slot 4
// of the +4 face) and 0xc29897 / 0xc2989f / 0xc298a7 / 0xc298af / 0xc298b7; SetAppName 0xc299da; InitOptions 0xc2a7bb.
DIFF_TEST(cTSAppFW_Slots, 0xc297fa)
{
    Env env(ctx);
    SetUpCommandLine(ctx);
    LearnOurVTables();
    for (Route route : {Route::OwnVTable, Route::Adapter}) {
        for (int run = 0; run < 150; ++run) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) results[side] = RunSlotsCase(ctx, side == 0, route, run);
            ExpectSame(ctx, (std::string(route == Route::Adapter ? "adapter" : "own vtable") + " slots " + std::to_string(run)).c_str(), results[0], results[1]);
        }
    }
}

// ---- 0xc296b4 PreTranslateMessage (cITSAppFW slot 3)

namespace {

alignas(8) uint8_t gMessageBlock[0x40];
void *__cdecl FakeMalloc(uint32_t bytes)
{
    Log("Malloc(%x)", bytes);
    std::memset(gMessageBlock, 0x77, sizeof(gMessageBlock));
    return gMessageBlock;
}
const char *MessageName(const void *p)
{
    return p == gMessageBlock ? "message" : p ? "other" : "null";
}
uint32_t __fastcall FakeMessageAddRef(void *self, void *)
{
    Log("%s.AddRef", MessageName(self));
    return 0x1100;
}
uint32_t __fastcall FakeMessageRelease(void *self, void *)
{
    Log("%s.Release", MessageName(self));
    return 0x1200;
}
uint32_t __fastcall FakeMessageSlot5(void *self, void *, uint32_t v)
{
    Log("%s.slot5(%x)", MessageName(self), v);
    return 0x1300;
}
uint32_t __fastcall FakeMessageSlot11(void *self, void *, uint32_t v)
{
    Log("%s.slot11(%x)", MessageName(self), v);
    return 0x1400;
}
void *gServerVt[8];
FakeObject gServer{gServerVt};
uint32_t __fastcall FakeServerPost(void *self, void *, void *message, uint32_t flag)
{
    Log("server%s.slot4(%s, %x)", self == &gServer ? "" : "?", MessageName(message), flag);
    return 0x1500;
}
void *__cdecl FakeGetServer()
{
    Log("0x9c5a89");
    return &gServer;
}
HANDLE gEventHandle;
HANDLE WINAPI FakeOpenEvent(DWORD access, BOOL inherit, const char *name)
{
    Log("OpenEventA(%lx, %d, %s)", access, inherit, name);
    return gEventHandle;
}
BOOL WINAPI FakeSetEvent(HANDLE h)
{
    Log("SetEvent(%p)", h);
    return TRUE;
}
BOOL WINAPI FakeCloseHandle(HANDLE h)
{
    Log("CloseHandle(%p)", h);
    return TRUE;
}
void *gDelegateVt[8];
FakeObject gDelegate{gDelegateVt};
MSG gMsg;
uint8_t gDelegateAnswer;
uint32_t __fastcall FakeDelegate(void *self, void *, void *msg)
{
    Log("delegate%s.slot3(%s)", self == &gDelegate ? "" : "?", msg == &gMsg ? "msg" : "other");
    return 0x6600u | gDelegateAnswer;
}

} // namespace

DIFF_TEST(cTSAppFW_PreTranslateMessage, 0xc296b4)
{
    Env env(ctx);
    SetUpCommandLine(ctx);
    LearnOurVTables();
    Redirect malloc0(ctx.image.At(0x1be3b0), reinterpret_cast<void *>(&FakeMalloc));
    Redirect malloc1(reinterpret_cast<void *>(&RZMemory::Malloc), reinterpret_cast<void *>(&FakeMalloc));
    Redirect server(ctx.image.At(0x9c5a89), reinterpret_cast<void *>(&FakeGetServer));
    Redirect ourServer(reinterpret_cast<void *>(&GetCachedMessageServer), reinterpret_cast<void *>(&FakeGetServer)); // ours
    gServerVt[4] = reinterpret_cast<void *>(&FakeServerPost);
    gDelegateVt[3] = reinterpret_cast<void *>(&FakeDelegate);
    void **messageVt = ctx.image.At<void *>(0xda4064);
    void *savedVt[12];
    std::memcpy(savedVt, messageVt, sizeof(savedVt));
    messageVt[1] = reinterpret_cast<void *>(&FakeMessageAddRef);
    messageVt[2] = reinterpret_cast<void *>(&FakeMessageRelease);
    messageVt[5] = reinterpret_cast<void *>(&FakeMessageSlot5);
    messageVt[11] = reinterpret_cast<void *>(&FakeMessageSlot11);
    for (Route route : {Route::OwnVTable, Route::Adapter}) {
        for (int run = 0; run < 300; ++run) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                std::mt19937 r(run * 31 + static_cast<int>(route));
                bool original = side == 0;
                LiveApp app(ctx, original, route, "The Sims 2");
                if (route == Route::Adapter) app.SetSlot(3, 3, reinterpret_cast<const void *>(&TrapOneArg));
                if (r() % 5 == 0) *reinterpret_cast<uint32_t *>(app.raw + 0x94) = 0;
                uint32_t shutdown = *reinterpret_cast<uint32_t *>(app.raw + 0x94);
                *reinterpret_cast<void **>(app.raw + 0x80) = r() % 3 ? &gDelegate : nullptr;
                gDelegateAnswer = static_cast<uint8_t>(r() % 3 == 0 ? 0 : r());
                gEventHandle = r() % 3 == 0 ? nullptr : reinterpret_cast<HANDLE>(0x4240 + r() % 16);
                std::memset(&gMsg, 0, sizeof(gMsg));
                uint32_t kind = r() % 6;
                gMsg.message = kind == 0 ? shutdown : kind == 1 ? 0 : kind == 2 ? shutdown + 1 : kind == 3 ? shutdown - 1 : kind == 4 ? 0xC024 : r();
                gMsg.wParam = r();
                void *msg = run % 9 == 4 ? nullptr : &gMsg;
                std::memset(gMessageBlock, 0x55, sizeof(gMessageBlock));
                uint32_t result;
                {
                    IatPatch open(ctx, 0xd9a3e4, reinterpret_cast<const void *>(&FakeOpenEvent));
                    IatPatch set(ctx, 0xd9a10c, reinterpret_cast<const void *>(&FakeSetEvent));
                    IatPatch close(ctx, 0xd9a17c, reinterpret_cast<const void *>(&FakeCloseHandle));
                    gLog.clear();
                    if (route == Route::Adapter)
                        result = original ? ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0xc296b4)(app.Face(3), msg)
                                          : ts::cTSAppFWAdapters::PreTranslateMessage(reinterpret_cast<cITSAppFW *>(app.Face(3)), nullptr, msg);
                    else
                        result = CallSlot<uint32_t>(app.Face(3), 3, msg);
                }
                uint8_t block[0x2c];
                std::memcpy(block, gMessageBlock, sizeof(block));
                bool gameVt = *reinterpret_cast<void **>(block) == static_cast<void *>(messageVt);
                std::memset(block, 0, 4);
                results[side] = Outcome(result, app.Digest()) + (gameVt ? "msgvt " : "other ") + Hex(block, sizeof(block));
            }
            ExpectSame(ctx, (std::string(route == Route::Adapter ? "adapter" : "own vtable") + " PreTranslateMessage " + std::to_string(run)).c_str(), results[0], results[1]);
        }
    }
    std::memcpy(messageVt, savedVt, sizeof(savedVt));
}

// ---- 0xc27605 DoMessage (cIGZMessageTarget2 slot 3)

namespace {

void *gInMessageVt[8], *gStandardVt[32], *gValueVt[4], *gStringServiceVt[48], *gLanguageVt[12];
FakeObject gInMessage{gInMessageVt}, gStandard{gStandardVt}, gValue{gValueVt}, gStringService{gStringServiceVt}, gLanguage{gLanguageVt};
int gObject60;
int gLanguageAnswer;
uint32_t gMessageId;
bool gAnswerStandard, gStateValue;
uint8_t gStateAnswer;

uint32_t __fastcall FakeInQI(void *self, void *, uint32_t iid, void **out)
{
    Log("message%s.QI(%x)", self == &gInMessage ? "" : "?", iid);
    if (gAnswerStandard && iid == 0x65297976) {
        *out = &gStandard;
        return 0x2201;
    }
    return 0x2200;
}
uint32_t __fastcall FakeInGetId(void *self, void *)
{
    Log("message%s.GetId", self == &gInMessage ? "" : "?");
    return gMessageId;
}
uint32_t __fastcall FakeStandardRelease(void *self, void *)
{
    Log("standard%s.Release", self == &gStandard ? "" : "?");
    return 0;
}
uint32_t __fastcall FakeStandardSlot29(void *self, void *, uint32_t key, void **out)
{
    Log("standard%s.slot29(%x)", self == &gStandard ? "" : "?", key);
    if (gStateValue) *out = &gValue;
    return 0x5500u | gStateAnswer;
}
uint32_t __fastcall FakeValueRelease(void *self, void *)
{
    Log("value%s.Release", self == &gValue ? "" : "?");
    return 0;
}
void __fastcall FakeApplyState(void *self, void *, void *value)
{
    Log("0xc2a3d6@%d(%s)", AppOff(self), value == &gValue ? "value" : value ? "other" : "null");
}
void __fastcall FakeObject60(void *self, void *)
{
    Log("0xcb28b0(%s)", self == &gObject60 ? "object60" : "other");
}
void __fastcall FakeFatal(void *self, void *)
{
    Log("0xc28de0@%d", AppOff(self));
}
void __fastcall FakeCrashData(void *self, void *)
{
    Log("0xc28e32@%d", AppOff(self));
}
void __cdecl FakeExpand(void *text, void *out, void *language)
{
    auto *in = static_cast<cTSString *>(text);
    Log("0xc27961(%s, %s, %s)", in->mString.mString.c_str(), out ? "out" : "null", language == &gLanguageAnswer ? "language" : "other");
    static_cast<cTSString *>(out)->mString.mString = "expanded " + in->mString.mString;
}
uint32_t __fastcall FakeStringServiceSlot43(void *self, void *, uint32_t v)
{
    Log("stringservice%s.slot43(%x)", self == &gStringService ? "" : "?", v);
    return 0;
}
void *__fastcall FakeLanguageSlot7(void *self, void *)
{
    Log("language%s.slot7", self == &gLanguage ? "" : "?");
    return &gLanguageAnswer;
}

constexpr uint32_t kTextGlobals = 0x10fda54; // six cRZStrings, 0x20 apart (the std::strings at 0x10fda58 + 0x20 * i)

} // namespace

DIFF_TEST(cTSAppFW_DoMessage, 0xc27605)
{
    Env env(ctx);
    SetUpCommandLine(ctx);
    LearnOurVTables();
    tsfake::ManagerFixture fixture(ctx);
    Redirect c0(ctx.image.At(0xc2a3d6), reinterpret_cast<void *>(&FakeApplyState));
    Redirect c1(ctx.image.At(0xcb28b0), reinterpret_cast<void *>(&FakeObject60));
    Redirect c1ours(MemberAddress(&ts::cTSAppPropertyFiles::Save), reinterpret_cast<void *>(&FakeObject60)); // lane MA
    Redirect c2(ctx.image.At(0xc28de0), reinterpret_cast<void *>(&FakeFatal));
    Redirect c3(ctx.image.At(0xc28e32), reinterpret_cast<void *>(&FakeCrashData));
    Redirect c4(ctx.image.At(0xc27961), reinterpret_cast<void *>(&FakeExpand));
    // Ours (lane MB).
    Redirect c0ours(MemberAddress<&ts::cTSAppFW::UnlockNeighborhoodFiles>(), reinterpret_cast<void *>(&FakeApplyState));
    Redirect c2ours(MemberAddress<&ts::cTSAppFW::ReportFatal>(), reinterpret_cast<void *>(&FakeFatal));
    Redirect c3ours(MemberAddress<&ts::cTSAppFW::AddCrashData>(), reinterpret_cast<void *>(&FakeCrashData));
    Redirect c4ours(reinterpret_cast<void *>(&appfw::AppendEditionSuffix), reinterpret_cast<void *>(&FakeExpand));
    gInMessageVt[0] = reinterpret_cast<void *>(&FakeInQI);
    gInMessageVt[4] = reinterpret_cast<void *>(&FakeInGetId);
    gStandardVt[2] = reinterpret_cast<void *>(&FakeStandardRelease);
    gStandardVt[29] = reinterpret_cast<void *>(&FakeStandardSlot29);
    gValueVt[2] = reinterpret_cast<void *>(&FakeValueRelease);
    gStringServiceVt[43] = reinterpret_cast<void *>(&FakeStringServiceSlot43);
    gLanguageVt[7] = reinterpret_cast<void *>(&FakeLanguageSlot7);
    void **stringCache = ctx.image.At<void *>(0x10faf6c), **languageCache = ctx.image.At<void *>(0x10f51c8);
    void *savedStringCache = *stringCache, *savedLanguageCache = *languageCache;
    uint8_t savedGlobals[6 * 0x20];
    std::memcpy(savedGlobals, ctx.image.At<uint8_t>(kTextGlobals), sizeof(savedGlobals));
    static const uint32_t kIds[] = {0xc3, 0xcd, 0xce, 0xb627ad3, 0xb627ad4, 0xb627ad2, 0xb627ad5, 0xce1505fd, 0, 0xc4, 0xb627ad6};
    for (Route route : {Route::OwnVTable, Route::Adapter}) {
        for (int run = 0; run < 400; ++run) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                std::mt19937 r(run * 131 + static_cast<int>(route));
                bool original = side == 0;
                LiveApp app(ctx, original, route, "The Sims 2");
                if (route == Route::Adapter) app.SetSlot(5, 3, reinterpret_cast<const void *>(&TrapOneArg));
                for (int i = 0; i < 6; ++i) reinterpret_cast<cRZString *>(ctx.image.At<uint8_t>(kTextGlobals + 0x20 * i))->Construct(r() % 2 ? "old" : "");
                *stringCache = &gStringService;
                *languageCache = &gLanguage;
                fixture.Reset(tsfake::Mode::Present);
                uint32_t found = r(); // which of the seven texts exist
                tsfake::gAnswerer = [found](const tsfake::Ask &ask, std::string &text) {
                    uint32_t bit = (ask.instance ^ (ask.group >> 3)) % 13;
                    if (!((found >> bit) & 1)) return false;
                    text = "text " + std::to_string(ask.group) + "/" + std::to_string(ask.instance);
                    return true;
                };
                if (r() % 3 == 0) app.raw[0x264] = static_cast<uint8_t>(r());
                *reinterpret_cast<void **>(app.raw + 0x60) = r() % 3 ? &gObject60 : nullptr;
                gMessageId = run < 22 ? kIds[run % 11] : r() % 5 == 0 ? r() : kIds[r() % 11];
                gAnswerStandard = r() % 6 != 0;
                gStateValue = r() % 3 != 0;
                gStateAnswer = static_cast<uint8_t>(r() % 3 == 0 ? 0 : r());
                void *message = run % 13 == 6 ? nullptr : &gInMessage;
                gLog.clear();
                uint32_t result;
                if (route == Route::Adapter)
                    result = original ? ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0xc27605)(app.Face(5), message)
                                      : ts::cTSAppFWAdapters::DoMessage(reinterpret_cast<cIGZMessageTarget2 *>(app.Face(5)), nullptr, message);
                else
                    result = CallSlot<uint32_t>(app.Face(5), 3, message);
                std::string state = Outcome(result, app.Digest());
                state += "asks " + std::to_string(tsfake::gAsks.size()) + " created " + std::to_string(tsfake::gResourcesCreated) + " released " +
                         std::to_string(tsfake::gResourcesReleased);
                for (const tsfake::Ask &a : tsfake::gAsks)
                    state += " [" + std::to_string(a.type) + " " + std::to_string(a.group) + " " + std::to_string(a.instance) + " " + std::to_string(a.keyWord3) + " " +
                             std::to_string(a.interfaceId) + " " + std::to_string(a.loadFlags) + "]";
                for (int i = 0; i < 6; ++i) {
                    auto *g = reinterpret_cast<cRZString *>(ctx.image.At<uint8_t>(kTextGlobals + 0x20 * i));
                    state += " g" + std::to_string(i) + " [" + g->mString + "] rc " + std::to_string(g->mRefCount) + (g->mVTable == ctx.image.At<void>(cRZString::kVTable) ? " rzvt" : " other");
                    g->Destruct();
                }
                results[side] = state;
            }
            ExpectSame(ctx, (std::string(route == Route::Adapter ? "adapter" : "own vtable") + " DoMessage " + std::to_string(run)).c_str(), results[0], results[1]);
        }
    }
    *stringCache = savedStringCache;
    *languageCache = savedLanguageCache;
    std::memcpy(ctx.image.At<uint8_t>(kTextGlobals), savedGlobals, sizeof(savedGlobals));
}

// ---- 0xc267fb AddApplicationServicesHere (cIGZApp slot 13): every constructor, both helpers and the options reader are
// recording fakes (at the original's RVA and at ours); the framework is a fake at g_Framework.

namespace {

struct ServiceKind {
    const char *name;
    uint32_t rva, size;
};
const ServiceKind kServices[] = {
    {"allocator", 0x1be59c, 0x20},   {"log", 0xc9c309, 0xfc},        {"properties", 0xc26045, 0x4c}, {"net", 0xc65932, 0x1cc},
    {"ssl", 0xc644aa, 0x60},         {"filesystem", 0xc75ccd, 0x1cc}, {"timer", 0xc6d0da, 0x50},      {"registry", 0xc6cb27, 0x28},
    {"clipboard", 0xc6d222, 0x38},   {"callbacks", 0xc68264, 0x24},   {"globals", 0xc2c4cf, 0x98},    {"world", 0xd0b6c0, 0xd4},
};
constexpr int kServiceCount = 12;
constexpr uint32_t kServiceFaces[] = {0, 4, 0x1c, 0x28};

struct Built {
    uint8_t *block;
    int kind;
};
Built gBuilt[32];
int gBuiltCount;
uint32_t gQiAnswers; // per kind: whether its QueryInterface answers
uint32_t gNewFails;  // per allocation index: whether operator new fails
int gNewCount;
void *gServiceVt[24];

// (No allocation here: operator new is a fake while the code under test runs.)
const char *ServiceName(const void *p)
{
    static char names[4][48];
    static int next;
    if (!p) return "null";
    for (int i = 0; i < gBuiltCount; ++i) {
        auto off = static_cast<const uint8_t *>(p) - gBuilt[i].block;
        if (off >= 0 && off < static_cast<intptr_t>(kServices[gBuilt[i].kind].size)) {
            char *name = names[next++ % 4];
            std::snprintf(name, 48, "%s+%d", kServices[gBuilt[i].kind].name, static_cast<int>(off));
            return name;
        }
    }
    return "?";
}

template <int K> void *__fastcall FakeServiceCtor(void *self, void *)
{
    Log("ctor %s", kServices[K].name);
    auto *block = static_cast<uint8_t *>(self);
    std::memset(block, 0x66, kServices[K].size);
    for (uint32_t off : kServiceFaces)
        if (off + 4 <= kServices[K].size) *reinterpret_cast<void **>(block + off) = gServiceVt;
    gBuilt[gBuiltCount++] = {block, K};
    return self;
}
uint32_t __fastcall FakeServiceQI(void *self, void *, uint32_t iid, void **out)
{
    Log("%s.QI(%x)", ServiceName(self), iid);
    for (int i = 0; i < gBuiltCount; ++i)
        if (static_cast<uint8_t *>(self) >= gBuilt[i].block && static_cast<uint8_t *>(self) < gBuilt[i].block + kServices[gBuilt[i].kind].size && ((gQiAnswers >> gBuilt[i].kind) & 1)) {
            *out = gBuilt[i].block + 4;
            return 0x7701;
        }
    return 0x7700;
}
uint32_t __fastcall FakeServiceAddRef(void *self, void *)
{
    Log("%s.AddRef", ServiceName(self));
    return 0;
}
uint32_t __fastcall FakeServiceRelease(void *self, void *)
{
    Log("%s.Release", ServiceName(self));
    return 0;
}
void __fastcall FakeServiceSlot20(void *self, void *, const char *a, const char *b, uint32_t c)
{
    Log("%s.slot20(%s, %s, %u)", ServiceName(self), a, b, c);
}
void *gFrameworkVt[24];
FakeObject gFramework{gFrameworkVt};
uint32_t __fastcall FakeAddSystemService(void *self, void *, void *service)
{
    Log("framework%s.AddSystemService(%s)", self == &gFramework ? "" : "?", ServiceName(service));
    return 0x8801;
}
uint8_t gSetupAnswer;
uint32_t __fastcall FakeSetupUserData(void *self, void *)
{
    Log("SetupUserDataDirectories@%d", AppOff(self));
    return 0x9900u | gSetupAnswer;
}
void __cdecl FakeAppendIni()
{
    Log("AppendIniCommandLine");
}
void __cdecl FakeSetupLogging()
{
    Log("SetupLogging");
}
void __fastcall FakeReadOptions(void *face, void *)
{
    Log("ReadApplicationOptions@%d", AppOff(face));
}
void *__cdecl FakeNew(size_t size)
{
    int index = gNewCount++;
    if ((gNewFails >> index) & 1) {
        Log("new(%x) fails", static_cast<unsigned>(size));
        return nullptr;
    }
    return HeapAlloc(GetProcessHeap(), 0, size);
}

} // namespace

DIFF_TEST(cTSAppFW_AddApplicationServicesHere, 0xc267fb)
{
    Env env(ctx);
    SetUpCommandLine(ctx);
    LearnOurVTables();
    gServiceVt[0] = reinterpret_cast<void *>(&FakeServiceQI);
    gServiceVt[1] = reinterpret_cast<void *>(&FakeServiceAddRef);
    gServiceVt[2] = reinterpret_cast<void *>(&FakeServiceRelease);
    gServiceVt[20] = reinterpret_cast<void *>(&FakeServiceSlot20);
    gFrameworkVt[3] = reinterpret_cast<void *>(&FakeAddSystemService);
    void *const fakes[kServiceCount] = {
        reinterpret_cast<void *>(&FakeServiceCtor<0>), reinterpret_cast<void *>(&FakeServiceCtor<1>), reinterpret_cast<void *>(&FakeServiceCtor<2>),
        reinterpret_cast<void *>(&FakeServiceCtor<3>), reinterpret_cast<void *>(&FakeServiceCtor<4>), reinterpret_cast<void *>(&FakeServiceCtor<5>),
        reinterpret_cast<void *>(&FakeServiceCtor<6>), reinterpret_cast<void *>(&FakeServiceCtor<7>), reinterpret_cast<void *>(&FakeServiceCtor<8>),
        reinterpret_cast<void *>(&FakeServiceCtor<9>), reinterpret_cast<void *>(&FakeServiceCtor<10>), reinterpret_cast<void *>(&FakeServiceCtor<11>),
    };
    void *const ours[kServiceCount] = {
        reinterpret_cast<void *>(&cGZAllocatorServiceAdapters::Construct), reinterpret_cast<void *>(&nGZLog::cLogServiceAdapters::Construct),   reinterpret_cast<void *>(&cGZPropertyManagerStringAdapters::Construct),
        reinterpret_cast<void *>(&cRZNetManagerAdapters::Construct), reinterpret_cast<void *>(&cRZNetSSLManagerAdapters::Construct), reinterpret_cast<void *>(&cGZFileSystemAdapters::Construct),
        reinterpret_cast<void *>(&cGZTimerServiceAdapters::Construct),        reinterpret_cast<void *>(&cGZRegistryAdapters::Construct),           reinterpret_cast<void *>(&cGZClipboardManagerAdapters::Construct),
        reinterpret_cast<void *>(&cRZCallbackSystemServiceAdapters::Construct), reinterpret_cast<void *>(&cTSGlobalsAdapters::Construct), reinterpret_cast<void *>(&nTSWorld::cWorldDBAdapters::Construct),
    };
    std::vector<std::unique_ptr<Redirect>> redirects;
    for (int k = 0; k < kServiceCount; ++k) {
        redirects.push_back(std::make_unique<Redirect>(ctx.image.At(kServices[k].rva), fakes[k]));
        redirects.push_back(std::make_unique<Redirect>(ours[k], fakes[k]));
    }
    redirects.push_back(std::make_unique<Redirect>(ctx.image.At(0xc2b078), reinterpret_cast<void *>(&FakeAppendIni)));
    redirects.push_back(std::make_unique<Redirect>(reinterpret_cast<void *>(&appservices3::AppendIniCommandLine), reinterpret_cast<void *>(&FakeAppendIni)));
    redirects.push_back(std::make_unique<Redirect>(ctx.image.At(0xc29d5a), reinterpret_cast<void *>(&FakeSetupLogging)));
    redirects.push_back(std::make_unique<Redirect>(reinterpret_cast<void *>(&appdebugger::SetupLogging), reinterpret_cast<void *>(&FakeSetupLogging)));
    redirects.push_back(std::make_unique<Redirect>(ctx.image.At(0xc274e3), reinterpret_cast<void *>(&FakeReadOptions)));
    redirects.push_back(std::make_unique<Redirect>(reinterpret_cast<void *>(&appinit::ReadApplicationOptions), reinterpret_cast<void *>(&FakeReadOptions)));
    void **framework = ctx.image.At<void *>(0x10c9378);
    void *savedFramework = *framework;
    void *ourNew = reinterpret_cast<void *>(static_cast<void *(__cdecl *)(size_t)>(&::operator new));
    for (Route route : {Route::OwnVTable, Route::Adapter}) {
        for (int run = 0; run < 200; ++run) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                std::mt19937 r(run * 613 + static_cast<int>(route));
                bool original = side == 0;
                LiveApp app(ctx, original, route, "The Sims 2");
                app.SetSlot(4, 15, reinterpret_cast<const void *>(&FakeSetupUserData));
                if (route == Route::Adapter) app.SetSlot(2, 13, reinterpret_cast<const void *>(&TrapNoArgs));
                gSetupAnswer = static_cast<uint8_t>(r() % 3 == 0 ? 0 : r());
                gQiAnswers = r();
                // A failing allocation (never the log service's or the file system's: the original calls their slots without a null
                // test, 0xc26891 and 0xc269cd).
                gNewFails = run % 4 == 3 ? (1u << (r() % 12)) & ~0x22u : 0;
                gNewCount = 0;
                gBuiltCount = 0;
                *framework = &gFramework;
                gLog.clear();
                uint32_t result;
                {
                    Redirect new0(ctx.image.At(0x824632), reinterpret_cast<void *>(&FakeNew));
                    Redirect new1(ourNew, reinterpret_cast<void *>(&FakeNew));
                    if (route == Route::Adapter)
                        result = original ? ctx.Original<uint32_t(__thiscall *)(void *)>(0xc267fb)(app.Face(2))
                                          : ts::cTSAppFWAdapters::AddApplicationServicesHere(reinterpret_cast<cIGZApp *>(app.Face(2)), nullptr);
                    else
                        result = CallSlot<uint32_t>(app.Face(2), 13);
                }
                *framework = savedFramework;
                std::string state = Outcome(result, app.Digest()) + "allocations " + std::to_string(gNewCount);
                for (int i = 0; i < gBuiltCount; ++i) HeapFree(GetProcessHeap(), 0, gBuilt[i].block);
                results[side] = state;
            }
            ExpectSame(ctx, (std::string(route == Route::Adapter ? "adapter" : "own vtable") + " AddApplicationServicesHere " + std::to_string(run)).c_str(), results[0], results[1]);
        }
    }
    while (!redirects.empty()) redirects.pop_back(); // in reverse (two of ours may share code)
}
