// The scene graph's vertex-format helpers (src/sg/cGeometryNodeFormats.h): the device format 0x18daa0, the stream
// format 0x18d990 and the vertex copy 0xc627e0, each run on the image and ours against the same fakes (formats, the
// formats manager planted in its cache cell 0x10caa24, a device and its enumeration), comparing the call streams, the
// results and the bytes written.
#include "difftest.h"
#include "free_recorder.h"

#include "game/game.h"
#include "gz/cVertexFormat.h"
#include "sg/cGeometryNodeFormats.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using nGZGraphic4::cIDeviceDX;
using nGZGraphic4::cIVertexFormat;
using nGZGraphic4::cIVertexFormatsManager;
using nGZGraphic4::cVertexElement;

namespace {

std::string gLog;

void Note(const char *fmt, ...)
{
    char buf[160];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gLog += buf;
    gLog += '\n';
}

uint32_t Mix(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u) * 0x85ebca6bu ^ (c + 0x165667b1u) * 0xc2b2ae35u;
    h ^= h >> 15;
    h *= 0x27d4eb2fu;
    return h ^ (h >> 13);
}

// A format answering from tables; questions outside them get answers made from their arguments (never a crash).
class FakeFormat final : public cIVertexFormat {
public:
    int id = 0;
    uint32_t streamCount = 0;
    std::vector<std::vector<cVertexElement>> streams;
    std::vector<uint32_t> streamSizes;
    std::vector<std::vector<uint32_t>> elementSizes;
    std::vector<std::vector<uint32_t>> elementOffsets;
    std::vector<cVertexElement> finds; // what FindElement answers, in turn
    size_t nextFind = 0;
    uint32_t salt = 0;

    bool QueryInterface(uint32_t, void **) override { return Note("f%d qi", id), false; }
    uint32_t AddRef() override { return Note("f%d add", id), 2; }
    uint32_t Release() override { return Note("f%d rel", id), 1; }
    uint32_t GetId() override { return Note("f%d id", id), 0; }
    uint32_t GetStreamCount() override
    {
        Note("f%d streams", id);
        return streamCount;
    }
    uint32_t GetElementCount(uint32_t stream) override
    {
        Note("f%d count %u", id, stream);
        return stream < streams.size() ? uint32_t(streams[stream].size()) : Mix(stream, salt, 1) % 3;
    }
    uint32_t GetTotalElementCount() override { return Note("f%d total", id), 0; }
    cVertexElement *GetElement(cVertexElement *out, uint32_t stream, uint32_t i) override
    {
        Note("f%d element %u %u", id, stream, i);
        if (stream < streams.size() && i < streams[stream].size()) {
            *out = streams[stream][i];
        } else {
            uint32_t w[4] = {Mix(stream, i, salt), Mix(stream, i, salt + 1), Mix(stream, i, salt + 2) % 8, Mix(stream, i, salt + 3)};
            std::memcpy(out, w, sizeof(w));
        }
        return out;
    }
    cVertexElement *FindElement(cVertexElement *out, uint32_t stream, uint32_t usage, uint8_t index) override
    {
        Note("f%d find %u %08x %u", id, stream, usage, index);
        *out = finds.empty() ? cVertexElement{} : finds[nextFind++ % finds.size()];
        return out;
    }
    cVertexElement *FindElementAny(cVertexElement *out, uint32_t, uint8_t) override { return Note("f%d any", id), out; }
    int32_t GetMaxIndex(uint32_t) override { return Note("f%d max", id), 0; }
    uint32_t GetStreamSize(uint32_t stream) override
    {
        Note("f%d size %u", id, stream);
        return stream < streamSizes.size() ? streamSizes[stream] : Mix(stream, salt, 2) % 64;
    }
    uint32_t GetElementSize(uint32_t stream, uint32_t i) override
    {
        Note("f%d esize %u %u", id, stream, i);
        return stream < elementSizes.size() && i < elementSizes[stream].size() ? elementSizes[stream][i] : 0;
    }
    uint32_t GetElementOffset(uint32_t stream, int32_t i) override
    {
        Note("f%d offset %u %d", id, stream, i);
        return stream < elementOffsets.size() && uint32_t(i) < elementOffsets[stream].size() ? elementOffsets[stream][i]
                                                                                             : Mix(stream, i, salt) % 32;
    }
    uint32_t GetOffset(uint32_t, uint32_t, uint8_t) override { return Note("f%d off", id), 0; }
    uint32_t GetOffsetAny(uint32_t, uint8_t) override { return Note("f%d offany", id), 0; }
    void Describe(cIGZString *) override { Note("f%d describe", id); }
};

