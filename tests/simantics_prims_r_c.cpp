// Differential test for the SimAntics primitive handler of opcode 0x12 (0x59cba0, PrimRObject::Prim12).
// Framework and notes: tests/simantics_prims_mid.h, tests/simantics_prims_r.h.
//
// The handler keeps a function-local hash set of object type ids (game memory at 0x10dbf54, built by the game's own
// instantiation): the test rebuilds it before every run (empty or filled), and sets the thread-safe-statics state so
// the original takes its fast path. Its callees that reach the rest of the game (0x5416a0, the log stream 0x1b4dd2 /
// 0x1b52d8 / 0x1b5941 / 0x57cb60) are redirected to logging stand-ins in the mapped image; the application forwarder
// 0x1d0c00 reaches a fake through its cache global.
#include "simantics_prims_r.h"
#include "code_patch.h"
#include "rz/RZFileStreams.h"

namespace {

constexpr uint32_t kSetRva = 0x10dbf54;
constexpr uint32_t kTypes[] = {0xcc7b08f8, 0x6c7b0905, 0xcc203477, 0x865a6812, 0x81e6bef9, 0xd20c0aee, 0x39ccf441};

uint8_t *gImageBase = nullptr;

void LogText(const char *text)
{
    gTrace.push_back(0x7e57u);
    for (const char *p = text; *p; ++p) gTrace.push_back(static_cast<uint8_t>(*p));
    gTrace.push_back(0xe0d0u);
}

const char *StdStringText(const void *cRZStringObject)
{
    return reinterpret_cast<const std::string *>(static_cast<const uint8_t *>(cRZStringObject) + 4)->c_str();
}

const char *const kNames[] = {"Sim", "chair_01", "a very long object name that goes on and on and on and on and on and on and on and on",
                              "", "tree%d", "x"};

// Slot 92: the object's name (a C string).
const char *__fastcall Name92(void *self, void *)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(92u << 8);
    return kNames[Mix(gSeed, Off(self), 92) % (sizeof(kNames) / sizeof(kNames[0]))];
}

// Slot 41: writes a position (three words) to the out pointer.
uint32_t __fastcall Pos41(void *self, void *, uint32_t *out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(41u << 8 | 1u);
    for (uint32_t i = 0; i < 3; ++i) out[i] = Mix(gSeed, 41, i);
    return 0;
}

// The frame's slot 37: fills a cRZString with the tree's name.
uint32_t __fastcall Frame37(void *self, void *, uint8_t *out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(37u << 8 | 1u);
    *reinterpret_cast<std::string *>(out + 4) = kNames[Mix(gSeed, 37, 0) % (sizeof(kNames) / sizeof(kNames[0]))];
    return 0;
}

// Slot 0 of the object: (iid, void **out), answers per interface id.
uint32_t __fastcall QiIid(void *self, void *, uint32_t iid, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(0u << 8 | 2u);
    gTrace.push_back(iid);
    gTrace.push_back(out ? 1u : 0u);
    const int key = iid == 0x5a69d3d5 ? 0x500 : 0x600;
    if (out) *out = reinterpret_cast<void *>(static_cast<uintptr_t>(Scripted(self, key)));
    return Scripted(self, key + 1);
}

// 0x5416a0 (__thiscall, 3 stack arguments): stand-in.
uint32_t __fastcall Stub5416a0(void *self, void *, void *object, const uint32_t *position, uint32_t zero)
{
    gTrace.push_back(0x5416a0u);
    gTrace.insert(gTrace.end(), {Norm(reinterpret_cast<uint32_t>(self)), Norm(reinterpret_cast<uint32_t>(object)), position[0], position[1], position[2], zero});
    return 0;
}

// 0x1b4dd2 (__thiscall, 5 stack arguments): the log stream's constructor; logs the path.
void *__fastcall Stub1b4dd2(void *self, void *, const void *path, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    gTrace.push_back(0x1b4dd2u);
    LogText(StdStringText(path));
    gTrace.insert(gTrace.end(), {a2, a3, a4, a5});
    return self;
}

void __fastcall Stub1b52d8(void *, void *)
{
    gTrace.push_back(0x1b52d8u);
}

uint32_t __fastcall Stub1b5941(void *, void *)
{
    gTrace.push_back(0x1b5941u);
    return 1;
}

void *__cdecl Stub57cb60(void *stream, const char *text)
{
    gTrace.push_back(0x57cb60u);
    LogText(text);
    return stream;
}

