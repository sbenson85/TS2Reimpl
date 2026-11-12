// Differential tests for the effects cheats (lane YB, src/ts/cTSEffectCheats.*, ts/cCheatCommand.h):
//   cTSEffectCheat: 0xbc46d0 ctor, 0xbc5930 deleting dtor, 0xbc7590 Execute, 0xbc9a90 GetName, 0xbc6630 GetHelp;
//   cTSEffectsManagerCheat: 0xbc4fb0, 0xbc59a0, 0xbc7e30, 0xbc9aa0, 0xbc6650 (shared_vf_bc6650);
//   cOverlayCheat: 0xbd9da0, 0xbda350, 0xbdade0, 0xbdb320, 0x76ed60 (shared_vf_76ed60);
//   0x1d0b4a the cheat family's QueryInterface, 0xbc5aa0 / 0xbdb700 the makers, 0xbc97f0 cTSEffectsManager::KillOrphans.
//
// The cheats are never registered in game (the makers leak them), so their slots are reached only through our vtables:
// each test builds the original object with the image's constructor and ours with our constructor (both on the same
// prefilled bytes) and calls the image's slot on the first and our vtable's on the second. Both run against one world of
// fakes (rebuilt at the same addresses before each side): a fake effects manager (a 0xc2c-byte blob with recording vtables
// at +0 and +8 and the fields the cheats read), visual effects, the terrain, the renderer, the effects parser, the
// framework service (game global 0x10c9378) and the services it hands out. Every call a fake receives is logged by name
// (pointers as the fake they are, floats as bits, strings as text) and the two logs compared, with a dump of the cheat's
// fields after each command. The option parser (CheatOptionSpec) is the game's on both sides; its state is compared
// through what it does (the fields Parse fills, Help's texts), never its bytes (heap pointers).
//
// Stood in on both sides: SendText (0xbca590, ours since lane XG) and KillOrphans (0xbc97f0, its own test below) in the
// Execute tests; the editor helpers 0xbc9c00 / 0x1b14c5 (not ours: image only); the game's operator new / delete
// (0x824632 / 0x824624, 0x82540f) by ours; the RZ allocator (cArguments' buffers) by the process heap.
//
// Not reached on purpose (the originals' faults, reproduced): `effect -pos` with no effect in the slot (an unchecked read
// past the vector), `effectsManager -load` without a resource service and `overlay` without the overlay service (null
// objects called).
#define NOMINMAX
#include "code_patch.h"
#include "command_parser.h"
#include "difftest.h"
#include "free_recorder.h"
#include "test_helpers.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cArguments.h"
#include "gz/cIGZString.h"
#include "rz/RZMemory.h"
#include "rz/cGZAllocatorService.h"
#include "rz/cRZString.h"
#include "ts/EffectsTransform.h"
#include "ts/cTSEffectCheats.h"
#include "ts/cTSEffectsManager.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

using nGZCommandParser::cArguments;
using nGZCommandParser::cICommand;
using Log = std::vector<std::string>;

constexpr uint32_t kEffectVTable = 0xe8cf38, kManagerVTable = 0xe8d3e0, kOverlayVTable = 0xe8e748;
constexpr uint32_t kFrameworkGlobal = 0x10c9378;

// ---- the log ----

Log *gLog = nullptr;
void L(const char *fmt, ...)
{
    if (!gLog) return;
    char buf[600];
    va_list a;
    va_start(a, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, a);
    va_end(a);
    gLog->emplace_back(buf);
}
uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
std::string Str(const char *s)
{
    return s ? "'" + std::string(s) + "'" : std::string("null");
}
// A cRZString's text (vtable, std::string, count: the game's and ours alike).
std::string RZText(const void *string)
{
    const uint32_t *w = static_cast<const uint32_t *>(string);
    const uint32_t size = w[5], cap = w[6];
    const char *s = cap > 15 ? reinterpret_cast<const char *>(w[1]) : reinterpret_cast<const char *>(w + 1);
    return "'" + std::string(s, size) + "'";
}

// ---- the fakes ----

struct Fake {
    void *const *vt;
    const char *name;
    int32_t refs;
};

// A visual effect: +0 its vtable, +4 its component face, +0x10 the count (KillOrphans reads it there), an id.
struct EffectFake {
    void *const *vt;
    void *const *vt4;
    uint32_t pad[2];
    int32_t refs; // +0x10
    uint32_t id;
    uint32_t more[10];
};
static_assert(offsetof(EffectFake, refs) == 0x10);

constexpr int kEffects = 6, kDescriptions = 10, kTables = 13;

struct World {
    alignas(16) uint8_t mgr[cTSEffectsManager::kSize];
    EffectFake effects[kEffects];
    Fake bcc, terrain, renderer, scriptParser, scriptState, framework, resources, resource, overlaySvc, overlayOwner,
        overlayMgr[2], cheatParser, desc[kDescriptions];
    alignas(4) uint8_t tables[kTables][0x2c];
    void *tableDescs[kTables][kDescriptions];
    void *pool[kEffects];
    uint32_t active[kEffects * 2];

    // What the fakes answer.
    bool terrainOn, heightOk, flag1, saveFailed, resourcesOn, loadOk, overlayOn[2], editOpens;
    float rect[4];
    const char *location;
    const char *stateText;
    uint32_t nextEffect;
    uint32_t listCount;
};
World gW;

std::string NameOf(const void *p)
{
    if (!p) return "null";
    const uint8_t *b = static_cast<const uint8_t *>(p);
    char buf[64];
    if (b >= gW.mgr && b < gW.mgr + sizeof(gW.mgr)) {
        std::snprintf(buf, sizeof(buf), "mgr+%x", unsigned(b - gW.mgr));
        return buf;
    }
    for (int i = 0; i < kEffects; ++i) {
        const uint8_t *e = reinterpret_cast<const uint8_t *>(&gW.effects[i]);
        if (b >= e && b < e + sizeof(EffectFake)) {
            std::snprintf(buf, sizeof(buf), "fx%u+%x", gW.effects[i].id, unsigned(b - e));
            return buf;
        }
    }
    const Fake *fakes[] = {&gW.bcc, &gW.terrain, &gW.renderer, &gW.scriptParser, &gW.scriptState, &gW.framework,
                           &gW.resources, &gW.resource, &gW.overlaySvc, &gW.overlayOwner, &gW.overlayMgr[0],
                           &gW.overlayMgr[1], &gW.cheatParser};
    for (const Fake *f : fakes)
        if (p == f) return f->name;
    for (const Fake &f : gW.desc)
        if (p == &f) return f.name;
    return "other";
}

template <int N> void __fastcall Trap(void *self, void *)
{
    L("TRAP %s slot %d", NameOf(self).c_str(), N);
}
template <size_t... I> std::array<void *, sizeof...(I)> TrapTable(std::index_sequence<I...>)
{
    return {reinterpret_cast<void *>(&Trap<int(I)>)...};
}
using Table = std::array<void *, 64>;
Table Traps()
{
    return TrapTable(std::make_index_sequence<64>());
}
template <class F> void Put(Table &t, int slot, F f)
{
    t[slot] = reinterpret_cast<void *>(f);
}