// The formats manager: records the elements added; EndFormat gives the format the test set up.
class FakeManager final : public cIVertexFormatsManager {
public:
    cIVertexFormat *built = nullptr;
    cIVertexFormat **expectOut = nullptr;

    bool QueryInterface(uint32_t, void **) override { return Note("m qi"), false; }
    uint32_t AddRef() override { return Note("m add"), 2; }
    uint32_t Release() override { return Note("m rel"), 1; }
    void BeginFormat() override { Note("m begin"); }
    void AddElement(uint32_t stream, uint32_t type, uint32_t usage, uint8_t index) override
    {
        Note("m add %u %u %08x %u", stream, type, usage, index);
    }
    void EndFormat(cIVertexFormat **out) override
    {
        Note("m end %d", out == expectOut);
        *out = built;
    }
    bool IsBuilding() override { return Note("m building"), false; }
    uint32_t GetFormatCount() override { return Note("m formats"), 0; }
    cIVertexFormat *GetFormat(uint32_t) override { return Note("m get"), nullptr; }
};

// The device (slot 15, the enumeration) and the enumeration (slots 20 and 21), as raw tables: only those slots are
// reached.
struct FakeEnum {
    void *const *vt;
    uint32_t cap = 0;     // HasProcessingCap's whole eax (only al is the answer)
    uint32_t limits[16] = {};
};
struct FakeDevice {
    void *const *vt;
    FakeEnum *enumeration = nullptr;
};

uint32_t __fastcall EnumCap(FakeEnum *self, void *, uint32_t index)
{
    Note("e cap %u", index);
    return self->cap;
}
uint32_t __fastcall EnumLimit(FakeEnum *self, void *, uint32_t index)
{
    Note("e limit %u", index);
    return self->limits[index & 15];
}
FakeEnum *__fastcall DeviceEnum(FakeDevice *self, void *)
{
    Note("d enum");
    return self->enumeration;
}
void *gEnumVt[30];
void *gDeviceVt[64];

void InitTables()
{
    gEnumVt[20] = reinterpret_cast<void *>(&EnumCap);
    gEnumVt[21] = reinterpret_cast<void *>(&EnumLimit);
    gDeviceVt[15] = reinterpret_cast<void *>(&DeviceEnum);
}

struct PlantFormats {
    uint32_t *cell;
    uint32_t saved;
    explicit PlantFormats(cIVertexFormatsManager *manager)
    {
        cell = GameAddress<uint32_t>(0x10caa24);
        saved = *cell;
        *cell = uint32_t(reinterpret_cast<uintptr_t>(manager));
    }
    ~PlantFormats() { *cell = saved; }
};

// A format with random streams of random elements (types weighted towards the four-byte one).
void RandomFormat(std::mt19937 &rng, FakeFormat &f, int id, uint32_t maxStreams)
{
    f.id = id;
    f.salt = rng();
    const uint32_t n = rng() % (maxStreams + 1);
    f.streams.assign(n, {});
    for (auto &s : f.streams) {
        s.resize(rng() % 5);
        for (cVertexElement &e : s) {
            uint32_t w[4] = {rng(), rng(), rng() % 3 == 0 ? 4u : rng() % 8, rng()};
            std::memcpy(&e, w, sizeof(w));
        }
    }
    f.streamCount = n;
    const uint32_t odd = rng() % 16;
    if (odd == 0) f.streamCount = 0xffffffffu; // negative: no streams
    if (odd == 1) f.streamCount = n + 1;       // one past the tables
}

} // namespace

