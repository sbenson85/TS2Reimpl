// Differential tests for the cDeviceDX scene bracket (src/gz/cDeviceDX.h), against the campaign's shared fake
// IDirect3DDevice9 (tests/d3dfx.h): every device call is logged with its slot name and normalised arguments, and
// LogD3DError's report and service calls land in the same log, so the two sides compare call for call.
//
// BeginScene's outermost call runs cDeviceStateDX's flush (0x17cdf0: the original on the original's side, ours from
// ours) against a 0x7600-byte block of seeded random bytes at one address standing in for the state object: the flush
// only passes pointers into the block to the device and never dereferences the random fields, so every dirty-group
// combination runs. Failing HRESULTs reach LogD3DError. Compared: the log, and every byte of the device object and
// the block.
#include "d3dfx.h"
#include "difftest.h"
#include "gz/cDeviceDX.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

drawfx::Fake gDevice; // the fake IDirect3DDevice9

constexpr size_t kStateBytes = 0x7600; // the part of cDeviceStateDX the flush reads and writes (it ends at 0x75b0)
alignas(16) uint8_t gState[kStateBytes];
alignas(16) uint8_t gDeviceObject[0x100];
alignas(16) uint8_t gSurfaces[0x30]; // three stand-in surfaces (render targets, depth surfaces)
d3dfx::Answers gAnswers;

struct Snapshot {
    std::vector<uint8_t> state, object;
    drawfx::Log log;
};

Snapshot Take() { return {std::vector<uint8_t>(gState, gState + kStateBytes), std::vector<uint8_t>(gDeviceObject, gDeviceObject + sizeof(gDeviceObject)), drawfx::gLog}; }

// An empty log and a fresh fake device with this case's answers.
void Fresh()
{
    drawfx::ResetAll();
    gDevice.Reset<fx_d3d9::IDirect3DDevice9>(d3dfx::kDeviceId);
    gAnswers.Apply(gDevice);
    drawfx::AddRange(gState, kStateBytes, 0x57000000);
    drawfx::AddRange(gSurfaces, sizeof(gSurfaces), 0x58000000);
}

void Restore(const Snapshot &s)
{
    std::memcpy(gState, s.state.data(), kStateBytes);
    std::memcpy(gDeviceObject, s.object.data(), sizeof(gDeviceObject));
    Fresh();
}

void PutFloat(size_t offset, float value) { std::memcpy(gState + offset, &value, 4); }
void PutWord(size_t offset, uint32_t value) { std::memcpy(gState + offset, &value, 4); }

// Seeds the state block, the device object and the device's answers for one case.
void Seed(std::mt19937 &gen, int depth)
{
    for (size_t i = 0; i < kStateBytes; i += 4) PutWord(i, gen());
    for (size_t i = 0; i < sizeof(gDeviceObject); ++i) gDeviceObject[i] = static_cast<uint8_t>(gen());
    // The flush's group flags (0x74, 0x75, 0x7c..0x7f bytes; 0x76..0x7a counts), mostly set so the groups run.
    for (size_t at = 0x74; at <= 0x7f; ++at) gState[at] = gen() % 4 == 0 ? 0 : static_cast<uint8_t>(gen() % 8 + 1);
    gState[0x16] = static_cast<uint8_t>(gen() % 5); // render targets
    PutWord(0x10, drawfx::P(&gDevice));
    // Surface size (ints) and the viewport / scissor fractions (floats) the flush scales by it.
    PutWord(0xaf0, gen() % 4096);
    PutWord(0xaf4, gen() % 4096);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (size_t at = 0xaf8; at <= 0xb04; at += 4) PutFloat(at, unit(gen));
    for (size_t at = 0xb0c; at <= 0xb20; at += 4) PutFloat(at, unit(gen));
    if (gen() % 4 == 0) PutWord(0xaf0, 0x80000000u + gen() % 16); // the unsigned-to-float correction path
    // The pending constant ranges as the state keeps them (start < end <= the array's count), so the flush hands the
    // device pointers into the block's constant arrays: {end offset, end bytes, start offset, count}.
    struct Group {
        size_t end, endBytes, start;
        uint32_t count;
    };
    for (const Group &g : {Group{0x76, 2, 0x80, 0x400}, Group{0x78, 2, 0x84, 0x100}, Group{0x7a, 2, 0x88, 0x100},
                           Group{0x7c, 1, 0x8c, 0xe0}, Group{0x7d, 1, 0x90, 0x10}, Group{0x7e, 1, 0x94, 0x10}}) {
        uint32_t end = 0;
        std::memcpy(&end, gState + g.end, g.endBytes);
        if (!end) continue;
        end = 1 + gen() % g.count;
        std::memcpy(gState + g.end, &end, g.endBytes);
        PutWord(g.start, gen() % end);
    }
    // The wanted (mWanted +0xa0, its targets at +0xa3c) and sent render targets and depth surfaces: null or one of a
    // few surfaces (their addresses are only compared and handed to the device).
    for (size_t at : {size_t(0xadc), size_t(0xae0), size_t(0xae4), size_t(0xae8), size_t(0xaec), size_t(0x7570),
                      size_t(0x7574), size_t(0x7578), size_t(0x757c), size_t(0x7580)})
        PutWord(at, gen() % 5 ? drawfx::P(gSurfaces + 0x10 * (gen() % 3)) : 0);

    auto *device = reinterpret_cast<nGZGraphic4::cDeviceDX *>(gDeviceObject);
    device->mDevice = reinterpret_cast<IDirect3DDevice9 *>(&gDevice);
    device->mState = reinterpret_cast<nGZGraphic4::cDeviceStateDX *>(gState);
    device->mSceneDepth = depth;
    gAnswers.Random(gen, fx_d3d9::IDirect3DDevice9.count);
}

