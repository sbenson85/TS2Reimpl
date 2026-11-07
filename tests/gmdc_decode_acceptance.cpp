// Milestone N-e's acceptance (docs/subsystems/scenegraph.md, "Milestone N"): the original GMDC decoder (the container
// 0xa70aa0 makes, its Read 0xa711b0) and ours (cGeometryDataContainer::Create, its Read) over real GMDC resources, compared
// by the canonical summary standalone/gmdc_decode prints (standalone/gmdc_summary.h).
//
// The resources are read at test time from the game copy's packages (local/game, read only; never committed). Without
// the copy the test prints SKIP and runs no case. Each seed is one resource of kResources: the package's DBPF index, its
// DIR list (which entries are QFS-compressed), the QFS decompression and the RCOL header (links, class ids) are this
// file's own code, not the game's or ours, so the bytes each decoder reads come from an independent reader. The root
// block after the header is what ReadRoot hands the container's Read; here each side's container reads it through our
// cGZPersistBinaryReader over a memory stream (as tests/sg_geometry_data_class.cpp does), with the triangle sets the
// director makes coming from the geometry library's factory 0x196560 on both sides.
//
// Per seed each side notes: the bytes consumed, the summary's seven counts and hashes, the releases and blocks left. For
// each resource the original's summary is printed as `gmdc-expected <package> <instance> <group> <resource> <line>`,
// the lines gmdc_decode's `summary` output is compared with (tools: the lane's comparison script, the doc's procedure).
#include "../standalone/gmdc_summary.h"

#include "code_patch.h"
#include "difftest.h"
#include "sgfx.h"

#include "gz/cGZPersistBinaryReader.h"
#include "hook/hooks.h"
#include "rz/StaticGetters.h"
#include "sg/cGeometryDataContainerClass.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace nGZSceneGraph;

constexpr uint32_t kFactory = 0xa70aa0;
constexpr uint32_t kMeshFactory = 0x196560;
constexpr uint32_t kGmdcType = 0xac4f8687, kDirType = 0xe86b1eef;
using FactoryFn = cISGResource *(__cdecl *)();
using MeshFactoryFn = cIGZUnknown *(__cdecl *)();

// ---- the resources: (package under local/game, group, instance, resource), picked across sizes ----
struct Pick {
    const char *package;
    uint32_t group, instance, resource;
};
constexpr const char *kLightRigs = "Base\\TSData\\Res\\Sims3D\\LightRigs.package";
constexpr const char *kCASFace = "Base\\TSData\\Res\\Sims3D\\CASFace.package";
constexpr const char *kObjects03 = "Base\\TSData\\Res\\Sims3D\\Objects03.package";
constexpr const char *kSims03 = "Base\\TSData\\Res\\Sims3D\\Sims03.package";
constexpr uint32_t kGroup = 0x1c0532fa;
constexpr Pick kPicks[] = {
    {kLightRigs, kGroup, 0xffe3c36b, 0x483bff87}, {kLightRigs, kGroup, 0xff42274d, 0x6149b51d},
    {kLightRigs, kGroup, 0xff12ea08, 0xc4251ce5}, {kCASFace, kGroup, 0xff0fe939, 0x13f96fc1},
    {kCASFace, kGroup, 0xff192a1f, 0x0178ceaa},   {kCASFace, kGroup, 0xffaa408d, 0xfdd5d310},
    {kCASFace, kGroup, 0xffac89d7, 0xbdcc15bc},   {kCASFace, kGroup, 0xff8ed2de, 0x22f2d873},
    {kCASFace, kGroup, 0xfffebfab, 0x7bbeba17},   {kObjects03, kGroup, 0xff6861b9, 0x63cd0918},
    {kObjects03, kGroup, 0xffdccb8e, 0xcc2d8a2e}, {kObjects03, kGroup, 0xff3d96d3, 0x4f565bf7},
    {kObjects03, kGroup, 0xff4dfa7c, 0x42a0af61}, {kObjects03, kGroup, 0xfff4fc32, 0x0449d970},
    {kObjects03, kGroup, 0xff08ebce, 0xc3252dfb}, {kObjects03, kGroup, 0xff77529d, 0x9217c717},
    {kObjects03, kGroup, 0xff286d94, 0x7156e389}, {kObjects03, kGroup, 0xffcb656d, 0xa314bd75},
    {kObjects03, kGroup, 0xff706d4a, 0xa635438c}, {kObjects03, kGroup, 0xff84ddbe, 0xd1201003},
    {kObjects03, kGroup, 0xffd92cf4, 0xc01c89d2}, {kObjects03, kGroup, 0xffa8cfdb, 0xb1f8ecc6},
    {kObjects03, kGroup, 0xffb8fdcd, 0xa675cd2d}, {kSims03, kGroup, 0xffa34b22, 0xc4428cf4},
    {kSims03, kGroup, 0xff9dd24f, 0x9312bc67},    {kSims03, kGroup, 0xff247f04, 0xb6ff7a30},
    {kSims03, kGroup, 0xffd983a3, 0xb7955633},    {kSims03, kGroup, 0xfff4c0bb, 0xe4154105},
    {kSims03, kGroup, 0xff848829, 0x69e8d76e},    {kSims03, kGroup, 0xffb9c2e6, 0xb8197eea},
    {kSims03, kGroup, 0xff13f34b, 0xbe74c5da},    {kSims03, kGroup, 0xff6e9c8f, 0xf4694545},
};
constexpr unsigned kResources = sizeof(kPicks) / sizeof(kPicks[0]);