// Slot 4 of the application's common interface (five stack arguments; the second is a cRZString to fill).
uint32_t __fastcall Common4(void *self, void *, uint32_t a, uint8_t *text, uint32_t c, uint32_t d, uint32_t e)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(4u << 8 | 5u);
    gTrace.insert(gTrace.end(), {a, c, d, e});
    *reinterpret_cast<std::string *>(text + 4) = "C:\\logs\\";
    return 1;
}

struct SetState {
    bool built = false;
    void Reset(difftest::Context &ctx, bool filled)
    {
        void *set = ctx.image.At<void>(kSetRva);
        if (built) ctx.Original<void(__cdecl *)()>(0xd94670)();
        ctx.Original<void *(__thiscall *)(void *)>(0x57c410)(set);
        built = true;
        if (filled) {
            for (uint32_t type : kTypes) {
                uint8_t out[8];
                ctx.Original<void *(__thiscall *)(void *, void *, const uint32_t *)>(0x4209f0)(set, out, &type);
            }
        }
    }
};

void Build12(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 303);
    Base(w, seed, r);
    SetParam(w, r);
    ClearQueues();
    Fake &ctx = w.fk[0], &frame = w.fk[1], &table = w.fk[2], &holder = w.fk[3], &object = w.fk[4], &typeOwner = w.fk[5], &manager = w.fk[6],
         &treeObj = w.fk[7], &stackObj = w.fk[8], &clock = w.fk[9], &owner = w.fk[10], &removable = w.fk[11], &common = w.fk[12];

    const uint16_t mode = static_cast<uint16_t>(Pick(r, {0, 0, 1, 1, 2, 3, 0xffff, static_cast<uint32_t>(r())}));
    PutWord(w, 4, mode);
    const uint16_t self47 = static_cast<uint16_t>(Edge16(r));
    const uint16_t frame12 = (r() % 3 == 0) ? self47 : static_cast<uint16_t>(Edge16(r));
    const uint16_t id = mode == 0 ? self47 : mode == 1 ? frame12 : 0;
    Use(w.vtThis, 47, 0);
    RetValue(&w.obj, 47, self47 | (r() & 0xffff0000u), w);
    Use(frame.vt, 12, 0);
    RetValue(&frame, 12, frame12 | (r() & 0xffff0000u), w);
    (void)id;

    // The registry table: the object, then (log) the two frame objects.
    Use(ctx.vt, 26, 0);
    RetObject(&ctx, 26, &table, w);
    table.vt[29] = reinterpret_cast<void *>(&Q1<29>);
    QObj(29, (r() % 8 == 0) ? nullptr : &object, w);
    QObj(29, (r() % 3 == 0) ? nullptr : &treeObj, w);
    QObj(29, (r() % 3 == 0) ? nullptr : &stackObj, w);

    // The type id: one of the protected ones or another.
    object.vt[0] = reinterpret_cast<void *>(&QiIid);
    Use(object.vt, 61, 0);
    RetObject(&object, 61, &typeOwner, w);
    Use(typeOwner.vt, 28, 0);
    RetValue(&typeOwner, 28, (r() % 3 == 0) ? kTypes[r() % 7] : (r() % 7 == 0 ? 0u : static_cast<uint32_t>(r())), w);
    object.vt[41] = reinterpret_cast<void *>(&Pos41);
    Use(object.vt, 116, 0);
    RetValue(&object, 116, (r() % 3 == 0) ? Pick(r, {1, 0x100, 0xff, static_cast<uint32_t>(r())}) : 0u, w);
    object.vt[92] = reinterpret_cast<void *>(&Name92);
    Use(object.vt, 47, 0);
    RetValue(&object, 47, Wide16(r), w);

    // The manager, holder and interfaces.
    w.vtThis[63] = nullptr;
    Use(w.vtThis, 63, 0);
    RetObject(&w.obj, 63, &manager, w);
    Use(manager.vt, 18, 2);
    w.vtThis[92] = reinterpret_cast<void *>(&Name92);
    Use(ctx.vt, 6, 0);
    RetObject(&ctx, 6, &holder, w);
    Use(holder.vt, 29, 1);
    RetObject(&object, 0x500, (r() % 4 == 0) ? nullptr : &removable, w);
    RetValue(&object, 0x501, (r() % 4 == 0) ? 0u : Pick(r, {1, 0x100, 0xff, static_cast<uint32_t>(r())}), w);
    Use(removable.vt, 2, 0);
    Use(removable.vt, 1, 0);
    // The 0x99c7f5a interface: an object whose +4 subobject has slot 33 (deleting oneself needs it present).
    owner.pad = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(w.vtSub[0]));
    w.vtSub[0][33] = gThunk[33][0];
    RetValue(reinterpret_cast<uint8_t *>(&owner) + 4, 33, (r() % 3 == 0) ? 0u : Pick(r, {1, 0x100, 0xff, static_cast<uint32_t>(r())}), w);
    const bool wantOwner = static_cast<int16_t>(id) == static_cast<int16_t>(self47) || r() % 3 == 0;
    RetObject(&object, 0x600, wantOwner ? &owner : nullptr, w);
    RetValue(&object, 0x601, static_cast<uint32_t>(r()), w);
    Use(owner.vt, 2, 0);

    // The developer log.
    Use(ctx.vt, 31, 0);
    RetValue(&ctx, 31, (r() % 3 == 0) ? Pick(r, {1, 0x100, 0xff}) : 0u, w);
    Use(frame.vt, 3, 0);
    RetValue(&frame, 3, Wide16(r), w);
    frame.vt[37] = reinterpret_cast<void *>(&Frame37);
    Use(frame.vt, 9, 0);
    RetValue(&frame, 9, Wide16(r), w);
    Use(frame.vt, 14, 0);
    RetValue(&frame, 14, Wide16(r), w);
    for (Fake *f : {&treeObj, &stackObj}) {
        f->vt[92] = reinterpret_cast<void *>(&Name92);
        Use(f->vt, 47, 0);
        RetValue(f, 47, Wide16(r), w);
    }
    Use(clock.vt, 18, 0);
    RetValue(&clock, 18, static_cast<uint32_t>(r()), w);
    {
        const size_t vtOffset = reinterpret_cast<uint8_t *>(w.vtThis) - reinterpret_cast<uint8_t *>(&w.obj);
        const size_t index = (0x2c8 - vtOffset) / 4;
        if (index == 47 || index == 63 || index == 92 || index == 0) w.canary = 0;
        *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(&w.obj) + 0x2c8) = &clock;
    }
    common.vt[4] = reinterpret_cast<void *>(&Common4);

    // Whether the type set starts filled (bytes[0x3c]).
    w.bytes[0x3c] = (r() % 3 != 0) ? 1 : 0;
}

} // namespace

