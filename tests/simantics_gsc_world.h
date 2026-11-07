#pragma once
// A reusable recording-fake world for whole-case differential tests of GenericSimCall (opcode 0x01, 0x58bc50; lane Z4).
//
// Built after tests/simantics_prims_gsc_case43.cpp (lane Z3) but data-driven: a test declares fake TYPES (a name, how many
// instances, and for each virtual slot the number and kind of its argument words and the kind of its return value). Each
// slot gets a machine-code thunk (so the callee pops exactly the declared number of words, as the original's callees do) that
// logs (object, slot, arguments) and returns a scripted value derived from the world's seed, so the original (the mapped
// image) and ours run on identical worlds and the whole log, the result (or thrown code) and the final reference counts of all
// fakes are compared. Slots nobody declared are traps: they log and throw, so a call to an unexpected slot (a wrong receiver,
// say) shows up as a divergence or a stop at the same place.
//
// Argument kinds (one character per stack word, in push order from the first argument):
//   V  a value (pointers into the world are normalised, stack addresses logged as a marker, the rest raw)
//   O  a void** out parameter: logged as present plus what it held; the slot writes a scripted object (AddRef'd) into it
//   S  const char *: logged by content           G  16 bytes at the pointer: logged by content
//   T  a cRZString * logged by content           t  a cRZString * logged by content and then filled with a scripted text
//   M  a message object: logged by content (one level deep)
//   p  a pointer logged only as present
// Return kinds (first character, then options):
//   v  void (the register holds junk)          i{a,b,..}  32-bit integer from the list (a default list when absent)
//   h{..}  16-bit result, junk in the top half  b  bool in al, junk in the other bytes
//   o:Type[?|!]  an object of a declared type ('?': may be null, '!': always the first instance)
//   T  a pointer to a world cRZString holding a scripted text   p  a pointer to 16 scripted bytes   c  a const char * to a text
//   f{..}  a float in st0
// A slot may also carry a hook that runs after the logging and produces the return value itself.
#include "game_allocator.h"
#include "difftest.h"
#include "game/game.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZString.h"
#include "simantics/PrimGenericSimCall.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace gscw {

constexpr int kMaxFakes = 96;
constexpr int kSlots = 400;
constexpr uint32_t kSelfId = 0xfff0;
constexpr uint32_t kHeapId = 0xfff1;

struct Fake {
    void **vt;              // +0x00
    int32_t *vbptr;         // +0x04 virtual-base table (for types with a sub-object)
    int32_t refs;           // +0x08
    uint32_t type;          // +0x0c
    uint32_t index;         // +0x10 instance number within its type
    int32_t vbtable[2];     // +0x14 {0, displacement of the sub-object from base + 4}
    void **subVt;           // +0x1c the sub-object: first word is its vtable pointer
    uint8_t data[0xe0];     // scratch the hooks may use
};
static_assert(sizeof(Fake) == 0x100, "Fake size");

struct Call;
struct Slot {
    int slot = 0;
    std::string args;
    std::string ret = "v";
    std::string out; // type name (with ?/!) of the object written through an 'O' argument
    std::function<uint32_t(Call &)> hook;
    bool quiet = false; // no log entry, no throw injection (library-level helpers such as an allocator)
};

struct TypeSpec {
    std::string name;
    int count = 1;
    bool vbase = false; // has a virtual-base sub-object (slots 1, 2 and `subSlots` live on it)
    std::vector<Slot> slots;
    std::vector<Slot> subSlots;
};

struct RetSpec {
    char kind = 'v';
    std::vector<int32_t> pool;
    int type = -1;
    bool nullable = false, fixedFirst = false;
};

struct SlotDesc {
    int type = -1;
    int slot = 0;
    bool sub = false;
    bool trap = false;
    int builtin = 0; // 1 AddRef, 2 Release
    std::string args;
    RetSpec ret;
    int outType = -1;
    bool outNullable = false, outFixed = false;
    std::function<uint32_t(Call &)> hook;
    int argWords = 0;
    bool quiet = false;
};

struct World {
    alignas(16) uint8_t self[0x300];
    Fake fk[kMaxFakes];
    uint8_t blobs[8][16];
    cRZString texts[8];
    uint32_t seed;
    // the object under test's argument words (slot 19 of the self type returns them)
    int16_t args[8];
    uint32_t argHigh;
    uint32_t user[16]; // free for the tests' hooks
};

struct Call;
struct Call {
    Fake *self;       // null for the object under test
    uint32_t id;
    int slot;
    const uint32_t *a;
    const SlotDesc *desc;
    uint32_t Rand(uint32_t salt = 0) const;
};