// The game copy: local\game above the test exe, or in the main checkout when the exe is in a worktree.
std::wstring GameDir()
{
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    for (int up = 0; up < 8; ++up) {
        const size_t slash = dir.find_last_of(L"\\/");
        if (slash == std::wstring::npos) break;
        dir.resize(slash);
        const std::wstring probe = dir + L"\\local\\game";
        if (GetFileAttributesW((probe + L"\\Base").c_str()) != INVALID_FILE_ATTRIBUTES) return probe;
    }
    const size_t at = std::wstring(exe).find(L"\\.claude\\worktrees\\");
    if (at != std::wstring::npos) {
        const std::wstring probe = std::wstring(exe).substr(0, at) + L"\\local\\game";
        if (GetFileAttributesW((probe + L"\\Base").c_str()) != INVALID_FILE_ATTRIBUTES) return probe;
    }
    return L"";
}

// `n` bytes of a file from `at` (the packages are large: only the header, the index, the DIR list and the picked
// resources are read).
bool ReadRange(const std::wstring &path, uint32_t at, uint32_t n, std::vector<uint8_t> &out)
{
    FILE *f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
    out.resize(n);
    const bool ok = std::fseek(f, long(at), SEEK_SET) == 0 && std::fread(out.data(), 1, n, f) == n;
    std::fclose(f);
    return ok;
}

uint32_t Word(const std::vector<uint8_t> &b, size_t at)
{
    uint32_t v = 0;
    if (at + 4 <= b.size()) std::memcpy(&v, b.data() + at, 4);
    return v;
}

// QFS (RefPack): a 4-byte compressed size, 0x10 0xfb, the 3-byte big-endian decompressed size, then the codes.
bool Decompress(const uint8_t *in, size_t n, std::vector<uint8_t> &out)
{
    if (n < 9 || in[4] != 0x10 || in[5] != 0xfb) return false;
    const size_t size = size_t(in[6]) << 16 | size_t(in[7]) << 8 | in[8];
    out.clear();
    out.reserve(size);
    size_t at = 9;
    while (at < n) {
        const uint32_t b0 = in[at];
        uint32_t plain = 0, copy = 0, offset = 0;
        bool stop = false;
        if (b0 < 0x80) {
            if (at + 2 > n) return false;
            const uint32_t b1 = in[at + 1];
            plain = b0 & 3;
            copy = ((b0 >> 2) & 7) + 3;
            offset = ((b0 & 0x60) << 3) + b1 + 1;
            at += 2;
        } else if (b0 < 0xc0) {
            if (at + 3 > n) return false;
            const uint32_t b1 = in[at + 1], b2 = in[at + 2];
            plain = b1 >> 6;
            copy = (b0 & 0x3f) + 4;
            offset = ((b1 & 0x3f) << 8) + b2 + 1;
            at += 3;
        } else if (b0 < 0xe0) {
            if (at + 4 > n) return false;
            const uint32_t b1 = in[at + 1], b2 = in[at + 2], b3 = in[at + 3];
            plain = b0 & 3;
            copy = ((b0 & 0x0c) << 6) + b3 + 5;
            offset = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1;
            at += 4;
        } else if (b0 < 0xfc) {
            plain = ((b0 & 0x1f) << 2) + 4;
            at += 1;
        } else {
            plain = b0 & 3;
            stop = true;
            at += 1;
        }
        if (at + plain > n) return false;
        out.insert(out.end(), in + at, in + at + plain);
        at += plain;
        if (copy) {
            if (offset > out.size()) return false;
            const size_t from = out.size() - offset;
            for (uint32_t i = 0; i < copy; ++i) out.push_back(out[from + i]);
        }
        if (stop) break;
    }
    return out.size() == size;
}

