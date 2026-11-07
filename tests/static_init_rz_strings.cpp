// Differential test for the owned cRZString "class-inline" XC entries (src/app/StaticInitRZStrings.cpp, lane GY;
// docs/subsystems/startup.md "Stage 3: the static initialisers"): 135 entries, 219 strings.
//
// Each entry against the image's initialiser in its slot (the slot checked to hold it), on the same start state:
// round 0 the image file's own bytes, rounds 1-3 junk over the whole object (or array) and 8 bytes on each side,
// except the capacity of the None and Characters elements, constant-initialised (15) in .data and relied on by their
// thunks (which free the union's pointer when it is above 15). Compared:
//   * construction: the window after the initialiser, the alias global (slots 643, 644), and the destructor
//     registration (exactly one);
//   * destruction: the registered thunk run (the image's, ours), the window after it and the heap blocks it freed
//     (difftest::FreeRecorder, in the order freed: which element's buffer, and its size).
// What the comparison leaves out, and why:
//   * the pointer of a long string (capacity above 15) after construction: each side allocates its own block; it is
//     compared by its size + 1 characters instead;
//   * bytes 1-3 of that pointer after destruction: the destructor writes only the union's first byte (the
//     terminator: the image's thunks inline _Tidy_deallocate, e.g. 0xd90f98 `mov byte ptr [0xffa3e0], 0`, or call
//     0x141455 / 0x141193), so the rest of the freed block's address stays, and it differs between the sides.
// The image's atexit (0x824cbe) is redirected to a recorder for the test; ours goes through staticinit::BindAtExit,
// bound to the same recorder (as tests/static_init_objects.cpp).
#include "difftest.h"
#include "free_recorder.h"
#include "app/StaticInit.h"

#include <windows.h>

#include <cstring>
#include <iterator>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kXcFirst = 0xd9a724;
constexpr uint32_t kAtExit = 0x824cbe;
constexpr uint32_t kMargin = 8;

using Thunk = void(__cdecl *)();
std::vector<Thunk> gRegistered;
int __cdecl RecordAtExit(Thunk f)
{
    gRegistered.push_back(f);
    return 0;
}

struct Redirect {
    uint8_t *site;
    uint8_t saved[5];
    Redirect(const difftest::Context &ctx, uint32_t rva, const void *target) : site(ctx.image.At<uint8_t>(rva))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    ~Redirect() { std::memcpy(site, saved, 5); }
};

struct AtExitCapture {
    Redirect image;
    staticinit::RegisterAtExit saved;
    explicit AtExitCapture(const difftest::Context &ctx)
        : image(ctx, kAtExit, reinterpret_cast<const void *>(&RecordAtExit)), saved(staticinit::BoundAtExit())
    {
        staticinit::BindAtExit(&RecordAtExit);
        gRegistered.clear();
    }
    ~AtExitCapture() { staticinit::BindAtExit(saved); }
};