inline World *gWorld = nullptr;
inline std::vector<uint32_t> gTrace;
inline std::vector<TypeSpec> gTypes;
inline std::vector<std::unique_ptr<SlotDesc>> gDescs;
inline std::vector<int> gFirst;
inline uintptr_t gImageBase;
inline uint32_t gImageSize;
inline uint32_t gThrowAt, gTickCount;
inline std::set<uint64_t> gCovered, gSpecced;
inline float gFloatRet;
inline uint8_t *gThunkPool;
inline size_t gThunkUsed;
inline bool gWorldBuilt;
inline int gCrashPrinted;
inline const bool gProgress = std::getenv("GSCW_PROGRESS") != nullptr;

// ---- implementation (header-only; each test file includes it once) --------------------------------------------------------
inline const char *const kTexts[] = {"", "a", "Bella Goth", "x1", "outfit_nm", "a text that does not fit a small buffer 123", "{0123}", "%d:%d"};
constexpr int kTextCount = sizeof(kTexts) / sizeof(kTexts[0]);

inline uint32_t Mix(uint32_t a, uint32_t b, uint32_t c, uint32_t d = 0)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u) * 0x85ebca6bu ^ (c + 0x165667b1u) * 0xc2b2ae35u ^ (d + 0x27d4eb2fu) * 0x1b873593u;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}

inline uint32_t Call::Rand(uint32_t salt) const
{
    return Mix(gWorld->seed, id, static_cast<uint32_t>(slot), salt);
}

inline uint32_t NormWord(uint32_t v)
{
    const uintptr_t base = reinterpret_cast<uintptr_t>(gWorld);
    if (v >= base && v < base + sizeof(World)) return 0x40000000u + static_cast<uint32_t>(v - base);
    const NT_TIB *tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
    if (v >= reinterpret_cast<uintptr_t>(tib->StackLimit) && v < reinterpret_cast<uintptr_t>(tib->StackBase)) return 0x5ac40000u;
    return v;
}

inline void Log(uint32_t v) { gTrace.push_back(v); }

inline void LogString(const char *s)
{
    if (!s) {
        Log(0xdead5001u);
        return;
    }
    const size_t n = std::strlen(s);
    Log(static_cast<uint32_t>(n));
    for (size_t i = 0; i < n; ++i) Log(static_cast<uint8_t>(s[i]));
}

inline void LogBytes(const void *p, size_t n)
{
    if (!p) {
        Log(0xdead5002u);
        return;
    }
    for (size_t i = 0; i < n; ++i) Log(static_cast<const uint8_t *>(p)[i]);
}

// A word of a heap object: image addresses (vtables) raw, other readable pointers by what they point at (one level).
inline void LogDeepWord(uint32_t w, bool deeper)
{
    if (w >= gImageBase && w < gImageBase + gImageSize) {
        Log(w - static_cast<uint32_t>(gImageBase) + 0x10000000u);
        return;
    }
    if (w >= 0x10000 && deeper) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(reinterpret_cast<void *>(static_cast<uintptr_t>(w)), &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
            !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && static_cast<uintptr_t>(w) + 32 <= reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
            Log(0xbeef0001u);
            const uint32_t *p = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(w));
            for (int i = 0; i < 8; ++i) LogDeepWord(p[i], false);
            return;
        }
    }
    Log(NormWord(w));
}

inline int TypeIndex(const std::string &name)
{
    for (size_t i = 0; i < gTypes.size(); ++i) {
        if (gTypes[i].name == name) return static_cast<int>(i);
    }
    std::printf("      unknown fake type %s\n", name.c_str());
    std::abort();
}

inline Fake *Instance(const char *type, int index = 0)
{
    return &gWorld->fk[gFirst[TypeIndex(type)] + index];
}
inline void *InstancePtr(const char *type, int index = 0) { return Instance(type, index); }

inline void ParseObjectSpec(const std::string &s, int &type, bool &nullable, bool &fixed)
{
    std::string n = s;
    nullable = fixed = false;
    if (!n.empty() && n.back() == '?') {
        nullable = true;
        n.pop_back();
    } else if (!n.empty() && n.back() == '!') {
        fixed = true;
        n.pop_back();
    }
    type = TypeIndex(n);
}

inline RetSpec ParseRet(const std::string &s)
{
    RetSpec r;
    if (s.empty()) return r;
    r.kind = s[0];
    if (r.kind == 'o') {
        ParseObjectSpec(s.substr(2), r.type, r.nullable, r.fixedFirst);
        return r;
    }
    const size_t open = s.find('{');
    if (open != std::string::npos) {
        std::string list = s.substr(open + 1, s.find('}') - open - 1);
        size_t pos = 0;
        while (pos < list.size()) {
            size_t comma = list.find(',', pos);
            if (comma == std::string::npos) comma = list.size();
            r.pool.push_back(static_cast<int32_t>(std::strtoll(list.substr(pos, comma - pos).c_str(), nullptr, 0)));
            pos = comma + 1;
        }
    }
    return r;
}