// What the original did over a test's cases: the test fails if a path never ran (a vacuous sweep).
struct Seen {
    int quiet = 0, deviceCalls = 0, loggedFailures = 0, sceneCall = 0;
};

template <typename Original, typename Ours>
void Compare(difftest::Context &ctx, Seen &seen, unsigned sceneSlot, const char *what, int depth, uint32_t round,
             Original original, Ours ours)
{
    Fresh();
    const Snapshot before = Take();
    original();
    const Snapshot a = Take();
    const drawfx::Call *last = d3dfx::LastCall(a.log, fx_d3d9::IDirect3DDevice9);
    seen.quiet += last == nullptr;
    seen.deviceCalls += last != nullptr;
    seen.loggedFailures += d3dfx::Notes(a.log, "LogD3DError") != 0;
    seen.sceneCall += last && last->slot == sceneSlot;
    Restore(before);
    ours();
    const Snapshot b = Take();
    drawfx::ResetAll();
    char label[64];
    std::snprintf(label, sizeof(label), "%s depth %d", what, depth);
    if (!drawfx::Same(ctx, a.log, b.log, label, round)) return;
    if (a.state != b.state || a.object != b.object)
        ctx.Fail("%s round %u: state %d object %d", label, round, a.state == b.state, a.object == b.object);
}

void CheckSeen(difftest::Context &ctx, const char *what, const Seen &seen)
{
    if (!seen.quiet || !seen.deviceCalls || !seen.loggedFailures || !seen.sceneCall)
        ctx.Fail("%s: a path never ran (quiet %d, device calls %d, logged failures %d, scene calls %d)", what, seen.quiet,
                 seen.deviceCalls, seen.loggedFailures, seen.sceneCall);
}

} // namespace

DIFF_TEST(cDeviceDX_BeginScene, 0x167850)
{
    d3dfx::ErrorLog errors(ctx);
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x167850);
    auto *device = reinterpret_cast<nGZGraphic4::cDeviceDX *>(gDeviceObject);
    std::mt19937 gen(0x167850);
    const int depths[] = {0, 0, 0, 1, 2, -1, -2, 5};
    Seen seen;
    for (uint32_t round = 0; round < 400; ++round) {
        const int depth = depths[round % 8];
        Seed(gen, depth);
        Compare(ctx, seen, fx_d3d9::dev::BeginScene, "BeginScene", depth, round, [&] { original(device); },
                [&] { device->cDeviceDX::BeginScene(); }); // the block has no vtable: a qualified call
    }
    CheckSeen(ctx, "BeginScene", seen);
}

DIFF_TEST(cDeviceDX_EndScene, 0x169660)
{
    d3dfx::ErrorLog errors(ctx);
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x169660);
    auto *device = reinterpret_cast<nGZGraphic4::cDeviceDX *>(gDeviceObject);
    std::mt19937 gen(0x169660);
    const int depths[] = {1, 1, 1, 2, 3, 0, -1, 7};
    Seen seen;
    for (uint32_t round = 0; round < 400; ++round) {
        const int depth = depths[round % 8];
        Seed(gen, depth);
        Compare(ctx, seen, fx_d3d9::dev::EndScene, "EndScene", depth, round, [&] { original(device); },
                [&] { device->cDeviceDX::EndScene(); });
    }
    CheckSeen(ctx, "EndScene", seen);
}