// The 135 entries (slot, image initialiser) of kind class-inline with class cRZString in db/initializers.jsonl,
// written down independently of our table: ours must name exactly these, in this order.
struct Expected {
    uint32_t slot, rva;
};
constexpr Expected kExpected[] = {
    {108, 0x138479}, {139, 0x138950}, {140, 0x1388f0}, {141, 0x138b10}, {142, 0x138990}, {143, 0x138a90},
    {144, 0x138bb0}, {145, 0x138b40}, {146, 0x1389a0}, {147, 0x138a10}, {149, 0x138d00}, {150, 0x138c80},
    {151, 0x139220}, {152, 0x138c40}, {153, 0x138d70}, {154, 0x138f70}, {155, 0x139000}, {156, 0x1390d0},
    {157, 0x139070}, {158, 0x1391b0}, {159, 0x139140}, {160, 0x138f40}, {161, 0x138ee0}, {162, 0x138e80},
    {163, 0x138e20}, {164, 0x138db0}, {187, 0x139520}, {188, 0x1394c0}, {196, 0x1397b0}, {198, 0x13a720},
    {199, 0x13a6a0}, {200, 0x13a620}, {201, 0x13a5e0}, {202, 0x13a5f0}, {203, 0x1397d0}, {204, 0x139c20},
    {209, 0x13a8a0}, {210, 0x13a830}, {211, 0x13a800}, {221, 0x13aa20}, {222, 0x13aae0}, {226, 0x13ab20},
    {228, 0x13ab90}, {229, 0x13b200}, {232, 0x13b490}, {344, 0x13ccfc}, {347, 0x13cda2}, {348, 0x13cd1e},
    {349, 0x13cd4a}, {350, 0x13cd76}, {351, 0x13ce52}, {352, 0x13cdce}, {353, 0x13cdfa}, {354, 0x13ce26},
    {355, 0x13cf02}, {356, 0x13ce7e}, {357, 0x13ceaa}, {358, 0x13ced6}, {359, 0x13cfb2}, {360, 0x13cf2e},
    {361, 0x13cf5a}, {362, 0x13cf86}, {365, 0x13d011}, {369, 0x13d103}, {370, 0x13d0d7}, {371, 0x13d15b},
    {372, 0x13d12f}, {373, 0x13d1b3}, {374, 0x13d187}, {465, 0x13db74}, {466, 0x13daf0}, {467, 0x13db48},
    {468, 0x13db1c}, {469, 0x13dc24}, {470, 0x13dba0}, {471, 0x13dbf8}, {472, 0x13dbcc}, {473, 0x13dcd4},
    {474, 0x13dc50}, {475, 0x13dca8}, {476, 0x13dc7c}, {486, 0x13de65}, {487, 0x13de39}, {488, 0x13debd},
    {489, 0x13de91}, {491, 0x13df30}, {492, 0x13df88}, {493, 0x13df5c}, {494, 0x13df04}, {498, 0x13e000},
    {623, 0x13ef90}, {624, 0x13f3a0}, {643, 0x13f685}, {644, 0x13f6bc}, {646, 0x13f6f3}, {654, 0x13f930},
    {655, 0x13f9f0}, {656, 0x13fa50}, {657, 0x13fb10}, {658, 0x13fdf0}, {659, 0x13f8d0}, {660, 0x13f870},
    {661, 0x13fe50}, {662, 0x13fc70}, {663, 0x13f990}, {664, 0x13fd30}, {665, 0x13fb70}, {666, 0x13fcd0},
    {667, 0x13fd90}, {668, 0x13ff10}, {669, 0x13feb0}, {670, 0x13fbf0}, {671, 0x13f810}, {672, 0x13fab0},
    {673, 0x13f7b0}, {682, 0x140040}, {734, 0x14052c}, {735, 0x140538}, {756, 0x14074e}, {757, 0x1407a6},
    {758, 0x14077a}, {759, 0x1407fe}, {760, 0x1407d2}, {761, 0x14082a}, {763, 0x140893}, {767, 0x140956},
    {768, 0x140982}, {786, 0x140d40}, {787, 0x140bf0}, {788, 0x140cd0}, {789, 0x140b80}, {790, 0x140c60},
    {791, 0x140b10}, {792, 0x140db0}, {800, 0x140f00},
};

uint32_t Dword(const std::vector<uint8_t> &w, size_t at)
{
    uint32_t v;
    std::memcpy(&v, w.data() + at, 4);
    return v;
}

} // namespace