inline Fake *FakeOf(void *p)
{
    uint8_t *b = static_cast<uint8_t *>(p);
    if (b >= reinterpret_cast<uint8_t *>(gWorld->fk) && b < reinterpret_cast<uint8_t *>(gWorld->fk + kMaxFakes)) return reinterpret_cast<Fake *>(b);
    return nullptr;
}

inline void *PickObject(int type, bool nullable, bool fixedFirst, uint32_t r)
{
    if (nullable && r % 100 < 28) return nullptr;
    if (type < 0) return nullptr;
    const int count = gTypes[type].count;
    return &gWorld->fk[gFirst[type] + (fixedFirst ? 0 : static_cast<int>((r >> 8) % count))];
}

inline uint32_t Tick()
{
    if (++gTickCount == gThrowAt) throw static_cast<short>(0x66);
    return 0;
}

inline uint32_t __cdecl Dispatch(const SlotDesc *d, void *selfp, const uint32_t *a)
{
    World &w = *gWorld;
    uint8_t *sp = static_cast<uint8_t *>(selfp);
    if (d->sub) sp -= offsetof(Fake, subVt);
    Fake *f = FakeOf(sp);
    const uint32_t id = f ? static_cast<uint32_t>(f - w.fk) : (sp == w.self ? kSelfId : kHeapId);
    const int slot = d->slot;
    const uint32_t logSlot = static_cast<uint32_t>(slot) | (d->sub ? 0x200u : 0u);
    if (d->trap) {
        static std::set<uint64_t> reported;
        if (reported.insert(static_cast<uint64_t>(d->type + 1) << 32 | logSlot).second) {
            std::printf("      call to an undeclared slot: %s slot 0x%x%s\n", d->type >= 0 ? gTypes[d->type].name.c_str() : "?", static_cast<unsigned>(slot),
                        d->sub ? " (sub)" : "");
        }
        Log(id);
        Log(0x7a70000u | logSlot);
        throw static_cast<short>(0x5555);
    }
    if (d->quiet) {
        Call quietCall{f, id, slot, a, d};
        return d->hook ? d->hook(quietCall) : 0u;
    }
    gCovered.insert(static_cast<uint64_t>(d->type + 1) << 32 | logSlot);
    if (d->builtin == 2) {
        Log(id);
        Log(logSlot);
        return static_cast<uint32_t>(--f->refs);
    }
    if (slot != 2) Tick(); // Release runs while errors unwind and must not throw
    if (gProgress) {
        std::printf("        call fake %x slot %x\n", id, logSlot);
        std::fflush(stdout);
    }
    Log(id);
    Log(logSlot | static_cast<uint32_t>(d->argWords) << 16);
    if (d->builtin == 1) return static_cast<uint32_t>(++f->refs);
    Call call{f, id, slot, a, d};
    // Answers are stable per (object, slot, first argument): a getter asked twice answers the same, a lookup by key answers per key.
    const uint32_t key = (d->argWords > 0 && d->args[0] == 'V') ? NormWord(a[0]) : 0u;
    const uint32_t r = Mix(w.seed, id, logSlot, key);
    bool writeOut = true;
    uint32_t ret = r;
    // the return value first (an out object depends on it for 'b' slots)
    switch (d->ret.kind) {
    case 'v':
        ret = r;
        break;
    case 'i':
    case 'h': {
        static const int32_t defaults[] = {0, 1, 2, 3, -1, 0x7cff, 0x7d00, 0x100, 0x10000, 5, 0x20, -2};
        const int32_t *pool = d->ret.pool.empty() ? defaults : d->ret.pool.data();
        const size_t n = d->ret.pool.empty() ? sizeof(defaults) / sizeof(defaults[0]) : d->ret.pool.size();
        uint32_t v = static_cast<uint32_t>(pool[(r >> 4) % n]);
        if (d->ret.kind == 'h') v = (Mix(r, 1, 2) & 0xffff0000u) | (v & 0xffffu);
        ret = v;
        break;
    }
    case 'b':
        ret = (Mix(r, 3, 4) & 0xffffff00u) | ((r >> 6) % 3 == 0 ? 0u : ((r >> 9) % 7 == 0 ? 0x81u : 1u));
        writeOut = (ret & 0xff) != 0;
        break;
    case 'o':
        ret = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(PickObject(d->ret.type, d->ret.nullable, d->ret.fixedFirst, r)));
        break;
    case 'T':
        ret = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&w.texts[r % 8]));
        break;
    case 'p':
        ret = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(w.blobs[r % 8]));
        break;
    case 'c':
        ret = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(kTexts[r % kTextCount]));
        break;
    case 'f': {
        static const float defaults[] = {0.0f, 1.0f, -1.0f, 0.5f, 2.5f, 100.0f};
        float fv = defaults[r % 6];
        if (!d->ret.pool.empty()) fv = static_cast<float>(d->ret.pool[r % d->ret.pool.size()]);
        std::memcpy(&gFloatRet, &fv, 4);
        ret = 0;
        break;
    }
    default:
        break;
    }
    for (int i = 0; i < d->argWords; ++i) {
        const uint32_t v = a[i];
        switch (d->args[i]) {
        case 'V':
            Log(NormWord(v));
            break;
        case 'p':
            Log(v ? 1u : 0u);
            break;
        case 'O': {
            void **out = reinterpret_cast<void **>(static_cast<uintptr_t>(v));
            Log(out ? 1u : 0u);
            if (out) {
                Log(*out ? 0xbad0u : 0u);
                void *obj = nullptr;
                if (writeOut) obj = PickObject(d->outType, d->outNullable, d->outFixed, Mix(r, static_cast<uint32_t>(i), 0x55));
                *out = obj;
                if (obj) ++static_cast<Fake *>(obj)->refs;
            }
            break;
        }
        case 'S':
            LogString(reinterpret_cast<const char *>(static_cast<uintptr_t>(v)));
            break;
        case 'G':
            LogBytes(reinterpret_cast<const void *>(static_cast<uintptr_t>(v)), 16);
            break;
        case 'D': // the dword the pointer points at
            Log(v ? *reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(v)) : 0xdead5004u);
            break;
        case 'E': // 8 bytes
            LogBytes(reinterpret_cast<const void *>(static_cast<uintptr_t>(v)), 8);
            break;
        case 'N': // 12 bytes (a position or a tile)
            LogBytes(reinterpret_cast<const void *>(static_cast<uintptr_t>(v)), 12);
            break;
        case 'B': // 24 bytes (a box)
            LogBytes(reinterpret_cast<const void *>(static_cast<uintptr_t>(v)), 24);
            break;
        case 'T':
        case 't': {
            cRZString *s = reinterpret_cast<cRZString *>(static_cast<uintptr_t>(v));
            LogString(s ? s->mString.c_str() : nullptr);
            if (s && d->args[i] == 't') s->mString = kTexts[Mix(r, static_cast<uint32_t>(i), 0x66) % kTextCount];
            break;
        }
        case 'M': {
            const uint32_t *m = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(v));
            Log(m ? 1u : 0u);
            if (m) {
                for (int k = 0; k < 11; ++k) LogDeepWord(m[k], true);
            }
            break;
        }
        default:
            Log(v);
            break;
        }
    }
    if (d->hook) ret = d->hook(call);
    return ret;
}

