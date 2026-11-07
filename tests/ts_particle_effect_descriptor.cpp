// cTSParticleEffectDescriptor (src/ts/cTSParticleEffectDescriptor.h), lane WO: original vs ours.
//
// Mode 0 builds the object with the original factory 0x4db0c0, mode 1 with ours; every slot then runs through the object's
// own vtable. After each step: the result, the stream log, and the whole object (the name's text, size, capacity and vtable
// word as well). At the end the last Release, with the frees by size.
//
// Fakes: the streams. Their words fail at random and leave the out untouched then, which is how the flag word the original
// starts from the stream argument shows. Errors come back with junk above the byte.
//
// Covered RVAs: 0x4db0c0 (Create), 0x4dbe40 (QueryInterface), 0x4dca30 (Write), 0x4dbfb0 (Read), 0x4dc6e0 (SetName), 0x4dade0
// (the deleting destructor), 0x1f0210 (Release), 0x1520c0 (SetFlag); the other folded slots through each side's vtable.
#include "ts_effects_collection.h"

#include "free_recorder.h"
#include "rz/cRZString.h"
#include "shape.h"
#include "ts/cTSParticleEffectDescriptor.h"

#include <cstring>

namespace {

using namespace fxtest;
using Desc = cTSParticleEffectDescriptor;

enum : uint32_t {
    evPut = 0xc1000000,
    evPutText = 0xc2000000,
    evGet = 0xc3000000,
    evGetText = 0xc4000000,
    evError = 0xc5000000,
    evSetMeta = 0xc6000000,
    evGetMeta = 0xc7000000,
    evVersion = 0xc8000000,
};

std::mt19937 *gR = nullptr; // the step's stream decisions

constexpr uint32_t kGameTable[1] = {0xdcd7fc};
const uint32_t kFaces[1] = {0};

uint32_t Answer(uint32_t ok) { return ((*gR)() & 0xffffff00u) | ok; }

struct Meta {
    void **vt;
    uint32_t version;
};
void *gMetaVT[80];
struct Stream {
    void **vt;
    Meta meta;
    void *object;
};
void *gInVT[24];
void *gOutVT[24];


void *__fastcall MetaVersion(Meta *m, void *)
{
    Ev({evVersion});
    return &m->version;
}
uint32_t __fastcall InU32(Stream *, void *, uint32_t *out)
{
    const bool ok = (*gR)() % 5 != 0;
    const uint32_t value = (*gR)() % 3 == 0 ? 0 : (*gR)();
    Ev({evGet, uint32_t(ok), value});
    if (ok) *out = value;
    return Answer(ok);
}
uint32_t __fastcall InStr(Stream *s, void *, cRZString *str)
{
    Ev({evGetText, uint32_t(reinterpret_cast<uint8_t *>(str) - static_cast<uint8_t *>(s->object))});
    if ((*gR)() % 4) str->mString = Name(*gR);
    return Answer(1);
}
uint32_t __fastcall StreamError(Stream *, void *)
{
    Ev({evError});
    const uint32_t r = (*gR)();
    return (r & 0xffffff00u) | (r % 4 == 0 ? 1u + ((r >> 8) & 0x7e) : 0u);
}
void *__fastcall InGetMeta(Stream *s, void *)
{
    Ev({evGetMeta});
    return &s->meta;
}
uint32_t __fastcall OutU32(Stream *, void *, uint32_t value)
{
    Ev({evPut, value});
    return Answer(1);
}
uint32_t __fastcall OutStr(Stream *s, void *, cRZString *str)
{
    Ev({evPutText, uint32_t(reinterpret_cast<uint8_t *>(str) - static_cast<uint8_t *>(s->object)), uint32_t(str->mString.size())});
    return Answer(1);
}
void __fastcall OutSetMeta(Stream *, void *, void *metadata)
{
    uint32_t words[5] = {};
    if (metadata) std::memcpy(words, metadata, sizeof(words));
    Ev({evSetMeta, uint32_t(metadata != nullptr), uint32_t(words[0] != 0), words[1], words[2], words[3], words[4]});
    if (metadata) ::operator delete(metadata, 0x14);
}

const bool gStreamsFilled = [] {
    gMetaVT[78] = reinterpret_cast<void *>(&MetaVersion);
    gInVT[9] = reinterpret_cast<void *>(&InU32);
    gInVT[15] = reinterpret_cast<void *>(&InStr);
    gInVT[18] = reinterpret_cast<void *>(&StreamError);
    gInVT[20] = reinterpret_cast<void *>(&InGetMeta);
    gOutVT[9] = reinterpret_cast<void *>(&OutU32);
    gOutVT[15] = reinterpret_cast<void *>(&OutStr);
    gOutVT[18] = reinterpret_cast<void *>(&StreamError);
    gOutVT[19] = reinterpret_cast<void *>(&OutSetMeta);
    return true;
}();

void PushDescriptor(Blob &b, const void *object)
{
    const auto *w = static_cast<const uint32_t *>(object);
    const auto *d = static_cast<const Desc *>(object);
    b.push_back(shape::VtCode(w[0], 1, shape::GameTables{kGameTable}, shape::Ours<Desc>(kFaces).face));
    for (int i = 1; i < 4; ++i) b.push_back(w[i]);
    b.push_back(*reinterpret_cast<const uint8_t *>(&d->mFlag));
    b.push_back(w[5] == uint32_t(reinterpret_cast<uintptr_t>(GameAddress<void>(cRZString::kVTable))) ||
                w[5] == uint32_t(reinterpret_cast<uintptr_t>(cRZString::VTable())));
    PushString(b, d->mName.mString);
    b.push_back(uint32_t(d->mName.mString.capacity()));
    b.push_back(w[12]);
}

void Drain(Blob &b)
{
    b.insert(b.end(), gEv.begin(), gEv.end());
    gEv.clear();
}

} // namespace