uint32_t __fastcall FkAddRef(Fake *f, void *)
{
    L("%s.AddRef", f->name);
    return uint32_t(++f->refs);
}
uint32_t __fastcall FkRelease(Fake *f, void *)
{
    L("%s.Release", f->name);
    return uint32_t(--f->refs);
}
void Counted(Table &t)
{
    Put(t, 1, &FkAddRef);
    Put(t, 2, &FkRelease);
}

// The effects manager (+0) and its collection face (+8).
void __fastcall MgSetFlag(void *, void *, uint32_t bit, bool on)
{
    L("mgr.SetFlag %u %d", bit, int(on));
    if (bit == 1) gW.flag1 = on;
}
bool __fastcall MgGetFlag(void *, void *, uint32_t bit)
{
    L("mgr.GetFlag %u", bit);
    return bit == 1 && gW.flag1;
}
bool __fastcall MgHasEffect(void *, void *, const char *name)
{
    L("mgr.HasEffect %s", Str(name).c_str());
    return name && std::strncmp(name, "fx_", 3) == 0;
}
bool __fastcall MgCreate(void *, void *, const char *name, void **out)
{
    L("mgr.CreateVisualEffect %s *out=%s", Str(name).c_str(), NameOf(*out).c_str());
    if (std::strcmp(name, "fx_bad") == 0) return false;
    EffectFake &e = gW.effects[gW.nextEffect++ % kEffects];
    ++e.refs;
    *out = &e;
    return true;
}
void __fastcall MgSetState(void *, void *, int32_t state)
{
    L("mgr.SetState %d", state);
}
void *__fastcall MgGetBCC(void *, void *)
{
    L("mgr.GetBCC");
    return &gW.bcc;
}
void __fastcall MgSetDebugBit(void *, void *, uint32_t bit, bool on)
{
    L("mgr.SetDebugBit %u %d", bit, int(on));
}
void __fastcall MgPrintStats(void *, void *, cIGZString *out)
{
    L("mgr.PrintStats %s", RZText(out).c_str());
    out->FromChar("stats: 3 effects");
}
void FillList(void *out, bool active)
{
    auto *list = static_cast<std::vector<fx::EffectListEntry> *>(out);
    list->reserve(gW.listCount);
    for (uint32_t i = 0; i < gW.listCount; ++i) {
        list->emplace_back();
        if (active) {
            EffectFake &e = gW.effects[(i * 2 + 1) % kEffects];
            ++e.refs; // the entry's count (its destructor releases it)
            list->back().mObject = reinterpret_cast<cIGZUnknown *>(&e);
        } else {
            list->back().mIndex = int32_t(i);
        }
    }
}
void __fastcall MgListActive(void *, void *, void *out, const char *filter)
{
    L("mgr.ListActive %s (%u)", Str(filter).c_str(), gW.listCount);
    FillList(out, true);
}
void __fastcall MgListEffects(void *, void *, void *out, const char *filter)
{
    L("mgr.ListEffects %s (%u)", Str(filter).c_str(), gW.listCount);
    FillList(out, false);
}
void __fastcall CoClear(void *self, void *)
{
    L("%s.Clear", NameOf(self).c_str());
}
void __fastcall CoLink(void *self, void *)
{
    L("%s.Link", NameOf(self).c_str());
}

// The visual effects.
EffectFake *Eff(void *p)
{
    return static_cast<EffectFake *>(p);
}
uint32_t __fastcall EfAddRef(void *self, void *)
{
    L("fx%u.AddRef", Eff(self)->id);
    return uint32_t(++Eff(self)->refs);
}
uint32_t __fastcall EfRelease(void *self, void *)
{
    L("fx%u.Release", Eff(self)->id);
    return uint32_t(--Eff(self)->refs);
}
void __fastcall EfSlot3(void *self, void *, uint32_t how)
{
    L("fx%u.Start %u", Eff(self)->id, how);
}
void __fastcall EfSlot4(void *self, void *, uint32_t how)
{
    // The manager's no-flush byte too (KillOrphans holds flushing off around these).
    L("fx%u.Stop %u noFlush %u", Eff(self)->id, how, unsigned(gW.mgr[0x8e]));
}
void __fastcall EfSuspend(void *self, void *, bool on)
{
    L("fx%u.SetSuspended %d", Eff(self)->id, int(on));
}
void __fastcall EfHide(void *self, void *, bool on)
{
    L("fx%u.SetHidden %d", Eff(self)->id, int(on));
}
void __fastcall EfUpdateFrom(void *self, void *, const fx::EffectsTransform *t)
{
    // pad[0] (byte 1) is left as the original's stack had it: not compared.
    char m[200];
    std::snprintf(m, sizeof(m), "%08x %08x %08x / %08x %08x %08x / %08x %08x %08x", Bits(t->m[0][0]), Bits(t->m[0][1]),
                  Bits(t->m[0][2]), Bits(t->m[1][0]), Bits(t->m[1][1]), Bits(t->m[1][2]), Bits(t->m[2][0]), Bits(t->m[2][1]),
                  Bits(t->m[2][2]));
    L("fx%u.UpdateFrom flags %u version %u m %s scale %08x at %08x %08x %08x", Eff(self)->id, t->flags,
      unsigned(const_cast<fx::EffectsTransform *>(t)->Version()), m, Bits(t->scale), Bits(t->translation.x),
      Bits(t->translation.y), Bits(t->translation.z));
}
bool __fastcall EfEventAlias(void *self, void *, uint32_t kind, const float *v, uint32_t count)
{
    L("fx%u.EventAlias %u x%u %08x %08x %08x", Eff(self)->id, kind, count, Bits(v[0]), Bits(v[1]), Bits(v[2]));
    return true;
}
bool __fastcall EfEvent(void *self, void *, uint32_t kind, const float *v, uint32_t count)
{
    L("fx%u.Event %u x%u %08x", Eff(self)->id, kind, count, Bits(v[0]));
    return false;
}
bool __fastcall EfSetResource(void *self, void *, uint32_t kind, void *resource)
{
    if (!resource) {
        L("fx%u.SetResource %u null", Eff(self)->id, kind);
        return true;
    }
    const uint32_t refs = static_cast<uint32_t *>(resource)[7];
    L("fx%u.SetResource %u %s count %u", Eff(self)->id, kind, RZText(resource).c_str(), refs);
    // Made with operator new (the game's is ours here) and a std::string alike on both sides: freed by the test.
    static_cast<cRZString *>(resource)->Destruct();
    ::operator delete(resource);
    return true;
}
bool __fastcall EfFlag20(void *self, void *)
{
    L("fx%u.slot20", Eff(self)->id);
    return Eff(self)->id & 1;
}
bool __fastcall EfFlag21(void *self, void *)
{
    L("fx%u.slot21", Eff(self)->id);
    return false;
}
void __fastcall EfTick(void *face, void *, uint32_t time, const uint32_t *counters)
{
    EffectFake *e = reinterpret_cast<EffectFake *>(static_cast<uint8_t *>(face) - 4);
    uint32_t any = 0;
    for (int i = 0; i < 12; ++i) any |= counters[i];
    L("fx%u+4.Tick %08x counters %08x", e->id, time, any);
}