// ---- thunks ----------------------------------------------------------------------------------------------------------------

inline void *MakeThunk(const SlotDesc *d, bool retFloat)
{
    if (!gThunkPool) gThunkPool = static_cast<uint8_t *>(VirtualAlloc(nullptr, 16 << 20, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    uint8_t *p = gThunkPool + gThunkUsed;
    uint8_t *const start = p;
    auto put = [&](std::initializer_list<uint8_t> bytes) {
        for (uint8_t b : bytes) *p++ = b;
    };
    auto put32 = [&](uint32_t v) {
        std::memcpy(p, &v, 4);
        p += 4;
    };
    put({0x51});                   // push ecx
    put({0x8D, 0x44, 0x24, 0x08}); // lea eax, [esp+8]
    put({0x50});                   // push eax
    put({0x51});                   // push ecx
    put({0x68});                   // push imm32
    put32(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(d)));
    put({0xE8}); // call rel32
    put32(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&Dispatch) - (reinterpret_cast<uintptr_t>(p) + 4)));
    put({0x83, 0xC4, 0x10}); // add esp, 16
    if (retFloat) {
        put({0xD9, 0x05});
        put32(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gFloatRet)));
    }
    if (d->argWords == 0) {
        put({0xC3});
    } else {
        put({0xC2, static_cast<uint8_t>(d->argWords * 4), 0x00});
    }
    gThunkUsed += ((p - start) + 15) & ~static_cast<size_t>(15);
    return start;
}

inline void *BuildThunk(int type, int slot, bool sub, const Slot *spec, int builtin)
{
    auto d = std::make_unique<SlotDesc>();
    d->type = type;
    d->slot = slot;
    d->sub = sub;
    d->builtin = builtin;
    if (!spec && !builtin) {
        d->trap = true;
    } else if (spec) {
        d->args = spec->args;
        d->argWords = static_cast<int>(spec->args.size());
        d->ret = ParseRet(spec->ret);
        if (!spec->out.empty()) ParseObjectSpec(spec->out, d->outType, d->outNullable, d->outFixed);
        d->hook = spec->hook;
        d->quiet = spec->quiet;
        gSpecced.insert(static_cast<uint64_t>(type + 1) << 32 | (static_cast<uint32_t>(slot) | (sub ? 0x200u : 0u)));
    }
    const bool fl = d->ret.kind == 'f';
    void *t = MakeThunk(d.get(), fl);
    gDescs.push_back(std::move(d));
    return t;
}

