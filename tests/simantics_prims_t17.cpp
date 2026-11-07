// Differential test for the SimAntics primitive handler of src/simantics/PrimT.h: opcode 0x17, PlaySound (0x59f370).
// Framework: tests/simantics_prims_mid.h, tests/simantics_prims_r.h (queues, code patches), tests/simantics_prims_t.h.
//
// Everything is a recording fake except the director singleton (set up as in the Prim10 test; its framework field is
// a fake) and the pure math the handler calls (the integer-to-vector conversion 0x7dcc00, sine and cosine 0x5a9de0 /
// 0x5a9a90 and the quaternion rotation 0x1a6c40: original code on both sides). The sound system probe 0x9c5b7e and the
// skeleton node lookup 0xae7490 are logging stand-ins. The cached message server is a fake.
//
// Fakes: fk0 context, fk1 frame (also this's owner), fk2 object table, fk3 person-like owner object, fk4 sound
// lookup object, fk5 the message, fk6 the message's relay, fk7 the object the sound belongs to, fk8 its interface,
// fk9 the frame's kind object, fk10 skeleton, fk11 its virtual base, fk12 framework, fk13 skeleton node;
// sub0 message server.
#include "code_patch.h"
#include "simantics_prims_t.h"

#include "rz/cRZString.h"
#include "world/WorldTileMath.h"

#include <cmath>
#include <cstdlib>

namespace {

int32_t gVbTable[3];
uint32_t gFwCall = 0;

const char *const kNames[] = {"", "plain", "VOX_Hello", "vox", "voxel thing", "MiXeD CaSe", "x"};
constexpr int kNameCount = sizeof(kNames) / sizeof(kNames[0]);

uint32_t FloatBitsOf(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

float ScriptFloat(uint32_t h)
{
    switch (h % 12) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return std::numeric_limits<float>::quiet_NaN();
    case 3: return -1.5f;
    case 4: return 3.25f;
    case 5: return 1.0f;
    default: return static_cast<float>(static_cast<int32_t>(Mix(h, 1, 2) % 2001) - 1000) / 8.0f;
    }
}

// A byte answer (al) with noise in the rest of eax.
uint32_t NoiseAl(std::mt19937 &r, bool nonzero)
{
    const uint32_t high = r() & ~0xffu;
    return nonzero ? (high | (1 + r() % 255)) : high;
}

// 0x9c5b7e (__cdecl): the sound system probe; the script's answer.
void *__cdecl StubSoundProbe()
{
    Tick();
    gTrace.push_back(0x9c5bu);
    return reinterpret_cast<void *>(static_cast<uintptr_t>(Scripted(&gW->fk[0], 903)));
}

// 0x7dcc00 (__cdecl): a cell position and level to a world position (it reads the terrain height).
float* __cdecl StubCellToWorld(float *out, const int32_t *cell, int32_t level)
{
    Tick();
    gTrace.push_back(0x7dccu);
    gTrace.insert(gTrace.end(), {static_cast<uint32_t>(cell[0]), static_cast<uint32_t>(cell[1]), static_cast<uint32_t>(level)});
    for (int i = 0; i < 3; ++i) out[i] = ScriptFloat(Scripted(&gW->fk[0], 905 + i));
    return out;
}

// 0xae7490 (__cdecl): finds the node named by the text in the skeleton.
void *__cdecl StubFindNode(const char *name, void *skeleton, int32_t flags)
{
    Tick();
    gTrace.push_back(0xae74u);
    for (const char *p = name; *p; ++p) gTrace.push_back(static_cast<uint8_t>(*p));
    gTrace.push_back(Norm(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(skeleton))));
    gTrace.push_back(static_cast<uint32_t>(flags));
    return reinterpret_cast<void *>(static_cast<uintptr_t>(Scripted(&gW->fk[0], 904)));
}

// The framework's slot 3: (clsid, iid, object**) -> al; the first call hands out the sound lookup object, the second
// the message.
uint32_t __fastcall Fw3(void *self, void *, uint32_t clsid, uint32_t iid, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(3u << 8 | 3u);
    gTrace.insert(gTrace.end(), {clsid, iid, out ? 1u : 0u});
    const int call = static_cast<int>(gFwCall++);
    if (out) *out = reinterpret_cast<void *>(static_cast<uintptr_t>(Scripted(self, 1000 + call)));
    return Scripted(self, 2000 + call);
}

