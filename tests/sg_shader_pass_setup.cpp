// Differential tests for a shader pass made from its cShaderInit descriptor (src/sg/cShaderPassSetup.cpp) and the name
// pool's lookup (src/sg/cShaderInit.cpp):
//   SG_ShaderPassSetup 0xaa53e0     SG_ShaderPartVecHash 0xaa4fe0     SG_ShaderNamePoolAt 0xc631d0
//
// Setup reaches the device (slots 16 and 20), its enumeration and state, the vertex formats and declarations managers (the
// image's caches at 0x10caa24 / 0x10caa28), the material manager (0x10f8fcc), the texture manager (0x10f8f94), the
// descriptor's programs, controllers, formats and textures: each is a fake with the same slot layout that logs its calls
// and answers what the case drew. The two vertex-format helpers (0x18daa0, 0x18d990) are logging stand-ins on both sides,
// and cRZStd's report slot (0x1bfb92) logs what would be reported. Each case runs the original and ours on two copies of
// the same pass and compares the answer, the call log and the pass afterwards (its vectors by contents and capacity).
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "gz/cShaderProgramsDX.h"
#include "rz/cRZStd.h"
#include "sg/cGeometryNodeFormats.h"
#include "sg/cShader.h"
#include "sg/cShaderInit.h"
#include "sg/cTextureManager.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace nGZSceneGraph;

