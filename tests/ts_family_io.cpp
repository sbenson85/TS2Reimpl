// Differential tests for cTSFamily::Read and ::Write against a recording fake stream (see ts_family.h).
#include "ts_family.h"

#include <intrin.h>

using namespace tsfam;

namespace {
enum : uint32_t { evGetU32 = 40, evGetS32, evGetU8, evGetError, evSetMeta, evSetU32, evSetS32, evStreamQI, evReadString };

struct Stream {
    void **vt;
    std::mt19937 rng;
    uint32_t u32Calls = 0;
    uint32_t version = 0;
    uint32_t errorBits = 0;   // GetError call n returns bit n
    uint32_t errorCalls = 0;
    bool dropFieldReads = false; // some reads into the object's own fields fail and leave them as they were
    bool answerQi = true;
    uint32_t id = 200;
    int32_t refs = 0;
} gStream;
void *gStreamVT[24];
void *gReaderVT[24];
Obj gReader;

uint8_t __fastcall StreamQI(Stream *, void *, uint32_t id, void **out)
{
    Ev({evStreamQI, id});
    if (!gStream.answerQi) return 0;
    *out = &gReader;
    ++gReader.refs;
    return 1;
}
// A failed read leaves its destination untouched, as cRZIStream's GetVoid does (DL). Only reads into memory off the stack
// fail this way (the object's fields): a stack local would keep the original's uninitialised bytes. Read stores many
// fields straight from the stream (0x49fa30), so a port that read into a local and copied would differ here.
bool DropRead(Stream *self, const void *out)
{
    const uintptr_t at = reinterpret_cast<uintptr_t>(out);
    const bool onStack = at >= __readfsdword(8) && at < __readfsdword(4);
    if (!self->dropFieldReads || onStack || self->rng() % 4 != 0) return false;
    Ev({evGetError, 0xd209, 0});
    return true;
}
uint8_t __fastcall StreamGetU32(Stream *self, void *, void *out)
{
    if (DropRead(self, out)) return 0;
    const uint32_t n = self->u32Calls++;
    uint32_t value;
    if (n == 0) value = self->rng();
    else if (n == 1) value = self->version;
    else if (n == 2) value = self->rng();
    else value = self->rng() % 6; // counts, kept small
    Ev({evGetU32, n, value});
    *static_cast<uint32_t *>(out) = value;
    return 1;
}
uint8_t __fastcall StreamGetS32(Stream *self, void *, void *out)
{
    if (DropRead(self, out)) return 0;
    const uint32_t value = self->rng();
    Ev({evGetS32, value});
    *static_cast<uint32_t *>(out) = value;
    return 1;
}
uint8_t __fastcall StreamGetU8(Stream *self, void *, void *out)
{
    if (DropRead(self, out)) return 0;
    const uint8_t value = self->rng() % 3 == 0 ? uint8_t(self->rng()) : uint8_t(self->rng() % 2);
    Ev({evGetU8, value});
    *static_cast<uint8_t *>(out) = value;
    return 1;
}
uint8_t __fastcall StreamGetError(Stream *self, void *)
{
    const uint32_t n = self->errorCalls++;
    const uint8_t value = (self->errorBits >> (n % 32)) & 1 ? uint8_t(1 + n % 3) : 0;
    Ev({evGetError, n, value});
    return value;
}
void __fastcall StreamSetMetadata(Stream *, void *, void *metadata)
{
    uint32_t words[5];
    std::memcpy(words, metadata, sizeof(words));
    Ev({evSetMeta, words[0] ? 1u : 0u, words[1], words[2], words[3], words[4]});
    ::operator delete(metadata, 0x14);
}
uint8_t __fastcall StreamSetU32(Stream *, void *, uint32_t value)
{
    Ev({evSetU32, value});
    return 1;
}
uint8_t __fastcall StreamSetS32(Stream *, void *, uint32_t value)
{
    Ev({evSetS32, value});
    return 1;
}
void __fastcall ReaderReadString(Obj *self, void *, void *text)
{
    Ev({evReadString, self->id});
    static_cast<cRZString *>(text)->mString = std::string("read") + char('a' + gStream.u32Calls % 26);
}
uint32_t __fastcall ReaderAddRef(Obj *self, void *) { return uint32_t(++self->refs); }
uint32_t __fastcall ReaderRelease(Obj *self, void *)
{
    Ev({evRelease, self->id});
    return uint32_t(--self->refs);
}

void ResetStream(uint32_t seed, uint32_t version)
{
    for (auto &slot : gStreamVT) slot = nullptr;
    gStreamVT[0] = reinterpret_cast<void *>(&StreamQI);
    gStreamVT[5] = reinterpret_cast<void *>(&StreamGetU8);
    gStreamVT[8] = reinterpret_cast<void *>(&StreamGetS32);
    gStreamVT[9] = reinterpret_cast<void *>(&StreamGetU32);
    gStreamVT[18] = reinterpret_cast<void *>(&StreamGetError);
    gStreamVT[19] = reinterpret_cast<void *>(&StreamSetMetadata);
    for (auto &slot : gReaderVT) slot = nullptr;
    gReaderVT[1] = reinterpret_cast<void *>(&ReaderAddRef);
    gReaderVT[2] = reinterpret_cast<void *>(&ReaderRelease);
    gReaderVT[20] = reinterpret_cast<void *>(&ReaderReadString);
    gReader.vt = gReaderVT;
    gReader.id = 150;
    gReader.refs = 1000;
    gStream.vt = gStreamVT;
    gStream.rng.seed(seed * 7919 + 3);
    gStream.u32Calls = 0;
    gStream.version = version;
    gStream.errorCalls = 0;
    gStream.errorBits = 0;
    gStream.dropFieldReads = seed % 5 == 1;
    gStream.answerQi = (seed % 4) != 0;
}
void ResetWriteStream()
{
    for (auto &slot : gStreamVT) slot = nullptr;
    gStreamVT[8] = reinterpret_cast<void *>(&StreamSetS32);
    gStreamVT[9] = reinterpret_cast<void *>(&StreamSetU32);
    gStreamVT[18] = reinterpret_cast<void *>(&StreamGetError);
    gStreamVT[19] = reinterpret_cast<void *>(&StreamSetMetadata);
    gStream.vt = gStreamVT;
    gStream.errorCalls = 0;
}
} // namespace

