// Differential tests for nTSCASCore::cTSBinInfoFactory Read 0xb8ddb0 / Write 0xb90e10 (src/cas/cTSBinInfoFactory.cpp,
// lane DU). Real pieces: the temporary cGZPropertySetResourceString and its serializer (reading / writing a binary
// record through the byte-stream fake of tests/gz_property_set_serializer.h). Fakes (tests/ts_services_fixture.h): the
// record (QI for its stream, slot 3 key, slot 15 segment), the persist system (cell 0x10faf6c: slot 5 GetResource for
// the 3IDR list, slot 3 the resource manager whose slot 34 finds the segment), the key list (slots 4, 5, 7), the
// director's framework (slot 3 makes a list), the resource (QI for the CAS face) and the CAS object, whose load / save
// slots log the set's contents (save also fills it). Compared: result, every fake call with its arguments, reference
// counts, the stream calls and the bytes written.
//
// Left out: Write with no list at all (GetResource succeeding without one): the original calls slot 7 of a
// null list (0xb910c5); a real persist system fills the pointer when it succeeds, and the framework fake always makes one.
#include "difftest.h"
#include "gz_property_set_serializer.h"
#include "ts_services_fixture.h"

#include "cas/cTSBinInfoFactory.h"

#include <cstring>
#include <vector>

namespace {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{0, "ddd"}, {2, "dd"}, {10, "dd"}, {11, "dd"}};

template <class Fn> Fn Game(uint32_t rva) { return reinterpret_cast<Fn>(GameAddress<void>(rva)); }

using svcfx::F;
using svcfx::Obj;

uint32_t gKey[4];

// The CAS resource: load (slots 25 / 29 / 87) and save (24 / 28 / 86) log the slot, the set's contents, the list word
// and the segment; save first puts two properties into the set.
struct Cas {
    void **vt;
};
void LogSet(void *set)
{
    const psfake::Blob dump = psfake::DumpSet(set);
    svcfx::Ev(uint32_t(dump.size()));
    svcfx::Ev(psfake::Hash(dump.data(), dump.size() * 4));
}
template <int Slot> void __fastcall CasLoad(Cas *, void *, void *set, uint32_t list, void *segment)
{
    svcfx::Ev(0x900 + Slot);
    LogSet(set);
    svcfx::Ev(list);
    svcfx::Ev(svcfx::Norm(uint32_t(uintptr_t(segment))));
}
template <int Slot> void __fastcall CasSave(Cas *, void *, void *set, uint32_t list)
{
    svcfx::Ev(0x900 + Slot);
    LogSet(set);
    svcfx::Ev(list);
    InlineAny value{GameAddress<const void>(gzany::kUint32VTable), 0x1234u + Slot};
    GZServiceString key("binidx");
    VirtualSlot<uint8_t(__thiscall *)(void *, void *, void *)>(set, 3)(set, &key, &value);
    InlineAny flag{GameAddress<const void>(gzany::kBooleanVTable), 1};
    GZServiceString key2("Flag");
    VirtualSlot<uint8_t(__thiscall *)(void *, void *, void *)>(set, 3)(set, &key2, &flag);
}
uint32_t __fastcall CasRelease(Cas *, void *) { svcfx::Ev(0x9ff); return 0; }
void *gCasVT[90];
Cas gCas{gCasVT};

