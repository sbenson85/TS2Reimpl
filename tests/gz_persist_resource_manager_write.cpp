// Differential test for cGZPersistResourceManager::vf14 (0x9b5610, writes a resource into a DB
// segment). Every object it touches is a recording fake: the resource (slot 3 GetKey), the segment
// (slots 20 TestForRecord, 22 OpenRecord, 23 CreateNewRecord, 24 CloseRecord, 25 AbortRecord), the
// manager itself (slot 24 FindFactory), the record (slot 11, slot 2 Release) and the factory (slot 6
// Write, slot 2 Release). Each case picks every fake's result (low byte 0, 1 or random, random upper
// bytes) and whether the out-parameter slots write an object, null or nothing. Both sides must make
// the same calls with the same arguments (fakes as ids, the key contents) and return the same byte.
#include "difftest.h"
#include "gz/cGZPersistResourceManager.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

struct Fake {
    void **vt;
    uint32_t id;
};

Fake gSelf, gRes, gSeg, gRec, gFactory;
void *gSelfVT[54];
void *gResVT[8];
void *gSegVT[32];
void *gRecVT[16];
void *gFactoryVT[8];

const void *gKeyArg;          // vf14's key argument (passed on to CreateNewRecord)
uint32_t gKey[4];             // what GetKey returns (type, group, instance, resource: 16 bytes, as the game)
uint32_t gResult[8];          // results: 0 test, 1 open, 2 create, 3 close, 4 abort, 5 find, 6 write, 7 setsize
uint32_t gOutMode[3];         // per out-slot (open, create, find): 0 writes the object, 1 writes null, 2 leaves it
std::vector<uint32_t> gTrace;

uint32_t Norm(const void *p)
{
    if (p == nullptr) return 0;
    if (p == &gSelf) return 0xf1;
    if (p == &gRes) return 0xf2;
    if (p == &gSeg) return 0xf3;
    if (p == &gRec) return 0xf4;
    if (p == &gFactory) return 0xf5;
    if (p == gKeyArg) return 0xf6;
    return 0xfe;
}

void Rec(uint32_t self, uint32_t slot)
{
    gTrace.push_back(self);
    gTrace.push_back(slot);
}

void RecKey(const uint32_t *key)
{
    gTrace.push_back(key[0]);
    gTrace.push_back(key[1]);
    gTrace.push_back(key[2]);
    gTrace.push_back(key[3]); // all 16 bytes: the key was once modelled as 12 (runs46 crash)
}

void WriteOut(void **out, uint32_t mode, void *object)
{
    gTrace.push_back(Norm(*out)); // what the caller's holder had before
    if (mode == 0) *out = object;
    else if (mode == 1) *out = nullptr;
}

// Writes 16 bytes into the caller's key, as the game's slot 3 implementations do (e.g. 0x49e6c0, movups), not
// sizeof our key type: a wrong-sized key in the replacement must show here, not be shared with the fake.
uint32_t *__fastcall ResGetKey(Fake *self, void *, uint32_t *out)
{
    Rec(self->id, 3);
    out[0] = gKey[0];
    out[1] = gKey[1];
    out[2] = gKey[2];
    out[3] = gKey[3];
    return out;
}

uint32_t __fastcall SegTest(Fake *self, void *, const uint32_t *key)
{
    Rec(self->id, 20);
    RecKey(key);
    return gResult[0];
}

uint32_t __fastcall SegOpen(Fake *self, void *, const uint32_t *key, void **out, uint32_t mode)
{
    Rec(self->id, 22);
    RecKey(key);
    gTrace.push_back(mode);
    WriteOut(out, gOutMode[0], &gRec);
    return gResult[1];
}

uint32_t __fastcall SegCreate(Fake *self, void *, const void *key, void **out)
{
    Rec(self->id, 23);
    gTrace.push_back(Norm(key));
    WriteOut(out, gOutMode[1], &gRec);
    return gResult[2];
}

uint32_t __fastcall SegClose(Fake *self, void *, void *record)
{
    Rec(self->id, 24);
    gTrace.push_back(Norm(record));
    return gResult[3];
}

