#include "app/StaticInitH5.h"

#include "app/StaticInit.h"
#include "audio/MmxResampleTables.h"
#include "game/game.h"
#include "game/original.h"
#include "misc/Quaternion.h"
#include "misc/StartupObjects.h"
#include "rz/WinApiTables.h"
#include "rz/cRZMemoryReport.h"
#include "rz/cRZStopwatchCalibration.h"

#include <time.h>

#include <array>
#include <utility>

namespace staticinit {

namespace {

using K = H5Kind;
constexpr H5Static kH5[] = {
    {93, 0x1382ac, rz::TryEnterCriticalSectionLoader::kRva, K::TryEnterLoader, 0xd90658},
    {94, 0x1382c2, rz::ShellExecuteLoader::kRva, K::ShellLoader, 0xd906b9},
    {98, 0x138306, 0, K::TimeZone, 0},
    {102, 0x138396, rz::WideFileFunctions::kRva, K::WideFiles, 0},
    {103, 0x1383a0, rz::WideDirectoryFunctions::kRva, K::WideDirectories, 0},
    {105, 0x1383aa, cRZStopwatchCalibration::kRva, K::Calibration, 0},
    {134, 0x1388d0, 0x10cca4e, K::RecordPoolReset, 0},
    {193, 0x1395b0, 0x10d9bbc, K::Rotation, 0},
    {754, 0x140871, cRZMemoryReport::kStatic, K::MemoryReport, 0xd98997},
    {755, 0x140856, cRZMemoryHook::kStatic, K::MemoryHook, 0xd9898d},
    {769, 0x1409ae, MmxResampleTables::kStatic, K::MmxTables, 0xd98a7e},
};
constexpr uint32_t kMemoryHookText = 0xe98100; // the image's entry passes this text; the constructor ignores it

void DestroyH5(const H5Static &e)
{
    switch (e.kind) {
    case K::TryEnterLoader: rz::TryEnterCriticalSectionLoader::Unload(); break;
    case K::ShellLoader: rz::ShellExecuteLoader::Unload(); break;
    case K::MemoryReport:
        OriginalFunction<void(__thiscall *)(void *), 0xc69888>()(GameAddress<void>(e.storage)); // ~cRZMemoryReport
        break;
    case K::MemoryHook: GameAddress<cRZMemoryHook>(e.storage)->Destruct(); break;
    default: break; // MmxTables: nothing
    }
}

template <size_t I> void __cdecl H5Thunk() { DestroyH5(kH5[I]); }
using Thunk = void(__cdecl *)();
template <size_t... I> constexpr std::array<Thunk, sizeof...(I)> H5Thunks(std::index_sequence<I...>) { return {&H5Thunk<I>...}; }
constexpr auto kH5Thunks = H5Thunks(std::make_index_sequence<std::size(kH5)>());

constexpr auto kOwned = [] {
    std::array<winmainstart::OwnedInitialiser, std::size(kH5)> out{};
    for (size_t i = 0; i < std::size(kH5); ++i) out[i] = {kH5[i].slot, kH5[i].imageRva, &InitH5, &kH5[i]};
    return out;
}();

constexpr bool SortedBySlot()
{
    for (size_t i = 1; i < std::size(kH5); ++i)
        if (kH5[i - 1].slot >= kH5[i].slot) return false;
    return true;
}
static_assert(SortedBySlot(), "H5 XC entries: one per slot, in slot order");

} // namespace

void __cdecl InitH5(const void *h5Static)
{
    const H5Static &e = *static_cast<const H5Static *>(h5Static);
    switch (e.kind) {
    case K::TryEnterLoader: GameAddress<rz::TryEnterCriticalSectionLoader>(e.storage)->Construct(); break;
    case K::ShellLoader: GameAddress<rz::ShellExecuteLoader>(e.storage)->Construct(); break;
    case K::TimeZone: _tzset(); break;
    case K::WideFiles: GameAddress<rz::WideFileFunctions>(e.storage)->Construct(); break;
    case K::WideDirectories: GameAddress<rz::WideDirectoryFunctions>(e.storage)->Construct(); break;
    case K::Calibration: GameAddress<cRZStopwatchCalibration>(e.storage)->Construct(); break;
    case K::RecordPoolReset: GameAddress<startupobj::RecordPoolReset>(e.storage)->Construct(); break;
    case K::Rotation: {
        const math::Quaternion a =
            math::Quaternion::FromAxisHalfAngle(GameAddress<float>(kRotationAxisA), *GameAddress<double>(kRotationHalfAngleA));
        math::Quaternion b =
            math::Quaternion::FromAxisHalfAngle(GameAddress<float>(kRotationAxisB), *GameAddress<double>(kRotationHalfAngleB));
        *GameAddress<math::Quaternion>(e.storage) = *b.PreMultiplyNormalised(&a);
        break;
    }
    case K::MemoryReport: GameAddress<cRZMemoryReport>(e.storage)->Construct(); break;
    case K::MemoryHook: GameAddress<cRZMemoryHook>(e.storage)->Construct(GameAddress<const char>(kMemoryHookText)); break;
    case K::MmxTables: GameAddress<MmxResampleTables>(e.storage)->Construct(); break;
    }
    if (e.thunk) BoundAtExit()(kH5Thunks[&e - kH5]);
}

const H5Static *H5Statics(size_t *count)
{
    *count = std::size(kH5);
    return kH5;
}

const winmainstart::OwnedInitialiser *OwnedH5(size_t *count)
{
    *count = kOwned.size();
    return kOwned.data();
}

} // namespace staticinit
