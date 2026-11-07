// Differential tests for nTSCASCore::cTSCASBodyPartSystem, lane JY: the record texts (slot 66 0xced0e0) and the outfit
// properties (slot 28 0xcf9f30). The property set and the slots called on the system are recording fakes; the string
// code (SplitOff, ParseGuidText, the containers) runs for real; Sprintf, atoi and the image's operator new / delete are
// stood in (they reach CRT state the harness has not initialised).
#include "cas_body_part_system.h"
#include "code_patch.h"
#include "rz/cRZString.h"

#include <cstdarg>
#include <cstdio>

namespace {

using namespace bpt;
using nTSCASCore::CasAlloc;
using nTSCASCore::ServiceString;
using nTSCASCore::ServiceStringVector;
using nTSCASCore::SurfaceRecord;
using RecordVector = std::vector<SurfaceRecord, CasAlloc<SurfaceRecord>>;

uint32_t Hash(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = static_cast<const uint8_t *>(p);
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 16777619u;
    return h;
}

uint32_t TextHash(const char *s)
{
    return Hash(2166136261u, s, std::strlen(s));
}

uint32_t gScriptSeed;
uint32_t Pick()
{
    uint32_t x = gScriptSeed ^ (static_cast<uint32_t>(gEvents.size()) * 0x9e3779b9u);
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

const char *const kPieces[] = {"", "a", "Head", "fifteen_chars_x", "sixteen_chars_xy", "a_much_longer_piece_of_text_here"};

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_CollectRecordTexts, 0xced0e0)
{
    std::mt19937 rng(0xced0e0);
    auto orig = ctx.Original<void(__thiscall *)(void *, ServiceStringVector *)>(0xced0e0);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    for (int i = 0; i < 1500; ++i) {
        Scenario sc = RandomScenario(rng);
        std::vector<std::array<int, 4>> records(rng() % 5);
        for (auto &r : records)
            for (int &k : r) k = static_cast<int>(rng() % 6);
        std::vector<int> existing(rng() % 4);
        for (int &k : existing) k = static_cast<int>(rng() % 6);
        const size_t reserve = rng() % 3 == 0 ? 0 : rng() % 8;
        Compare(ctx, "CollectRecordTexts", sc, vt, [&](World &w, bool original) -> uint64_t {
            auto *recs = new RecordVector();
            for (auto &r : records) {
                SurfaceRecord rec;
                for (int k = 0; k < 4; ++k) rec.mText[k] = kPieces[r[k]];
                recs->push_back(rec);
            }
            w.S()->mRecords = recs;
            ServiceStringVector out;
            out.reserve(reserve);
            for (int k : existing) out.push_back(ServiceString(kPieces[k]));
            if (original) orig(w.S(), &out);
            else w.S()->CollectRecordTexts(&out);
            uint32_t h = 2166136261u;
            for (const ServiceString &s : out) {
                const uint32_t cap = static_cast<uint32_t>(s.capacity());
                h = Hash(h, s.c_str(), s.size() + 1);
                h = Hash(h, &cap, 4);
            }
            const uint64_t state = (static_cast<uint64_t>(h) << 32) | (out.size() << 16) | out.capacity();
            w.S()->mRecords = nullptr;
            delete recs;
            return state;
        });
    }
}

// ---- Slot 28 --------------------------------------------------------------------------------------------------------

namespace {

struct PropFake {
    const void *const *vtable;
};
PropFake gQueried, gProps;
const char *gHairName, *gSkinName, *gCountName;  // the property names, read from the image
std::array<const void *, 40> gQueriedVt, gPropsVt;

uint32_t __fastcall QueriedKey(PropFake *, void *, uint32_t id)
{
    gEvents.push_back({0x6013, id});
    return id * 3 + 1;
}
void __fastcall QueriedSet(PropFake *, void *, void **out, uint32_t key)
{
    gEvents.push_back({0x6021, key});
    out[0] = reinterpret_cast<void *>(0x1234);
    out[1] = &gProps;
    out[2] = reinterpret_cast<void *>(0x5a5a0008); // the real callee (0xacc410) writes all 0x14 bytes
    out[3] = reinterpret_cast<void *>(0x5a5a000c);
    out[4] = nullptr;
}

// The values the property set answers: GUID texts, counts (including 0, negative, non-numbers) and overlay lists.
const char *__fastcall PropsGet(PropFake *, void *, const char *name)
{
    gEvents.push_back({0x6118, TextHash(name)});
    const uint32_t x = Pick();
    if (x % 4 == 0) return nullptr;
    // GUID texts for the tones (ParseGuidText's own range is its test's: at most four groups here), counts for the overlay
    // count, lists for the overlays.
    static const char *const kGuids[] = {"0x11223344-55667788-99aabbcc-ddeeff00", "{01234567-89abcdef-01234567-89abcdef}",
                                         "", "12", "deadbeef"};
    static const char *const kCounts[] = {"3", "0", "-2", "x", "2", "1", "5", ""};
    static const char *const kLists[] = {"12345678-12345678-12345678-12345678|second|third", "|leading", "no_delimiter",
                                         "a|b", "", "trailing|", "a_rather_long_overlay_name_past_sixteen|b"};
    if (std::strcmp(name, gHairName) == 0 || std::strcmp(name, gSkinName) == 0) return kGuids[(x >> 4) % 5];
    if (std::strcmp(name, gCountName) == 0) return kCounts[(x >> 4) % 8];
    return kLists[(x >> 4) % 7];
}

template <int kSlot> void __fastcall Unexpected(void *self, void *)
{
    std::fprintf(stderr, "unexpected call of slot %d on a property fake %p\n", kSlot, self);
    std::abort();
}
template <size_t... k> void FillUnexpected(std::array<const void *, 40> &vt, std::index_sequence<k...>)
{
    ((vt[k] = reinterpret_cast<const void *>(&Unexpected<static_cast<int>(k)>)), ...);
}

template <int kSlot> void __fastcall SelfVoid(void *self, void *) { gEvents.push_back({static_cast<uint32_t>(kSlot), Rel(self)}); }
template <int kSlot> void __fastcall SelfGuid(void *self, void *, const uint32_t *g)
{
    gEvents.push_back({static_cast<uint32_t>(kSlot), Rel(self), g[0], g[1], g[2], g[3]});
}
void __fastcall SelfText(void *self, void *, const char *text, uint32_t a, uint32_t b)
{
    gEvents.push_back({38, Rel(self), TextHash(text), a, b});
}

// cRZString::Sprintf (0x1ad6f7 and ours) on both sides: the image's formatting of %d reaches CRT state the harness has not
// initialised. The format and the text formatted are logged.
cRZString *__cdecl StandSprintf(cRZString *self, const char *fmt, ...)
{
    char text[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    gEvents.push_back({0x1ad6f7, TextHash(fmt), TextHash(text)});
    self->mString = text;
    return self;
}

// The image's atoi (0x830972, library code) for the original side; ours calls our CRT's.
int __cdecl StandAtoi(const char *text)
{
    return std::atoi(text);
}

void *__cdecl ImageNew(size_t size) { return ::operator new(size); }
void __cdecl ImageSizedDelete(void *p, size_t size) { ::operator delete(p, size); }

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_ApplyOutfitProperties, 0xcf9f30)
{
    std::mt19937 rng(0xcf9f30);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint8_t)>(0xcf9f30);
    VTable vt = CopyVTable(ctx);
    vt.slots[16] = reinterpret_cast<const void *>(&SelfVoid<16>);
    vt.slots[74] = reinterpret_cast<const void *>(&SelfGuid<74>);
    vt.slots[54] = reinterpret_cast<const void *>(&SelfGuid<54>);
    vt.slots[38] = reinterpret_cast<const void *>(&SelfText);
    Env env(ctx);
    FillUnexpected(gQueriedVt, std::make_index_sequence<40>());
    gQueriedVt[19] = reinterpret_cast<const void *>(&QueriedKey);
    gQueriedVt[33] = reinterpret_cast<const void *>(&QueriedSet);
    gQueried.vtable = gQueriedVt.data();
    FillUnexpected(gPropsVt, std::make_index_sequence<40>());
    gPropsVt[24] = reinterpret_cast<const void *>(&PropsGet);
    gProps.vtable = gPropsVt.data();
    void **const cache = ctx.image.At<void *>(0x10f8fcc); // GetCachedQueriedObject's cache
    void *const saved = *cache;
    *cache = &gQueried;
    gHairName = ctx.image.At<const char>(0xeb9f50);
    gSkinName = ctx.image.At<const char>(0xeb9f5c);
    gCountName = ctx.image.At<const char>(0xdbc964);
    shape::StandIn sprintfStand(0x1ad6f7, &cRZString::Sprintf, &StandSprintf);
    Redirect atoiStand(ctx, 0x830972, reinterpret_cast<const void *>(&StandAtoi));
    // The image's operator new / sized delete (its strings past 15 characters) go to ours.
    Redirect newStand(ctx, 0x824632, reinterpret_cast<const void *>(&ImageNew));
    Redirect deleteStand(ctx, 0x824624, reinterpret_cast<const void *>(&ImageSizedDelete));
    for (int i = 0; i < 3000; ++i) {
        Scenario sc = RandomScenario(rng);
        gScriptSeed = static_cast<uint32_t>(rng());
        const uint32_t id = rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : rng() % 8;
        const uint8_t reset = rng() % 2 ? 0 : static_cast<uint8_t>(rng() % 3 == 0 ? 0x80 : 1);
        Compare(ctx, "ApplyOutfitProperties", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), id, reset);
            else w.S()->ApplyOutfitProperties(id, reset);
            return 0;
        });
    }
    *cache = saved;
}
