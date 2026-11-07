// Differential test for the SimAntics primitive handler of opcode 0x1c (0x53de40, PrimRObject::Prim1C).
// Framework and notes: tests/simantics_prims_mid.h, tests/simantics_prims_r.h.
//
// The handler loads a string through the string-table loader (cTSString, 0x1d0df0). The loader runs for real on both
// sides; the resource manager it asks is a fake that answers with a scripted text, so both sides see the same one.
#include "simantics_prims_r.h"
#include "ts_string_fake.h"

#include <windows.h>

namespace {

std::string gText;

uint8_t gRegistry[0x1a40]; // the object registry at this + 0x1c4: a vector of object pointers at +0x1a24 / +0x1a28

void LogWords(const uint16_t *p)
{
    const uintptr_t base = reinterpret_cast<uintptr_t>(gW);
    if (!p) {
        gTrace.push_back(0xdead0001u);
    } else if (reinterpret_cast<uintptr_t>(p) >= base && reinterpret_cast<uintptr_t>(p) < base + sizeof(World)) {
        gTrace.push_back(Norm(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p))));
    } else {
        gTrace.push_back(0x1000u);
        for (int i = 0; i < 4; ++i) gTrace.push_back(p[i]);
    }
}

// The resource manager the string-table loader (cTSString, 0x1d0df0) asks, as a fake (tests/ts_string_fake.h): it logs the key the
// loader built and answers with a scripted text, so both sides run the real loader against the same stand-in.
struct LoaderRedirect {
    tsfake::ManagerFixture manager;
    explicit LoaderRedirect(difftest::Context &ctx) : manager(ctx)
    {
        manager.InstallManagerOnly();
        tsfake::gAnswerer = [](const tsfake::Ask &ask, std::string &text) {
            gTrace.push_back(0x10adu);
            gTrace.insert(gTrace.end(), {ask.instance >> 16, ask.instance & 0xffff, Norm(ask.group), static_cast<uint32_t>(ask.loadFlags) & 0xff, ask.type});
            text = gText;
            return true;
        };
    }
};

// Slot 31 of the tree object: (name string, flag, flag) -> the tree id; logs the string.
uint32_t __fastcall Tree31(void *self, void *, const uint8_t *name, uint32_t b2, uint32_t b3)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(31u << 8 | 3u);
    const std::string &s = *reinterpret_cast<const std::string *>(name + 4);
    gTrace.push_back(static_cast<uint32_t>(s.size()));
    for (size_t i = 0; i < s.size(); ++i) gTrace.push_back(static_cast<uint8_t>(s[i]));
    // The two flags are pushed as dwords of which the original sets only the low byte (`mov byte ptr [ebp-0x24], al`):
    // the upper 24 bits are whatever the stack held (a pointer, leftovers of earlier calls), so only the low byte
    // is compared. Ours passes clean 0/1 values (the callee takes bools).
    gTrace.push_back(b2 & 0xff);
    gTrace.push_back(b3 & 0xff);
    return Scripted(self, 31);
}

uint32_t __fastcall Frame30(void *self, void *, uint32_t i)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(30u << 8 | 1u);
    gTrace.push_back(i);
    return Mix(gSeed, 30, i);
}

uint32_t __fastcall This19(void *self, void *, uint32_t arg)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(19u << 8 | 1u);
    gTrace.push_back(arg);
    return Mix(gSeed, 19, arg);
}

// Slots 247 (6 arguments) and 248 (5 arguments) of the caller: the argument words are logged by value.
uint32_t __fastcall Run247(void *self, void *, uint32_t a1, const uint16_t *words, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(247u << 8 | 6u);
    gTrace.push_back(Norm(a1));
    LogWords(words);
    gTrace.insert(gTrace.end(), {Norm(a3), a4, a5, a6});
    return Scripted(self, 247);
}

uint32_t __fastcall Run248(void *self, void *, uint32_t a1, const uint16_t *words, uint32_t a3, uint32_t a4, uint32_t a5)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(248u << 8 | 5u);
    gTrace.push_back(Norm(a1));
    LogWords(words);
    gTrace.insert(gTrace.end(), {a3, a4, a5});
    return Scripted(self, 248);
}