// The terrain, its owner, the renderer, the effects parser and its state.
void *__fastcall BcGet(void *, void *, uint32_t which)
{
    L("bcc.slot7 %u", which);
    return gW.terrainOn ? &gW.terrain : nullptr;
}
void __fastcall TrBounds(void *, void *, float *l, float *t, float *r, float *b)
{
    L("terrain.Bounds");
    *l = gW.rect[0];
    *t = gW.rect[1];
    *r = gW.rect[2];
    *b = gW.rect[3];
}
bool __fastcall TrInside(void *, void *, float x, float y)
{
    L("terrain.Inside %08x %08x", Bits(x), Bits(y));
    return gW.heightOk && x >= 0.0f;
}
float __fastcall TrHeight(void *, void *, float x, float y)
{
    L("terrain.Height %08x %08x", Bits(x), Bits(y));
    return x * 0.5f + y + 0.25f;
}
void __fastcall RnSetResources(void *, void *, void *resources)
{
    L("renderer.slot2 %s", NameOf(resources).c_str());
}
const char *__fastcall RnLocation(void *, void *, const char *name, uint32_t kind)
{
    L("renderer.Location %s %u", Str(name).c_str(), kind);
    return gW.location;
}
bool __fastcall SpFailed(void *, void *)
{
    L("scriptParser.slot7");
    return gW.saveFailed;
}
void __fastcall SpTell(void *, void *, uint32_t what)
{
    L("scriptParser.slot8 %u", what);
}
void *__fastcall SpState(void *, void *)
{
    L("scriptParser.slot9");
    return &gW.scriptState;
}
const char *__fastcall SsText(void *, void *)
{
    L("scriptState.slot44");
    return gW.stateText;
}