// A package's resource by key, decompressed when its DIR entry lists it. Index entries are 24 bytes (type, group,
// instance, resource, offset, size) or 20 (no resource); DIR entries likewise 20 or 16 (no resource), the last word the
// decompressed size.
struct Package {
    std::wstring path;
    size_t entrySize = 0;
    std::vector<uint32_t> index; // the entries' words
    std::vector<uint32_t> dir;   // the compressed keys: type, group, instance, resource

    bool Load(const std::wstring &file)
    {
        path = file;
        index.clear();
        dir.clear();
        std::vector<uint8_t> header, bytes;
        if (!ReadRange(path, 0, 0x60, header) || Word(header, 0) != 0x46504244) return false; // "DBPF"
        const uint32_t count = Word(header, 0x24), at = Word(header, 0x28), size = Word(header, 0x2c);
        if (count == 0 || size % count != 0 || !ReadRange(path, at, size, bytes)) return false;
        entrySize = size / count;
        if (entrySize != 24 && entrySize != 20) return false;
        const size_t words = entrySize / 4;
        index.resize(size_t(count) * words);
        std::memcpy(index.data(), bytes.data(), size);
        for (size_t e = 0; e < count; ++e) {
            const uint32_t *w = &index[e * words];
            if (w[0] != kDirType) continue;
            if (!ReadRange(path, w[words - 2], w[words - 1], bytes)) return false;
            const size_t dirWords = words - 1; // the key, then the decompressed size
            for (size_t k = 0; k + dirWords * 4 <= bytes.size(); k += dirWords * 4) {
                uint32_t key[4] = {0, 0, 0, 0};
                std::memcpy(key, bytes.data() + k, (dirWords - 1) * 4);
                dir.insert(dir.end(), key, key + 4);
            }
        }
        return true;
    }

    bool Find(uint32_t type, const Pick &p, std::vector<uint8_t> &out) const
    {
        const size_t words = entrySize / 4;
        const bool hasResource = entrySize == 24;
        for (size_t e = 0; e * words < index.size(); ++e) {
            const uint32_t *w = &index[e * words];
            if (w[0] != type || w[1] != p.group || w[2] != p.instance || (hasResource && w[3] != p.resource)) continue;
            std::vector<uint8_t> bytes;
            if (!ReadRange(path, w[words - 2], w[words - 1], bytes)) return false;
            bool compressed = false;
            for (size_t k = 0; k < dir.size(); k += 4)
                if (dir[k] == type && dir[k + 1] == p.group && dir[k + 2] == p.instance &&
                    (!hasResource || dir[k + 3] == p.resource))
                    compressed = true;
            if (compressed) return Decompress(bytes.data(), bytes.size(), out);
            out = std::move(bytes);
            return true;
        }
        return false;
    }
};

