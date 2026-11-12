// cTSModelEffect's two helpers: the axes part 0xaef310 and the white material 0xc05be0, against fakes of the mesh builder,
// the framework and the material manager that log every call.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"

#include "misc/UtilitiesE.h"
#include "rz/StaticGetters.h"
#include "ts/cTSModelEffect.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>

namespace {

std::string gLog;
void Log(const char *format, ...)
{
    char line[160];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    gLog += line;
    gLog += '\n';
}
uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

#define UNUSED(n) virtual void s##n() { Log("unexpected slot %d", n); }

class Unknown {
public:
    virtual bool QueryInterface(uint32_t, void **) { Log("QI"); return false; }
    virtual uint32_t AddRef() { Log("addref"); return ++refs; }
    virtual uint32_t Release() { Log("release"); return --refs; }
    uint32_t refs = 1;
};

class Mesh {
public:
    virtual void Begin(int a) { Log("begin %d", a); } // 0
    virtual void End() { Log("end"); }                // 1
    UNUSED(2) UNUSED(3) UNUSED(4) UNUSED(5) UNUSED(6) UNUSED(7) UNUSED(8)
    virtual void AddVertex(const float *v) { Log("vertex %08x %08x %08x", Bits(v[0]), Bits(v[1]), Bits(v[2])); } // 9
    UNUSED(10) UNUSED(11) UNUSED(12) UNUSED(13) UNUSED(14) UNUSED(15) UNUSED(16) UNUSED(17) UNUSED(18)
    UNUSED(19) UNUSED(20) UNUSED(21) UNUSED(22) UNUSED(23) UNUSED(24) UNUSED(25) UNUSED(26)
    virtual void BeginSubset(const char *name) { Log("subset %s", name); } // 27
    virtual void BeginTriangle() { Log("triangle"); }                      // 28
    virtual void EndTriangle() { Log("end triangle"); }                    // 29
    UNUSED(30) UNUSED(31) UNUSED(32) UNUSED(33) UNUSED(34)
    virtual void AddIndex(int index) { Log("index %d", index); } // 35
    const int32_t *bases = kBases;
    Unknown unknown; // at bases[1] + 4 from the object
    static constexpr int32_t kBases[2] = {0, 4};
};

class Part : public Unknown {
public:
    UNUSED(3) UNUSED(4) UNUSED(5) UNUSED(6) UNUSED(7) UNUSED(8) UNUSED(9) UNUSED(10) UNUSED(11) UNUSED(12) UNUSED(13)
    UNUSED(14) UNUSED(15) UNUSED(16) UNUSED(17) UNUSED(18) UNUSED(19) UNUSED(20) UNUSED(21) UNUSED(22) UNUSED(23)
    UNUSED(24) UNUSED(25) UNUSED(26) UNUSED(27) UNUSED(28) UNUSED(29) UNUSED(30) UNUSED(31)
    virtual void SetMesh(void *mesh) { Log("set mesh %d", mesh == gMeshUnknown); } // 32
    static void *gMeshUnknown;
};
void *Part::gMeshUnknown;

class Material : public Unknown {
public:
    UNUSED(3) UNUSED(4) UNUSED(5) UNUSED(6) UNUSED(7) UNUSED(8) UNUSED(9)
    virtual void SetName(const char *text) { Log("name %s", text); } // 10
    UNUSED(11)
    virtual void SetDefinition(const char *text) { Log("definition %s", text); }                    // 12
    virtual void SetProperty(const char *name, const char *value) { Log("%s = %s", name, value); } // 13
};

class Materials {
public:
    UNUSED(0) UNUSED(1) UNUSED(2) UNUSED(3) UNUSED(4) UNUSED(5) UNUSED(6) UNUSED(7) UNUSED(8) UNUSED(9) UNUSED(10)
    UNUSED(11) UNUSED(12) UNUSED(13) UNUSED(14) UNUSED(15) UNUSED(16) UNUSED(17) UNUSED(18)
    virtual uint32_t Find(const char *name) { Log("find %s", name); return found; } // 19
    UNUSED(20) UNUSED(21) UNUSED(22)
    virtual uint32_t Add(Material *material, int kind)
    {
        Log("add %d refs %u kind %d", material == gMaterial, material->refs, kind);
        return added;
    } // 23
    UNUSED(24) UNUSED(25) UNUSED(26) UNUSED(27) UNUSED(28) UNUSED(29) UNUSED(30)
    virtual bool Has(const char *name) { Log("has %s", name); return has; } // 31
    uint32_t found = 0, added = 0;
    bool has = false;
    static Material *gMaterial;
};
Material *Materials::gMaterial;

struct World {
    Mesh mesh;
    Part part;
    Material material;
    Materials materials;
};
World *gWorld;

class Com {
public:
    UNUSED(0) UNUSED(1) UNUSED(2)
    virtual uint32_t GetClassObject(uint32_t classId, uint32_t iid, void **out) // 3
    {
        Log("class %08x %08x was %p", classId, iid, *out);
        if (classId == 0x7ba3838c) *out = &gWorld->part;
        else if (classId == 0x9bffc10d) *out = &gWorld->mesh;
        else if (classId == 0x49596978) *out = &gWorld->material;
        else *out = nullptr;
        return 1;
    }
};
Com gCom;

void *StandCom()
{
    Log("framework");
    return &gCom;
}
void *StandMaterials()
{
    Log("materials");
    return &gWorld->materials;
}

} // namespace

