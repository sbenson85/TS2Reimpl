// Differential tests for the display monitor (src/gz/cDisplayMonitorW32.*; lane VU, O-c): the factory 0x1545c0 and every
// slot of the object it makes, the original's through the image's vtable 0xd9c978 (QueryInterface 0x1547b0, AddRef
// 0x148fb0, Release 0x153f50, the name shared_vf_154780, the work area shared_vf_1547e0, the rectangle 0x154790, the
// aspect shared_vf_1547a0, SetMonitor 0x154620, GetMonitor 0x46390, the deleting destructor 0x154550), ours through ours.
//
// Each case makes a monitor, asks it for a seeded interface, sets a seeded monitor once or twice, reads every slot and
// releases it to destruction. The multimon statics are preset (initialised; USER32's GetMonitorInfoA a recording fake or
// missing, the one-monitor fallback) and the imports SetMonitor calls are faked with impfx (SystemParametersInfoA,
// GetSystemMetrics, lstrcpyA, CreateDCA, GetDeviceCaps, DeleteDC), the same for both sides. Compared: the call log with
// arguments (the device names as text), every result, and the object's words: the count, the monitor, the name (size and
// text), both rectangles and the aspect.
//
// Not compared where the original reads its uninitialised MONITORINFOEXA (the fallback for another handle, or no work
// area: src/gz/cDisplayMonitorW32.cpp, ORIGINAL-BUG): the name, the rectangles and the text CreateDCA gets. The name's
// capacity after an assignment is the string library's (ours is the toolchain's), so only its first value is compared.
#include "difftest.h"
#include "drawfx.h"
#include "impfx.h"

#include "game/game.h"
#include "gz/MultiMon.h"
#include "gz/cDisplayMonitorW32.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>

namespace {

constexpr uint32_t kInitDone = 0x10c96d4, kGetMonitorInfoStatic = 0x10c96c8;
constexpr uint32_t kFake = nMultiMon::kFakeMonitor;

struct Script {
    bool real = true;           // USER32's GetMonitorInfoA found (else the fallback)
    uint32_t infoResult = 1;
    RECT monitorRect{}, workRect{};
    uint32_t flags = 1;
    std::string name;           // what GetMonitorInfoA writes as the device
    bool spiOk = true;
    RECT spiWork{};
    int metrics[2] = {1024, 768};
    bool dcOk = true;
    int caps[2] = {320, 240};   // HORZSIZE, VERTSIZE
    bool defined = true;        // the original writes nothing uninitialised into the object
} gScript;

void NoteText(const char *label, const char *text)
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = label;
    c.text = text;
    drawfx::gLog.push_back(std::move(c));
}

void NoteRect(const char *label, const RECT &r)
{
    drawfx::Note(label, {uint32_t(r.left), uint32_t(r.top), uint32_t(r.right), uint32_t(r.bottom)});
}

// USER32's GetMonitorInfoA, as the stubs' static holds it: logs and writes the whole structure.
BOOL WINAPI FakeGetMonitorInfo(HMONITOR monitor, MONITORINFO *info)
{
    drawfx::Note("user32.GetMonitorInfoA", {uint32_t(reinterpret_cast<uintptr_t>(monitor)), info->cbSize});
    auto *ex = reinterpret_cast<MONITORINFOEXA *>(info);
    ex->rcMonitor = gScript.monitorRect;
    ex->rcWork = gScript.workRect;
    ex->dwFlags = gScript.flags;
    std::memset(ex->szDevice, 0, sizeof(ex->szDevice));
    std::memcpy(ex->szDevice, gScript.name.c_str(), gScript.name.size());
    return gScript.infoResult;
}

uint32_t SystemParametersInfoH(drawfx::Fake &, const uint32_t *a)
{
    if (gScript.spiOk) *reinterpret_cast<RECT *>(uintptr_t(a[2])) = gScript.spiWork;
    return gScript.spiOk;
}
uint32_t GetSystemMetricsH(drawfx::Fake &, const uint32_t *a) { return uint32_t(gScript.metrics[a[0] & 1]); }
uint32_t LstrcpyH(drawfx::Fake &, const uint32_t *a)
{
    std::strcpy(reinterpret_cast<char *>(uintptr_t(a[0])), reinterpret_cast<const char *>(uintptr_t(a[1])));
    return a[0];
}
uint32_t CreateDCH(drawfx::Fake &, const uint32_t *a)
{
    if (gScript.defined) {
        NoteText("CreateDCA driver", reinterpret_cast<const char *>(uintptr_t(a[0])));
        NoteText("CreateDCA device", reinterpret_cast<const char *>(uintptr_t(a[1])));
    }
    drawfx::Note("CreateDCA same", uint32_t(a[0] == a[1]));
    return gScript.dcOk ? 0x4a0000d0u : 0u;
}
uint32_t GetDeviceCapsH(drawfx::Fake &, const uint32_t *a)
{
    return a[1] == HORZSIZE ? uint32_t(gScript.caps[0]) : a[1] == VERTSIZE ? uint32_t(gScript.caps[1]) : 0x77u;
}