// The lookup object's slot 5: (string*): the sound's name.
uint32_t __fastcall Look5(void *self, void *, void *text)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(5u << 8 | 1u);
    static_cast<cRZString *>(text)->mString = kNames[Scripted(self, 5) % kNameCount];
    return 0;
}

void LogText(const void *p)
{
    const std::string &s = static_cast<const cRZString *>(p)->mString;
    gTrace.push_back(static_cast<uint32_t>(s.size()));
    for (char c : s) gTrace.push_back(static_cast<uint8_t>(c));
}

// The message's slot 18: (id, float3*); slot 21: (id, string*).
uint32_t __fastcall Msg18(void *self, void *, uint32_t id, const float *v)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(18u << 8 | 2u);
    gTrace.push_back(id);
    for (int i = 0; i < 3; ++i) gTrace.push_back(FloatBitsOf(v[i]));
    return 0;
}

uint32_t __fastcall Msg21(void *self, void *, uint32_t id, const void *text)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(21u << 8 | 2u);
    gTrace.push_back(id);
    LogText(text);
    return 0;
}

// Slot 12 of the object: (iid, iid, holder*) -> al; writes the skeleton.
uint32_t __fastcall ObjX12(void *self, void *, uint32_t a, uint32_t b, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(12u << 8 | 3u);
    gTrace.insert(gTrace.end(), {a, b, out ? 1u : 0u});
    if (out) *out = reinterpret_cast<void *>(static_cast<uintptr_t>(Scripted(self, 1012)));
    return Scripted(self, 12);
}

// A QueryInterface whose object (slot 0's script) and answer (al, script 1099) are separate.
uint32_t __fastcall QiX(void *self, void *, uint32_t iid, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(0u << 8 | 2u);
    gTrace.insert(gTrace.end(), {iid, out ? 1u : 0u});
    if (out) *out = reinterpret_cast<void *>(static_cast<uintptr_t>(Scripted(self, 0)));
    return Scripted(self, 1099);
}

// A placement writer (slot 54): the position and the rotation from the script.
uint32_t __fastcall Place54(void *self, void *, float *out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(54u << 8 | 1u);
    for (int i = 0; i < 7; ++i) out[i] = ScriptFloat(Scripted(self, 1054 + i));
    return 0;
}

uint32_t __fastcall MsgAddRefX(void *, void *)
{
    return 0;
}

