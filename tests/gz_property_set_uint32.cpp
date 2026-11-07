// Differential tests for the uint32-keyed property-set dispatcher cGZPropertySetSerializerT<cKeyUint32Traits> in the standalone
// shape (src/gz/cGZPropertySetSerializerUint32.size.cpp; constructor / destructor lane DY, moved here from
// tests/cas_bin_info_factory_create.cpp; the rest lane EQ):
//   0x241e79 Construct, 0x241f2c Destruct, 0x241fc9 DeletingDestructor, 0x2425ad Read, 0x242547 ReadWith, 0x242899 Write, the
//   folded QueryInterface 0x237d6f, AddRef 0x15e520 and Release 0x242810.
// Two routes for ours: the vtable route (our constructor, the slots through our vtable, ReadWith called as a member) and the
// adapter route (the game's constructor, a copy of the game's vtable with the hooked slots 3 / 4 / 5 trapped, the adapters called).
// The stream, its record face and the delegates are recording fakes (tests/ts_services_fixture.h); an empty binary slot makes the
// binary delegate (the original's 0x236be6 / our class, lane EU), whose Read / Write then run on the fake stream (failing at the first read or
// write). Compared: results, the fakes' call log and counts, the dispatcher's words (vtable word as "the side's own"), the made
// delegate's words.
#include "difftest.h"
#include "free_recorder.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZPropertySet.h"
#include "gz/cGZPropertySetSerializer.h"

#include <cstring>
#include <new>

using namespace svcfx;