uint32_t Pick(std::mt19937 &g, std::initializer_list<uint32_t> v) { return v.begin()[g() % v.size()]; }
RECT RandomRect(std::mt19937 &g)
{
    RECT r;
    r.left = LONG(int32_t(g() % 4000) - 2000);
    r.top = LONG(int32_t(g() % 3000) - 1500);
    r.right = r.left + LONG(g() % 3000);
    r.bottom = r.top + LONG(g() % 2000);
    return r;
}
std::string RandomName(std::mt19937 &g)
{
    switch (g() % 6) {
    case 0: return "";
    case 1: return "\\\\.\\DISPLAY1";
    case 2: return "\\\\.\\DISPLAY" + std::to_string(g() % 40);
    default: {
        const unsigned n = g() % 32; // up to CCHDEVICENAME - 1: some past the string's 15 in-place characters
        std::string s;
        for (unsigned i = 0; i < n; ++i) s += char('!' + g() % 90);
        return s;
    }
    }
}

uint32_t Handle(std::mt19937 &g) { return Pick(g, {0, kFake, kFake, 0x5e000001, 0x5e000002, kFake + 1}); }

void SeedScript(uint32_t seed)
{
    std::mt19937 g(seed);
    Script &s = gScript;
    s = Script{};
    s.real = g() % 3 != 0;
    s.infoResult = Pick(g, {1, 1, 0});
    s.monitorRect = RandomRect(g);
    s.workRect = RandomRect(g);
    s.flags = Pick(g, {0, 1, 3});
    s.name = RandomName(g);
    s.spiOk = g() % 5 != 0;
    s.spiWork = RandomRect(g);
    s.metrics[0] = int(Pick(g, {640, 1024, 1920, 0}));
    s.metrics[1] = int(Pick(g, {480, 768, 1080, 0}));
    s.dcOk = g() % 6 != 0;
    for (int &c : s.caps) c = int(Pick(g, {0, 1, 7, 320, 240, 477, 268, 0xffffffffu, 0x80000000u, 0x7fffffffu, g()}));
}

struct World {
    impfx::Imports imp;
    explicit World(difftest::Context &ctx) : imp(ctx)
    {
        IMPFX_ADD(imp, SystemParametersInfoA, "uu").On(&SystemParametersInfoH);
        IMPFX_ADD(imp, GetSystemMetrics, "u").On(&GetSystemMetricsH);
        IMPFX_ADD(imp, lstrcpyA, "is").On(&LstrcpyH);
        IMPFX_ADD(imp, CreateDCA).On(&CreateDCH);
        IMPFX_ADD(imp, GetDeviceCaps, "iu").On(&GetDeviceCapsH);
        IMPFX_ADD(imp, DeleteDC).Returns(1);
    }
};

// The slots, called through the object's own vtable.
template <unsigned N, class R, class... A> R Slot(void *object, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*static_cast<void ***>(object))[N])(object, args...);
}

template <class T> T At(const void *object, uint32_t offset)
{
    T v;
    std::memcpy(&v, static_cast<const uint8_t *>(object) + offset, sizeof(T));
    return v;
}

// The object's words: the count, the monitor, the aspect; with `whole`, the name and both rectangles.
void NoteObject(const char *label, void *object, bool whole, bool work, bool capacity)
{
    drawfx::Note(label, {At<uint32_t>(object, 4), At<uint32_t>(object, 8), At<uint32_t>(object, 0x44)});
    if (capacity) drawfx::Note("capacity", At<uint32_t>(object, 0x20));
    if (!whole) return;
    drawfx::Note("name size", At<uint32_t>(object, 0x1c));
    const char *text = At<uint32_t>(object, 0x20) > 15 ? At<const char *>(object, 0xc)
                                                        : static_cast<const char *>(object) + 0xc;
    NoteText("name", text);
    NoteRect("rect", At<RECT>(object, 0x24));
    if (work) NoteRect("work", At<RECT>(object, 0x34));
}