namespace {

std::string gLog;
std::set<uint32_t> gLines;     // the report lines seen
const uint8_t *gPassBase = nullptr; // the pass being set up: addresses in it are logged as offsets

void Note(const char *what, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0)
{
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s %x %x %x %x\n", what, a, b, c, d);
    gLog += buf;
}

uint32_t Id(const void *p) { return uint32_t(uintptr_t(p)); }

// The apply function a pass got, as the image's RVA of the one it stands for: the original stores the image's eight, ours
// stores its own (ShaderPass::ApplyFor, by the same index). Anything else is logged as its address.
constexpr uint32_t kApplyRvas[8] = {0xaa44d0, 0xaa3d70, 0xaa3e90, 0xaa3f70, 0xaa40a0, 0xaa4180, 0xaa42b0, 0xaa43a0};
uint32_t ApplyId(ShaderPass::ApplyFn fn)
{
    const void *const p = reinterpret_cast<const void *>(fn);
    for (uint32_t i = 0; i < 8; ++i)
        if (fn == ShaderPass::ApplyFor(i) || p == GameAddress<void>(kApplyRvas[i])) return kApplyRvas[i];
    return Id(p);
}

// An out-pointer: its offset in the pass, or "elsewhere" (a local of the function under test).
uint32_t Where(const void *p)
{
    const uint8_t *q = static_cast<const uint8_t *>(p);
    return (q >= gPassBase && q < gPassBase + sizeof(ShaderPass)) ? uint32_t(q - gPassBase) : 0xeeee;
}

template <int N> struct Pads : Pads<N - 1> {
    virtual void pad(Pads<N> *) {}
};
template <> struct Pads<0> {};
template <int N, class B> struct PadsAfter : PadsAfter<N - 1, B> {
    virtual void pad(PadsAfter<N, B> *) {}
};
template <class B> struct PadsAfter<0, B> : B {};

// A counted object: slots 0..2, its counting logged.
struct FakeUnk {
    uint32_t id = 0;
    virtual bool QueryInterface(uint32_t iid, void **) { Note("qi", id, iid); return false; }
    virtual uint32_t AddRef() { Note("addref", id); return 2; }
    virtual uint32_t Release() { Note("release", id); return 1; }
};

// A vertex or pixel program (cIShaderProgram): slot 6 created, slot 10 its info (+7 hardware).
struct FakeProgram : PadsAfter<3, FakeUnk> {
    bool created = true;
    nGZGraphic4::cShaderProgramInfo info{};
    virtual bool IsCreated() { Note("prog-created", id); return created; } // 6
    virtual void p7() {}
    virtual void p8() {}
    virtual void p9() {}
    virtual nGZGraphic4::cShaderProgramInfo *GetInfo() { Note("prog-info", id); return &info; } // 10
};

// The part mapper (the pass's mP8): slot 9 maps a key.
struct FakeMapper : PadsAfter<6, FakeUnk> {
    uint32_t words[3] = {};
    virtual const uint32_t *Map(void *, const ShaderKey *key) { Note("map", id, key->w[0], key->w[1], key->w[2]); return words; } // 9
};

// A shader controller: slot 8 validates, 9 applies, 10 its dynamic state flags; `reg` is what the material manager
// registers it as.
struct FakeController : PadsAfter<5, FakeUnk> {
    bool valid = true;
    uint32_t flags = 0;
    FakeController *reg = nullptr;
    virtual bool Init(void *caps, const ShaderKey *key, bool hardware, bool report) // 8
    {
        Note("ctl-init", id, Id(caps), key->w[0] ^ key->w[1] ^ key->w[2], (hardware ? 0x10 : 0) | (report ? 1 : 0));
        return valid;
    }
    virtual void Apply(void *state, const ShaderKey *key) { Note("ctl-apply", id, Id(state), key->w[0] ^ key->w[1] ^ key->w[2]); } // 9
    virtual uint32_t Flags() { Note("ctl-flags", id); return flags; } // 10
};

// A vertex format: slot 4 its stream count; what the two helpers make of it.
struct FakeFormat : PadsAfter<1, FakeUnk> {
    uint32_t streams = 1;
    FakeFormat *stream = nullptr;  // MakeStreamVertexFormat's answer (null: nothing made)
    FakeFormat *device = nullptr;  // MakeDeviceVertexFormat's answer
    bool deviceOk = true;
    virtual uint32_t GetStreamCount() { Note("fmt-streams", id); return streams; } // 4
};

struct FakeBlock : PadsAfter<3, FakeUnk> {
    bool created = true;
    virtual bool IsCreated() { Note("block-created", id); return created; } // 6
};

// A scene-graph texture: slot 3 restore, 6 created, 8 the device texture.
struct FakeTexture : FakeUnk {
    bool created = true;
    virtual void Restore() { Note("tex-restore", id); }          // 3
    virtual void p4() {}
    virtual void p5() {}
    virtual bool IsCreated() { Note("tex-created", id); return created; } // 6
    virtual void p7() {}
    virtual void *GetDeviceTexture(uint32_t face) { Note("tex-device", id, face); return reinterpret_cast<void *>(uintptr_t(0x7000 + id)); } // 8
};

struct FakeEnum : Pads<20> {
    uint8_t caps[8] = {};
    virtual uint8_t HasProcessingCap(uint32_t i) { Note("caps", i); return caps[i & 7]; } // 20
};

struct FakeState : Pads<9> {
    FakeBlock *block = nullptr;
    bool validOk = true;
    uint32_t failAt = 0xffffffff;  // the command ValidateCommand refuses
    virtual void Reset(uint32_t flags) { Note("st-reset", flags); }                         // 9
    virtual uint32_t CreateStateBlock(uint32_t groups, uint32_t out)                        // 10
    {
        Note("st-block", groups, Where(reinterpret_cast<void *>(uintptr_t(out))));
        *reinterpret_cast<void **>(uintptr_t(out)) = block;
        return 1;
    }
    virtual void p11() {}
    virtual void ApplyCommand(uint32_t command) { Note("st-command", command); }            // 12
    virtual void SetTexture(uint32_t stage, void *t) { Note("st-texture", stage, Id(t)); }  // 13
    virtual void SetVertexShader(void *s) { Note("st-vs", Id(s)); }                         // 14
    virtual void SetPixelShader(void *s) { Note("st-ps", Id(s)); }                          // 15
    virtual void p16() {}
    virtual void p17() {}
    virtual void p18() {}
    virtual void p19() {}
    virtual void p20() {}
    virtual void p21() {}
    virtual void p22() {}
    virtual void p23() {}
    virtual void p24() {}
    virtual void p25() {}
    virtual void p26() {}
    virtual void p27() {}
    virtual void p28() {}
    virtual bool ValidateCommand(const void *command, bool hardware, bool log)              // 29
    {
        Note("st-validate", Id(command), hardware, log);
        return Id(command) != failAt;
    }
};

struct FakeDevice : Pads<16> {
    FakeEnum *caps = nullptr;
    FakeState *state = nullptr;
    virtual void *GetDeviceEnumConst() { Note("dev-enum"); return caps; } // 16
    virtual void p17() {}
    virtual void p18() {}
    virtual void p19() {}
    virtual void *GetStateConst() { Note("dev-state"); return state; } // 20
};

struct FakeFormats : Pads<8> {
    FakeFormat *format = nullptr;
    virtual void *GetFormat(uint32_t id) { Note("formats-get", id); return format; } // 8
};

// The declarations manager: slot 3 answers the next declaration (counted) or fails.
struct FakeDeclarations : Pads<3> {
    FakeUnk decls[3];
    uint32_t failMask = 0; // bit n: the n-th call this run fails
    uint32_t calls = 0;
    virtual bool GetDeclaration(void *device, void *format, void **out) // 3
    {
        const uint32_t n = calls++;
        Note("decl-get", Id(device), Id(format), Where(out));
        if (failMask & (1u << n)) return false;
        *out = &decls[n % 3];
        return true;
    }
};

// The material manager: slot 26 registers an object (the mapper source as the mapper, a controller as its `reg`).
struct FakeMaterials : Pads<26> {
    FakeUnk *mapperSource = nullptr;
    FakeMapper *mapper = nullptr;
    virtual uint32_t Register(void *object, uint32_t iid, void **out) // 26
    {
        Note("mat-register", Id(object), iid, Where(out));
        void *answer = object == mapperSource ? static_cast<void *>(mapper) : static_cast<void *>(static_cast<FakeController *>(object)->reg);
        if (answer) static_cast<FakeUnk *>(answer)->AddRef();
        *out = answer;
        return 1;
    }
};

const char *const kTextureNames[] = {"", "tex-one", "tex-two", "tex-three"};

struct FakeTextures : Pads<18> {
    FakeTexture *textures[4] = {};
    virtual void AddRefId(uint32_t id) { Note("tm-addref", id); }   // 18
    virtual void ReleaseId(uint32_t id) { Note("tm-release", id); } // 19
    virtual void p20() {}
    virtual void p21() {}
    virtual void p22() {}
    virtual void *GetTexture(uint32_t id) { Note("tm-get", id); return id < 4 ? textures[id] : nullptr; } // 23
    virtual TextureInfo GetInfo(uint32_t id) // 24
    {
        Note("tm-info", id);
        return TextureInfo{id < 4 ? Id(kTextureNames[id]) : 0u, 3};
    }
};

// The two vertex-format helpers, on both sides.
void __cdecl StandStream(nGZGraphic4::cIVertexFormat **out, nGZGraphic4::cIVertexFormat *format, int32_t to, int32_t from)
{
    FakeFormat *f = reinterpret_cast<FakeFormat *>(format);
    Note("make-stream", f ? f->id : 0, uint32_t(to), uint32_t(from), *out ? 1 : 0);
    if (f && f->stream) {
        f->stream->AddRef();
        *out = reinterpret_cast<nGZGraphic4::cIVertexFormat *>(f->stream);
    }
}

bool __cdecl StandDevice(nGZGraphic4::cIVertexFormat **out, nGZGraphic4::cIDeviceDX *device, nGZGraphic4::cIVertexFormat *format)
{
    FakeFormat *f = reinterpret_cast<FakeFormat *>(format);
    Note("make-device", Id(device), f ? f->id : 0, *out ? 1 : 0);
    if (!f) return false;
    if (f->device) {
        f->device->AddRef();
        *out = reinterpret_cast<nGZGraphic4::cIVertexFormat *>(f->device);
    }
    return f->device && f->deviceOk; // true only with a format made, as the helper's
}

int32_t __cdecl StandReport(cRZStd *self, const char *group, int32_t level, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    std::string arg;
    if (std::strstr(format, "%s")) {
        arg = va_arg(args, const char *);
    } else if (std::strchr(format, '%')) {
        char b[16];
        std::snprintf(b, sizeof(b), "%x", va_arg(args, uint32_t));
        arg = b;
    }
    va_end(args);
    char buf[64];
    std::snprintf(buf, sizeof(buf), " %u %d %x ", self->mSecond, level, self->mFlags);
    gLog += std::string("report ") + self->File() + " " + group + buf + arg + "\n" + format;
    gLines.insert(self->mSecond);
    return 0;
}

// The image's cached managers, planted for the test and put back after.
struct Plant {
    void **cell;
    void *saved;
    Plant(difftest::Context &ctx, uint32_t rva, void *object) : cell(ctx.image.At<void *>(rva)), saved(*cell) { *cell = object; }
    ~Plant() { *cell = saved; }
    Plant(const Plant &) = delete;
    Plant &operator=(const Plant &) = delete;
};

void FreeVec(ShaderPartVec &v)
{
    if (v.mBegin) ::operator delete(v.mBegin);
    v = ShaderPartVec{};
}

void SnapVec(std::string &o, const ShaderPartVec &v)
{
    char b[32];
    std::snprintf(b, sizeof(b), "[%u/%u:", uint32_t(v.mEnd - v.mBegin), uint32_t(v.mCap - v.mBegin));
    o += b;
    for (cIShaderPart *const *it = v.mBegin; it != v.mEnd; ++it) {
        std::snprintf(b, sizeof(b), " %x", Id(*it));
        o += b;
    }
    o += "]";
}

// The pass by value: every field, its vectors by contents and capacity (never their blocks' addresses).
std::string Snap(const ShaderPass &p)
{
    std::string o;
    char b[64];
    std::snprintf(b, sizeof(b), "apply %x b %x %x %x %x p %x %x %x ", ApplyId(p.mApply), p.mB4, p.mB5,
                  p.mB6, p.mB7, Id(p.mP8), Id(p.mPc), Id(p.mP10));
    o += b;
    SnapVec(o, p.mV14);
    std::snprintf(b, sizeof(b), " p20 %x ", Id(p.mP20));
    o += b;
    SnapVec(o, p.mV24);
    SnapVec(o, p.mV30);
    std::snprintf(b, sizeof(b), " b %x %x %x %x m %x %x ", p.mB3c, p.mB3d, p.mB3e, p.mPad3f, p.m40, p.m44);
    o += b;
    std::snprintf(b, sizeof(b), "p %x %x %x %x ", Id(p.mP48), Id(p.mP4c), Id(p.mP50), Id(p.mP54));
    o += b;
    SnapVec(o, p.mV58);
    for (uint32_t h : p.mHandles) {
        std::snprintf(b, sizeof(b), " %x", h);
        o += b;
    }
    return o;
}

// A vector with room for `cap` (none: null) holding `items`.
void MakeVec(ShaderPartVec &v, const std::vector<cIShaderPart *> &items, uint32_t cap)
{
    v = ShaderPartVec{};
    if (cap == 0) return;
    v.mBegin = static_cast<cIShaderPart **>(::operator new(cap * sizeof(void *)));
    v.mEnd = v.mBegin;
    v.mCap = v.mBegin + cap;
    for (cIShaderPart *p : items) *v.mEnd++ = p;
}

// One case's world.
struct World {
    FakeEnum caps;
    FakeState state;
    FakeDevice device;
    FakeFormats formats;
    FakeDeclarations declarations;
    FakeMaterials materials;
    FakeTextures textures;
    FakeTexture texture[4];
    FakeFormat format[6];        // 0 the format, 1 its stream format, 2 the device format, 3 the output, 4 its device one
    FakeProgram program[4];      // 0 preprocess, 1 vertex, 2 pixel, 3 one the pass held before
    FakeMapper mapper[2];        // 0 what the descriptor's object registers as, 1 one the pass held before
    FakeUnk mapperSource;
    FakeController controller[8];
    FakeBlock block;
    FakeUnk oldDecl;
    std::vector<ShaderDescriptor> descs;
    std::vector<uint32_t> initNames, descNames[3];
    std::vector<cIShaderObject *> initObjects, partsA[3], partsB[3];
    ShaderInitData init;
    ShaderKey key;
    uint32_t index = 0;
    bool report = false;