namespace {

using Disp32 = cGZPropertySetSerializerUint32;
using Disp32Adapters = cGZPropertySetSerializerUint32Adapters;
using Disp32Face = cIGZPropertySetSerializerUint32;
// A live serializer's words +4..+0xf after a non-deleting destruction, as logged (svcfx::DumpWords): 'p' pointer, 'd' data.
constexpr char kDispKinds[] = "ppd";

template <class Fn> Fn Game(uint32_t rva) { return reinterpret_cast<Fn>(GameAddress<void>(rva)); }

const void *OurDispVTable()
{
    alignas(8) static unsigned char buffer[sizeof(Disp32)];
    static const void *vt = [] {
        auto *d = ::new (buffer) Disp32;
        const void *word = *reinterpret_cast<const void *const *>(d);
        d->~Disp32();
        return word;
    }();
    return vt;
}

// The vtable word as a code: 1 the side's own (the game's 0xdadc24, ours, or the trap copy), else raw.
uint32_t VtCode(const void *object, int side, void **traps)
{
    const void *word = *static_cast<const void *const *>(object);
    if (side == 0) return word == GameAddress<void>(Disp32::kOriginalVTable) ? 1u : uint32_t(uintptr_t(word));
    return word == OurDispVTable() || word == traps ? 1u : uint32_t(uintptr_t(word));
}

// Trap copy of the game's dispatcher vtable: Read (2 args), Write (3), the deleting destructor (1).
uint32_t gTraps = 0;
uint32_t __fastcall Trap1(void *, void *, uint32_t) { gTraps |= 0x20; return 0; }
uint32_t __fastcall Trap2(void *, void *, uint32_t, uint32_t) { gTraps |= 0x8; return 0; }
uint32_t __fastcall Trap3(void *, void *, uint32_t, uint32_t, uint32_t) { gTraps |= 0x10; return 0; }
void *gTrapVT[6];
void **TrapVTable()
{
    std::memcpy(gTrapVT, GameAddress<void>(Disp32::kOriginalVTable), sizeof(gTrapVT));
    gTrapVT[3] = reinterpret_cast<void *>(&Trap2);
    gTrapVT[4] = reinterpret_cast<void *>(&Trap3);
    gTrapVT[5] = reinterpret_cast<void *>(&Trap1);
    return gTrapVT;
}

// The fake stream F(0): QI (record face F(1) when allowed), GetUint32 (slot 9) hands out the magic.
// Only the dispatcher's peek (the first slot 9 call of a Read) succeeds; a made delegate's own reads and writes (slot 9 is also
// cIGZOStream::SetUint32, a value) fail without touching the argument.
uint32_t gQiOk, gMagic, gGetOk, gGetCalls;
uint32_t StreamQI(Obj *, uint32_t *args)
{
    if (!gQiOk) return 0;
    *reinterpret_cast<void **>(uintptr_t(args[1])) = &F(1);
    ++F(1).refs;
    return 1;
}
uint32_t StreamGetUint32(Obj *, uint32_t *args)
{
    if (!gGetOk || gGetCalls++ != 0) return 0;
    *reinterpret_cast<uint32_t *>(uintptr_t(args[0])) = gMagic;
    return 1;
}

void SetupFakes(uint32_t seekOk, uint32_t delegateResult)
{
    ResetAll();
    gGetCalls = 0;
    F(0).Set(0, "dp", 0u, &StreamQI);
    F(0).Set(9, "p", 0u, &StreamGetUint32);
    // Whatever else a made delegate asks of the stream fails (cIGZIStream / cIGZOStream arities: 17 GetVoid / SetVoid take 2,
    // 18 GetError none).
    for (unsigned slot : {3u, 4u, 5u, 6u, 7u, 8u, 12u, 15u, 19u}) F(0).Set(slot, 1, 0u);
    F(0).Set(17, 2, 0u);
    F(0).Set(18, 0, 1u);
    F(1).Set(13, 1, seekOk);                   // SeekAbsolute
    for (unsigned d = 2; d < 4; ++d) {
        F(d).Set(3, "pp", delegateResult); // Read (stream, set)
        F(d).Set(4, "pp-", delegateResult); // Write (stream, set, flag)
    }
}

// Our binary delegate's vtable (lane EU: the dispatcher makes ours, the original the game's 0xdac8f0).
uint32_t OurBinaryVTable()
{
    cGZPropertySetSerializerBinaryUint32 b;
    return *reinterpret_cast<const uint32_t *>(&b);
}

// A delegate the dispatcher holds: a fake by its id, or a made one by its words (vtable as "a binary delegate's": the game's on
// the original side, ours on ours; count, the next word).
void DelegateWords(Blob &b, const void *delegate)
{
    if (!delegate) return b.push_back(0);
    for (unsigned i = 0; i < kFakes; ++i)
        if (delegate == &F(i)) return b.push_back(200 + i);
    const auto *w = static_cast<const uint32_t *>(delegate);
    const uint32_t game = uint32_t(uintptr_t(GameAddress<void>(cGZPropertySetSerializerBinaryUint32::kOriginalVTable)));
    const uint32_t vt = (w[0] == game || w[0] == OurBinaryVTable()) && game != OurBinaryVTable() ? 1u : w[0];
    b.insert(b.end(), {0xde1e0000u, vt, w[1], w[2] & 0xffff});
}

} // namespace