void ReadSlots(void *object, bool whole, bool known)
{
    RECT out{-7, -7, -7, -7};
    const RECT *r = Slot<5, RECT *>(object, &out);
    drawfx::Note("rect result", uint32_t(r == &out));
    if (whole) NoteRect("rect out", out);
    RECT work{-7, -7, -7, -7};
    r = Slot<4, RECT *>(object, &work);
    drawfx::Note("work result", uint32_t(r == &work));
    if (known) NoteRect("work out", work);
    const char *name = Slot<3, const char *>(object);
    if (whole) NoteText("name slot", name);
    drawfx::NoteFloat("aspect", Slot<6, float>(object));
    drawfx::Note("monitor", uint32_t(reinterpret_cast<uintptr_t>(Slot<8, HMONITOR>(object))));
}

using CreateFn = void *(__cdecl *)();

void RunCase(difftest::Context &ctx, World &w, uint32_t seed, unsigned seen[3])
{
    auto side = [&](bool original) {
        drawfx::Cells cells;
        SeedScript(seed);
        return drawfx::Side(
            [&] {
                w.imp.BeginSide();
                cells.Set(kInitDone, 1u);
                cells.Set(kGetMonitorInfoStatic,
                          gScript.real ? reinterpret_cast<const void *>(&FakeGetMonitorInfo) : nullptr);
            },
            [&] {
                std::mt19937 g(seed * 3 + 1);
                void *object = original ? ctx.Original<CreateFn>(0x1545c0)()
                                        : static_cast<void *>(nGZGraphic4::cDisplayMonitorW32::Create());
                NoteObject("made", object, false, false, true);
                drawfx::Note("made name size", At<uint32_t>(object, 0x1c));
                drawfx::Note("made name", uint32_t(At<uint8_t>(object, 0xc)));

                // QueryInterface: a seeded id, a null out-pointer now and then.
                const uint32_t iid = Pick(g, {1, 0xfa12b663, 0xfb64c519, 0, 2, 0xfb64c518, 0x7b64ce01, g()});
                void *got = reinterpret_cast<void *>(uintptr_t(0x5555));
                const bool asked = Slot<0, bool>(object, iid, g() % 6 == 0 ? static_cast<void **>(nullptr) : &got);
                drawfx::Note("qi", {uint32_t(asked), uint32_t(got == object), uint32_t(got == reinterpret_cast<void *>(uintptr_t(0x5555)))});
                drawfx::Note("addref", Slot<1, uint32_t>(object));

                bool whole = true, work = false; // the work area: only a set of a monitor writes it
                const unsigned sets = 1 + g() % 2;
                for (unsigned i = 0; i < sets; ++i) {
                    const uint32_t monitor = Handle(g);
                    const bool defined = monitor == 0 || gScript.real || (monitor == kFake && gScript.spiOk);
                    gScript.defined = defined;
                    whole = defined;
                    if (monitor != 0) work = defined;
                    if (original) seen[monitor == 0 ? 0 : gScript.real ? 1 : 2] += defined;
                    Slot<7, void>(object, reinterpret_cast<HMONITOR>(uintptr_t(monitor)));
                    NoteObject("set", object, whole, work, false);
                }
                ReadSlots(object, whole, work);

                // Up to two more references, so a release can answer a count above 1.
                for (unsigned i = 0, more = g() % 3; i < more; ++i) drawfx::Note("addref more", Slot<1, uint32_t>(object));

                // Released to destruction (the count: 1 from AddRef, 1 more from a found interface, up to 2 more).
                for (int i = 0; i < 6; ++i) {
                    const uint32_t left = Slot<2, uint32_t>(object);
                    drawfx::Note("release", left);
                    if (left == 0) break;
                }
            });
    };
    auto a = side(true);
    auto b = side(false);
    std::vector<std::string> errors = a.second;
    errors.insert(errors.end(), b.second.begin(), b.second.end());
    drawfx::Same(ctx, a.first, b.first, "DisplayMonitor", seed, errors);
}

} // namespace

DIFF_TEST(DisplayMonitor_Lifetime, 0x1545c0)
{
    World w(ctx);
    unsigned seen[3] = {};
    for (uint32_t seed = 1; seed <= 4000 && ctx.failures.empty(); ++seed) RunCase(ctx, w, seed, seen);
    if (!seen[0] || !seen[1] || !seen[2])
        ctx.Fail("a path never ran (no monitor %u, USER32 %u, fallback %u)", seen[0], seen[1], seen[2]);
}