    explicit World(std::mt19937 &rng)
    {
        auto chance = [&](uint32_t n) { return rng() % n == 0; }; // one in n
        uint32_t next = 0x100;
        for (FakeUnk *u : std::initializer_list<FakeUnk *>{&texture[0], &texture[1], &texture[2], &texture[3], &format[0], &format[1],
                                                            &format[2], &format[3], &format[4], &format[5], &program[0], &program[1],
                                                            &program[2], &program[3], &mapper[0], &mapper[1], &mapperSource, &block,
                                                            &oldDecl, &declarations.decls[0], &declarations.decls[1], &declarations.decls[2]})
            u->id = next++;
        for (FakeController &c : controller) c.id = next++;
        device.caps = &caps;
        device.state = &state;
        caps.caps[0] = uint8_t(chance(2) ? 0 : (chance(2) ? 1 : 0x41));
        caps.caps[3] = uint8_t(chance(12) ? 0 : 1);
        state.block = chance(25) ? nullptr : &block;
        block.created = !chance(25);
        state.failAt = chance(25) ? 0 : 0xffffffff; // set to a command below
        for (FakeTexture &t : texture) t.created = !chance(40);
        for (uint32_t i = 1; i < 4; ++i) textures.textures[i] = &texture[i];

        // The formats: the main one, its stream and device variants and the output's.
        formats.format = &format[0];
        for (FakeFormat &f : format) f.streams = chance(30) ? 0x11 : 1 + rng() % 0x10;
        format[0].stream = chance(20) ? nullptr : &format[1];
        format[1].device = &format[2];
        format[1].deviceOk = !chance(20);
        format[3].device = chance(20) ? nullptr : &format[4];
        format[3].deviceOk = !chance(20);
        declarations.failMask = chance(6) ? 1u << (rng() % 3) : 0;
        for (FakeProgram &p : program) {
            p.created = !chance(25);
            p.info.hardware = uint8_t(chance(2) ? 0 : 1);
        }
        mapper[0].words[0] = rng();
        mapper[0].words[1] = rng();
        mapper[0].words[2] = rng() | (chance(8) ? 0 : 1);
        mapper[1].words[0] = rng();
        mapper[1].words[1] = rng();
        mapper[1].words[2] = rng() | 1;
        materials.mapperSource = &mapperSource;
        materials.mapper = chance(8) ? nullptr : &mapper[0];
        for (FakeController &c : controller) {
            c.valid = !chance(30);
            const uint32_t r = rng() % 8;
            c.flags = r < 3 ? 0 : r < 5 ? 1 : r < 7 ? 2 : 3;
            c.reg = chance(8) ? &controller[rng() % 8] : &c;
        }

        // The descriptors: one to three, the pass's at `index`.
        const uint32_t count = 1 + rng() % 3;
        index = rng() % count;
        descs.resize(count);
        for (uint32_t d = 0; d < count; ++d) {
            ShaderDescriptor &s = descs[d];
            std::memset(&s, 0, sizeof(s));
            const bool preprocess = chance(2);
            s.mObject00 = chance(3) ? reinterpret_cast<cIShaderObject *>(&mapperSource) : nullptr;
            s.mObject1c = preprocess ? reinterpret_cast<cIShaderObject *>(&program[0]) : nullptr;
            s.mObject20 = (preprocess != chance(20)) ? reinterpret_cast<cIShaderObject *>(&format[3]) : nullptr;
            s.mObject24 = chance(3) ? nullptr : reinterpret_cast<cIShaderObject *>(&program[1]);
            s.mObject28 = chance(3) ? nullptr : reinterpret_cast<cIShaderObject *>(&program[2]);
            const uint32_t nA = (preprocess || chance(20)) ? rng() % 3 : 0;
            for (uint32_t k = 0; k < nA; ++k) {
                FakeController *c = &controller[rng() % 3];
                if (!chance(10)) c->flags = 1;
                if (chance(10)) c->reg = nullptr;
                partsA[d].push_back(reinterpret_cast<cIShaderObject *>(c));
            }
            const uint32_t nB = rng() % 3;
            for (uint32_t k = 0; k < nB; ++k) partsB[d].push_back(reinterpret_cast<cIShaderObject *>(&controller[3 + rng() % 5]));
            const uint32_t nNames = rng() % 4;
            for (uint32_t k = 0; k < nNames; ++k) descNames[d].push_back(rng() % 0x1000 * 4);
            s.mNames.w[0] = rng() % 0x100000 * 4;
            for (ShaderTextureSlot &t : s.mTextures) {
                t.mHandle = chance(4) ? rng() % 4 : 0xffffffff;
                t.mValue = rng() % 8;
            }
            s.mByteC4 = uint8_t(rng() % 2);
            s.mByteC5 = uint8_t(rng() % 2);
            s.mByteC6 = uint8_t(rng() % 2);
            s.mByteC7 = uint8_t(rng() % 2);
        }
        const uint32_t nObjects = rng() % 3;
        for (uint32_t k = 0; k < nObjects; ++k) initObjects.push_back(reinterpret_cast<cIShaderObject *>(&controller[3 + rng() % 5]));
        const uint32_t nNames = rng() % 4;
        for (uint32_t k = 0; k < nNames; ++k) initNames.push_back(rng() % 0x1000 * 4);

        std::memset(&init, 0, sizeof(init));
        init.mAttribute = rng();
        init.mNames.w[0] = rng() % 0x100000 * 4;
        SetPool(init.mNames, initNames);
        SetObjects(init.mObjects, initObjects);
        for (uint32_t d = 0; d < count; ++d) {
            SetPool(descs[d].mNames, descNames[d]);
            SetObjects(descs[d].mPartsA, partsA[d]);
            SetObjects(descs[d].mPartsB, partsB[d]);
        }
        init.mDescs.mBegin = descs.data();
        init.mDescs.mEnd = init.mDescs.mCap = descs.data() + count;
        if (state.failAt == 0) {
            // A command the state refuses: one of the pass's (the init's or its descriptor's), when there is one.
            std::vector<uint32_t> all;
            for (uint32_t n : initNames) all.push_back(n + init.mNames.w[0]);
            for (uint32_t n : descNames[index]) all.push_back(n + descs[index].mNames.w[0]);
            state.failAt = all.empty() ? 0xffffffff : all[rng() % all.size()];
        }

        key.w[0] = rng();
        key.w[1] = rng();
        key.w[2] = rng() | (chance(8) ? 0 : 1);
        report = !chance(4);
    }