uint32_t RecordQI(Obj *self, uint32_t *args)
{
    if (args[0] != self->value[0] || !self->value[1]) return 0;
    *reinterpret_cast<void **>(args[1]) = &psfake::gStream;
    return 1;
}
uint32_t RecordSegment(Obj *self, uint32_t *args)
{
    if (self->value[2]) *reinterpret_cast<void **>(args[1]) = &F(7);
    return 1;
}
uint32_t GetResource(Obj *self, uint32_t *args) // persist slot 5
{
    const uint32_t *key = reinterpret_cast<const uint32_t *>(args[0]);
    for (int i = 0; i < 4; ++i) svcfx::Ev(key[i]);
    if (self->value[0]) *reinterpret_cast<void **>(args[2]) = &F(8);
    return self->value[1];
}
uint32_t FindSegment(Obj *self, uint32_t *args) // manager slot 34
{
    if (self->value[0]) *reinterpret_cast<void **>(args[1]) = &F(9);
    return self->value[1];
}
uint32_t SetKey(Obj *, uint32_t *args)
{
    const uint32_t *key = reinterpret_cast<const uint32_t *>(args[0]);
    for (int i = 0; i < 4; ++i) svcfx::Ev(key[i]);
    return 0;
}
uint32_t FrameworkCreate(Obj *, uint32_t *args)
{
    *reinterpret_cast<void **>(args[2]) = &F(8);
    return 1;
}
uint32_t ResourceQI(Obj *self, uint32_t *args)
{
    if (args[0] != self->value[0] || !self->value[1]) return 0;
    *reinterpret_cast<void **>(args[1]) = &gCas;
    return 1;
}

void Setup(uint32_t type, uint32_t streamIid, uint32_t mask, uint32_t casIid)
{
    svcfx::ResetAll();
    gCasVT[2] = reinterpret_cast<void *>(&CasRelease);
    gCasVT[24] = reinterpret_cast<void *>(&CasSave<24>);
    gCasVT[28] = reinterpret_cast<void *>(&CasSave<28>);
    gCasVT[86] = reinterpret_cast<void *>(&CasSave<86>);
    gCasVT[25] = reinterpret_cast<void *>(&CasLoad<25>);
    gCasVT[29] = reinterpret_cast<void *>(&CasLoad<29>);
    gCasVT[87] = reinterpret_cast<void *>(&CasLoad<87>);
    gKey[0] = type;
    gKey[1] = 0x7f00aa01;
    gKey[2] = 0x55;
    gKey[3] = 0x66;
    svcfx::AddRange(gKey, sizeof(gKey), 0x6e000000);
    svcfx::AddRange(&gCas, sizeof(gCas), 0x6c000000);
    svcfx::AddRange(&psfake::gStream, sizeof(psfake::gStream), 0x6d000000);
    F(0).Set(0, "dp", 0, &RecordQI); // record
    F(0).value[0] = streamIid;
    F(0).value[1] = mask & 1;
    F(0).value[2] = mask & 2;
    F(0).Set(3, 0, gKey);
    F(0).Set(15, "dp", 0, &RecordSegment);
    F(10).Set(5, "pdp--", 0, &GetResource); // persist system
    F(10).value[0] = mask & 4;
    F(10).value[1] = (mask & 8) ? 1 : 0;
    F(10).Set(3, 0, &F(11));
    F(11).Set(34, "pp", 0, &FindSegment); // resource manager
    F(11).value[0] = mask & 16;
    F(11).value[1] = (mask & 32) ? 1 : 0;
    F(8).Set(7, 0, 0x7777); // key list
    F(8).Set(4, "p", 0u);
    F(8).Set(5, 0, &F(12));
    F(12).Set(4, "p", 0, &SetKey);
    F(13).Set(3, "ddp", 0, &FrameworkCreate); // framework
    svcfx::gFrameworkIndex = 13;
    F(2).Set(0, "dp", 0, &ResourceQI); // resource
    F(2).value[0] = casIid;
    F(2).value[1] = (mask & 64) ? 1 : 0;
}

psfake::Blob Capture(uint8_t ok)
{
    psfake::Blob b = svcfx::gEv;
    svcfx::CaptureFakes(b, kValueKinds);
    b.insert(b.end(), psfake::g.log.begin(), psfake::g.log.end());
    b.push_back(uint32_t(psfake::g.out.size()));
    b.push_back(psfake::Hash(psfake::g.out.data(), psfake::g.out.size()));
    b.push_back(uint32_t(psfake::gRecord.refs));
    b.push_back(ok);
    return b;
}