// The vtables are built once per type (the thunks identify the object by `ecx`); instances point at them.
inline std::vector<std::vector<void *>> gVtables, gSubVtables;

inline void InitTypes(std::vector<TypeSpec> types)
{
    gDescs.clear();
    gThunkUsed = 0;
    gCovered.clear();
    gSpecced.clear();
    gTypes = std::move(types);
    gVtables.assign(gTypes.size(), std::vector<void *>(kSlots));
    gSubVtables.assign(gTypes.size(), std::vector<void *>(kSlots));
    gFirst.clear();
    int next = 0;
    for (size_t t = 0; t < gTypes.size(); ++t) {
        gFirst.push_back(next);
        next += gTypes[t].count;
    }
    if (next > kMaxFakes) {
        std::printf("      too many fakes (%d)\n", next);
        std::abort();
    }
}

// Called once all types are registered (their return specs may name each other).
inline void BuildVtables()
{
    for (size_t t = 0; t < gTypes.size(); ++t) {
        const bool isSelf = gTypes[t].name == "Self";
        for (int s = 0; s < kSlots; ++s) {
            const Slot *spec = nullptr;
            for (const Slot &sl : gTypes[t].slots) {
                if (sl.slot == s) spec = &sl;
            }
            int builtin = 0;
            if (!spec && !isSelf && s == 1) builtin = 1;
            if (!spec && !isSelf && s == 2) builtin = 2;
            gVtables[t][s] = BuildThunk(static_cast<int>(t), s, false, spec, builtin);
            if (gTypes[t].vbase) {
                const Slot *sub = nullptr;
                for (const Slot &sl : gTypes[t].subSlots) {
                    if (sl.slot == s) sub = &sl;
                }
                int b2 = 0;
                if (!sub && s == 1) b2 = 1;
                if (!sub && s == 2) b2 = 2;
                gSubVtables[t][s] = BuildThunk(static_cast<int>(t), s, true, sub, b2);
            }
        }
    }
}

// Resets the world to a fresh state for `seed`: instances, vtables, reference counts, the object under test.
inline void ResetWorld(uint32_t seed)
{
    World &w = *gWorld;
    if (gWorldBuilt) {
        for (auto &t : w.texts) t.Destruct();
    }
    gWorldBuilt = true;
    std::memset(&w, 0, sizeof(w));
    w.seed = seed;
    for (size_t t = 0; t < gTypes.size(); ++t) {
        for (int i = 0; i < gTypes[t].count; ++i) {
            Fake &f = w.fk[gFirst[t] + i];
            f.vt = reinterpret_cast<void **>(gVtables[t].data());
            f.refs = 1;
            f.type = static_cast<uint32_t>(t);
            f.index = static_cast<uint32_t>(i);
            if (gTypes[t].vbase) {
                f.vbtable[0] = 0;
                f.vbtable[1] = static_cast<int32_t>(offsetof(Fake, subVt)) - 4;
                f.vbptr = f.vbtable;
                f.subVt = reinterpret_cast<void **>(gSubVtables[t].data());
            }
        }
    }
    std::mt19937 r(seed * 7919u + 13u);
    for (auto &b : w.blobs) {
        for (uint8_t &x : b) x = static_cast<uint8_t>(r() % 5 == 0 ? 0 : r());
    }
    if (r() % 3 == 0) std::memset(w.blobs[r() % 8], 0, 16); // some null GUIDs
    for (int i = 0; i < 8; ++i) w.texts[i].Construct(kTexts[(i + seed) % kTextCount]);
    gTrace.clear();
    gTickCount = 0;
    gThrowAt = (r() % 4 == 0) ? 1 + r() % 40 : 0;
    // the object under test
    PrimGscObject *self = reinterpret_cast<PrimGscObject *>(w.self);
    const int selfType = TypeIndex("Self");
    self->mVTable = reinterpret_cast<void **>(gVtables[selfType].data());
    self->mSecondary = self->mVTable;
}

inline void SetObjectFields(const char *context, const char *field240, const char *field48, const char *field2c8 = nullptr)
{
    PrimGscObject *self = reinterpret_cast<PrimGscObject *>(gWorld->self);
    self->mContext = InstancePtr(context);
    self->mField240 = field240 ? InstancePtr(field240) : nullptr;
    self->mField48 = field48 ? InstancePtr(field48) : nullptr;
    self->mField2c8 = field2c8 ? InstancePtr(field2c8) : nullptr;
}

// ---- running both sides -----------------------------------------------------------------------------------------------------

struct Outcome {
    uint32_t ret = 0;
    int thrown = 0;
    bool crashed = false;
    std::vector<uint32_t> trace;
    std::vector<int32_t> refs;
};