// The RCOL header ReadRoot reads: an optional 0xffff0000 | version word, the link count, the links (group, instance,
// resource from version 1, type), the class id count and ids. Returns the root block's offset, or 0 if it is not one
// GMDC block.
size_t RootBlock(const std::vector<uint8_t> &b)
{
    size_t at = 0;
    uint32_t version = 0, links = Word(b, at);
    at += 4;
    if ((links & 0xffff0000u) == 0xffff0000u) {
        version = links & 0xffff;
        links = Word(b, at);
        at += 4;
    }
    at += size_t(links) * (version >= 1 ? 16 : 12);
    const uint32_t blocks = Word(b, at);
    at += 4;
    if (blocks != 1 || Word(b, at) != kGmdcType || at + 4 > b.size()) return 0;
    return at + 4;
}

std::vector<std::vector<uint8_t>> gBlocks; // per seed, the root block
std::vector<std::string> gExpected;        // per seed, the original's summary line

// ---- the memory stream our reader reads ----
const std::vector<uint8_t> *gBytes;
size_t gAt;

bool TakeBytes(void *p, size_t n)
{
    if (gBytes->size() - gAt < n) {
        drawfx::Note("short read", uint32_t(gAt));
        return false;
    }
    std::memcpy(p, gBytes->data() + gAt, n);
    gAt += n;
    return true;
}

#define PA_BAD(n) \
    virtual void Bad##n() { drawfx::Note("bad slot " #n, n); }

// A cIGZIStream over the package bytes: the slots the reader calls (cGZPersistBinaryReader.h), numbers little-endian as
// in the files. Texts go into the string through its FromChar (slot 3: text, length).
struct MemIStream {
    PA_BAD(0) PA_BAD(1) PA_BAD(2) PA_BAD(3) PA_BAD(4)
    virtual bool GetUint8(uint8_t *d) { return TakeBytes(d, 1); } // 5
    PA_BAD(6)
    virtual bool GetUint16(uint16_t *d) { return TakeBytes(d, 2); } // 7
    virtual bool GetSint32(int32_t *d) { return TakeBytes(d, 4); }  // 8
    virtual bool GetUint32(uint32_t *d) { return TakeBytes(d, 4); } // 9
    PA_BAD(10) PA_BAD(11)
    virtual bool GetFloat32(float *d) { return TakeBytes(d, 4); } // 12
    PA_BAD(13) PA_BAD(14)
    virtual bool GetGZStr(void *str) // 15
    {
        // The package format (cRZIStream's GetGZStr): the length as a packed uint32, 7 bits a byte, low first.
        uint32_t n = 0;
        for (int shift = 0;; shift += 7) {
            uint8_t b = 0;
            if (!TakeBytes(&b, 1)) return false;
            if (shift < 32) n |= uint32_t(b & 0x7f) << shift;
            if (!(b & 0x80)) break;
        }
        if (gBytes->size() - gAt < n) {
            drawfx::Note("short text", uint32_t(gAt));
            return false;
        }
        const char *text = reinterpret_cast<const char *>(gBytes->data() + gAt);
        gAt += n;
        using Fn = void(__thiscall *)(void *, const char *, uint32_t);
        reinterpret_cast<Fn>((*reinterpret_cast<void ***>(str))[3])(str, text, n);
        return true;
    }
    PA_BAD(16)
    virtual bool GetVoid(void *d, uint32_t n) { return TakeBytes(d, n); } // 17
};
MemIStream gIStream;

#undef PA_BAD

// ---- our reader, with a vtable of its own members (a slot nobody calls is null: a call faults) ----
struct NoCount {
    uint32_t AddRef() { return 1; }
    uint32_t Release() { return 1; }
};
void *gReaderSlots[30];