// ---- 0x18daa0 ---------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_GeometryFormats_Device, 0x18daa0)
{
    using Fn = bool(__cdecl *)(cIVertexFormat **, void *, cIVertexFormat *);
    auto original = ctx.Original<Fn>(0x18daa0);
    InitTables();
    gLog.reserve(1 << 16);
    std::mt19937 rng(0x18daa0);
    int results[2] = {}, noStreams = 0, tooWide = 0, colours = 0, packed = 0;
    for (uint32_t iter = 0; iter < 3000; ++iter) {
        FakeFormat format, built;
        RandomFormat(rng, format, 1, 5);
        RandomFormat(rng, built, 2, 4);
        built.streamSizes.resize(built.streams.size());
        for (uint32_t &s : built.streamSizes) s = rng() % 4 == 0 ? rng() : rng() % 80;
        FakeEnum enumeration;
        enumeration.vt = gEnumVt;
        enumeration.cap = rng() % 2 ? rng() & ~0xffu : rng();
        for (uint32_t &l : enumeration.limits) l = rng();
        const uint32_t pick = rng() % 10;
        enumeration.limits[0xa] = pick == 0 ? 0 : pick == 1 ? 0xffffffffu : pick == 2 ? 0x80000000u : 1 + rng() % 4;
        enumeration.limits[0xe] = rng() % 4 == 0 ? 0xfffffff0u + rng() % 16 : rng() % 100;
        FakeDevice device{gDeviceVt, &enumeration};
        FakeManager manager;
        manager.built = &built;
        PlantFormats plant(&manager);
        cIVertexFormat *out[2] = {reinterpret_cast<cIVertexFormat *>(uintptr_t(0x1234)),
                                  reinterpret_cast<cIVertexFormat *>(uintptr_t(0x1234))};
        gLog.clear();
        manager.expectOut = &out[0];
        const bool a = original(&out[0], &device, &format);
        const std::string logA = gLog;
        gLog.clear();
        manager.expectOut = &out[1];
        const bool b = nGZSceneGraph::MakeDeviceVertexFormat(&out[1], reinterpret_cast<cIDeviceDX *>(&device), &format);
        ++ctx.cases;
        ++results[a];
        noStreams += enumeration.limits[0xa] == 0;
        tooWide += !a && enumeration.limits[0xa] != 0;
        colours += (enumeration.cap & 0xff) == 0 && logA.find("m add") != std::string::npos;
        packed += logA.find("m add 2 ") != std::string::npos;
        if (a != b || logA != gLog || out[0] != out[1])
            ctx.Fail("iter %u: result %d/%d out equal %d\noriginal\n%sours\n%s", iter, a, b, out[0] == out[1], logA.c_str(),
                     gLog.c_str());
    }
    if (results[0] < 300 || results[1] < 300 || noStreams < 100 || tooWide < 300 || colours < 300 || packed < 100)
        ctx.Fail("coverage: false %d true %d no streams %d too wide %d colours %d stream 2 %d", results[0], results[1], noStreams,
                 tooWide, colours, packed);
}

// ---- 0x18d990 ---------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_GeometryFormats_Stream, 0x18d990)
{
    using Fn = void(__cdecl *)(cIVertexFormat **, cIVertexFormat *, int32_t, int32_t);
    auto original = ctx.Original<Fn>(0x18d990);
    gLog.reserve(1 << 16);
    std::mt19937 rng(0x18d990);
    int built = 0, skipped = 0, elements = 0;
    for (uint32_t iter = 0; iter < 2000; ++iter) {
        FakeFormat format, made;
        RandomFormat(rng, format, 1, 4);
        made.id = 2;
        FakeManager manager;
        manager.built = &made;
        PlantFormats plant(&manager);
        const int32_t to = rng() % 4 == 0 ? int32_t(rng()) : int32_t(rng() % 4);
        const int32_t from = rng() % 6 == 0 ? -1 - int32_t(rng() % 3) : int32_t(rng() % 6);
        cIVertexFormat *out[2] = {nullptr, nullptr};
        gLog.clear();
        manager.expectOut = &out[0];
        original(&out[0], &format, to, from);
        const std::string logA = gLog;
        gLog.clear();
        manager.expectOut = &out[1];
        nGZSceneGraph::MakeStreamVertexFormat(&out[1], &format, to, from);
        ++ctx.cases;
        const bool did = out[0] != nullptr;
        built += did;
        skipped += !did;
        elements += did && logA.find("m add") != std::string::npos;
        if (logA != gLog || out[0] != out[1])
            ctx.Fail("iter %u (to %d from %d): out equal %d\noriginal\n%sours\n%s", iter, to, from, out[0] == out[1], logA.c_str(),
                     gLog.c_str());
    }
    if (built < 300 || skipped < 300 || elements < 300)
        ctx.Fail("coverage: built %d skipped %d with elements %d", built, skipped, elements);
}