DIFF_TEST(ModelEffect_AxesPart, 0xaef310)
{
    using Fn = void **(__cdecl *)(void **, float);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandCom};
    const Fn fns[2] = {ctx.Original<Fn>(0xaef310), &cTSModelEffect::MakeAxesPart};
    std::mt19937 r(0xaef310);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        const float size = seed == 0 ? 0.0f : seed == 1 ? 1.0f : static_cast<float>(int32_t(r())) * 1e-7f;
        std::string logs[2];
        uint32_t refs[2][2];
        for (int side = 0; side < 2; ++side) {
            World world;
            gWorld = &world;
            Part::gMeshUnknown = &world.mesh.unknown;
            gLog.clear();
            void *out = &world; // what was there before does not matter
            void **returned = fns[side](&out, size);
            if (returned != &out || out != &world.part) ctx.Fail("seed %u side %d: the part", seed, side);
            logs[side] = gLog;
            refs[side][0] = world.part.refs;
            refs[side][1] = world.mesh.unknown.refs;
        }
        if (logs[0] != logs[1] || std::memcmp(refs[0], refs[1], sizeof(refs[0])) != 0)
            ctx.Fail("seed %u: refs %u %u / %u %u\n%.900s\n--- ours\n%.900s", seed, refs[0][0], refs[0][1], refs[1][0], refs[1][1],
                     logs[0].c_str(), logs[1].c_str());
        ++ctx.cases;
    }
}

DIFF_TEST(ModelEffect_WhiteMaterial, 0xc05be0)
{
    using Fn = uint32_t(__cdecl *)();
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandCom};
    shape::StandIn manager{0xae8900, &GetCachedQueriedObject, &StandMaterials};
    const Fn fns[2] = {ctx.Original<Fn>(0xc05be0), &cTSModelEffect::GetWhiteMaterial};
    for (uint32_t seed = 0; seed < 8; ++seed) {
        std::string logs[2];
        uint32_t results[2], refs[2];
        for (int side = 0; side < 2; ++side) {
            World world;
            gWorld = &world;
            Materials::gMaterial = &world.material;
            world.materials.has = seed & 1;
            world.materials.found = 0x100 + seed;
            world.materials.added = 0x200 + seed;
            gLog.clear();
            results[side] = fns[side]();
            logs[side] = gLog;
            refs[side] = world.material.refs;
        }
        if (logs[0] != logs[1] || results[0] != results[1] || refs[0] != refs[1])
            ctx.Fail("seed %u: %x/%x, refs %u/%u\n%.700s\n--- ours\n%.700s", seed, results[0], results[1], refs[0], refs[1],
                     logs[0].c_str(), logs[1].c_str());
        ++ctx.cases;
    }
}
