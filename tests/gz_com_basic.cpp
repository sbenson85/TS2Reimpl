// cGZCOM: constructor, destructors, QueryInterface, Release, SetServiceRunning, the folded and inherited slots (see
// gz_com_support.h). Ours is called through its own vtable; the secondary faces' slots are the compiler's adjustor
// thunks (the original's 0x145b36, 0x145b3e, 0x145c04, 0x145c0c, 0x145c24, 0x14510c).
#include "free_recorder.h"
#include "gz_com_support.h"

DIFF_TEST(GZCOM_Construct, 0x144e52)
{
    for (int fill : {0xCD, 0x00, 0xFF}) {
        cGZCOM *a = MakeCom(ctx, kOriginal, fill), *b = MakeCom(ctx, kOurs, fill);
        ExpectSameBytes(ctx, "construct: bytes", Canon(a, kOriginal), Canon(b, kOurs));
        for (cGZCOM *c : {a, b}) {
            ++ctx.cases;
            if (c->mLock != &c->mCritSect) ctx.Fail("construct: lock pointer not the embedded section");
            if (!HeadOk(Word(c, 0x34)) || !HeadOk(Word(c, 0x3c))) ctx.Fail("construct: container head is not an empty tree head");
            if (c->mClasses.size() != 0 || c->mLibraries.size() != 0) ctx.Fail("construct: containers not empty");
        }
        ++ctx.cases;
        if (Word<uint32_t>(b, 0) != reinterpret_cast<uint32_t>(gVt[kOurs].com[0]) || gVt[kOurs].com[0] == gVt[kOriginal].com[0])
            ctx.Fail("construct: ours does not carry our vtable");
        ExpectSame(ctx, "construct: section", std::to_string(a->mCritSect.mBlock->lockCount), std::to_string(b->mCritSect.mBlock->lockCount));
        // The generator is seeded from the clock (seed -1), so the two states differ (masked in
        // Canon); both are non-zero seeds with the integer and real seeds equal.
        for (cGZCOM *c : {a, b}) {
            ++ctx.cases;
            uint32_t seeds[2];
            std::memcpy(seeds, reinterpret_cast<char *>(c) + 0x50, sizeof(seeds));
            if (seeds[0] == 0 || seeds[0] != seeds[1]) ctx.Fail("construct: generator seeds %x %x", seeds[0], seeds[1]);
        }
        DestroyCom(ctx, a, kOriginal);
        DestroyCom(ctx, b, kOurs);
    }
}

DIFF_TEST(GZCOM_Destruct, 0x145075)
{
    for (int round = 0; round < 3; ++round) {
        cGZCOM *c[2] = {MakeCom(ctx, kOriginal), MakeCom(ctx, kOurs)};
        for (int side = 0; side < 2; ++side) {
            for (int i = 0; i < round * 5; ++i) {
                char name[16];
                std::snprintf(name, sizeof(name), "lib%02d", (i * 7) % 11);
                Add(c[side], side, name);
                c[side]->mClasses.insert({static_cast<uint32_t>(i * 13), cGZCOMClassEntry{1, nullptr}});
            }
        }
        for (int side = 0; side < 2; ++side) {
            cGZCOM *com = c[side];
            // Every block the object owns: the two heads, the nodes, the section's block.
            std::vector<void *> blocks;
            blocks.reserve(64);
            blocks.push_back(Word(com, 0x34));
            blocks.push_back(Word(com, 0x3c));
            for (auto &lib : com->mLibraries) blocks.push_back(reinterpret_cast<char *>(const_cast<cGZCOMLibrary *>(&lib)) - 0x10);
            for (auto &cl : com->mClasses) blocks.push_back(reinterpret_cast<char *>(const_cast<std::pair<const uint32_t, cGZCOMClassEntry> *>(&cl)) - 0x10);
            blocks.push_back(com->mCritSect.mBlock);
            difftest::FreeRecorder rec;
            if (side == kOriginal) {
                ctx.Original<void(__thiscall *)(void *)>(kDtor)(com);
            } else {
                com->cGZCOM::~cGZCOM();
            }
            rec.Stop();
            int live = 0;
            for (void *p : blocks) live += rec.Freed(p) ? 0 : 1;
            ++ctx.cases;
            if (live) ctx.Fail("destruct (%s, round %d): %d of %zu blocks still allocated", side ? "ours" : "original", round, live, blocks.size());
        }
        ExpectSameBytes(ctx, "destruct: bytes", Canon(c[0], kOriginal), Canon(c[1], kOurs));
        ::operator delete(c[0]);
        ::operator delete(c[1]);
    }
}