DIFF_TEST(PrimR_Prim12, 0x59cba0)
{
    gImageBase = ctx.image.At<uint8_t>(0);
    CodePatch p1(ctx, 0x5416a0, reinterpret_cast<void *>(&Stub5416a0));
    // The log stream is ours (rz/RZFileStreams.h): the stand-ins cover the image's and ours alike.
    shape::StandIn p2(0x1b4dd2, static_cast<cRZOFStreamFromString>(&cRZOFStream::Construct), &Stub1b4dd2);
    shape::StandIn p3(0x1b52d8, &cRZOFStream::Destruct, &Stub1b52d8);
    shape::StandIn p4(0x1b5941, &cRZOFStream::GetError, &Stub1b5941);
    CodePatch p5(ctx, 0x57cb60, reinterpret_cast<void *>(&Stub57cb60));

    // The thread-safe-statics state: initialised for the original (the game's epoch above the guard); ours reads its
    // own CRT's epoch and takes the same path.
    uint8_t *setBytes = ctx.image.At<uint8_t>(kSetRva);
    uint8_t savedSet[0x30];
    std::memcpy(savedSet, setBytes, sizeof(savedSet));
    int *guard = ctx.image.At<int>(0x10dbf74);
    const int savedGuard = *guard;
    const uint32_t tlsIndex = *ctx.image.At<uint32_t>(0x10df814);
    int *tls = reinterpret_cast<int **>(__readfsdword(0x2c))[tlsIndex];
    const int savedEpoch = tls[1];
    *guard = 5;
    tls[1] = 0x7fffffff;
    void **cache = ctx.image.At<void *>(0x10cc040);
    void *const savedCache = *cache;

    SetState set;
    gBeforeRun = [&](World &w) {
        ResetQueues();
        set.Reset(ctx, w.bytes[0x3c] != 0);
        *cache = &w.fk[12];
    };
    Stats st = RunHandler(ctx, "Prim12", 0x59cba0, 40000, Build12,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) {
                              return reinterpret_cast<PrimRObject *>(o)->Prim12(f, p);
                          });
    gBeforeRun = nullptr;
    if (set.built) ctx.Original<void(__cdecl *)()>(0xd94670)();
    std::memcpy(setBytes, savedSet, sizeof(savedSet));
    *guard = savedGuard;
    tls[1] = savedEpoch;
    *cache = savedCache;
    if (st.throws < 500 || st.byRet[0] < 2000 || st.byRet[1] < 2000 || st.byRet[2] < 500)
        ctx.Fail("coverage: %u throws, returns 0/1/2: %u/%u/%u", st.throws, st.byRet[0], st.byRet[1], st.byRet[2]);
}