inline const char *gSide = "";
inline DWORD gCrashCode;
inline uintptr_t gCrashAddress, gCrashAccess, gCrashEsp;
inline int CrashFilter(EXCEPTION_POINTERS *info)
{
    if (info->ExceptionRecord->ExceptionCode == 0xE06D7363u) return EXCEPTION_CONTINUE_SEARCH; // C++ exceptions pass
    gCrashCode = info->ExceptionRecord->ExceptionCode;
    gCrashAddress = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress) - gImageBase;
    gCrashAccess = info->ExceptionRecord->NumberParameters > 1 ? info->ExceptionRecord->ExceptionInformation[1] : 0;
    gCrashEsp = info->ContextRecord->Esp;
    return EXCEPTION_EXECUTE_HANDLER;
}

template <typename Call> inline uint32_t Guarded(Call &call, const PrimParamView *param, bool &crashed)
{
    __try {
        return call(param);
    } __except (CrashFilter(GetExceptionInformation())) {
        if (gCrashPrinted++ < 6) {
            std::printf("      CRASH (%s) %lx at image+%zx accessing %zx (esp %zx) after %zu log words, last:", gSide, gCrashCode, gCrashAddress, gCrashAccess,
                        gCrashEsp, gTrace.size());
            for (size_t i = gTrace.size() > 8 ? gTrace.size() - 8 : 0; i < gTrace.size(); ++i) std::printf(" %x", gTrace[i]);
            std::printf("\n");
        }
        crashed = true;
        return 0xdead;
    }
}

template <typename Fn> inline Outcome RunSide(int caseNumber, Fn call)
{
    PrimParamView param;
    std::memset(&param, 0, sizeof(param));
    param.mWord4 = static_cast<int16_t>(caseNumber);
    Outcome o;
    try {
        o.ret = Guarded(call, &param, o.crashed);
    } catch (short code) {
        o.thrown = code;
    }
    o.trace = gTrace;
    for (int i = 0; i < kMaxFakes; ++i) o.refs.push_back(gWorld->fk[i].refs);
    return o;
}

inline bool Equal(const Outcome &a, const Outcome &b, std::string &why)
{
    char buf[512];
    if (a.crashed != b.crashed) {
        why = a.crashed ? "the original crashed, ours did not" : "ours crashed, the original did not";
        return false;
    }
    if (a.crashed) {
        // both crashed (a path the original itself does not guard): the logs up to the crash must agree
        if (a.trace != b.trace) {
            why = "both crashed, but the logs differ";
            return false;
        }
        return true;
    }
    if (a.thrown != b.thrown) {
        std::snprintf(buf, sizeof(buf), "thrown: original %d, ours %d", a.thrown, b.thrown);
        why = buf;
        return false;
    }
    if (!a.thrown && a.ret != b.ret) {
        std::snprintf(buf, sizeof(buf), "result: original %u, ours %u", a.ret, b.ret);
        why = buf;
        return false;
    }
    const size_t n = (std::min)(a.trace.size(), b.trace.size());
    for (size_t i = 0; i < n; ++i) {
        if (a.trace[i] != b.trace[i]) {
            std::string ctx;
            for (size_t k = i > 6 ? i - 6 : 0; k < i; ++k) {
                std::snprintf(buf, sizeof(buf), " %x", a.trace[k]);
                ctx += buf;
            }
            std::snprintf(buf, sizeof(buf), "trace entry %zu: original 0x%x, ours 0x%x (next original 0x%x, ours 0x%x; lengths original %zu, ours %zu); before (original):%s", i, a.trace[i], b.trace[i],
                          i + 1 < n ? a.trace[i + 1] : 0u, i + 1 < n ? b.trace[i + 1] : 0u, a.trace.size(), b.trace.size(), ctx.c_str());
            why = buf;
            return false;
        }
    }
    if (a.trace.size() != b.trace.size()) {
        std::snprintf(buf, sizeof(buf), "trace length: original %zu, ours %zu (the shorter ends with %x)", a.trace.size(), b.trace.size(),
                      n ? a.trace[n - 1] : 0u);
        why = buf;
        return false;
    }
    for (size_t i = 0; i < a.refs.size(); ++i) {
        if (a.refs[i] != b.refs[i]) {
            std::snprintf(buf, sizeof(buf), "reference count of fake %zu: original %d, ours %d", i, a.refs[i], b.refs[i]);
            why = buf;
            return false;
        }
    }
    return true;
}

// ---- code patches for original callees that need the world --------------------------------------------------------------------