DIFF_TEST(Family_Read, 0x49fa30)
{
    uint32_t versions[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x54, 0x55, 0x56, 0x100, 0xffffffff};
    for (uint32_t seed = 0; seed < 40; ++seed) {
        for (uint32_t version : versions) {
            for (int route = 0; route < 2; ++route) {
                gRoute = Route(route);
                Blob blobs[2];
                for (int side = 0; side < 2; ++side) {
                    gOurSide = side != 0;
                    Build(W(), seed * 31 + version, 0xb);
                    gTrapHits = 0;
                    ResetStream(seed, version);
                    // Error bits: the last GetError call (the result) and some of the per-element ones.
                    gStream.errorBits = seed % 3 == 0 ? (seed * 2654435761u) : 0;
                    const uint8_t r = Call<uint8_t(__thiscall *)(Family *, void *)>(ctx, side == 0, 0x49fa30, FAM_ADAPTER(Read), &gStream);
                    blobs[side].push_back(r);
                    blobs[side].push_back(uint32_t(gReader.refs));
                    blobs[side].push_back(uint32_t(gTrapHits));
                    Capture(blobs[side]);
                }
                Same(ctx, blobs[0], blobs[1], "Read", RouteSeed(seed * 1000 + version));
            }
        }
    }
}

DIFF_TEST(Family_Write, 0x4a1100)
{
    for (uint32_t seed = 0; seed < 300; ++seed) {
        for (int route = 0; route < 2; ++route) {
            gRoute = Route(route);
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                gOurSide = side != 0;
                Build(W(), seed, 0xb);
                gTrapHits = 0;
                ResetWriteStream();
                gStream.errorBits = seed % 3 == 0 ? (seed * 2654435761u) : 0;
                const uint8_t r = Call<uint8_t(__thiscall *)(Family *, void *)>(ctx, side == 0, 0x4a1100, FAM_ADAPTER(Write), &gStream);
                blobs[side].push_back(r);
                blobs[side].push_back(uint32_t(gTrapHits));
                Capture(blobs[side]);
            }
            Same(ctx, blobs[0], blobs[1], "Write", RouteSeed(seed));
        }
    }
}