    static void SetPool(NamePool &pool, std::vector<uint32_t> &names)
    {
        if (names.empty()) return;
        pool.w[3] = Id(names.data());
        pool.w[4] = pool.w[5] = Id(names.data() + names.size());
    }
    static void SetObjects(ObjectVec &v, std::vector<cIShaderObject *> &objects)
    {
        if (objects.empty()) return;
        v.mBegin = objects.data();
        v.mEnd = v.mCap = objects.data() + objects.size();
    }

    // The pass before the call: zeroed, or holding counted things already (released as Setup replaces them).
    void FillPass(ShaderPass &p, std::mt19937 &rng)
    {
        auto chance = [&](uint32_t n) { return rng() % n == 0; };
        std::memset(&p, 0, sizeof(p));
        for (uint32_t &h : p.mHandles) h = chance(5) ? rng() % 4 : 0xffffffff;
        p.mB4 = uint8_t(rng() % 2);
        p.mB7 = uint8_t(chance(6) ? 1 : 0);
        p.mPad3f = uint8_t(rng());
        p.m40 = rng();
        p.m44 = rng();
        auto part = [](void *f) { return reinterpret_cast<cIShaderPart *>(f); };
        if (chance(3)) p.mP8 = part(&mapper[1]);
        if (chance(4)) p.mPc = part(chance(2) ? &program[3] : &program[0]);
        if (chance(4)) p.mP10 = part(&oldDecl);
        if (chance(4)) p.mP20 = part(&oldDecl);
        if (chance(4)) p.mP48 = part(&oldDecl);
        if (chance(4)) p.mP4c = part(&oldDecl);
        if (chance(4)) p.mP50 = part(chance(2) ? &program[3] : &program[1]);
        if (chance(4)) p.mP54 = part(&program[3]);
        std::vector<cIShaderPart *> held;
        if (chance(6)) held.push_back(part(&controller[3 + rng() % 5]));
        MakeVec(p.mV14, {}, chance(2) ? 0 : 1 + rng() % 3);
        MakeVec(p.mV24, held, held.empty() && chance(2) ? 0 : 1 + rng() % 3);
        MakeVec(p.mV30, {}, chance(2) ? 0 : 1 + rng() % 3);
        MakeVec(p.mV58, {}, chance(2) ? 0 : 1 + rng() % 3);
    }