// ---- 0xc627e0 ---------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_GeometryFormats_Copy, 0xc627e0)
{
    using Fn = uint32_t(__cdecl *)(const void *, uint32_t, cIVertexFormat *, uint32_t, void *, uint32_t, cIVertexFormat *,
                                   uint32_t, uint32_t);
    auto original = ctx.Original<Fn>(0xc627e0);
    gLog.reserve(1 << 16);
    std::mt19937 rng(0xc627e0);
    int copied = 0, empty = 0, large = 0, partial = 0;
    for (uint32_t iter = 0; iter < 2000; ++iter) {
        const uint32_t mode = iter % 20;
        // The destination format: stream `ds` of `stride` bytes, elements of sizes that fit it (not always whole words).
        FakeFormat dst, src;
        dst.id = 1;
        src.id = 2;
        dst.salt = rng();
        src.salt = rng();
        const uint32_t ds = rng() % 3, ss = rng() % 3;
        uint32_t stride = mode == 0 ? 0x1000 + 4 * (rng() % 64) : mode == 1 ? rng() % 4 : 4 * (1 + rng() % 16) + (rng() % 4 == 0 ? rng() % 4 : 0);
        dst.streamSizes.assign(3, 0);
        dst.streamSizes[ds] = stride;
        dst.streams.assign(3, {});
        dst.elementSizes.assign(3, {});
        uint32_t room = stride >> 2;
        while (rng() % 6 != 0) {
            const uint32_t words = room == 0 ? 0 : rng() % (room + 1);
            const uint32_t size = mode == 1 ? rng() % 4 : words * 4 + (rng() % 5 == 0 ? rng() % 4 : 0);
            room -= size >> 2;
            cVertexElement e;
            uint32_t w[4] = {rng(), rng(), rng() % 8, rng()};
            std::memcpy(&e, w, sizeof(w));
            dst.streams[ds].push_back(e);
            dst.elementSizes[ds].push_back(size);
        }
        partial += room != 0;
        src.elementOffsets.assign(3, {});
        src.elementOffsets[ss].resize(8);
        for (uint32_t &o : src.elementOffsets[ss]) o = rng() % 61;
        src.finds.resize(1 + rng() % 4);
        for (cVertexElement &e : src.finds) {
            uint32_t w[4] = {rng(), rng(), rng(), rng()};
            std::memcpy(&e, w, sizeof(w));
            e.position = uint8_t(rng() % (rng() % 8 == 0 ? 256 : 8));
        }
        const uint32_t count = mode == 0 ? rng() % 3 : rng() % 9;
        const uint32_t srcStart = mode == 0 ? 0 : rng() % 9, dstStart = mode == 0 ? 0 : rng() % 9;
        std::vector<uint8_t> source((srcStart + count) * stride + 128);
        for (uint8_t &x : source) x = uint8_t(rng());
        std::vector<uint8_t> out[2];
        out[0].resize((dstStart + count) * stride + 64);
        for (uint8_t &x : out[0]) x = uint8_t(rng());
        out[1] = out[0];
        gLog.clear();
        src.nextFind = 0;
        difftest::AllocationCounter countA;
        const uint32_t a = original(source.data(), srcStart, &src, ss, out[0].data(), dstStart, &dst, ds, count);
        countA.Stop();
        const std::string logA = gLog;
        gLog.clear();
        src.nextFind = 0;
        difftest::AllocationCounter countB;
        const uint32_t b = nGZSceneGraph::CopyVertexStream(source.data(), srcStart, &src, ss, out[1].data(), dstStart, &dst, ds, count);
        countB.Stop();
        ++ctx.cases;
        copied += count != 0 && stride >= 4;
        empty += count == 0;
        large += mode == 0 && count != 0;
        if (a != b || logA != gLog || out[0] != out[1] || countA.Blocks() != countB.Blocks() || countA.Bytes() != countB.Bytes())
            ctx.Fail("iter %u (stride %u count %u): result %u/%u bytes equal %d blocks %lld/%lld\noriginal\n%sours\n%s", iter, stride,
                     count, a, b, out[0] == out[1], countA.Blocks(), countB.Blocks(), logA.c_str(), gLog.c_str());
    }
    if (copied < 1000 || empty < 100 || large < 50 || partial < 300)
        ctx.Fail("coverage: copied %d empty %d large %d with spare words %d", copied, empty, large, partial);
}