void MakeTable()
{
    using R = cGZPersistBinaryReader;
    void **r = gReaderSlots;
    r[0] = MemberAddress(&R::QueryInterface);
    r[1] = MemberAddress(&NoCount::AddRef);
    r[2] = MemberAddress(&NoCount::Release);
    r[3] = MemberAddress(&R::ReadRoot);
    r[4] = MemberAddress(&R::GetBool);
    r[5] = MemberAddress(&R::GetSint8);
    r[6] = MemberAddress(&R::GetUint8);
    r[7] = MemberAddress(&R::GetSint16);
    r[8] = MemberAddress(&R::GetUint16);
    r[9] = MemberAddress(&R::GetSint32);
    r[10] = MemberAddress(&R::GetUint32);
    r[11] = MemberAddress(&R::GetFloat32);
    r[12] = MemberAddress(&R::GetFloat64);
    r[13] = MemberAddress(&R::GetBoolArray);
    r[14] = MemberAddress(&R::GetVoid);
    r[15] = MemberAddress(&R::GetVoid);
    r[16] = MemberAddress(&R::GetSint16Array);
    r[17] = MemberAddress(&R::GetUint16Array);
    r[18] = MemberAddress(&R::GetSint32Array);
    r[19] = MemberAddress(&R::GetUint32Array);
    r[20] = MemberAddress(&R::GetFloat32Array);
    r[21] = MemberAddress(&R::GetFloat64Array);
    r[22] = MemberAddress(&R::GetGZStr);
    r[23] = MemberAddress(&R::Skip);
    r[24] = MemberAddress(&R::ReadObject);
    r[25] = MemberAddress(&R::GetObjectRef);
    r[26] = MemberAddress(&R::ReadStringRecord);
    r[29] = MemberAddress(&R::DeletingDestructor);
}

cGZPersistBinaryReader gReaderObject;

ifaces::cIGZPersistReader *Reader()
{
    gReaderObject = cGZPersistBinaryReader{};
    gReaderObject.mVTable = gReaderSlots;
    gReaderObject.mRefCount = 1;
    gReaderObject.mStream = &gIStream;
    return reinterpret_cast<ifaces::cIGZPersistReader *>(&gReaderObject);
}

// ---- the triangle sets the director makes: the library's, each held once more by the test ----
difftest::Context *gCtx;
std::vector<cITriangleSetShape *> gMeshes;

struct FakeCom {
    virtual uint32_t QueryInterface(uint32_t, void **) { return 0; }
    virtual uint32_t AddRef() { return 1; }
    virtual uint32_t Release() { return 1; }
    virtual uint32_t GetClassObject(uint32_t classId, uint32_t iid, void **out)
    {
        if (classId != cITriangleSetShape::kClassId) {
            drawfx::Note("COM", {classId, iid});
            *out = nullptr;
            return 0;
        }
        cIGZUnknown *u = gCtx->Original<MeshFactoryFn>(kMeshFactory)();
        const bool ok = u->QueryInterface(iid, out);
        auto *mesh = static_cast<cITriangleSetShape *>(*out);
        mesh->AddRef();
        difftest::NotCounted quiet;
        drawfx::AddRange(mesh, 0x30, 0x6d000000u | uint32_t(gMeshes.size()) << 12);
        gMeshes.push_back(mesh);
        return ok;
    }
};
FakeCom gCom;
void *__cdecl StandFramework() { return &gCom; }

// The allocator service the library's vectors free through (FreeThroughAllocatorService 0x197100's static: cache
// 0x10c95d4, guard 0x10c95d8): the fake while alive, as tests/sg_geometry_data_class.cpp explains.
uint32_t __fastcall ServiceNop(void *, void *) { return 0; }
void *__fastcall ServiceAlloc(void *, void *, uint32_t n) { return std::malloc(n ? n : 1); }
void __fastcall ServiceFree(void *, void *, void *p) { std::free(p); }
void *gServiceVTable[5] = {reinterpret_cast<void *>(&ServiceNop), reinterpret_cast<void *>(&ServiceNop),
                           reinterpret_cast<void *>(&ServiceNop), reinterpret_cast<void *>(&ServiceAlloc),
                           reinterpret_cast<void *>(&ServiceFree)};
void *gServiceObject = gServiceVTable;

struct ServiceFake {
    void **cache;
    int32_t *guard;
    void *savedCache;
    int32_t savedGuard;
    explicit ServiceFake(difftest::Context &ctx)
        : cache(ctx.image.At<void *>(0x10c95d4)), guard(ctx.image.At<int32_t>(0x10c95d8)), savedCache(*cache),
          savedGuard(*guard)
    {
        *cache = &gServiceObject;
        *guard = INT32_MIN;
    }
    ~ServiceFake()
    {
        *cache = savedCache;
        *guard = savedGuard;
    }
};

uint32_t gSeed; // the seed Build was called for (the side's run follows)