uint32_t __fastcall SegAbort(Fake *self, void *, void *record)
{
    Rec(self->id, 25);
    gTrace.push_back(Norm(record));
    return gResult[4];
}

uint32_t __fastcall SelfFindFactory(Fake *self, void *, void *resource, void **out)
{
    Rec(self->id, 24);
    gTrace.push_back(Norm(resource));
    WriteOut(out, gOutMode[2], &gFactory);
    return gResult[5];
}

uint32_t __fastcall FactoryWrite(Fake *self, void *, void *resource, void *record)
{
    Rec(self->id, 6);
    gTrace.push_back(Norm(resource));
    gTrace.push_back(Norm(record));
    return gResult[6];
}

uint32_t __fastcall RecordSetSize(Fake *self, void *, uint32_t size)
{
    Rec(self->id, 11);
    gTrace.push_back(size);
    return gResult[7];
}

uint32_t __fastcall Release(Fake *self, void *)
{
    Rec(self->id, 2);
    return 0x55;
}

void Setup()
{
    gSelf = {gSelfVT, 0xf1};
    gRes = {gResVT, 0xf2};
    gSeg = {gSegVT, 0xf3};
    gRec = {gRecVT, 0xf4};
    gFactory = {gFactoryVT, 0xf5};
    gSelfVT[24] = reinterpret_cast<void *>(&SelfFindFactory);
    gResVT[3] = reinterpret_cast<void *>(&ResGetKey);
    gSegVT[20] = reinterpret_cast<void *>(&SegTest);
    gSegVT[22] = reinterpret_cast<void *>(&SegOpen);
    gSegVT[23] = reinterpret_cast<void *>(&SegCreate);
    gSegVT[24] = reinterpret_cast<void *>(&SegClose);
    gSegVT[25] = reinterpret_cast<void *>(&SegAbort);
    gRecVT[2] = reinterpret_cast<void *>(&Release);
    gRecVT[11] = reinterpret_cast<void *>(&RecordSetSize);
    gFactoryVT[2] = reinterpret_cast<void *>(&Release);
    gFactoryVT[6] = reinterpret_cast<void *>(&FactoryWrite);
}

uint32_t Result(std::mt19937 &rng)
{
    uint32_t upper = rng() & 0xffffff00u;
    switch (rng() % 4) {
    case 0: return upper;
    case 1: return upper | 1;
    case 2: return upper | (rng() & 0xff);
    default: return 0;
    }
}

using Vf14Fn = uint8_t(__thiscall *)(void *, void *, const void *, void *);

} // namespace

DIFF_TEST(GZPersistResourceManager_WriteResource, 0x9b5610)
{
    auto vf14 = ctx.Original<Vf14Fn>(0x9b5610);
    Setup();
    std::mt19937 rng(14);
    uint32_t keyArg[3];
    gKeyArg = keyArg;
    for (int i = 0; i < 20000; ++i) {
        for (auto &r : gResult) r = Result(rng);
        for (auto &m : gOutMode) m = rng() % 4 == 0 ? (rng() % 2 ? 1u : 2u) : 0u;
        // A slot that succeeds without an object would make both sides call through null: a
        // succeeding open/create/find always hands one out.
        if (gResult[1] & 0xff) gOutMode[0] = 0;
        if (gResult[2] & 0xff) gOutMode[1] = 0;
        if (gResult[5] & 0xff) gOutMode[2] = 0;
        for (auto &k : gKey) k = rng();
        for (auto &k : keyArg) k = rng();

        gTrace.clear();
        uint8_t ra = vf14(&gSelf, &gRes, keyArg, &gSeg);
        std::vector<uint32_t> ta = gTrace;
        gTrace.clear();
        uint8_t rb = reinterpret_cast<cGZPersistResourceManager *>(&gSelf)->vf14(&gRes, keyArg, &gSeg);
        std::vector<uint32_t> tb = gTrace;
        ++ctx.cases;
        if (ra != rb || ta != tb)
            ctx.Fail("vf14 case %d: original returned %u (%zu trace words), ours %u (%zu)", i, ra, ta.size(), rb, tb.size());
    }
}