struct CodePatch {
    uint8_t *site;
    uint8_t saved[5];
    CodePatch(difftest::Context &ctx, uint32_t rva, void *stand) : CodePatch(ctx.image.At<uint8_t>(rva), stand) {}
    // The same on our own code (a replacement the code under test reaches directly, not through the image), so a callee's
    // stand-in serves both sides of the caller's test.
    CodePatch(void *ours, void *stand) : site(static_cast<uint8_t *>(ours))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stand) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    CodePatch(const CodePatch &) = delete;
    ~CodePatch() { std::memcpy(site, saved, 5); }
};

// ---- the driver ----------------------------------------------------------------------------------------------------------------

struct Summary {
    uint32_t returned[4] = {};
    uint32_t threw = 0, crashed = 0;
    std::map<int, uint32_t> byResult;
    std::map<int, uint32_t> byThrown;
};

// Runs `worlds` seeds: build(seed) fills the world (after ResetWorld and the type table are in place), then the original and ours
// run on identical copies. Returns the summary; failures are reported through ctx.
template <typename Build>
inline Summary RunSeeds(difftest::Context &ctx, int caseNumber, uint32_t worlds, const char *frameType, Build build)
{
    using Fn = uint32_t(__thiscall *)(void *, void *, const void *);
    const Fn original = ctx.Original<Fn>(0x58bc50);
    Summary s;
    auto frame = [&]() -> void * { return frameType ? InstancePtr(frameType) : nullptr; };
    const char *only = std::getenv("GSCW_ONLY");
    for (uint32_t seed = only ? static_cast<uint32_t>(std::atoi(only)) : 1; seed <= (only ? static_cast<uint32_t>(std::atoi(only)) : worlds); ++seed) {
        ++ctx.cases;
        ResetWorld(seed);
        build(seed);
        gSide = "original";
        gTrace.clear();
        if (gProgress) {
            std::printf("      seed %u original\n", seed);
            std::fflush(stdout);
        }
        PrimGscObject *self = reinterpret_cast<PrimGscObject *>(gWorld->self);
        const Outcome a = RunSide(caseNumber, [&](const PrimParamView *p) { return original(self, frame(), p); });
        if (gProgress) {
            std::printf("      original returned %u (thrown %d)\n", a.ret, a.thrown);
            std::fflush(stdout);
        }
        ResetWorld(seed);
        build(seed);
        gSide = "ours";
        gTrace.clear();
        if (gProgress) {
            std::printf("      seed %u ours\n", seed);
            std::fflush(stdout);
        }
        const Outcome b = RunSide(caseNumber, [&](const PrimParamView *p) { return self->GenericSimCall(frame(), p); });
        std::string why;
        if (!Equal(a, b, why)) {
            ctx.Fail("case %d seed %u: %s", caseNumber, seed, why.c_str());
            static int dumped = 0;
            if (dumped++ < 2) {
                size_t at = 0;
                while (at < a.trace.size() && at < b.trace.size() && a.trace[at] == b.trace[at]) ++at;
                const size_t from = at > 24 ? at - 24 : 0;
                for (int side = 0; side < 2; ++side) {
                    const Outcome &o = side ? b : a;
                    std::printf("      %s log from word %zu:", side ? "ours    " : "original", from);
                    for (size_t i = from; i < o.trace.size() && i < at + 24; ++i) std::printf(" %x", o.trace[i]);
                    std::printf("\n");
                }
            }
            continue;
        }
        if (a.crashed) ++s.crashed;
        else if (a.thrown) {
            ++s.threw;
            ++s.byThrown[a.thrown];
        } else {
            ++s.byResult[static_cast<int>(a.ret)];
        }
    }
    return s;
}

inline void Report(const Summary &s)
{
    std::printf("      threw %u, both crashed %u;", s.threw, s.crashed);
    for (auto &kv : s.byResult) std::printf(" result %d x %u;", kv.first, kv.second);
    std::printf("\n      thrown codes:");
    for (auto &kv : s.byThrown) std::printf(" 0x%x x %u;", kv.first, kv.second);
    std::printf("\n");
    for (uint64_t key : gSpecced) {
        if (!gCovered.count(key)) {
            std::printf("      slot never reached: %s slot 0x%x%s\n", gTypes[(key >> 32) - 1].name.c_str(), static_cast<unsigned>(key & 0x1ff),
                        (key & 0x200) ? " (sub)" : "");
        }
    }
}

inline Slot S(int slot, const char *args, const std::string &ret, const char *out = "", std::function<uint32_t(Call &)> hook = nullptr)
{
    Slot s;
    s.slot = slot;
    s.args = args;
    s.ret = ret;
    s.out = out;
    s.hook = std::move(hook);
    return s;
}