void Build(uint32_t seed, drawfx::Cells &cells)
{
    gSeed = seed;
    sgfx::gPool.Reset(cells);
    for (std::string &text : sgfx::gPool.texts) std::string().swap(text);
    sgfx::gResources.Reset(cells);
}

// One side's decode of the seed's block: the container from the side's factory, its face's Read over our reader.
void Decode(bool original, uint32_t seed)
{
    difftest::AllocationCounter counter;
    {
        difftest::NotCounted quiet;
        gMeshes.clear();
    }
    gBytes = &gBlocks[seed];
    gAt = 0;
    cISGResource *r = original ? gCtx->Original<FactoryFn>(kFactory)() : cGeometryDataContainer::Create();
    void *out = nullptr;
    const bool ok = r->QueryInterface(cIGeometryDataContainer::kIid, &out);
    drawfx::Note("made", uint32_t(ok && out != nullptr));
    if (!out) return;
    auto *face = static_cast<cIGeometryDataContainer *>(out);
    {
        difftest::NotCounted quiet; // pointers into the container log as tag | offset on both sides
        drawfx::AddRange(reinterpret_cast<uint8_t *>(face) - 0x2c, cGeometryDataContainer::kSize, 0x6c000000u);
    }
    face->Read(Reader());
    drawfx::Note("read to", {uint32_t(gAt), uint32_t(gBytes->size())});
    {
        difftest::NotCounted quiet;
        const gmdcsum::Summary s = gmdcsum::Summarize(*static_cast<const cGeometryDataContainer *>(face));
        for (int i = 0; i < gmdcsum::Summary::kSections; ++i)
            drawfx::Note("section", {uint32_t(i), s.counts[i], uint32_t(s.hashes[i]), uint32_t(s.hashes[i] >> 32)});
        if (original) gExpected[seed] = s.Line();
    }
    drawfx::Note("container released", face->Release());
    for (cITriangleSetShape *m : gMeshes) drawfx::Note("triangle set released", m->Release());
    sgfx::gResources.NoteRefs();
    sgfx::gPool.NoteCounts();
    drawfx::Note("blocks left", uint32_t(counter.Blocks()));
}

} // namespace

DIFF_TEST(gmdc_decode_acceptance, 0xa711b0)
{
    const std::wstring game = GameDir();
    if (game.empty()) {
        std::printf("  SKIP gmdc_decode_acceptance: no game copy (local/game)\n");
        return;
    }
    gBlocks.assign(kResources, {});
    gExpected.assign(kResources, {});
    {
        Package package;
        std::wstring loaded;
        for (unsigned i = 0; i < kResources; ++i) {
            const Pick &p = kPicks[i];
            char mark[64];
            std::snprintf(mark, sizeof(mark), "loading resource %u", i);
            ctx.Mark(mark);
            const std::wstring path = game + L"\\" + std::wstring(p.package, p.package + std::strlen(p.package));
            if (path != loaded) {
                if (!package.Load(path)) {
                    std::printf("  SKIP gmdc_decode_acceptance: cannot read %s\n", p.package);
                    return;
                }
                loaded = path;
            }
            std::vector<uint8_t> resource;
            if (!package.Find(kGmdcType, p, resource)) {
                ctx.Fail("%s: no GMDC %08x %08x %08x (or a bad QFS stream)", p.package, p.group, p.instance, p.resource);
                return;
            }
            const size_t root = RootBlock(resource);
            if (root == 0) {
                ctx.Fail("%s %08x: not an RCOL with one GMDC block", p.package, p.instance);
                return;
            }
            gBlocks[i].assign(resource.begin() + root, resource.end());
        }
    }

    gCtx = &ctx;
    MakeTable();
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    ServiceFake service(ctx);
    sgfx::CheckSides(ctx, "gmdc", kResources, Build,
                     [&](bool original, std::mt19937 &) { Decode(original, gSeed); });
    for (unsigned i = 0; i < kResources; ++i)
        std::printf("  gmdc-expected %s %08x %08x %08x %s\n", kPicks[i].package, kPicks[i].instance, kPicks[i].group,
                    kPicks[i].resource, gExpected[i].c_str());
}