// Slot 15 of the interface's subobject (at the interface + 4): eight stack arguments.
uint32_t __fastcall Start15(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, const uint16_t *words,
                            uint32_t a7, uint32_t a8)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(15u << 8 | 8u);
    gTrace.insert(gTrace.end(), {Norm(a1), a2, a3, a4, a5});
    LogWords(words);
    gTrace.insert(gTrace.end(), {a7, a8});
    return Scripted(self, 15);
}

constexpr int kRegistrySlotCheck[] = {0, 19, 47, 247, 248, 312, 313};

void Build1C(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 202);
    Base(w, seed, r);
    SetParam(w, r);
    ClearQueues();
    Fake &ctx = w.fk[0], &frame = w.fk[1], &holder = w.fk[2], &keyOwner = w.fk[3], &src = w.fk[4], &q = w.fk[5], &elem = w.fk[6],
         &Y = w.fk[7], &T = w.fk[8], &entry = w.fk[9], &iface = w.fk[10], &entry2 = w.fk[11];
    Fake &sec = *reinterpret_cast<Fake *>(reinterpret_cast<uint8_t *>(&w.obj) + 4);

    // The text.
    static const char *const kTexts[] = {"",
                                         "",
                                         "x",
                                         "tree name",
                                         "tree name  ",
                                         "a \t \t",
                                         "   ",
                                         "\t",
                                         "0123456789abcdef",
                                         "0123456789abcdef  ",
                                         "a long long tree name with spaces   \t",
                                         "inner  space\t kept"};
    gText = kTexts[r() % (sizeof(kTexts) / sizeof(kTexts[0]))];

    // The flag word: bit 0 direct id, 1 source selector, 2/3 passed on, 4/5 argument sources, 6 high id bit.
    uint16_t flags = static_cast<uint16_t>(r());
    if (r() % 3 == 0) flags &= 0x7f;
    PutWord(w, 6, flags);
    w.param[9] = static_cast<uint8_t>(Pick(r, {0, 0, 1, 1, 2, 2, 3, 3, 3, 4, 255, static_cast<uint32_t>(r())}));

    // Early exit when the frame's slot 18 says 1.
    Use(ctx.vt, 24, 0);
    RetObject(&ctx, 24, &holder, w);
    Use(frame.vt, 18, 0);
    RetValue(&frame, 18, (r() % 6 == 0) ? 1u : Pick(r, {0, 2, 3, 0xffffffffu, static_cast<uint32_t>(r())}), w);
    w.vtSec[29] = nullptr;
    Use(w.vtSec, 29, 0);
    RetValue(&sec, 29, Pick(r, {0, 1, 0x100, 0xff00, 0xffffffffu, static_cast<uint32_t>(r())}), w);

    // The key and the source.
    Use(frame.vt, 20, 0);
    RetObject(&frame, 20, &keyOwner, w);
    Use(keyOwner.vt, 38, 0);
    RetValue(&keyOwner, 38, static_cast<uint32_t>(r()), w);
    holder.vt[14] = reinterpret_cast<void *>(&Q1<14>);
    QObj(14, ((flags & 1) && r() % 2) ? nullptr : &src, w);
    Use(src.vt, 70, 0);
    Use(src.vt, 18, 0);
    RetObject(&src, 70, &q, w);
    RetObject(&src, 18, &q, w);
    Use(q.vt, 11, 0);
    RetValue(&q, 11, static_cast<uint32_t>(r()), w);

    // Mode 3: the tree-set entries.
    QObj(14, (r() % 3 == 0) ? nullptr : &entry, w);
    QObj(14, (r() % 4 == 0) ? nullptr : &entry2, w);
    for (Fake *e : {&entry, &entry2}) {
        Use(e->vt, 18, 0);
        Use(e->vt, 62, 1);
        RetObject(e, 18, (r() % 8 == 0) ? nullptr : &T, w);
    }
    w.vtThis[19] = reinterpret_cast<void *>(&This19);
    Use(w.vtThis, 312, 0);
    Use(w.vtThis, 313, 1);

    // The registry: element numbers from the frame's slot 12.
    const uint32_t count = r() % 4;
    for (uint32_t i = 0; i < count; ++i) {
        void *p = (r() % 5 == 0) ? nullptr : static_cast<void *>(&elem);
        std::memcpy(w.bytes + i * 4, &p, 4);
    }
    std::memcpy(w.bytes + 0x3c, &count, 4);
    Use(frame.vt, 12, 0);
    RetValue(&frame, 12, Pick(r, {0, 1, 1, 2, 3, count, count + 1, 0xffff, 0x8000, static_cast<uint32_t>(r())}), w);
    Use(elem.vt, 280, 0);
    RetObject(&elem, 280, (r() % 8 == 0) ? nullptr : &Y, w);
    Use(Y.vt, 89, 0);
    RetObject(&Y, 89, &T, w);
    {
        // this + 0x1c4 lies inside the fake's vtable array: it must not hit a slot the handler uses.
        const size_t vtOffset = reinterpret_cast<uint8_t *>(w.vtThis) - reinterpret_cast<uint8_t *>(&w.obj);
        const size_t index = (0x1c4 - vtOffset) / 4;
        for (int s : kRegistrySlotCheck)
            if (index == static_cast<size_t>(s)) w.canary = 0; // would show as a state mismatch on every seed
        *reinterpret_cast<uint8_t **>(reinterpret_cast<uint8_t *>(&w.obj) + 0x1c4) = gRegistry;
    }

    // The tree and its id.
    T.vt[31] = reinterpret_cast<void *>(&Tree31);
    RetValue(&T, 31, (r() % 6 == 0) ? (static_cast<uint32_t>(r()) & 0xffff0000u) : Wide16(r), w);

    // Arguments.
    Use(frame.vt, 26, 0);
    RetValue(&frame, 26, Pick(r, {0, 1, 2, 3, 4, 5, 255, 0x100, static_cast<uint32_t>(r())}), w);
    frame.vt[30] = reinterpret_cast<void *>(&Frame30);
    Use(frame.vt, 19, 1);
    w.vtSec[7] = reinterpret_cast<void *>(&ReadQ);
    gReadQ = {Wide16(r), Wide16(r), Wide16(r)};

    // The start paths.
    iface.pad = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(w.vtSub[0]));
    w.vtSub[0][15] = reinterpret_cast<void *>(&Start15);
    RetValue(reinterpret_cast<uint8_t *>(&iface) + 4, 15, Pick(r, {0, 1, 0x100, 0xff00, 0xffffffffu, static_cast<uint32_t>(r())}), w);
    Use(iface.vt, 2, 0);
    w.vtThis[0] = reinterpret_cast<void *>(&Qi0);
    Y.vt[0] = reinterpret_cast<void *>(&Qi0);
    RetObject(&w.obj, 0, &iface, w);
    RetObject(&Y, 0, &iface, w);
    Use(w.vtThis, 47, 0);
    Use(Y.vt, 47, 0);
    RetValue(&w.obj, 47, static_cast<uint32_t>(r()), w);
    RetValue(&Y, 47, static_cast<uint32_t>(r()), w);
    w.vtThis[247] = reinterpret_cast<void *>(&Run247);
    w.vtThis[248] = reinterpret_cast<void *>(&Run248);
    RetValue(&w.obj, 247, Pick(r, {0, 1, 0x100, 0xff00, 0xffffffffu, static_cast<uint32_t>(r())}), w);
    RetValue(&w.obj, 248, Pick(r, {0, 1, 0x100, 0xff00, 0xffffffffu, static_cast<uint32_t>(r())}), w);
    // Y can be the caller too (mode 3 through the registry).
    Y.vt[247] = reinterpret_cast<void *>(&Run247);
    Y.vt[248] = reinterpret_cast<void *>(&Run248);
}

} // namespace

DIFF_TEST(PrimR_Prim1C, 0x53de40)
{
    LoaderRedirect redirect(ctx);
    gBeforeRun = [](World &w) {
        ResetQueues();
        uint32_t count;
        std::memcpy(&count, w.bytes + 0x3c, 4);
        *reinterpret_cast<uint8_t **>(gRegistry + 0x1a24) = w.bytes;
        *reinterpret_cast<uint8_t **>(gRegistry + 0x1a28) = w.bytes + count * 4;
    };
    Stats st = RunHandler(ctx, "Prim1C", 0x53de40, 60000, Build1C,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) {
                              return reinterpret_cast<PrimRObject *>(o)->Prim1C(f, p);
                          });
    gBeforeRun = nullptr;
    if (st.throws < 1000 || st.byRet[0] < 3000 || st.byRet[1] < 1000 || st.byRet[3] < 1000)
        ctx.Fail("coverage: %u throws, returns 0/1/3: %u/%u/%u", st.throws, st.byRet[0], st.byRet[1], st.byRet[3]);
}