    void Rewind() { declarations.calls = 0; }
};

size_t Diverge(const std::string &a, const std::string &b)
{
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    return i;
}

void Clone(ShaderPass &to, const ShaderPass &from)
{
    to = from;
    ShaderPartVec ShaderPass::*const vecs[] = {&ShaderPass::mV14, &ShaderPass::mV24, &ShaderPass::mV30, &ShaderPass::mV58};
    for (auto v : vecs) {
        const ShaderPartVec &src = from.*v;
        std::vector<cIShaderPart *> items(src.mBegin, src.mEnd);
        MakeVec(to.*v, items, uint32_t(src.mCap - src.mBegin));
    }
}

void FreePass(ShaderPass &p)
{
    FreeVec(p.mV14);
    FreeVec(p.mV24);
    FreeVec(p.mV30);
    FreeVec(p.mV58);
}

} // namespace

// ---- 0xaa53e0 -------------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_ShaderPassSetup, 0xaa53e0)
{
    using SetupFn = bool(__thiscall *)(ShaderPass *, void *, uint32_t, const void *, const ShaderKey *, bool);
    auto original = ctx.Original<SetupFn>(0xaa53e0);
    shape::CodePatch reportPatch(0x1bfb92, reinterpret_cast<const void *>(&StandReport));
    shape::StandIn stream(0x18d990, &MakeStreamVertexFormat, &StandStream);
    shape::StandIn device(0x18daa0, &MakeDeviceVertexFormat, &StandDevice);
    gLog.reserve(1 << 16);
    gLines.clear();
    std::mt19937 rng(0xaa53e0);
    int successes = 0, failures = 0, validateRefused = 0, mapped = 0, withOutput = 0;
    for (int iter = 0; iter < 6000; ++iter) {
        World w(rng);
        Plant formats(ctx, 0x10caa24, &w.formats);
        Plant declarations(ctx, 0x10caa28, &w.declarations);
        Plant materials(ctx, 0x10f8fcc, &w.materials);
        Plant textures(ctx, 0x10f8f94, &w.textures);
        ShaderPass a, b;
        w.FillPass(a, rng);
        Clone(b, a);

        gLog.clear();
        gPassBase = reinterpret_cast<const uint8_t *>(&a);
        w.Rewind();
        const bool ra = original(&a, &w.device, w.index, &w.init, &w.key, w.report);
        const std::string logA = gLog;

        gLog.clear();
        gPassBase = reinterpret_cast<const uint8_t *>(&b);
        w.Rewind();
        const bool rb = b.Setup(reinterpret_cast<nGZGraphic4::cIDeviceDX *>(&w.device), w.index,
                                reinterpret_cast<const ShaderInitView *>(&w.init), &w.key, w.report);
        ++ctx.cases;

        const std::string sa = Snap(a), sb = Snap(b);
        if (ra != rb || logA != gLog || sa != sb) {
            const size_t at = Diverge(logA, gLog), atPass = Diverge(sa, sb);
            ctx.Fail("iter %d (index %u report %d): result %d/%d; pass from %u: %s / %s; log from %u:\n%s\n--- ours:\n%s", iter, w.index,
                     w.report, ra, rb, uint32_t(atPass), sa.substr(atPass, 40).c_str(), sb.substr(atPass, 40).c_str(), uint32_t(at),
                     logA.substr(at > 60 ? at - 60 : 0, 160).c_str(), gLog.substr(at > 60 ? at - 60 : 0, 160).c_str());
        }
        if (ra) ++successes;
        else ++failures;
        char refused[48];
        std::snprintf(refused, sizeof(refused), "st-validate %x ", w.state.failAt);
        if (!ra && logA.find(refused) != std::string::npos) ++validateRefused;
        if (logA.find("map ") != std::string::npos) ++mapped;
        if (ra && w.descs[w.index].mObject20) ++withOutput;
        FreePass(a);
        FreePass(b);
        if (ctx.failures.size() > 3) break;
    }
    // Every report line Setup has, and enough of the success path.
    const uint32_t lines[] = {0x72,  0xa2,  0xa7,  0xb5,  0xba,  0xbf,  0xc6,  0xcf,  0xd5,  0xdb,  0xe1,  0xe7,
                              0xed,  0x10d, 0x112, 0x118, 0x122, 0x12a, 0x138, 0x144, 0x156, 0x162, 0x17d, 0x1ac};
    std::string missing;
    for (uint32_t l : lines)
        if (!gLines.count(l)) {
            char b[16];
            std::snprintf(b, sizeof(b), " %x", l);
            missing += b;
        }
    if (!missing.empty() || successes < 100 || validateRefused < 10 || mapped < 100 || withOutput < 20)
        ctx.Fail("coverage: report lines missing:%s; successes %d failures %d refused commands %d mapped %d with output %d",
                 missing.c_str(), successes, failures, validateRefused, mapped, withOutput);
}