void Build17(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 17);
    Base(w, seed, r);
    SetParam(w, r);
    ClearQueues();
    Fake &ctx = w.fk[0], &frame = w.fk[1], &table = w.fk[2], &ownerObj = w.fk[3], &lookup = w.fk[4], &message = w.fk[5], &relay = w.fk[6],
         &objX = w.fk[7], &iface = w.fk[8], &kindObj = w.fk[9], &skeleton = w.fk[10], &base = w.fk[11], &framework = w.fk[12], &node = w.fk[13];
    gFwCall = 0;

    // The parameter: sound number (word +4), flags byte +8 (0xff, or bits 1 and 6), operand (+0xa, +0xb).
    switch (r() % 9) {
    case 0: PutWord(w, 4, static_cast<uint16_t>(Edge16(r))); break;
    case 1: PutWord(w, 4, static_cast<uint16_t>(10000 + r() % 20000)); break;
    case 2: PutWord(w, 4, static_cast<uint16_t>(20000 + r() % 12768)); break;
    case 3: PutWord(w, 4, static_cast<uint16_t>(-static_cast<int>(r() % 32768))); break;
    case 4: PutWord(w, 4, 0x8000); break;
    case 5: PutWord(w, 4, static_cast<uint16_t>(9999 + r() % 3)); break;
    case 6: PutWord(w, 4, static_cast<uint16_t>(19999 + r() % 3)); break;
    default: PutWord(w, 4, static_cast<uint16_t>(r() % 300)); break;
    }
    w.param[8] = (r() % 4 == 0) ? 0xff : static_cast<uint8_t>(r());

    w.vtSec[7] = reinterpret_cast<void *>(&Read7);
    gRead.hasTarget = false;
    Use(w.vtThis, 47, 0);
    RetValue(&w.obj, 47, (r() % 3 == 0) ? 0u : Wide16(r), w);
    Use(ctx.vt, 26, 0);
    RetObject(&ctx, 26, &table, w);
    RetValue(&ctx, 903, (r() % 5 == 0) ? 0u : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&w.canary)) | 1u, w);
    RetObject(&ctx, 904, (r() % 3 == 0) ? nullptr : &node, w);
    for (int i = 0; i < 3; ++i) RetValue(&ctx, 905 + i, static_cast<uint32_t>(r()), w);

    // The frame, also this's owner: slots 12 (a number), 20 (the kind object), 39 (verbose).
    Use(frame.vt, 12, 0);
    RetValue(&frame, 12, Wide16(r), w);
    Use(frame.vt, 20, 0);
    RetObject(&frame, 20, &kindObj, w);
    Use(frame.vt, 39, 0);
    RetValue(&frame, 39, (r() % 2) ? NoiseAl(r, true) : NoiseAl(r, false), w);
    for (int s : {14, 15, 16}) {
        Use(kindObj.vt, s, 0);
        RetValue(&kindObj, s, static_cast<uint32_t>(r()), w);
    }

    table.vt[29] = reinterpret_cast<void *>(&Q1<29>);
    QObj(29, (r() % 4 == 0) ? nullptr : &ownerObj, w);
    QObj(29, (r() % 5 == 0) ? nullptr : &objX, w);
    Use(ownerObj.vt, 92, 0);
    RetValue(&ownerObj, 92, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(kNames[2])), w);

    // The framework: slot 3 hands out the lookup object, then the message.
    framework.vt[3] = reinterpret_cast<void *>(&Fw3);
    // (a "yes" comes with the lookup object: the original uses it without a check)
    const bool lookupYes = r() % 8 != 0;
    RetObject(&framework, 1000, (lookupYes || r() % 2) ? &lookup : nullptr, w);
    RetValue(&framework, 2000, NoiseAl(r, lookupYes), w);
    RetObject(&framework, 1001, (r() % 10 == 0) ? nullptr : &message, w);
    RetValue(&framework, 2001, NoiseAl(r, true), w);

    Use(lookup.vt, 3, 2);
    RetValue(&lookup, 3, (r() % 5 == 0) ? NoiseAl(r, false) : NoiseAl(r, true), w);
    lookup.vt[5] = reinterpret_cast<void *>(&Look5);
    RetValue(&lookup, 5, static_cast<uint32_t>(r()), w);
    Use(lookup.vt, 2, 0);

    // The message.
    Use(message.vt, 6, 0);
    RetObject(&message, 6, &relay, w);
    Use(message.vt, 12, 2);
    Use(message.vt, 15, 2);
    Use(message.vt, 5, 1);
    Use(message.vt, 2, 0);
    message.vt[18] = reinterpret_cast<void *>(&Msg18);
    message.vt[21] = reinterpret_cast<void *>(&Msg21);
    message.vt[0] = reinterpret_cast<void *>(&Qi0);
    RetObject(&message, 0, &w.sub[1], w); // the message interface the query hands out
    Use(w.vtSub[1], 5, 1);
    Use(w.vtSub[1], 2, 0);
    Use(relay.vt, 33, 1);

    // The object the sound belongs to.
    Use(objX.vt, 1, 0);
    Use(objX.vt, 2, 0);
    Use(objX.vt, 13, 1);
    RetValue(&objX, 13, (r() % 3 == 0) ? static_cast<uint32_t>(r() & 0xff) : static_cast<uint32_t>(r()), w);
    Use(objX.vt, 131, 0);
    RetValue(&objX, 131, Wide16(r), w);
    Use(objX.vt, 36, 0);
    RetValue(&objX, 36, Wide16(r), w);
    Use(objX.vt, 33, 0);
    RetValue(&objX, 33, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(w.bytes)), w);
    Use(objX.vt, 43, 0);
    RetValue(&objX, 43, static_cast<uint32_t>(Pick(r, {0, 1, 2, 3, 4, 8, 0xfffffffeu, 100, static_cast<uint32_t>(r())})), w);
    Use(objX.vt, 188, 0);
    RetValue(&objX, 188, (r() % 4 == 0) ? NoiseAl(r, false) : NoiseAl(r, true), w);
    objX.vt[0] = reinterpret_cast<void *>(&QiX);
    {
        // The answer (al) says whether the interface came; a "yes" always comes with the object (the original
        // uses it without a check).
        const bool has = r() % 3 != 0;
        RetObject(&objX, 0, has ? &iface : nullptr, w);
        RetValue(&objX, 1099, NoiseAl(r, has), w);
    }
    objX.vt[12] = reinterpret_cast<void *>(&ObjX12);
    RetObject(&objX, 1012, &skeleton, w); // the original sends garbage when this fails: always answer yes
    RetValue(&objX, 12, NoiseAl(r, true), w);

    Use(iface.vt, 10, 2);
    RetValue(&iface, 10, (r() % 3 == 0) ? 0u : Wide16(r), w);
    Use(iface.vt, 2, 0);

    // The skeleton: slot 54 fills the placement; released through its virtual base.
    skeleton.vt[54] = reinterpret_cast<void *>(&Place54);
    for (int i = 0; i < 7; ++i) RetValue(&skeleton, 1054 + i, static_cast<uint32_t>(r()), w);
    skeleton.pad = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gVbTable));
    Use(base.vt, 2, 0);
    node.vt[54] = reinterpret_cast<void *>(&Place54);
    for (int i = 0; i < 7; ++i) RetValue(&node, 1054 + i, static_cast<uint32_t>(r()), w);

    // The message server.
    Use(w.vtSub[0], 3, 1);
}

} // namespace