// Construct (the adapter 0x241e79 and our constructor) over filled memory; Destruct (0x241f2c: the adapter, and our destructor
// called as a member) and the deleting destructor (0x241fc9: through our vtable, and the adapter) with no, one or both delegates
// (logging fakes: the release order and the words left behind compared).
DIFF_TEST(PropSetSer32_Lifetime, 0x241e79)
{
    for (uint32_t fill : {0u, 0xcdcdcdcdu, 0xffffffffu}) {
        Blob result[3];
        for (int s = 0; s < 3; ++s) {
            alignas(8) uint32_t memory[5];
            for (uint32_t &w : memory) w = fill;
            void *r = s == 0 ? Game<void *(__thiscall *)(void *)>(0x241e79)(memory)
                    : s == 1 ? static_cast<void *>(Disp32Adapters::Construct(memory, nullptr))
                             : static_cast<void *>(::new (memory) Disp32);
            result[s] = {r == memory ? 1u : 0u, VtCode(memory, s == 0 ? 0 : 1, nullptr)};
            result[s].insert(result[s].end(), memory + 1, memory + 5);
        }
        Same(ctx, result[0], result[1], "Construct (adapter)", fill);
        Same(ctx, result[0], result[2], "Construct (ours)", fill);
    }
    for (uint32_t mask = 0; mask < 16; ++mask) {
        const uint32_t how = mask >> 2; // 0 Destruct, 1 deleting flag 0, 2 deleting flag 1 (heap), 3 Destruct again
        for (int route = 0; route < 2; ++route) {
            // The deleting destructor's adapter is gone (lane GT: only the game's vtable reaches 0x241fc9): no adapter route.
            if (route == 1 && (how == 1 || how == 2)) continue;
            Blob result[2];
            for (int s = 0; s < 2; ++s) {
                ResetAll();
                auto *x = static_cast<Disp32 *>(::operator new(sizeof(Disp32)));
                if (s == 0 || route == 1) Game<void *(__thiscall *)(void *)>(0x241e79)(x);
                else ::new (x) Disp32;
                if (s == 1 && route == 1) *reinterpret_cast<void ***>(x) = TrapVTable();
                x->mRefCount = 7;
                x->mXml = mask & 1 ? &F(0) : nullptr;
                x->mBinary = mask & 2 ? &F(1) : nullptr;
                gTraps = 0;
                difftest::FreeRecorder rec;
                if (how == 0 || how == 3) {
                    if (s == 0) Game<void(__thiscall *)(void *)>(0x241f2c)(x);
                    else if (route == 0) x->~Disp32();
                    else Disp32Adapters::Destruct(x, nullptr);
                } else {
                    const uint32_t flags = how == 2;
                    void *ret = s == 0 ? Game<void *(__thiscall *)(void *, uint32_t)>(0x241fc9)(x, flags)
                                       : VirtualSlot<void *(__thiscall *)(void *, uint32_t)>(x, 5)(x, flags);
                    result[s].push_back(ret == x);
                }
                rec.Stop();
                const bool freed = rec.Freed(x);
                result[s].insert(result[s].end(), {freed, gTraps});
                if (!freed) {
                    const auto *w = reinterpret_cast<const uint32_t *>(x);
                    result[s].push_back(VtCode(x, s, gTrapVT));
                    svcfx::DumpWords(result[s], w + 1, 12, kDispKinds);
                    ::operator delete(x);
                }
                result[s].insert(result[s].end(), gEv.begin(), gEv.end());
                CaptureFakes(result[s]);
            }
            Same(ctx, result[0], result[1], route ? "destroy (adapters)" : "destroy (vtable)", mask);
        }
    }
}