// ---- 0xaa4fe0 -------------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_ShaderPartVecHash, 0xaa4fe0)
{
    auto original = ctx.Original<uint32_t(__stdcall *)(const ShaderPartVec *)>(0xaa4fe0);
    std::mt19937 rng(0xaa4fe0);
    for (int iter = 0; iter < 4000; ++iter) {
        const uint32_t n = iter < 4 ? uint32_t(iter) : rng() % 12;
        std::vector<cIShaderPart *> items(n);
        for (cIShaderPart *&p : items) {
            const uint32_t r = rng();
            p = reinterpret_cast<cIShaderPart *>(uintptr_t(rng() % 4 == 0 ? 0 : rng() % 3 == 0 ? r : r & ~3u));
        }
        ShaderPartVec v{};
        if (n) {
            v.mBegin = items.data();
            v.mEnd = v.mCap = items.data() + n;
        }
        const uint32_t a = original(&v), b = ShaderPartVecHash(&v);
        ++ctx.cases;
        if (a != b) ctx.Fail("iter %d (%u elements): %08x / %08x", iter, n, a, b);
    }
}

// ---- 0xc631d0 -------------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_ShaderNamePoolAt, 0xc631d0)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(const NamePool *, uint32_t)>(0xc631d0);
    std::mt19937 rng(0xc631d0);
    for (int iter = 0; iter < 2000; ++iter) {
        std::vector<uint32_t> entries(1 + rng() % 8);
        for (uint32_t &e : entries) e = rng();
        NamePool pool{};
        pool.w[0] = rng();
        pool.w[1] = rng();
        pool.w[2] = rng();
        pool.w[3] = Id(entries.data());
        pool.w[4] = pool.w[5] = Id(entries.data() + entries.size());
        const uint32_t i = rng() % uint32_t(entries.size());
        const uint32_t a = original(&pool, i), b = pool.At(i);
        ++ctx.cases;
        if (a != b) ctx.Fail("iter %d entry %u: %08x / %08x", iter, i, a, b);
    }
}