DIFF_TEST(PrimT_Prim17, 0x59f370)
{
    CodePatch p1(ctx, 0x9c5b7e, reinterpret_cast<void *>(&StubSoundProbe));
    CodePatch p2(ctx, 0xae7490, reinterpret_cast<void *>(&StubFindNode));
    shape::StandIn p3(0x7dcc00, &worldmath::FixedToPosition, &StubCellToWorld);

    // The director singleton in the image (as in the Prim10 test): the framework field answers with a fake.
    uint8_t *director = ctx.image.At<uint8_t>(0x10c9390);
    const uint32_t tlsIndex = *ctx.image.At<uint32_t>(0x10df814);
    int *tls = reinterpret_cast<int **>(__readfsdword(0x2c))[tlsIndex];
    const int savedEpoch = tls[1];
    const int savedGuard = *ctx.image.At<int>(0x10c93f4);
    uint8_t savedBytes[0x64];
    std::memcpy(savedBytes, director, sizeof(savedBytes));
    static void *fakeVTable[32];
    static struct {
        void *vtable;
        uint32_t pad;
    } fakeService;
    fakeVTable[23] = reinterpret_cast<void *>(+[](void *, void *) -> void * { return nullptr; });
    fakeService.vtable = fakeVTable;
    void *const savedService = *ctx.image.At<void *>(0x10c9378);
    *ctx.image.At<void *>(0x10c9378) = &fakeService;
    std::memset(director, 0xCD, 0x64);
    ctx.Original<void *(__thiscall *)(void *)>(0x144897)(director);
    *ctx.image.At<int>(0x10c93f4) = 5;
    tls[1] = 0x7fffffff;
    void **server = ctx.image.At<void *>(0x10f519c);
    void *const savedServer = *server;

    // The two float triples the handler reads from game globals.
    float *globals = ctx.image.At<float>(0x10dbdcc);
    float savedGlobals[6];
    std::memcpy(savedGlobals, globals, sizeof(savedGlobals));
    std::mt19937 gr(17);
    for (float *g = globals; g < globals + 6; ++g) *g = ScriptFloat(gr());
    // 0x10dbdd8 follows 0x10dbdcc by 12 bytes: the two triples are consecutive.

    gBeforeRun = [&](World &w) {
        ResetQueues();
        *reinterpret_cast<void **>(director + 0x30) = &w.fk[12];
        *server = &w.sub[0];
        gFwCall = 0;
        gVbTable[0] = 0;
        gVbTable[1] = static_cast<int32_t>(reinterpret_cast<uint8_t *>(&w.fk[11]) - reinterpret_cast<uint8_t *>(&w.fk[10]) - 4);
    };
    Stats st = RunHandler(ctx, "Prim17", 0x59f370, 80000, Build17,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) { return SelfT(o)->Prim17(f, p); });
    gBeforeRun = nullptr;
    std::memcpy(globals, savedGlobals, sizeof(savedGlobals));
    tls[1] = savedEpoch;
    *ctx.image.At<int>(0x10c93f4) = savedGuard;
    std::memcpy(director, savedBytes, sizeof(savedBytes));
    *ctx.image.At<void *>(0x10c9378) = savedService;
    *server = savedServer;
    if (st.throws < 500 || st.byRet[1] < 20000) ctx.Fail("coverage: %u throws, returns 1: %u", st.throws, st.byRet[1]);
}