DIFF_TEST(ParticleEffectDescriptor_Slots, 0x4db0c0)
{
    using CreateFn = void *(__cdecl *)();
    alignas(8) static uint8_t otherBlock[sizeof(cRZString)];
    auto *other = reinterpret_cast<cRZString *>(otherBlock);
    other->Construct();
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            gEv.clear();
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 104729u + 7);
            gR = &r;
            void *object = mode ? static_cast<void *>(Desc::Create()) : ctx.Original<CreateFn>(0x4db0c0)();
            Stream in{gInVT, Meta{gMetaVT, r()}, object};
            Stream out{gOutVT, Meta{gMetaVT, 0}, object};
            PushDescriptor(b, object);
            CallSlot<uint32_t>(object, 1);
            for (uint32_t step = 0; step < 40; ++step) {
                b.push_back(0x0b000000u | step);
                const uint32_t op = r() % 16;
                b.push_back(op);
                switch (op) {
                case 0: { // QueryInterface: what it answers, and one reference
                    static const uint32_t kIds[] = {1, 0xe4fda3d4, 0xfba4da59, 0xac28edfd, 2};
                    const uint32_t iid = kIds[r() % 5];
                    void *answer = reinterpret_cast<void *>(uintptr_t(0x1234));
                    const bool ok = CallSlot<uint8_t>(object, 0, iid, &answer) != 0;
                    b.push_back(iid);
                    b.push_back(uint32_t(ok));
                    b.push_back(answer == object ? 1u : uint32_t(uintptr_t(answer)));
                    if (ok) b.push_back(CallSlot<uint32_t>(object, 2));
                    break;
                }
                case 1:
                    b.push_back(CallSlot<uint32_t>(object, 1));
                    b.push_back(CallSlot<uint32_t>(object, 1));
                    b.push_back(CallSlot<uint32_t>(object, 2));
                    b.push_back(CallSlot<uint32_t>(object, 2));
                    break;
                case 2:
                case 3:
                    b.push_back(CallSlot<uint8_t>(object, 3, static_cast<void *>(&out)));
                    break;
                case 4:
                case 5:
                case 6:
                    b.push_back(CallSlot<uint8_t>(object, 4, static_cast<void *>(&in)));
                    break;
                case 7:
                    b.push_back(CallSlot<uint32_t>(object, 5));
                    break;
                case 8:
                    CallSlot<void>(object, 6, uint32_t(r()));
                    b.push_back(CallSlot<uint32_t>(object, 7));
                    break;
                case 9:
                    CallSlot<void>(object, 8, uint32_t(r()));
                    b.push_back(CallSlot<uint32_t>(object, 9));
                    break;
                case 10:
                    CallSlot<void>(object, 10, bool(r() % 2));
                    b.push_back(CallSlot<uint8_t>(object, 11));
                    break;
                case 11:
                case 12: {
                    const uint32_t which = r() % 4;
                    if (which == 0) {
                        other->mString.clear();
                    } else {
                        other->mString = Name(r);
                        if (which == 3) other->mString += std::string(40, 'x');
                    }
                    CallSlot<void>(object, 12, static_cast<const void *>(other));
                    break;
                }
                case 13: // the object's own name given back
                    CallSlot<void>(object, 12, static_cast<const void *>(CallSlot<void *>(object, 13)));
                    break;
                case 14:
                    b.push_back(uint32_t(static_cast<uint8_t *>(CallSlot<void *>(object, 13)) - static_cast<uint8_t *>(object)));
                    break;
                case 15:
                    (void)r();
                    break;
                }
                Drain(b);
                PushDescriptor(b, object);
            }
            b.push_back(0x0b0000ffu);
            const size_t longName = static_cast<Desc *>(object)->mName.mString.capacity() > 15;
            difftest::FreeRecorder rec;
            static_cast<Desc *>(object)->mRefCount = seed % 2; // a count of 0 goes as well
            b.push_back(CallSlot<uint32_t>(object, 2));
            rec.Stop();
            Drain(b);
            size_t sizes[4] = {};
            const size_t freed = rec.SortedSizes(sizes, 4);
            b.push_back(uint32_t(freed));
            b.push_back(uint32_t(longName));
            b.push_back(uint32_t(rec.Freed(object)));
            for (size_t i = 0; i < freed && i < 4; ++i) b.push_back(uint32_t(sizes[i]));
            gR = nullptr;
        }
        Same(ctx, blobs[0], blobs[1], "ParticleEffectDescriptor_Slots", seed);
    }
    other->Destruct();
}