std::vector<uint8_t> BinxRecord()
{
    psfake::Bytes r;
    r.U32(gzpropser::kBinaryMagic);
    r.U16(2);
    r.U32(3);
    r.U32(gzany::kTypeUint32);
    r.Text("binidx");
    r.U32(7);
    r.U32(gzany::kTypeString);
    r.Text("creatorid");
    r.Text("00000000-0000-0000-0000-000000000000");
    r.U32(gzany::kTypeUint32);
    r.Text("sortindex");
    r.U32(0xffffffff);
    return r.b;
}

constexpr uint32_t kTypes[] = {nTSCASCore::cTSBinInfoFactory::kTypeSimFragment, nTSCASCore::cTSBinInfoFactory::kTypeBinInfo,
                               nTSCASCore::cTSBinInfoFactory::kTypeOutfitData, 0x12345678};
constexpr uint32_t kIids[] = {nTSCASCore::cTSBinInfoFactory::kIidSimFragment, nTSCASCore::cTSBinInfoFactory::kIidBinInfo,
                              nTSCASCore::cTSBinInfoFactory::kIidOutfitData, 0x1};

} // namespace

DIFF_TEST(BinInfoFactory_Read, 0xb8ddb0)
{
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    globals.Set(0x10faf6c, &F(10));
    const std::vector<uint8_t> record = BinxRecord();
    for (int t = 0; t < 4; ++t)
        for (uint32_t mask = 0; mask < 128; ++mask)
            for (int truncate = 0; truncate < 2; ++truncate) {
                if (!(mask & 1) && truncate) continue;
                psfake::Blob runs[2];
                for (int side = 0; side < 2; ++side) {
                    Setup(kTypes[t], 0x051b11bd, mask, kIids[t]);
                    const std::vector<uint8_t> data = truncate ? std::vector<uint8_t>(record.begin(), record.end() - 3) : record;
                    psfake::Begin(true, data, -1, true);
                    uint8_t factory[0xc] = {};
                    const uint8_t ok = side == 0 ? Game<uint8_t(__thiscall *)(void *, void *, void *)>(0xb8ddb0)(factory, &F(2), &F(0))
                                                 : reinterpret_cast<nTSCASCore::cTSBinInfoFactory *>(factory)->Read(&F(2), &F(0));
                    runs[side] = Capture(ok);
                }
                ++ctx.cases;
                if (runs[0] != runs[1])
                    ctx.Fail("type %08x mask %02x truncate %d: %zu/%zu words, ok %u/%u", kTypes[t], mask, truncate, runs[0].size(), runs[1].size(),
                             runs[0].back(), runs[1].back());
            }
}

DIFF_TEST(BinInfoFactory_Write, 0xb90e10)
{
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    globals.Set(0x10faf6c, &F(10));
    for (int t = 0; t < 4; ++t)
        for (uint32_t mask = 0; mask < 128; ++mask)
            for (int failAt = -1; failAt < 3; ++failAt) {
                if ((mask & 16) || (mask & 32)) continue; // the manager is not used by Write
                // GetResource succeeding without handing out a list leaves it null: the original then calls slot 7 of
                // null (0xb910c5). A real persist system always fills the pointer when it succeeds.
                if ((mask & 8) && !(mask & 4)) continue;
                psfake::Blob runs[2];
                for (int side = 0; side < 2; ++side) {
                    Setup(kTypes[t], 0xc51b11cb, mask, kIids[t]);
                    psfake::Begin(false, {}, failAt, (mask & 2) != 0);
                    uint8_t factory[0xc] = {};
                    const uint8_t ok = side == 0 ? Game<uint8_t(__thiscall *)(void *, void *, void *)>(0xb90e10)(factory, &F(2), &F(0))
                                                 : reinterpret_cast<nTSCASCore::cTSBinInfoFactory *>(factory)->Write(&F(2), &F(0));
                    runs[side] = Capture(ok);
                }
                ++ctx.cases;
                if (runs[0] != runs[1])
                    ctx.Fail("type %08x mask %02x fail %d: %zu/%zu words, ok %u/%u", kTypes[t], mask, failAt, runs[0].size(), runs[1].size(),
                             runs[0].back(), runs[1].back());
            }
}