// The framework service and what it hands out.
bool __fastcall FwGet(void *, void *, uint32_t classId, uint32_t iid, void **out)
{
    L("framework.Get %08x %08x *out=%s", classId, iid, NameOf(*out).c_str());
    Fake *f = nullptr;
    if (classId == 0x056b906e && gW.resourcesOn) f = &gW.resources;
    if (classId == 0x8af76dc8) f = &gW.overlaySvc;
    if (!f) return false;
    ++f->refs;
    *out = f;
    return true;
}
bool __fastcall RsGet(void *, void *, const uint32_t *key, uint32_t type, void **out, uint32_t a, uint32_t b)
{
    L("resources.Get %08x %08x %08x %08x type %08x *out=%s %u %u", key[0], key[1], key[2], key[3], type,
      NameOf(*out).c_str(), a, b);
    if (!gW.loadOk) return false;
    ++gW.resource.refs;
    *out = &gW.resource;
    return true;
}
void __fastcall RcLoad(void *, void *, void *into)
{
    L("resource.slot5 %s", NameOf(into).c_str());
}
void *__fastcall OsOwner(void *, void *)
{
    L("overlaySvc.slot4");
    return &gW.overlayOwner;
}
void *__fastcall OoManager(void *, void *, int32_t type)
{
    L("overlayOwner.slot26 %d", type);
    return type >= 0 && type < 2 && gW.overlayOn[type] ? &gW.overlayMgr[type] : nullptr;
}
void __fastcall OmStatus(Fake *f, void *, cIGZString *out)
{
    L("%s.Status %s", f->name, RZText(out).c_str());
    out->FromChar("overlay status");
}
void __fastcall OmDecal(Fake *f, void *, const char *name, const float *at, float size, float turns)
{
    L("%s.Decal %s %08x %08x %08x size %08x turns %08x", f->name, Str(name).c_str(), Bits(at[0]), Bits(at[1]), Bits(at[2]),
      Bits(size), Bits(turns));
}
// The cheat parser's scalar evaluation (cBaseParser slots 30..33, __thiscall, ret 4): the text read as a number.
bool __fastcall CpBool(Fake *f, void *, const char *text)
{
    L("%s.EvalBool %s", f->name, Str(text).c_str());
    return text && (std::atoi(text) != 0 || std::strcmp(text, "true") == 0 || std::strcmp(text, "on") == 0);
}
float __fastcall CpFloat(Fake *f, void *, const char *text)
{
    L("%s.EvalFloat %s", f->name, Str(text).c_str());
    return text ? float(std::atof(text)) : 0.0f;
}
int32_t __fastcall CpInt(Fake *f, void *, const char *text)
{
    L("%s.EvalInt %s", f->name, Str(text).c_str());
    return text ? int32_t(std::strtol(text, nullptr, 0)) : 0;
}
// The colours (slots 37..39, __thiscall, ret 8): four (or three) numbers into `out`.
float *ColorOf(Fake *f, float *out, const char *text, int count, const char *what)
{
    L("%s.EvalColor%s %s", f->name, what, Str(text).c_str());
    float v[4] = {};
    if (text) std::sscanf(text, "%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3]);
    for (int i = 0; i < count; ++i) out[i] = v[i];
    return out;
}
float *__fastcall CpRGBA(Fake *f, void *, float *out, const char *text)
{
    return ColorOf(f, out, text, 4, "RGBA");
}
float *__fastcall CpARGB(Fake *f, void *, float *out, const char *text)
{
    return ColorOf(f, out, text, 4, "ARGB");
}
float *__fastcall CpRGB(Fake *f, void *, float *out, const char *text)
{
    return ColorOf(f, out, text, 3, "RGB");
}
// The cheat parser's vector evaluation (cBaseParser slots 34..36, __thiscall, ret 0xc): up to `count` numbers read from
// the text (the rest 0), `out` returned.
float *EvalVector(Fake *f, float *out, const char *text, uint32_t minimum, int count)
{
    L("%s.EvalVector%d %s minimum %u", f->name, count, Str(text).c_str(), minimum);
    float v[4] = {};
    if (text) std::sscanf(text, "%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3]);
    for (int i = 0; i < count; ++i) out[i] = v[i];
    return out;
}
float *__fastcall CpVector2(Fake *f, void *, float *out, const char *text, uint32_t minimum)
{
    return EvalVector(f, out, text, minimum, 2);
}
float *__fastcall CpVector3(Fake *f, void *, float *out, const char *text, uint32_t minimum)
{
    return EvalVector(f, out, text, minimum, 3);
}
float *__fastcall CpVector4(Fake *f, void *, float *out, const char *text, uint32_t minimum)
{
    return EvalVector(f, out, text, minimum, 4);
}
uint32_t __fastcall DsSize(Fake *f, void *)
{
    L("%s.GetSize", f->name);
    return 0x40;
}

struct Tables {
    Table mgr = Traps(), coll = Traps(), effect = Traps(), face = Traps(), bcc = Traps(), terrain = Traps(),
          renderer = Traps(), scriptParser = Traps(), scriptState = Traps(), framework = Traps(), resources = Traps(),
          resource = Traps(), overlaySvc = Traps(), overlayOwner = Traps(), overlayMgr = Traps(), cheatParser = Traps(),
          desc = Traps();
    Tables()
    {
        Put(mgr, 7, &MgSetFlag);
        Put(mgr, 8, &MgGetFlag);
        Put(mgr, 12, &MgHasEffect);
        Put(mgr, 14, &MgCreate);
        Put(mgr, 15, &MgSetState);
        Put(mgr, 24, &MgGetBCC);
        Put(mgr, 31, &MgSetDebugBit);
        Put(mgr, 35, &MgPrintStats);
        Put(mgr, 36, &MgListActive);
        Put(mgr, 37, &MgListEffects);
        Put(coll, 3, &CoClear);
        Put(coll, 4, &CoLink);
        Put(effect, 1, &EfAddRef);
        Put(effect, 2, &EfRelease);
        Put(effect, 3, &EfSlot3);
        Put(effect, 4, &EfSlot4);
        Put(effect, 7, &EfSuspend);
        Put(effect, 8, &EfHide);
        Put(effect, 10, &EfUpdateFrom);
        Put(effect, 12, &EfEventAlias);
        Put(effect, 13, &EfEvent);
        Put(effect, 14, &EfSetResource);
        Put(effect, 20, &EfFlag20);
        Put(effect, 21, &EfFlag21);
        Put(face, 5, &EfTick);
        Put(bcc, 7, &BcGet);
        Put(terrain, 3, &TrBounds);
        Put(terrain, 4, &TrInside);
        Put(terrain, 5, &TrHeight);
        Put(renderer, 2, &RnSetResources);
        Put(renderer, 5, &RnLocation);
        Put(scriptParser, 7, &SpFailed);
        Put(scriptParser, 8, &SpTell);
        Put(scriptParser, 9, &SpState);
        Put(scriptState, 44, &SsText);
        Put(framework, 5, &FwGet);
        Counted(resources);
        Put(resources, 5, &RsGet);
        Counted(resource);
        Put(resource, 5, &RcLoad);
        Counted(overlaySvc);
        Put(overlaySvc, 4, &OsOwner);
        Put(overlayOwner, 26, &OoManager);
        Put(overlayMgr, 5, &OmDecal);
        Put(overlayMgr, 10, &OmStatus);
        Put(desc, 6, &DsSize);
        Put(cheatParser, 30, &CpBool);
        Put(cheatParser, 31, &CpFloat);
        Put(cheatParser, 32, &CpInt);
        Put(cheatParser, 33, &CpInt);
        Put(cheatParser, 34, &CpVector2);
        Put(cheatParser, 35, &CpVector3);
        Put(cheatParser, 36, &CpVector4);
        Put(cheatParser, 37, &CpRGBA);
        Put(cheatParser, 38, &CpARGB);
        Put(cheatParser, 39, &CpRGB);
    }
};
const Tables &T()
{
    static const Tables t;
    return t;
}

void SetFake(Fake &f, const Table &t, const char *name)
{
    f.vt = t.data();
    f.name = name;
    f.refs = 1;
}
uint32_t &MgrWord(uint32_t offset)
{
    return *reinterpret_cast<uint32_t *>(gW.mgr + offset);
}
void SetVector(uint32_t offset, const void *first, size_t bytes)
{
    const uint32_t begin = uint32_t(reinterpret_cast<uintptr_t>(first));
    MgrWord(offset) = begin;
    MgrWord(offset + 4) = begin + uint32_t(bytes);
    MgrWord(offset + 8) = begin + uint32_t(bytes);
}

// The world for a seed: the same bytes at the same addresses on every side.
void BuildWorld(uint32_t seed)
{
    std::memset(static_cast<void *>(&gW), 0, sizeof(gW));
    std::mt19937 r(seed * 7919u + 3);
    const Tables &t = T();
    *reinterpret_cast<const void **>(gW.mgr) = t.mgr.data();
    *reinterpret_cast<const void **>(gW.mgr + 8) = t.coll.data();
    for (int i = 0; i < kEffects; ++i) {
        EffectFake &e = gW.effects[i];
        e.vt = t.effect.data();
        e.vt4 = t.face.data();
        e.refs = 1 + int32_t(r() % 3);
        e.id = uint32_t(i + 1);
    }
    SetFake(gW.bcc, t.bcc, "bcc");
    SetFake(gW.terrain, t.terrain, "terrain");
    SetFake(gW.renderer, t.renderer, "renderer");
    SetFake(gW.scriptParser, t.scriptParser, "scriptParser");
    SetFake(gW.scriptState, t.scriptState, "scriptState");
    SetFake(gW.framework, t.framework, "framework");
    SetFake(gW.resources, t.resources, "resources");
    SetFake(gW.resource, t.resource, "resource");
    SetFake(gW.overlaySvc, t.overlaySvc, "overlaySvc");
    SetFake(gW.overlayOwner, t.overlayOwner, "overlayOwner");
    SetFake(gW.overlayMgr[0], t.overlayMgr, "overlayMgr0");
    SetFake(gW.overlayMgr[1], t.overlayMgr, "overlayMgr1");
    SetFake(gW.cheatParser, t.cheatParser, "cheatParser");
    static const char *const kDescNames[kDescriptions] = {"desc0", "desc1", "desc2", "desc3", "desc4",
                                                          "desc5", "desc6", "desc7", "desc8", "desc9"};
    for (int i = 0; i < kDescriptions; ++i) SetFake(gW.desc[i], t.desc, kDescNames[i]);

    // The manager's fields: the effects parser (+0xaa4), the renderer (+0xaa8), the up axis (+0xaf8), the display bytes
    // (+0xb50..+0xb52), the collection's thirteen description tables (+0x3c; each table's descriptions at +0x20).
    MgrWord(0xaa4) = uint32_t(reinterpret_cast<uintptr_t>(&gW.scriptParser));
    MgrWord(0xaa8) = uint32_t(reinterpret_cast<uintptr_t>(&gW.renderer));
    MgrWord(0xaf8) = r() % 3;
    gW.mgr[0xb50] = uint8_t(r());
    gW.mgr[0xb51] = uint8_t(r());
    gW.mgr[0xb52] = uint8_t(r());
    SetVector(0x3c, gW.tables, sizeof(gW.tables));
    for (int i = 0; i < kTables; ++i) {
        const uint32_t n = r() % 3 == 0 ? 0 : r() % 4;
        for (uint32_t k = 0; k < n; ++k) gW.tableDescs[i][k] = &gW.desc[(i + k * 3) % kDescriptions];
        const uint32_t first = uint32_t(reinterpret_cast<uintptr_t>(gW.tableDescs[i]));
        uint32_t *v = reinterpret_cast<uint32_t *>(gW.tables[i] + 0x20);
        v[0] = first;
        v[1] = first + 4 * n;
        v[2] = first + 4 * kDescriptions;
    }

    gW.terrainOn = r() % 4 != 0;
    gW.heightOk = r() % 3 != 0;
    static const float kRect[] = {0.0f, 10.0f, -4.0f, 64.0f, 3.5f};
    for (float &v : gW.rect) v = kRect[r() % 5];
    gW.flag1 = r() % 2;
    gW.saveFailed = r() % 2;
    gW.resourcesOn = r() % 4 != 0;
    gW.loadOk = r() % 2;
    gW.overlayOn[0] = r() % 4 != 0;
    gW.overlayOn[1] = r() % 2;
    gW.editOpens = r() % 2;
    static const char *const kLocations[] = {nullptr, "fx/effects.txt:42", "nocolon.txt", "a:b:7", ":3", "trail:"};
    gW.location = kLocations[r() % 6];
    static const char *const kStates[] = {nullptr, "line 3: bad token", ""};
    gW.stateText = kStates[r() % 3];
    gW.nextEffect = r() % kEffects;
    gW.listCount = r() % 4;
}

// ---- stand-ins ----

void __stdcall StandSendText(const char *text)
{
    L("SendText %s", Str(text).c_str());
}
void __fastcall StandKillOrphans(void *manager, void *)
{
    L("KillOrphans %s", NameOf(manager).c_str());
}
bool __cdecl StandOpenEditor(const char *file, const char *line)
{
    L("OpenEditor %s %s", Str(file).c_str(), Str(line).c_str());
    return gW.editOpens;
}
void __cdecl StandShowFile(const char *file)
{
    L("ShowFile %s", Str(file).c_str());
}
void *__cdecl GameNew(size_t n)
{
    return ::operator new(n);
}
void __cdecl GameDelete(void *p, size_t)
{
    ::operator delete(p);
}
void __cdecl GameDeleteUnsized(void *p)
{
    ::operator delete(p);
}
void *__cdecl TestRzMalloc(uint32_t bytes)
{
    return std::malloc(bytes ? bytes : 1);
}
void __cdecl TestRzFree(void *p)
{
    std::free(p);
}

// The patches every test here needs: the game's operator new / delete and the RZ allocator, and the framework global.
struct Env {
    // The option parser's code registers a static's exit handler on first use: the image's atexit works on an exit table
    // the harness never initialises.
    testhelp::AtexitRedirect atexit;
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
    shape::CodePatch gameDeleteUnsized{0x82540fu, shape::CodeOf(&GameDeleteUnsized)};
    shape::StandIn rzMalloc{0x1be3b0u, &RZMemory::Malloc, &TestRzMalloc};
    shape::StandIn rzFree{0x1be3a1u, &RZMemory::Free, &TestRzFree};
    void *savedFramework;
    explicit Env(difftest::Context &ctx) : atexit(ctx), savedFramework(*GameAddress<void *>(kFrameworkGlobal)) { *GameAddress<void *>(kFrameworkGlobal) = &gW.framework; }
    ~Env() { *GameAddress<void *>(kFrameworkGlobal) = savedFramework; }
};
void __stdcall StandFreeService(void *memory, uint32_t)
{
    ::operator delete(memory);
}

struct ExecuteStands {
    shape::StandIn sendText{0xbca590u, &cTSEffectsManager::SendText, &StandSendText};
    shape::StandIn kill{0xbc97f0u, &cTSEffectsManager::KillOrphans, &StandKillOrphans};
    shape::CodePatch openEditor{0xbc9c00u, shape::CodeOf(&StandOpenEditor)};
    shape::CodePatch showFile{0x1b14c5u, shape::CodeOf(&StandShowFile)};
    // The original frees the list vector through the allocator service (0x197100, ours and verified); both sides free
    // through the test's heap instead, so the service is never looked up.
    shape::StandIn freeService{0x197100u, &FreeThroughAllocatorService, &StandFreeService};
};

// ---- the cheats on both sides ----

struct Kind {
    const char *name;
    uint32_t ctor, execute, dtor, vtable, size;
};
const Kind kEffect{"effect", 0xbc46d0, 0xbc7590, 0xbc5930, kEffectVTable, 0xdc};
const Kind kManager{"effectsManager", 0xbc4fb0, 0xbc7e30, 0xbc59a0, kManagerVTable, 0xb4};
const Kind kOverlay{"overlay", 0xbd9da0, 0xbdade0, 0xbda350, kOverlayVTable, 0xbc};

cICommand *MakeOurs(const Kind &k, void *memory, cITSEffectsManager *manager)
{
    if (&k == &kEffect) return new (memory) cTSEffectCheat(manager);
    if (&k == &kManager) return new (memory) cTSEffectsManagerCheat(manager);
    return new (memory) cOverlayCheat(manager);
}
void *MakeSide(difftest::Context &ctx, const Kind &k, int side, void *memory, cITSEffectsManager *manager)
{
    if (side == 0) return ctx.Original<void *(__thiscall *)(void *, cITSEffectsManager *)>(k.ctor)(memory, manager);
    return MakeOurs(k, memory, manager);
}
// A slot through the object's own vtable (the image's on the original object, ours on ours).
template <class Fn> Fn Slot(void *object, int slot)
{
    return VirtualSlot<Fn>(object, slot);
}
cITSEffectsManager *Manager()
{
    return reinterpret_cast<cITSEffectsManager *>(gW.mgr);
}

uint32_t Word(const void *object, uint32_t offset)
{
    uint32_t w;
    std::memcpy(&w, static_cast<const uint8_t *>(object) + offset, 4);
    return w;
}
const char *Ptr(const void *object, uint32_t offset)
{
    return reinterpret_cast<const char *>(uintptr_t(Word(object, offset)));
}

// The cheat's fields after a command (the option parser's own bytes excepted: heap pointers; its bit word compared).
void DumpCheat(const Kind &k, const void *o, bool threw)
{
    L("  parser %s refs %u", NameOf(Ptr(o, 4)).c_str(), Word(o, 8));
    if (&k == &kEffect) {
        L("  manager %s bits %08x name %s texture %s", NameOf(Ptr(o, 0xc)).c_str(), Word(o, 0x8c), Str(Ptr(o, 0xa8)).c_str(),
          Str(Ptr(o, 0xac)).c_str());
        std::string words;
        for (uint32_t off = 0xb0; off < 0xdc; off += 4) {
            char w[12];
            std::snprintf(w, sizeof(w), " %08x", Word(o, off));
            words += w;
        }
        L("  fields%s", words.c_str());
        // The effect slots (+0x10, a vector of counted pointers).
        const void *const *first = reinterpret_cast<const void *const *>(uintptr_t(Word(o, 0x10)));
        const void *const *last = reinterpret_cast<const void *const *>(uintptr_t(Word(o, 0x14)));
        std::string slots;
        for (const void *const *p = first; p != last; ++p) slots += " " + NameOf(*p);
        L("  slots [%u]%s", unsigned(last - first), slots.c_str());
    } else if (&k == &kManager) {
        L("  manager %s bits %08x pattern %s", NameOf(Ptr(o, 0xc)).c_str(), Word(o, 0x80), Str(Ptr(o, 0x9c)).c_str());
        L("  fields %08x %08x %08x %08x %08x", Word(o, 0xa0), Word(o, 0xa4), Word(o, 0xa8), Word(o, 0xac), Word(o, 0xb0));
        L("  mgr display %02x %02x noAlpha %02x", gW.mgr[0xb50], gW.mgr[0xb51], gW.mgr[0xb52]);
    } else {
        // The decal name points into the arguments only when this command's -decal was parsed (otherwise it is what an
        // earlier command left, or the constructor's garbage).
        const bool decal = !threw && (Word(o, 0x7c) >> 1 & 1);
        L("  manager %s bits %08x type %08x decal %s", NameOf(Ptr(o, 0x98)).c_str(), Word(o, 0x7c), Word(o, 0x9c),
          decal ? Str(Ptr(o, 0xa0)).c_str() : "-");
        L("  fields %08x %08x %08x %08x %08x %08x", Word(o, 0xa4), Word(o, 0xa8), Word(o, 0xac), Word(o, 0xb0), Word(o, 0xb4),
          Word(o, 0xb8));
    }
}

void DumpWorld()
{
    std::string refs;
    for (const EffectFake &e : gW.effects) {
        char w[16];
        std::snprintf(w, sizeof(w), " fx%u:%d", e.id, e.refs);
        refs += w;
    }
    L("world refs%s resources %d resource %d overlaySvc %d flag1 %d", refs.c_str(), gW.resources.refs, gW.resource.refs,
      gW.overlaySvc.refs, int(gW.flag1));
}

// Runs one script (the commands in order) on a side: the cheat built, given a parser (slot 3), each command parsed and
// executed (thrown cErrors compared by their text and types), the fields dumped, then the cheat destroyed (slot 9 without
// freeing).
void RunScript(difftest::Context &ctx, const Kind &k, int side, uint32_t seed, const std::vector<std::string> &script, Log &log)
{
    BuildWorld(seed);
    gLog = &log;
    alignas(16) static uint8_t memory[0x100];
    std::memset(memory, 0x5a, sizeof(memory));
    void *object = MakeSide(ctx, k, side, memory, Manager());
    Slot<void(__thiscall *)(void *, void *, void *)>(object, 3)(object, &gW.cheatParser, nullptr);
    for (const std::string &command : script) {
        L("> %s", command.c_str());
        char mark[96];
        std::snprintf(mark, sizeof(mark), "%s seed %u: %.60s", side ? "ours" : "original", seed, command.c_str());
        ctx.Mark(mark);
        alignas(8) uint8_t argsMemory[sizeof(cArguments)];
        cArguments *args = new (argsMemory) cArguments();
        args->Parse(command.c_str(), 1, 0);
        cptest::Outcome o = cptest::RunGuarded([&] {
            if (side == 0) ctx.Original<void(__thiscall *)(void *, void *)>(k.execute)(object, args);
            else static_cast<cICommand *>(object)->Execute(args);
        });
        if (o.threw) {
            // The vtable word differs (the image's cError table on one side, our owned one on the other); the text and
            // the types it is thrown as are compared.
            const size_t text = o.detail.find(" size ");
            L("  threw%s", text == std::string::npos ? o.detail.c_str() : o.detail.c_str() + text);
        }
        DumpCheat(k, object, o.threw);
        args->~cArguments();
    }
    Slot<void *(__thiscall *)(void *, uint32_t)>(object, 9)(object, 0);
    DumpWorld();
    gLog = nullptr;
}

void Compare(difftest::Context &ctx, const char *what, uint32_t seed, const Log &a, const Log &b)
{
    ++ctx.cases;
    const size_t n = std::max(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const char *x = i < a.size() ? a[i].c_str() : "(end)";
        const char *y = i < b.size() ? b[i].c_str() : "(end)";
        if (std::strcmp(x, y) != 0) {
            // The command being run, then the lines before the difference.
            std::string context;
            for (size_t j = i; j-- > 0;)
                if (a[j].compare(0, 2, "> ") == 0) {
                    if (j + 3 < i) context = "\n      " + a[j] + "\n      ...";
                    break;
                }
            for (size_t j = i >= 3 ? i - 3 : 0; j < i; ++j) context += "\n      " + a[j];
            ctx.Fail("%s seed %u line %u:%s\n    original: %s\n    ours:     %s", what, seed, unsigned(i), context.c_str(), x, y);
            return;
        }
    }
    for (const std::string &line : a)
        if (line.compare(0, 4, "TRAP") == 0) {
            ctx.Fail("%s seed %u: an unexpected call: %s", what, seed, line.c_str());
            return;
        }
}

// ---- scripts ----

template <class T, size_t N> const T &Pick(std::mt19937 &r, const T (&values)[N])
{
    return values[r() % N];
}
const char *const kBools[] = {"true", "false", "1", "0", "on", "off"};
const char *const kNumbers[] = {"0", "1.5", "-2", "10", "0.25", "3"};

std::vector<std::string> EffectScript(uint32_t seed)
{
    std::mt19937 r(seed);
    std::vector<std::string> script;
    uint32_t slots = 0; // the cheat's slot count, as the commands grow it
    const int commands = 3 + int(r() % 8);
    for (int c = 0; c < commands; ++c) {
        std::string s = "effect";
        // A bad option (at the end) makes Parse throw: nothing of the command runs, the slots do not grow.
        const bool bad = r() % 14 == 0;
        const bool named = r() % 2 == 0;
        if (named) s += std::string(" ") + Pick(r, {"fx_a", "fx_b", "fx_bad", "nosuch"});
        uint32_t id = 0;
        std::string tail;
        if (r() % 3 == 0) {
            id = r() % 4;
            s += " -id " + std::to_string(id);
        }
        // -texture is a `string` option on a const char * field (the original's fault): the parser assigns a std::string
        // over +0xac..+0xc3, the pointer "reggrid2", the position, the slot (its size word) and the frame (its capacity
        // word). With the capacity above 15 it copies into "reggrid2" itself (a write into read-only data in the game),
        // with the text no longer than the capacity into the bytes at +0xac (the pointer becomes characters); only a
        // capacity below the length and at most 15 grows it on the heap: the pointer becomes a copy, the slot the length,
        // the frame the new capacity. Each -texture here comes after a -frame that sets such a capacity.
        if (r() % 5 == 0) {
            const char *name = Pick(r, {"tex1", "a_texture_name_past_the_short_buffer"});
            tail = std::string(" -frame ") + Pick(r, {"0", "3"}) + " -texture " + name;
            id = uint32_t(std::strlen(name));
        }
        if (named && !bad && id >= slots) slots = id + 1;
        // -pos reads the slot unchecked (the original's fault): only where it exists.
        if (id < slots && r() % 3 == 0) {
            s += std::string(" -pos ") + Pick(r, kNumbers) + " " + Pick(r, kNumbers);
            if (r() % 2) s += std::string(" ") + Pick(r, kNumbers);
        }
        const char *const flags[] = {" -start", " -stop", " -hard", " -soft", " -clear", " -edit"};
        for (const char *f : flags)
            if (r() % 6 == 0) s += f;
        if (!tail.empty()) s += tail;
        else if (r() % 5 == 0) s += std::string(" -frame ") + Pick(r, {"0", "7", "-3"});
        if (r() % 5 == 0) s += std::string(" -state ") + Pick(r, {"0", "2", "11"});
        if (r() % 5 == 0) {
            s += std::string(" -attract ") + Pick(r, kNumbers);
            if (r() % 2) s += std::string(" ") + Pick(r, {"(1,2,3)", "(0.5,-1,4)", "1,2,3"});
        }
        if (r() % 5 == 0) s += std::string(" -paused ") + Pick(r, kBools);
        if (r() % 5 == 0) s += std::string(" -hidden ") + Pick(r, kBools);
        if (bad) s += Pick(r, {" -frame", " -bogus", " -pos 1"});
        script.push_back(s);
    }
    return script;
}

std::vector<std::string> ManagerScript(uint32_t seed, bool resources)
{
    std::mt19937 r(seed);
    std::vector<std::string> script;
    const int commands = 2 + int(r() % 6);
    for (int c = 0; c < commands; ++c) {
        std::string s = "effectsManager";
        const char *const patterns[] = {"", " fx*", " a"};
        const char *const plain[] = {" -stats", " -locationInfo", " -memStats", " -version", " -v", " -save"};
        for (const char *p : plain)
            if (r() % 6 == 0) s += p;
        if (r() % 5 == 0) s += std::string(" -listActive") + Pick(r, patterns);
        if (r() % 5 == 0) s += std::string(" -list") + Pick(r, patterns);
        if (r() % 6 == 0) s += std::string(" -where") + Pick(r, patterns);
        if (r() % 5 == 0) s += std::string(" -kill") + Pick(r, patterns);
        if (r() % 5 == 0) s += std::string(" -showBounds ") + Pick(r, kBools);
        if (r() % 6 == 0) s += std::string(" -boundsSchema ") + Pick(r, {"0", "3", "-1"});
        if (r() % 5 == 0) s += std::string(" -showColliders ") + Pick(r, kBools);
        if (r() % 5 == 0) s += std::string(" -noAlpha ") + Pick(r, kBools);
        if (r() % 5 == 0)
            s += std::string(" -appFlag ") + Pick(r, {"0", "5", "31", "32", "-1", "100"}) + " " + Pick(r, kBools);
        if (r() % 5 == 0) s += std::string(" -state ") + Pick(r, {"active", "inactive", "pause", "hidden", "bogus"});
        // -load calls the resource service unchecked (the original's fault): only when there is one.
        if (resources && r() % 5 == 0) s += " -load";
        if (r() % 14 == 0) s += Pick(r, {" -bogus", " -appFlag 3"});
        script.push_back(s);
    }
    return script;
}

std::vector<std::string> OverlayScript(uint32_t seed)
{
    std::mt19937 r(seed);
    std::vector<std::string> script;
    const int commands = 2 + int(r() % 5);
    for (int c = 0; c < commands; ++c) {
        std::string s = std::string("overlay ") + Pick(r, {"terrain", "water", "terrain", "water", "lava", ""});
        if (r() % 2) s += " -status";
        if (r() % 2) {
            s += std::string(" -decal ") + Pick(r, {"spot", "a_decal_name_past_the_short_buffer"}) + " " +
                 Pick(r, {"(1,2,3)", "(0.5,-1,4)"}) + " " + Pick(r, kNumbers);
            if (r() % 2) s += std::string(" ") + Pick(r, kNumbers);
        }
        if (r() % 12 == 0) s += " -bogus";
        script.push_back(s);
    }
    return script;
}

void RunKind(difftest::Context &ctx, const Kind &k, uint32_t seeds)
{
    Env env{ctx};
    ExecuteStands stands;
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        BuildWorld(seed);
        std::vector<std::string> script = &k == &kEffect    ? EffectScript(seed)
                                          : &k == &kManager ? ManagerScript(seed, gW.resourcesOn)
                                                            : OverlayScript(seed);
        Log logs[2];
        for (int side = 0; side < 2; ++side) {
            ctx.Mark(side ? "ours" : "original");
            RunScript(ctx, k, side, seed, script, logs[side]);
        }
        Compare(ctx, k.name, seed, logs[0], logs[1]);
        if (!ctx.failures.empty()) return;
    }
}

// ---- tests ----

DIFF_TEST(EffectCheat_Execute, 0xbc7590)
{
    RunKind(ctx, kEffect, 400);
}

DIFF_TEST(EffectsManagerCheat_Execute, 0xbc7e30)
{
    RunKind(ctx, kManager, 400);
}

DIFF_TEST(OverlayCheat_Execute, 0xbdade0)
{
    RunKind(ctx, kOverlay, 300);
}

// The constructors (the fields they set, the rest left as it was; the parser's definition through Help and the Execute
// tests), and every slot through the image's vtable on the original object and ours on ours: QueryInterface (0x1d0b4a),
// the counts, the parser, GetName, GetHelp, the base's slots 7, 8, 10, 11, and the last Release deleting the object
// (slot 9, the deleting destructor: what it releases and frees).
void CheckSlots(difftest::Context &ctx, const Kind &k)
{
    Env env{ctx};
    Log logs[2];
    for (int side = 0; side < 2; ++side) {
        BuildWorld(1); // each side starts from the same world (the effects handed out, the references)
        Log &log = logs[side];
        gLog = &log;
        void *object = ::operator new(k.size);
        std::memset(object, 0xa5, k.size);
        MakeSide(ctx, k, side, object, Manager());
        const uint32_t vt = Word(object, 0);
        if (side == 0 && vt != uint32_t(reinterpret_cast<uintptr_t>(ctx.image.At(k.vtable))))
            ctx.Fail("%s: the original's vtable word %08x", k.name, vt);
        // Every byte outside the option parser (its first and last words are compared as data: the mode and the end).
        const uint32_t specAt = &k == &kEffect ? 0x1c : &k == &kManager ? 0x10 : 0xc;
        std::string bytes;
        for (uint32_t off = 4; off < k.size; off += 4) {
            if (off >= specAt && off < specAt + 0x8c) continue;
            char w[24];
            const uint32_t word = Word(object, off);
            if (word == uint32_t(reinterpret_cast<uintptr_t>(gW.mgr))) std::snprintf(w, sizeof(w), " +%x:mgr", off);
            else std::snprintf(w, sizeof(w), " +%x:%08x", off, word);
            bytes += w;
        }
        L("constructed%s", bytes.c_str());
        // The parser's bit word starts clear.
        L("bits %08x", Word(object, specAt + 0x70));

        void *out = reinterpret_cast<void *>(uintptr_t(0x5eed));
        const uint32_t iids[] = {1, 0xaada39c7, 0x12345678, 0, 0xc97cf5cd};
        for (uint32_t iid : iids) {
            out = reinterpret_cast<void *>(uintptr_t(0x5eed));
            const bool got = Slot<bool(__thiscall *)(void *, uint32_t, void **)>(object, 0)(object, iid, &out);
            L("QI %08x -> %d out %s refs %u", iid, int(got), out == object ? "self" : out == reinterpret_cast<void *>(0x5eed) ? "unset" : "other",
              Word(object, 8));
        }
        L("QI null out -> %d refs %u", int(Slot<bool(__thiscall *)(void *, uint32_t, void **)>(object, 0)(object, 1, nullptr)),
          Word(object, 8));
        for (int i = 0; i < 2; ++i) L("AddRef -> %u", Slot<uint32_t(__thiscall *)(void *)>(object, 1)(object));
        L("Release -> %u", Slot<uint32_t(__thiscall *)(void *)>(object, 2)(object));
        Slot<void(__thiscall *)(void *, void *, void *)>(object, 3)(object, &gW.cheatParser, &gW.terrain);
        L("SetContext: parser %s", NameOf(Ptr(object, 4)).c_str());
        L("GetName %s", Str(Slot<const char *(__thiscall *)(void *)>(object, 5)(object)).c_str());
        for (uint32_t index = 0; index < 4; ++index)
            L("GetHelp %u %s", index, Str(Slot<const char *(__thiscall *)(void *, uint32_t)>(object, 6)(object, index)).c_str());
        L("slot7 %d", int(Slot<bool(__thiscall *)(void *)>(object, 7)(object)));
        L("slot8 %u", Slot<uint32_t(__thiscall *)(void *)>(object, 8)(object));
        L("slot10 %s", Str(Slot<const char *(__thiscall *)(void *)>(object, 10)(object)).c_str());
        L("slot11 %s", NameOf(Slot<void *(__thiscall *)(void *)>(object, 11)(object)).c_str());
        if (&k == &kEffect) {
            // Two effect slots held (the destructor releases them).
            alignas(8) uint8_t argsMemory[sizeof(cArguments)];
            ExecuteStands stands;
            for (const char *command : {"effect fx_a -id 1", "effect fx_b -id 0"}) {
                cArguments *args = new (argsMemory) cArguments();
                args->Parse(command, 1, 0);
                if (side == 0) ctx.Original<void(__thiscall *)(void *, void *)>(k.execute)(object, args);
                else static_cast<cICommand *>(object)->Execute(args);
                args->~cArguments();
            }
        }
        // The last references: the deleting destructor frees the object.
        uint32_t left = Word(object, 8);
        shape::DeleteRecorder deletes;
        for (; left > 1; --left) Slot<uint32_t(__thiscall *)(void *)>(object, 2)(object);
        deletes.Begin();
        const uint32_t last = Slot<uint32_t(__thiscall *)(void *)>(object, 2)(object);
        deletes.End();
        L("last Release -> %u, %d deletes, the object's %s", last, deletes.Count(),
          deletes.Count() > 0 && deletes.Is(deletes.Count() - 1, object, k.size) ? "last, sized" : "missing");
        deletes.Release();
        DumpWorld();
        gLog = nullptr;
    }
    Compare(ctx, k.name, 0, logs[0], logs[1]);
}

DIFF_TEST(EffectCheat_Slots, 0xbc46d0)
{
    CheckSlots(ctx, kEffect);
}

DIFF_TEST(EffectsManagerCheat_Slots, 0xbc4fb0)
{
    CheckSlots(ctx, kManager);
}

DIFF_TEST(OverlayCheat_Slots, 0xbd9da0)
{
    CheckSlots(ctx, kOverlay);
}

// The makers (0xbc5aa0, 0xbdb700): what they allocate (sizes in order, the cheats among them by their vtable and
// manager), nothing else touched; the cheats are then destroyed by the test.
struct Allocations {
    static constexpr int kMax = 256;
    void *block[kMax];
    size_t bytes[kMax];
    int count;
};
Allocations gAllocations;
void Allocated(void *p, size_t bytes)
{
    if (gAllocations.count < Allocations::kMax) {
        gAllocations.block[gAllocations.count] = p;
        gAllocations.bytes[gAllocations.count] = bytes;
    }
    ++gAllocations.count;
}
void Resized(void *, size_t) {}
void Freed(void *) {}
const difftest::HeapWatcher kWatcher{&Allocated, &Resized, &Freed};

DIFF_TEST(EffectCheats_Makers, 0xbc5aa0)
{
    Env env{ctx};
    BuildWorld(2);
    // Our cheats' vtable words, from a cheat of each kind.
    uint32_t ourVTables[3];
    const Kind *kinds[3] = {&kEffect, &kManager, &kOverlay};
    for (int i = 0; i < 3; ++i) {
        alignas(16) uint8_t memory[0x100];
        cICommand *c = MakeOurs(*kinds[i], memory, Manager());
        ourVTables[i] = Word(memory, 0);
        c->~cICommand();
    }
    struct Maker {
        const char *name;
        uint32_t rva;
        void(__cdecl *ours)(void *, cITSEffectsManager *);
        std::vector<const Kind *> made;
    };
    const Maker makers[] = {{"MakeEffectCheats", 0xbc5aa0, &MakeEffectCheats, {&kEffect, &kManager}},
                            {"MakeOverlayCheat", 0xbdb700, &MakeOverlayCheat, {&kOverlay}}};
    for (const Maker &m : makers) {
        Log logs[2];
        for (int side = 0; side < 2; ++side) {
            gLog = &logs[side];
            uint8_t cheats[12];
            std::memset(cheats, 0x77, sizeof(cheats));
            gAllocations.count = 0;
            difftest::WatchHeap(&kWatcher);
            if (side == 0) ctx.Original<void(__cdecl *)(void *, cITSEffectsManager *)>(m.rva)(cheats, Manager());
            else m.ours(cheats, Manager());
            difftest::WatchHeap(nullptr);
            const int count = std::min(gAllocations.count, Allocations::kMax);
            std::string sizes;
            for (int i = 0; i < count; ++i) sizes += " " + std::to_string(gAllocations.bytes[i]);
            L("%d allocations:%s", gAllocations.count, sizes.c_str());
            bool untouched = true;
            for (uint8_t byte : cheats) untouched = untouched && byte == 0x77;
            L("cheat list untouched %d", int(untouched));
            // The cheats, in the order made: each block of the kind's size that carries the side's vtable for it (all
            // found before any is destroyed: a freed block is never read).
            std::vector<std::pair<int, const Kind *>> found;
            for (int i = 0; i < count; ++i)
                for (const Kind *k : m.made) {
                    const int index = k == &kEffect ? 0 : k == &kManager ? 1 : 2;
                    const uint32_t vt =
                        side == 0 ? uint32_t(reinterpret_cast<uintptr_t>(ctx.image.At(k->vtable))) : ourVTables[index];
                    if (gAllocations.bytes[i] == k->size && Word(gAllocations.block[i], 0) == vt) found.emplace_back(i, k);
                }
            for (const auto &[i, k] : found) {
                void *block = gAllocations.block[i];
                const uint32_t managerAt = k == &kOverlay ? 0x98 : 0xc;
                L("made %s (allocation %d): manager %s refs %u parser %s", k->name, i, NameOf(Ptr(block, managerAt)).c_str(),
                  Word(block, 8), NameOf(Ptr(block, 4)).c_str());
                L("  help %s", Str(Slot<const char *(__thiscall *)(void *, uint32_t)>(block, 6)(block, 0)).c_str());
            }
            for (const auto &[i, k] : found) {
                void *block = gAllocations.block[i];
                Slot<void *(__thiscall *)(void *, uint32_t)>(block, 9)(block, 1);
            }
            gLog = nullptr;
        }
        Compare(ctx, m.name, 0, logs[0], logs[1]);
    }
}

// 0xbc97f0 KillOrphans: the active effects the pool alone holds (count 1) stopped at once (Slot4(1)), with flushing held
// off (+0x8e) during the walk and the active count (+0xb00) cleared.
DIFF_TEST(EffectsManager_KillOrphans, 0xbc97f0)
{
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Log logs[2];
        for (int side = 0; side < 2; ++side) {
            BuildWorld(seed);
            std::mt19937 r(seed + 11);
            gLog = &logs[side];
            for (int i = 0; i < kEffects; ++i) {
                gW.effects[i].refs = int32_t(r() % 3); // 0, 1 (orphaned) or 2
                gW.pool[i] = &gW.effects[i];
            }
            const uint32_t active = r() % (kEffects * 2 + 1);
            for (uint32_t i = 0; i < active; ++i) gW.active[i] = r() % kEffects;
            SetVector(0xa74, gW.pool, sizeof(gW.pool));
            SetVector(0xa8c, gW.active, active * 4);
            MgrWord(0xb00) = r();
            gW.mgr[0x8e] = uint8_t(r() % 2);
            if (side == 0) ctx.Original<void(__thiscall *)(void *)>(0xbc97f0)(gW.mgr);
            else reinterpret_cast<cTSEffectsManager *>(gW.mgr)->KillOrphans();
            L("after: active count %08x noFlush %02x", MgrWord(0xb00), unsigned(gW.mgr[0x8e]));
            DumpWorld();
            gLog = nullptr;
        }
        Compare(ctx, "KillOrphans", seed, logs[0], logs[1]);
        if (!ctx.failures.empty()) return;
    }
}

} // namespace