// Read / ReadWith / Write: the stream's record face present or not, the magic (both binary magics, XML text, 0), a failed peek or
// rewind, the binary flag (0, 1, 0x100: low byte only), delegates present (fakes F(2) XML / F(3) binary) or a binary one made
// (the game's class); the slots QueryInterface / AddRef / Release through each route.
DIFF_TEST(PropSetSer32_Dispatch, 0x2425ad)
{
    AllocatorScope allocator;
    const uint32_t magics[] = {0xcbe750e0, 0x2bea4862, 0x6d783f3c, 0};
    for (uint32_t c = 0; c < 2 * 3 * 64; ++c) {
        const int route = c & 1;
        const uint32_t op = (c >> 1) % 3; // 0 Read, 1 ReadWith, 2 Write
        const uint32_t v = c / 6;
        const uint32_t qiOk = v & 1, getOk = (v >> 1) & 1, seekOk = (v >> 2) & 1, makeBinary = (v >> 3) & 1;
        const uint32_t magic = magics[(v >> 4) & 3];
        const uint32_t flag = op == 0 ? 0 : (v % 3 == 0 ? 0u : v % 3 == 1 ? 1u : 0x100u);
        Blob result[2];
        for (int s = 0; s < 2; ++s) {
            SetupFakes(seekOk, 0x100 | (c & 0xff));
            gQiOk = qiOk, gGetOk = getOk && op == 0, gMagic = magic;
            // The set the delegates are handed: the game's uint32 set (only fakes and the made delegate's failing Read see it).
            alignas(8) uint8_t set[0x18];
            Game<void *(__thiscall *)(void *)>(0x1bec5f)(set);
            AddRange(set, sizeof(set), 0x5e700000);
            auto *x = static_cast<Disp32 *>(::operator new(sizeof(Disp32)));
            if (s == 0 || route == 1) Game<void *(__thiscall *)(void *)>(0x241e79)(x);
            else ::new (x) Disp32;
            if (s == 1 && route == 1) *reinterpret_cast<void ***>(x) = TrapVTable();
            x->mXml = &F(2);
            x->mBinary = makeBinary ? nullptr : &F(3);
            gTraps = 0;
            auto *typed = reinterpret_cast<cGZPropertySetUint32 *>(set);
            uint32_t r;
            if (s == 0) {
                r = op == 0 ? Game<uint8_t(__thiscall *)(void *, void *, void *)>(0x2425ad)(x, &F(0), set)
                  : op == 1 ? Game<uint8_t(__thiscall *)(void *, void *, void *, uint32_t)>(0x242547)(x, &F(0), set, flag)
                            : Game<uint8_t(__thiscall *)(void *, void *, void *, uint32_t)>(0x242899)(x, &F(0), set, flag);
            } else if (route == 0) {
                Disp32Face *face = x;
                r = op == 0 ? face->Read(&F(0), typed)
                  : op == 1 ? x->ReadWith(&F(0), typed, uint8_t(flag))
                            : VirtualSlot<uint8_t(__thiscall *)(void *, void *, void *, uint32_t)>(x, 4)(x, &F(0), set, flag);
            } else {
                r = op == 0 ? Disp32Adapters::Read(x, nullptr, &F(0), typed)
                  : op == 1 ? reinterpret_cast<uint8_t(__fastcall *)(void *, void *, void *, void *, uint32_t)>(&Disp32Adapters::ReadWith)(x, nullptr, &F(0), set, flag)
                            : reinterpret_cast<uint8_t(__fastcall *)(void *, void *, void *, void *, uint32_t)>(&Disp32Adapters::Write)(x, nullptr, &F(0), set, flag);
            }
            Blob &b = result[s];
            b.insert(b.end(), {uint32_t(r & 0xff), gTraps, VtCode(x, s, gTrapVT), x->mRefCount});
            DelegateWords(b, x->mXml);
            DelegateWords(b, x->mBinary);
            // The slots no other case reaches: QI (1, the iid, others), AddRef, Release (at 1: the deleting destructor, which
            // releases the delegates). Folded, never hooked: on the adapter route the game's vtable is put back (the game's
            // bodies run on both sides there; their destructor call reaches our hooked 0x241fc9 only in game).
            if (s == 1 && route == 1) *reinterpret_cast<const void **>(x) = GameAddress<const void>(Disp32::kOriginalVTable);
            for (uint32_t iid : {1u, 0xebe701f3u, 0xebb10cddu}) {
                void *out = reinterpret_cast<void *>(0x7777);
                const uint32_t ok = s == 0 || route == 1 ? VirtualSlot<uint8_t(__thiscall *)(void *, uint32_t, void **)>(x, 0)(x, iid, &out)
                                                         : static_cast<Disp32Face *>(x)->QueryInterface(iid, &out);
                b.insert(b.end(), {ok, out == x ? 1u : svcfx::NormOut(uint32_t(uintptr_t(out)), 0x7777), x->mRefCount});
            }
            x->mRefCount = 1;
            difftest::FreeRecorder rec;
            const uint32_t left = s == 0 || route == 1 ? VirtualSlot<uint32_t(__thiscall *)(void *)>(x, 2)(x) : static_cast<Disp32Face *>(x)->Release();
            rec.Stop();
            b.insert(b.end(), {left, rec.Freed(x), gTraps});
            if (!rec.Freed(x)) ::operator delete(x);
            Game<void(__thiscall *)(void *)>(0x1bed39)(set);
            b.insert(b.end(), gEv.begin(), gEv.end());
            CaptureFakes(b);
        }
        Same(ctx, result[0], result[1], route ? "dispatch (adapters)" : "dispatch (vtable)", c);
    }
}