// The deleting destructor is the +8 face's slot 3: the original's body 0x145114 (this = the object) and its thunk
// 0x14510c (this + 8); ours through our +8 vtable (the compiler's thunk to its scalar deleting destructor).
DIFF_TEST(GZCOM_DeletingDestructor, 0x145114)
{
    for (int variant = 0; variant < 2; ++variant) {
        for (uint32_t flags : {0u, 1u, 2u, 3u, 0xfffffffeu, 0xffffffffu}) {
            cGZCOM *c[2] = {MakeCom(ctx, kOriginal), MakeCom(ctx, kOurs)};
            void *rets[2];
            bool live[2];
            for (int side = 0; side < 2; ++side) {
                Add(c[side], side, "x");
                difftest::FreeRecorder rec;
                if (side == kOriginal) {
                    rets[0] = variant == 0 ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeleting)(c[0], flags)
                                           : ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeletingThunk)(reinterpret_cast<char *>(c[0]) + 8, flags);
                } else {
                    rets[1] = Call<void *>(ctx, kOurs, c[1], 2, 3, 0, flags);
                }
                rec.Stop();
                live[side] = !rec.Freed(c[side]);
            }
            ++ctx.cases;
            // The thunk returns what the body returns: the object (not the face).
            if (rets[0] != c[0] || rets[1] != c[1]) ctx.Fail("deleting destructor %x (variant %d): returned a different object", flags, variant);
            if (live[0] != live[1] || live[0] != ((flags & 1) == 0)) ctx.Fail("deleting destructor %x: freed original=%d ours=%d", flags, !live[0], !live[1]);
            if (flags & 1) continue;
            ExpectSameBytes(ctx, "deleting destructor: bytes", Canon(c[0], kOriginal), Canon(c[1], kOurs));
            ::operator delete(c[0]);
            ::operator delete(c[1]);
        }
    }
}

DIFF_TEST(GZCOM_QueryInterface, 0x145b14)
{
    const uint32_t ids[] = {cIGZCOM::kIID, 0x287fb697, 1, 0, 0x4241330c, 0x4241330e, 0xdeadbeef};
    for (int face = 0; face < 3; ++face) {
        uint32_t rva = face == 0 ? kQI : face == 1 ? kQISys : kQIUnk;
        for (uint32_t riid : ids) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                cGZCOM *com = MakeCom(ctx, side);
                InstallVtables(com, side);
                gLog.clear();
                void *out = reinterpret_cast<void *>(0x5555);
                bool r = Call<bool>(ctx, side, com, face, 0, rva, riid, &out);
                char text[64];
                std::snprintf(text, sizeof(text), "ret=%d out=%ld ", r ? 1 : 0, out == reinterpret_cast<void *>(0x5555) ? -1L : static_cast<long>(static_cast<char *>(out) - reinterpret_cast<char *>(com)));
                results[side] = std::string(text) + Joined(gLog);
                DestroyCom(ctx, com, side);
            }
            char what[64];
            std::snprintf(what, sizeof(what), "QueryInterface +%d riid %x", kFaceOffset[face], riid);
            ExpectSame(ctx, what, results[0], results[1]);
        }
    }
}

DIFF_TEST(GZCOM_Release, 0x145bfc)
{
    for (int face = 0; face < 3; ++face) {
        uint32_t rva = face == 0 ? kRelease : face == 1 ? kReleaseSys : kReleaseUnk;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            cGZCOM *com = MakeCom(ctx, side);
            InstallVtables(com, side);
            gLog.clear();
            uint32_t r = Call<uint32_t>(ctx, side, com, face, 2, rva);
            char text[64];
            std::snprintf(text, sizeof(text), "ret=%x ", r);
            results[side] = std::string(text) + Joined(gLog);
            DestroyCom(ctx, com, side);
        }
        ExpectSame(ctx, "Release", results[0], results[1]);
    }
}

