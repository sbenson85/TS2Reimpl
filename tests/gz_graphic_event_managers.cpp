// Differential tests of the graphics classes' cached connection-manager getters (lane WI): cGraphicSystemDX's
// 0x159390 (the behaviour template's static, src/gz/cAbstractBehaviorTypeMT.h, cell 0x10c9774) and cDeviceDX's 0x167e00
// (src/gz/cDeviceDX.cpp, cell 0x10ca22c). Both are cdecl with no arguments: the cell's manager, or, with the cell null,
// the event system service's (0x5b1e896f as 0x1b1e895a, asked of the framework's slot 5; its slot 3 answers the
// manager, stored in the cell when there is a service; the service released either way).
//
// The world, per side: the cell preset to a tagged manager or null; the framework cell 0x10c9378 holding a drawfx
// framework fake or null; the fake's GetSystemService handing out the service fake (counted) or nothing; the service's
// GetConnectionManager answering the manager, another tagged object or null. Compared: the call log (every fake call
// with its arguments), the result, the cell after the call and the fakes' counts.
#include "difftest.h"
#include "drawfx_core.h"

#include "gz/cDeviceDX.h"
#include "gz/cGraphicSystemDX.h"

#include <cstdint>

namespace {

constexpr uint32_t kFrameworkCell = 0x10c9378;
constexpr uint32_t kGraphicCell = 0x10c9774;
constexpr uint32_t kDeviceCell = 0x10ca22c;
constexpr uint32_t kManagerTag = 0x7b000000;
constexpr uint32_t kOtherTag = 0x7c000000;

inline constexpr drawfx::Slot kFrameworkSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},
    {3, drawfx::Ret::Int, true, "GetSystemService", "uui"},
};
inline constexpr drawfx::Iface kFramework{"cIGZFrameWork", 6, kFrameworkSlots};

inline constexpr drawfx::Slot kServiceSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {0, drawfx::Ret::Int, true, "GetConnectionManager", ""},
};
inline constexpr drawfx::Iface kService{"cIEventSystemService", 4, kServiceSlots};

drawfx::Fake gFramework;
drawfx::Fake gService;
uint32_t gManager[4];
uint32_t gOther[4];

uint32_t Word(const void *p) { return uint32_t(uintptr_t(p)); }

// The framework: GetSystemService answers the service (counted) when value[0] says so, for the event service's ids.
uint32_t FrameworkGetService(drawfx::Fake &self, const uint32_t *args)
{
    if (!self.value[0] || args[0] != 0x5b1e896f || args[1] != 0x1b1e895a) return 0;
    ++gService.refs;
    *reinterpret_cast<void **>(uintptr_t(args[2])) = &gService;
    return 1;
}

class Cells {
public:
    explicit Cells(uint32_t cell) : cell_(cell), saved_(*GameAddress<uint32_t>(cell)), framework_(*GameAddress<uint32_t>(kFrameworkCell)) {}
    ~Cells()
    {
        *GameAddress<uint32_t>(cell_) = saved_;
        *GameAddress<uint32_t>(kFrameworkCell) = framework_;
    }
    Cells(const Cells &) = delete;
    Cells &operator=(const Cells &) = delete;

private:
    uint32_t cell_, saved_, framework_;
};

// Modes (seed): the cell preset or null; the framework there or not; the service given or not; the manager the
// service answers (ours, another, null).
void BuildWorld(uint32_t cell, uint32_t seed)
{
    gFramework.Reset<kFramework>(0x970);
    gFramework.On(5, &FrameworkGetService);
    gFramework.value[0] = (seed >> 2) % 2;
    gService.Reset<kService>(0x971);
    const uint32_t answer = (seed >> 3) % 3;
    gService.Returns(3, answer == 0 ? Word(gManager) : answer == 1 ? Word(gOther) : 0u);
    drawfx::AddRange(gManager, sizeof(gManager), kManagerTag);
    drawfx::AddRange(gOther, sizeof(gOther), kOtherTag);
    *GameAddress<void *>(cell) = seed % 2 ? static_cast<void *>(gManager) : nullptr;
    *GameAddress<void *>(kFrameworkCell) = (seed >> 1) % 2 ? &gFramework : nullptr;
}

template <class Ours>
void RunGetter(difftest::Context &ctx, const char *what, uint32_t rva, uint32_t cell, Ours ours)
{
    using Getter = void *(__cdecl *)();
    Cells cells(cell);
    for (uint32_t seed = 0; seed < 24; ++seed) {
        auto side = [&](bool useOurs) {
            return drawfx::Side([&] { BuildWorld(cell, seed); }, [&] {
                void *r = useOurs ? ours() : ctx.Original<Getter>(rva)();
                drawfx::Note("result", drawfx::Norm(Word(r)));
                drawfx::Note("cell", drawfx::Norm(*GameAddress<uint32_t>(cell)));
                drawfx::Note("refs", {uint32_t(gService.refs), uint32_t(gFramework.refs)});
                // A second call answers from the cell when the first stored one.
                void *again = useOurs ? ours() : ctx.Original<Getter>(rva)();
                drawfx::Note("again", drawfx::Norm(Word(again)));
                drawfx::Note("refs", {uint32_t(gService.refs), uint32_t(gFramework.refs)});
            });
        };
        auto a = side(false);
        auto b = side(true);
        if (!drawfx::Same(ctx, a.first, b.first, what, seed, a.second.empty() ? b.second : a.second)) return;
    }
}

} // namespace

// 0x159390 cGraphicSystemDX's getter (the template's static for cGraphicSystemDXBehaviorTraits).
DIFF_TEST(GraphicEventManager_GraphicSystemDX, 0x159390)
{
    using Behavior = nGZEvent::cAbstractBehaviorTypeMT<nGZGraphic4::cIGraphicSystemDX, nGZGraphic4::cGraphicSystemDXBehaviorTraits>;
    RunGetter(ctx, "graphic system getter", 0x159390, kGraphicCell, [] { return static_cast<void *>(Behavior::GetEventManager()); });
}

// 0x167e00 cDeviceDX's getter.
DIFF_TEST(GraphicEventManager_DeviceDX, 0x167e00)
{
    RunGetter(ctx, "device getter", 0x167e00, kDeviceCell,
              [] { return static_cast<void *>(nGZGraphic4::cDeviceDX::GetEventManager()); });
}