// A lookup slot (one value argument) that finds an object of `type` except for `nullPercent` per cent of the keys.
inline Slot Lookup(int slot, const char *args, const char *type, uint32_t nullPercent)
{
    const std::string typeName = type;
    return S(slot, args, std::string("o:") + type + "?", "", [typeName, nullPercent](Call &c) -> uint32_t {
        const uint32_t r = c.Rand(c.desc->argWords > 0 ? NormWord(c.a[0]) : 0u);
        if (r % 100 < nullPercent) return 0;
        const int t = TypeIndex(typeName);
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gWorld->fk[gFirst[t] + static_cast<int>((r >> 8) % gTypes[t].count)]));
    });
}

// The object under test: slot 19 reads argument word i (hooked to the world's words), slot 20 stores one.
inline TypeSpec SelfType(std::vector<Slot> extra)
{
    TypeSpec t;
    t.name = "Self";
    t.slots.push_back(S(19, "V", "i", "", [](Call &c) -> uint32_t {
        const int32_t index = static_cast<int32_t>(c.a[0]);
        if (index < 0 || index >= 8) return gWorld->argHigh;
        return (gWorld->argHigh & 0xffff0000u) | static_cast<uint16_t>(gWorld->args[index]);
    }));
    t.slots.push_back(S(20, "VV", "v"));
    for (Slot &s : extra) t.slots.push_back(std::move(s));
    return t;
}

// The frame: slot 12 the stack object's id word, slot 18 the resume state, slot 19 stores it.
inline TypeSpec FrameType(const char *resumePool = "i")
{
    TypeSpec t;
    t.name = "Frame";
    t.slots = {S(12, "", "h"), S(18, "", resumePool), S(19, "V", "v")};
    return t;
}

// Prologue results of the context: slots 9, 10, 26, 24 (their types must be declared by the test).
inline std::vector<Slot> PrologueSlots(const char *entry, const char *people, const char *objects, const char *defs)
{
    return {S(9, "", std::string("o:") + entry + "!"), S(10, "", std::string("o:") + people + "!"), S(26, "", std::string("o:") + objects + "!"),
            S(24, "", std::string("o:") + defs + "!")};
}

// Random argument words for the object under test: each word from `pools[i]` (a list of values) or fully random.
inline void RandomArgs(uint32_t seed, const std::vector<std::vector<int32_t>> &pools, bool strict = false)
{
    std::mt19937 r(seed * 104729u + 7u);
    World &w = *gWorld;
    for (int i = 0; i < 8; ++i) {
        w.args[i] = static_cast<int16_t>(r());
        if (i < static_cast<int>(pools.size()) && !pools[i].empty() && (strict || r() % 5 != 0)) w.args[i] = static_cast<int16_t>(pools[i][r() % pools[i].size()]);
    }
    w.argHigh = r() % 3 == 0 ? (r() & 0xffff0000u) : 0u;
}

// Messages (cRZMessage2Standard, 0x2c bytes from the game's allocator 0x1be3b0): the allocator is plain memory, and the message
// class's slots in the game's vtable are replaced by the thunks of the fake type "Msg" (declare slots 1, 2, 5, 11, 12, 30 there) so
// the message's own methods log instead of running. Ours allocates through the global allocator pointer (RZMemory).
inline void *__cdecl TestMalloc(uint32_t bytes)
{
    void *p = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (p) std::memset(p, 0xcd, bytes);
    return p;
}

struct MessageSupport {
    CodePatch alloc;
    GeneralAllocator *allocator;
    void **vtable;
    void *saved[64];
    MessageSupport(difftest::Context &ctx) : alloc(ctx, 0x1be3b0, reinterpret_cast<void *>(&TestMalloc))
    {
        allocator = static_cast<GeneralAllocator *>(::operator new(sizeof(GeneralAllocator)));
        allocator->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
        RZMemory::testGlobalAllocator = allocator;
        vtable = ctx.image.At<void *>(cRZMessage2Standard::kVTable);
        DWORD old;
        VirtualProtect(vtable, sizeof(saved), PAGE_READWRITE, &old);
        std::memcpy(saved, vtable, sizeof(saved));
        const int type = TypeIndex("Msg");
        for (const Slot &s : gTypes[type].slots) vtable[s.slot] = gVtables[type][s.slot];
    }
    MessageSupport(const MessageSupport &) = delete;
    ~MessageSupport()
    {
        std::memcpy(vtable, saved, sizeof(saved));
        RZMemory::testGlobalAllocator = nullptr;
        difftest_alloc::ShutdownAndRelease(allocator);
        ::operator delete(allocator);
    }
};

inline void Setup(difftest::Context &ctx, std::vector<TypeSpec> types)
{
    static World world;
    gWorld = &world;
    gImageBase = ctx.image.Base();
    gImageSize = ctx.image.Size();
    // Each test prints its own first crashes: the budget is an inline variable shared by every test on this world, so a test run
    // after another one printed nothing, and the same test printed its crashes or not depending on its place in a shard (lane NB).
    gCrashPrinted = 0;
    InitTypes(std::move(types));
    BuildVtables();
}

} // namespace gscw