DIFF_TEST(GZCOM_SetServiceRunning, 0x145c1c)
{
    std::mt19937 rng(7);
    for (int face = 0; face < 2; ++face) {
        uint32_t rva = face == 0 ? kSetRunning : kSetRunningSys;
        int slot = face == 0 ? 11 : 7;
        std::vector<uint32_t> values = {0, 1, 2, 0xff, 0x100, 0x101, 0x80000000, 0xffffffff};
        for (int i = 0; i < 20; ++i) values.push_back(rng());
        for (uint32_t v : values) {
            cGZCOM *c[2] = {MakeCom(ctx, kOriginal), MakeCom(ctx, kOurs)};
            for (int side = 0; side < 2; ++side) {
                Word<uint8_t>(c[side], 0x1c) = 0x5a;
                Call<void>(ctx, side, c[side], face, slot, rva, v);
            }
            ExpectSameBytes(ctx, "SetServiceRunning", Canon(c[0], kOriginal), Canon(c[1], kOurs));
            DestroyCom(ctx, c[0], kOriginal);
            DestroyCom(ctx, c[1], kOurs);
        }
    }
}

DIFF_TEST(GZCOM_GetFramework, 0x145520)
{
    std::mt19937 rng(9);
    for (int i = 0; i < 20; ++i) {
        cGZCOM *c[2] = {MakeCom(ctx, kOriginal), MakeCom(ctx, kOurs)};
        void *r[2];
        auto *framework = reinterpret_cast<cIGZFrameWork *>(static_cast<uintptr_t>(rng()));
        for (int side = 0; side < 2; ++side) {
            c[side]->mFramework = framework;
            r[side] = Call<void *>(ctx, side, c[side], 0, 4, kGetFramework);
        }
        ++ctx.cases;
        if (r[0] != r[1] || r[0] != framework) ctx.Fail("GetFramework: %p vs %p", r[0], r[1]);
        ExpectSameBytes(ctx, "GetFramework: bytes", Canon(c[0], kOriginal), Canon(c[1], kOurs));
        DestroyCom(ctx, c[0], kOriginal);
        DestroyCom(ctx, c[1], kOurs);
    }
}

// Every other slot, each side's own vtable: the original's game vtable entry against ours. The cIGZUnknown slots of
// the +4 / +8 faces (AddRef 0x145248 / 0x145250 thunks to the folded 0x145240), the +4 face's cRZSystemService
// accessors and default bodies, the +8 face's RemoveRef and RefCount. Raw byte 0x80 for the running flag.
DIFF_TEST(GZCOM_Slots, 0x145240)
{
    struct SlotCase {
        int face, slot, args; // args: number of dword arguments (0 or 1)
    };
    const SlotCase cases[] = {{0, 1, 0}, {1, 1, 0}, {2, 1, 0}, {1, 3, 0}, {1, 4, 1}, {1, 5, 0}, {1, 6, 0}, {1, 8, 0},
                              {1, 9, 0}, {1, 10, 1}, {1, 11, 1}, {1, 12, 0}, {2, 4, 0}, {2, 5, 0}};
    std::mt19937 rng(21);
    for (const SlotCase &sc : cases) {
        for (int round = 0; round < 8; ++round) {
            uint32_t arg = rng();
            uint32_t count = round == 0 ? 0 : round == 1 ? 1 : rng() % 50;
            uint8_t running = round == 2 ? 0x80 : static_cast<uint8_t>(rng() % 2);
            cGZCOM *c[2] = {MakeCom(ctx, kOriginal), MakeCom(ctx, kOurs)};
            uint32_t r[2];
            for (int side = 0; side < 2; ++side) {
                c[side]->mRefCount = count;
                Word<uint8_t>(c[side], 0x1c) = running;
                c[side]->mTickPriority = 0x1234 + round;
                uint32_t rva = reinterpret_cast<uint32_t>(gVt[kOriginal].com[sc.face][sc.slot]) - reinterpret_cast<uint32_t>(GameAddress<void>(0));
                r[side] = sc.args ? Call<uint32_t>(ctx, side, c[side], sc.face, sc.slot, rva, arg) : Call<uint32_t>(ctx, side, c[side], sc.face, sc.slot, rva);
            }
            ++ctx.cases;
            // Byte results (the flag getter, the default bodies) are compared in al only: the original leaves the
            // upper bytes of eax as they were.
            bool byteResult = sc.face == 1 && (sc.slot == 6 || (sc.slot >= 8 && sc.slot <= 11));
            uint32_t mask = byteResult ? 0xff : 0xffffffff;
            if ((r[0] & mask) != (r[1] & mask)) ctx.Fail("slot %d of face %d: original %x, ours %x", sc.slot, sc.face, r[0], r[1]);
            ExpectSameBytes(ctx, "slots: bytes", Canon(c[0], kOriginal), Canon(c[1], kOurs));
            DestroyCom(ctx, c[0], kOriginal);
            DestroyCom(ctx, c[1], kOurs);
        }
    }
}