DIFF_TEST(StaticInit_RZStrings, 0x138479)
{
    AtExitCapture capture(ctx);
    size_t n = 0, elementCount = 0;
    const staticinit::RZStringStatic *entries = staticinit::RZStringStatics(&n);
    const staticinit::RZStringElement *elements = staticinit::RZStringElements(&elementCount);
    if (n != 135 || elementCount != 219) ctx.Fail("%u entries (135), %u strings (219)", static_cast<unsigned>(n), static_cast<unsigned>(elementCount));
    std::mt19937 rng(135);
    const uint32_t base = static_cast<uint32_t>(ctx.image.Base());
    for (size_t i = 0; i < n; ++i) {
        const staticinit::RZStringStatic &e = entries[i];
        if (i >= std::size(kExpected) || kExpected[i].slot != e.slot || kExpected[i].rva != e.imageRva)
            ctx.Fail("entry %u: slot %u %08x, expected %s", static_cast<unsigned>(i), e.slot, e.imageRva,
                     i < std::size(kExpected) ? "another" : "none");
        uint32_t held = *ctx.image.At<uint32_t>(kXcFirst + 4 * e.slot) - base;
        if (held != e.imageRva) {
            ctx.Fail("slot %u holds %08x, our table says %08x", e.slot, held, e.imageRva);
            continue;
        }
        const uint32_t size = e.count * e.stride + 2 * kMargin;
        uint8_t *window = ctx.image.At<uint8_t>(e.storage - kMargin);
        const std::vector<uint8_t> file(window, window + size);
        uint32_t *alias = e.alias ? ctx.image.At<uint32_t>(e.alias) : nullptr;
        const uint32_t savedAlias = alias ? *alias : 0;
        auto image = ctx.Original<void(__cdecl *)()>(e.imageRva);
        for (int round = 0; round < 4; ++round) {
            std::vector<uint8_t> start = file;
            uint32_t aliasStart = savedAlias;
            if (round) {
                for (auto &x : start) x = static_cast<uint8_t>(rng());
                aliasStart = static_cast<uint32_t>(rng());
                // The capacity of a None / Characters element stays the file's (15): the thunk frees the union's
                // pointer when it is above 15.
                for (uint32_t k = 0; k < e.count; ++k) {
                    const staticinit::RZStringForm form = elements[e.first + k].form;
                    const size_t at = kMargin + k * e.stride;
                    if (form == staticinit::RZStringForm::None || form == staticinit::RZStringForm::Characters)
                        std::memcpy(&start[at + 0x18], &file[at + 0x18], 4);
                }
            }
            struct Run {
                std::vector<uint8_t> built, destroyed;
                std::vector<std::string> texts;
                uint32_t alias = 0;
                size_t registrations = 0;
                std::vector<size_t> freed;
            };
            auto run = [&](auto init) {
                Run r;
                std::memcpy(window, start.data(), size);
                if (alias) *alias = aliasStart;
                gRegistered.clear();
                init();
                r.built.assign(window, window + size);
                r.alias = alias ? *alias : 0;
                std::vector<bool> heap(e.count);
                std::vector<uintptr_t> buffers(e.count, 0);
                for (uint32_t k = 0; k < e.count; ++k) {
                    const size_t at = kMargin + k * e.stride;
                    heap[k] = Dword(r.built, at + 0x18) > 15;
                    if (!heap[k]) continue;
                    buffers[k] = Dword(r.built, at + 4);
                    const char *p = reinterpret_cast<const char *>(buffers[k]);
                    r.texts.emplace_back(p, Dword(r.built, at + 0x14) + 1);
                    std::memset(&r.built[at + 4], 0, 4);
                }
                r.registrations = gRegistered.size();
                if (r.registrations == 1) {
                    difftest::FreeRecorder rec;
                    gRegistered[0]();
                    rec.Stop();
                    // Each free as (the element whose buffer it was, or -1; the block's size), in order.
                    for (size_t k = 0; k < rec.Count(); ++k) {
                        size_t who = SIZE_MAX;
                        for (uint32_t j = 0; j < e.count; ++j)
                            if (buffers[j] && buffers[j] == reinterpret_cast<uintptr_t>(rec.Pointer(k))) who = j;
                        r.freed.push_back(who);
                        r.freed.push_back(rec.Size(k));
                    }
                    r.destroyed.assign(window, window + size);
                    for (uint32_t k = 0; k < e.count; ++k)
                        if (heap[k]) std::memset(&r.destroyed[kMargin + k * e.stride + 5], 0, 3);
                }
                return r;
            };
            Run a = run([&] { image(); });
            Run b = run([&] { staticinit::InitRZString(&e); });
            ++ctx.cases;
            if (a.registrations != 1 || b.registrations != 1) {
                ctx.Fail("slot %u: %u registrations by the image's entry, %u by ours", e.slot,
                         static_cast<unsigned>(a.registrations), static_cast<unsigned>(b.registrations));
                continue;
            }
            if (a.built != b.built || a.texts != b.texts) ctx.Fail("slot %u round %d: built object differs", e.slot, round);
            if (a.alias != b.alias) ctx.Fail("slot %u round %d: alias %08x, ours %08x", e.slot, round, a.alias, b.alias);
            if (a.destroyed != b.destroyed) ctx.Fail("slot %u round %d: destroyed object differs", e.slot, round);
            if (a.freed != b.freed) ctx.Fail("slot %u round %d: the destructors free different blocks (%u, ours %u)", e.slot, round,
                                             static_cast<unsigned>(a.freed.size()), static_cast<unsigned>(b.freed.size()));
        }
        std::memcpy(window, file.data(), size);
        if (alias) *alias = savedAlias;
    }
}
