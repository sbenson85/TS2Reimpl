// The device call log (host/shared/devlog.h, host/shared/d3dlog.h; TS2HOST_DEVLOG). Not differential (no original
// function): rva 0. The recorder's frame grouping, digests and full lists; then the Direct3D wrappers over fake
// objects (tests/host_devlog_fakes.h): calls reach the device unchanged, records name resources by content so two runs
// with objects at different addresses log the same text, uploads are seen through the patched lock slots (once, with
// row padding left out), dynamic vertex data drawn is hashed, and calls from another thread are async.
#include "difftest.h"

#include "../host/shared/d3dlog.h"
#include "../host/shared/devlog.h"
#include "host_devlog_fakes.h"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::vector<std::string> Lines(const std::string &text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) out.push_back(line);
    return out;
}

bool Has(const std::string &text, const std::string &part)
{
    return text.find(part) != std::string::npos;
}

std::string Hex12(uint64_t v)
{
    char b[16];
    std::snprintf(b, sizeof(b), "%012llx", static_cast<unsigned long long>(v & 0xffffffffffffull));
    return b;
}

std::string Hex16(uint64_t v)
{
    char b[20];
    std::snprintf(b, sizeof(b), "%016llx", static_cast<unsigned long long>(v));
    return b;
}

} // namespace

DIFF_TEST(HostDevLog_FrameSet, 0)
{
    devlog::FrameSet s;
    if (!s.Parse("900,4000-4002") || !s.Contains(900) || s.Contains(901) || !s.Contains(4000) || !s.Contains(4002) || s.Contains(4003))
        ctx.Fail("900,4000-4002");
    devlog::FrameSet bad;
    if (bad.Parse("9x") || bad.Parse("5-3") || bad.Parse("-") || !bad.Empty()) ctx.Fail("malformed specs accepted");
    ctx.cases += 2;
}

DIFF_TEST(HostDevLog_Recorder, 0)
{
    uint32_t frame = 3;
    std::string out;
    devlog::Recorder rec([&] { return frame; }, [&](std::string_view s) { out.append(s.data(), s.size()); });
    devlog::FrameSet full;
    full.Parse("4");
    rec.SetFullFrames(full);
    rec.SetMainThread(GetCurrentThreadId());

    rec.Record(devlog::Kind::Stream, "A 1");
    rec.Record(devlog::Kind::Async, "Create x");
    frame = 4;
    rec.Record(devlog::Kind::Stream, "B 2", true);
    rec.Record(devlog::Kind::Async, "Create y");
    std::thread([&] { rec.Record(devlog::Kind::Stream, "C 3"); }).join(); // another thread: async, marked
    rec.Record(devlog::Kind::Stream, "Present", false, true);
    rec.Mark("menu idle");
    frame = 6;
    rec.Record(devlog::Kind::Stream, "D 4");
    frame = 5; // a late record (another thread still on frame 5) joins the open frame 6
    rec.Record(devlog::Kind::Stream, "E 5");
    frame = 6;
    rec.RequestFull(2, "lot"); // frame 6 has records already and isn't full: 7 and 8 are
    frame = 7;
    rec.Record(devlog::Kind::Stream, "F 6");
    rec.Finish();
    rec.Finish(); // nothing open: writes nothing

    const auto h = [](const char *s) { return devlog::Hash(s, std::strlen(s)); };
    const std::string f3 = "frame 3 stream 1 " + Hex16(devlog::Mix(0, h("A 1"))) + " async 1 " + Hex16(h("Create x")) + " " +
                           Hex16(devlog::Mix(0, h("Create x"))) + " draws 0 presents 0";
    const uint64_t stream4 = devlog::Mix(devlog::Mix(0, h("B 2")), h("Present"));
    const uint64_t seq4 = devlog::Mix(devlog::Mix(0, h("Create y")), h("w C 3"));
    const std::string f4 = "frame 4 stream 2 " + Hex16(stream4) + " async 2 " + Hex16(h("Create y") + h("w C 3")) + " " + Hex16(seq4) +
                           " draws 1 presents 1";
    const std::vector<std::string> expected = {
        f3,
        "mark 4 menu_idle",
        f4,
        "s 4 0 B 2",
        "a 4 0 Create y",
        "a 4 1 w C 3",
        "s 4 1 Present",
        "full lot 7 2",
        "frame 6 stream 2 " + Hex16(devlog::Mix(devlog::Mix(0, h("D 4")), h("E 5"))) + " async 0 " + Hex16(0) + " " + Hex16(0) +
            " draws 0 presents 0",
        "frame 7 stream 1 " + Hex16(devlog::Mix(0, h("F 6"))) + " async 0 " + Hex16(0) + " " + Hex16(0) + " draws 0 presents 0",
        "s 7 0 F 6",
    };
    const std::vector<std::string> got = Lines(out);
    if (got != expected) {
        ctx.Fail("recorder output differs (%zu lines, expected %zu)", got.size(), expected.size());
        for (size_t i = 0; i < got.size() || i < expected.size(); ++i) {
            const std::string a = i < got.size() ? got[i] : "-", b = i < expected.size() ? expected[i] : "-";
            if (a != b) ctx.Fail("line %zu: got \"%s\", expected \"%s\"", i, a.c_str(), b.c_str());
        }
    }
    ++ctx.cases;
}

namespace {

struct ScenarioResult {
    std::string log;
    std::vector<std::string> deviceCalls;
    DWORD zEnable = 0;
    UINT drawCount = 0;
    bool sameTexture = false; // the device got the texture the game passed, unwrapped
    int surfaceLocks = 0, surfaceUnlocks = 0;
    std::string textureNameAfterUpload, textureNameAtCreation;
};

constexpr uint32_t kVertexBytes = 96;

uint8_t VertexByte(uint32_t i, uint8_t seed)
{
    return static_cast<uint8_t>(i * 7 + seed);
}

// One "game run" against fresh fakes (at fresh addresses, with fresh padding noise). frame7Seed changes only the
// vertex data uploaded in frame 7; texel changes one texel of the texture.
ScenarioResult Run(uint32_t noise, uint8_t frame7Seed, uint8_t texel, bool worker)
{
    ScenarioResult result;
    uint32_t frame = 5;
    devlog::Recorder rec([&] { return frame; }, [&](std::string_view s) { result.log.append(s.data(), s.size()); });
    devlog::FrameSet full;
    full.Parse("5-7");
    rec.SetFullFrames(full);

    auto *fakeD3d = new fakes::FakeDirect3D(noise);
    IDirect3D9 *d3d = d3dlog::Wrap(fakeD3d, rec);
    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = 800;
    pp.BackBufferHeight = 600;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.Windowed = TRUE;
    pp.hDeviceWindow = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x1000 + noise)); // differs per run: never logged
    IDirect3DDevice9 *device = nullptr;
    d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8);
    d3d->CreateDevice(0, D3DDEVTYPE_HAL, pp.hDeviceWindow, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);
    fakes::FakeDevice *fake = fakeD3d->device;

    // Frame 5: resources.
    IDirect3DVertexBuffer9 *vb = nullptr;
    device->CreateVertexBuffer(kVertexBytes, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &vb, nullptr);
    IDirect3DTexture9 *texture = nullptr;
    device->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr);
    D3DLOCKED_RECT locked{};
    result.textureNameAtCreation = "?";
    texture->LockRect(0, &locked, nullptr, 0);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 8; ++x) static_cast<uint8_t *>(locked.pBits)[y * locked.Pitch + x] = static_cast<uint8_t>(y * 8 + x + 1);
    static_cast<uint8_t *>(locked.pBits)[locked.Pitch + 3] = texel;
    texture->UnlockRect(0);

    // Frame 6: upload the vertices, draw with the texture, present.
    frame = 6;
    void *data = nullptr;
    vb->Lock(0, 0, &data, D3DLOCK_DISCARD);
    for (uint32_t i = 0; i < kVertexBytes; ++i) static_cast<uint8_t *>(data)[i] = VertexByte(i, 0);
    vb->Unlock();
    device->SetTexture(0, texture);
    device->SetRenderState(D3DRS_ZENABLE, 1);
    device->SetStreamSource(0, vb, 0, 16);
    device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);
    device->Present(nullptr, nullptr, nullptr, nullptr);
    if (worker) std::thread([&] { device->SetRenderState(D3DRS_LIGHTING, 0); }).join();

    // Frame 7: overwrite the second half (no overwrite), draw it; a second texture upload through the level surface.
    frame = 7;
    vb->Lock(48, 48, &data, D3DLOCK_NOOVERWRITE);
    for (uint32_t i = 0; i < 48; ++i) static_cast<uint8_t *>(data)[i] = VertexByte(i + 48, frame7Seed);
    vb->Unlock();
    device->DrawPrimitive(D3DPT_TRIANGLELIST, 3, 1);
    IDirect3DSurface9 *level = nullptr;
    texture->GetSurfaceLevel(0, &level);
    RECT rect{1, 0, 2, 1};
    level->LockRect(&locked, &rect, 0);
    static_cast<uint8_t *>(locked.pBits)[0] = 0x77;
    level->UnlockRect();
    level->Release();
    device->SetTexture(0, texture);

    // Frame 8: digest only.
    frame = 8;
    D3DRECT clear{0, 0, 10, 10};
    device->Clear(1, &clear, D3DCLEAR_TARGET, 0xff102030, 1.0f, 0);

    result.deviceCalls = fake->calls;
    result.zEnable = fake->renderState[D3DRS_ZENABLE];
    result.drawCount = fake->drawCount;
    result.sameTexture = fake->texture0 == texture;
    auto *levelFake = static_cast<fakes::FakeSurface *>(static_cast<fakes::FakeTexture *>(texture)->mLevel);
    result.surfaceLocks = levelFake->locks;
    result.surfaceUnlocks = levelFake->unlocks;

    vb->Release();
    texture->Release();
    device->Release(); // the last reference: the recorder writes frame 8
    d3d->Release();
    return result;
}

// The record lines of a frame (s/a lines), without the frame's digest line.
std::vector<std::string> FrameLines(const std::string &log, uint32_t frame)
{
    std::vector<std::string> out;
    const std::string s = "s " + std::to_string(frame) + " ", a = "a " + std::to_string(frame) + " ";
    for (const std::string &line : Lines(log))
        if (line.rfind(s, 0) == 0 || line.rfind(a, 0) == 0) out.push_back(line);
    return out;
}

std::string DigestLine(const std::string &log, uint32_t frame)
{
    const std::string head = "frame " + std::to_string(frame) + " ";
    for (const std::string &line : Lines(log))
        if (line.rfind(head, 0) == 0) return line;
    return "";
}

} // namespace

DIFF_TEST(HostDevLog_Wrapper, 0)
{
    const ScenarioResult a = Run(1, 0, 0x40, false);
    const ScenarioResult b = Run(2, 0, 0x40, false); // other addresses, other padding noise, other window handle

    // Forwarding: the device saw every call, with the game's arguments and objects.
    const std::vector<std::string> expectedCalls = {"CreateVertexBuffer", "CreateTexture", "SetTexture", "SetRenderState", "SetStreamSource",
                                                    "DrawPrimitive", "Present", "DrawPrimitive", "SetTexture", "Clear"};
    if (a.deviceCalls != expectedCalls) ctx.Fail("calls reaching the device: %zu, expected %zu", a.deviceCalls.size(), expectedCalls.size());
    if (a.zEnable != 1 || a.drawCount != 1 || !a.sameTexture) ctx.Fail("arguments reaching the device");
    // The texture's lock went through its level surface's (patched) vtable once, without a second upload record.
    if (a.surfaceLocks != 2 || a.surfaceUnlocks != 2) ctx.Fail("level surface locks %d/%d", a.surfaceLocks, a.surfaceUnlocks);
    ++ctx.cases;

    // Same game, other addresses: the same text.
    if (a.log != b.log) {
        ctx.Fail("two runs of the same calls log different text");
        const auto la = Lines(a.log), lb = Lines(b.log);
        for (size_t i = 0; i < la.size() && i < lb.size(); ++i)
            if (la[i] != lb[i]) {
                ctx.Fail("first difference, line %zu: \"%s\" vs \"%s\"", i, la[i].c_str(), lb[i].c_str());
                break;
            }
    }
    ++ctx.cases;

    // Records the comparison relies on.
    const std::vector<std::string> f5 = FrameLines(a.log, 5), f6 = FrameLines(a.log, 6), f7 = FrameLines(a.log, 7);
    std::string all5, all6, all7;
    for (const auto &l : f5) all5 += l + "\n";
    for (const auto &l : f6) all6 += l + "\n";
    for (const auto &l : f7) all7 += l + "\n";
    uint8_t first[48];
    for (uint32_t i = 0; i < 48; ++i) first[i] = VertexByte(i, 0);
    uint8_t whole[kVertexBytes];
    for (uint32_t i = 0; i < kVertexBytes; ++i) whole[i] = VertexByte(i, 0);
    const std::string vbName = "Vd0:";
    if (!Has(all5, "s 5 0 CheckDeviceFormat 0 1 22 0x0 3 21 =0x8876086a") || !Has(all5, "s 5 1 CreateDevice 0 1 1 0x40 800 600 22 0") || !Has(all5, "CreateVertexBuffer 96 0x208 0x0 0 Vd0:") ||
        !Has(all5, "CreateTexture 2 2 1 0x0 21 1 T:"))
        ctx.Fail("frame 5 records:\n%s", all5.c_str());
    // Exactly one upload record for the texture lock, its rows without the padding.
    size_t uploads = 0;
    for (const auto &l : f5) uploads += l.find(" Upload T:") != std::string::npos;
    if (uploads != 1) ctx.Fail("frame 5: %zu texture upload records", uploads);
    if (!Has(all6, "s 6 0 Upload Vd0:") || !Has(all6, " 96 0x0 #" + Hex12(devlog::Hash(whole, sizeof(whole)))))
        ctx.Fail("frame 6 dynamic upload:\n%s", all6.c_str());
    if (!Has(all6, "SetRenderState 7 0x1") ||
        !Has(all6, "DrawPrimitive 4 1 s0=" + vbName) || !Has(all6, "/16 #" + Hex12(devlog::Hash(first, sizeof(first)))) ||
        !Has(all6, "Present null null 0 0"))
        ctx.Fail("frame 6 records:\n%s", all6.c_str());
    if (!Has(DigestLine(a.log, 6), " draws 1 presents 1")) ctx.Fail("frame 6 digest: %s", DigestLine(a.log, 6).c_str());
    if (!Has(all7, "DrawPrimitive 4 1 s0=") || !Has(all7, "/16 #" + Hex12(devlog::Hash(whole + 48, 48))))
        ctx.Fail("frame 7 draw:\n%s", all7.c_str());
    // The surface upload belongs to the texture: its name changes, and SetTexture shows the new one.
    std::string uploadedName;
    for (const auto &l : f7) {
        const size_t at = l.find(" Upload T:");
        if (at != std::string::npos) uploadedName = l.substr(at + 8, 14);
    }
    if (uploadedName.empty() || !Has(all7, "SetTexture 0 " + uploadedName) || Has(all6, "SetTexture 0 " + uploadedName))
        ctx.Fail("texture names: upload %s\n%s", uploadedName.c_str(), all7.c_str());
    if (!Has(DigestLine(a.log, 8), "frame 8 stream 1 ") || !FrameLines(a.log, 8).empty()) ctx.Fail("frame 8: digest only");
    ++ctx.cases;

    // A change in frame 7's vertex data changes frame 7's records and nothing before.
    const ScenarioResult c = Run(3, 1, 0x40, false);
    if (FrameLines(c.log, 6) != f6 || DigestLine(c.log, 6) != DigestLine(a.log, 6)) ctx.Fail("frame 7 change shows in frame 6");
    if (DigestLine(c.log, 7) == DigestLine(a.log, 7) || FrameLines(c.log, 7) == f7) ctx.Fail("frame 7 change not seen");
    ++ctx.cases;

    // A different texel renames the texture from its upload on (frame 5's async set, frame 6's SetTexture).
    const ScenarioResult d = Run(4, 0, 0x41, false);
    if (DigestLine(d.log, 5) == DigestLine(a.log, 5) || DigestLine(d.log, 6) == DigestLine(a.log, 6)) ctx.Fail("texel change not seen");
    ++ctx.cases;

    // A call from another thread: async, marked "w".
    const ScenarioResult e = Run(5, 0, 0x40, true);
    std::string all6e;
    for (const auto &l : FrameLines(e.log, 6)) all6e += l + "\n";
    if (!Has(all6e, " w SetRenderState 137 0x0") || Has(all6e, "s 6 6 ")) ctx.Fail("worker call:\n%s", all6e.c_str());
    ++ctx.cases;

    // The lock slots go back as they were.
    d3dlog::ResetForTests();
    auto *probe = new fakes::FakeVertexBuffer(16, 0, D3DFMT_VERTEXDATA);
    void *p = nullptr;
    probe->Lock(0, 0, &p, 0);
    IDirect3DVertexBuffer9 *asInterface = probe;
    asInterface->Lock(0, 0, &p, 0);
    if (probe->locks != 2) ctx.Fail("restored vtable: %d locks reached the fake", probe->locks);
    probe->Release();
    ++ctx.cases;
}

namespace {

// A mesh drawn from a static pool and a dynamic ring at positions that differ per run (format 2): `vertexAt` and
// `indexAt` other vertices and indices lie before it in the pools, the ring upload is at `ringAt`. absolute: the
// indices hold pool positions and the base vertex is 0 (another way to draw the same thing). seed changes the mesh.
std::string PoolRun(uint32_t noise, uint32_t vertexAt, uint32_t indexAt, uint32_t ringAt, bool absolute, uint8_t seed)
{
    std::string log;
    uint32_t frame = 1;
    devlog::Recorder rec([&] { return frame; }, [&](std::string_view s) { log.append(s.data(), s.size()); });
    devlog::FrameSet full;
    full.Parse("1-3");
    rec.SetFullFrames(full);
    auto *fakeD3d = new fakes::FakeDirect3D(noise);
    IDirect3D9 *d3d = d3dlog::Wrap(fakeD3d, rec);
    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = 640;
    pp.BackBufferHeight = 480;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.Windowed = TRUE;
    IDirect3DDevice9 *device = nullptr;
    d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);
    IDirect3DVertexBuffer9 *pool = nullptr, *ring = nullptr;
    IDirect3DIndexBuffer9 *indices = nullptr;
    device->CreateVertexBuffer(64 * 16, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &pool, nullptr);
    device->CreateIndexBuffer(96 * 2, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &indices, nullptr);
    device->CreateVertexBuffer(256, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &ring, nullptr);
    void *p = nullptr;
    if (vertexAt) { // other meshes' vertices
        pool->Lock(0, vertexAt * 16, &p, 0);
        for (uint32_t i = 0; i < vertexAt * 16; ++i) static_cast<uint8_t *>(p)[i] = static_cast<uint8_t>(i + noise);
        pool->Unlock();
    }
    pool->Lock(vertexAt * 16, 4 * 16, &p, 0);
    for (uint32_t i = 0; i < 64; ++i) static_cast<uint8_t *>(p)[i] = static_cast<uint8_t>(i * 3 + seed);
    pool->Unlock();
    indices->Lock(0, (indexAt + 6) * 2, &p, 0);
    auto *ix = static_cast<uint16_t *>(p);
    for (uint32_t i = 0; i < indexAt; ++i) ix[i] = static_cast<uint16_t>(i * 5 + noise);
    const uint16_t mesh[6] = {0, 1, 2, 2, 1, 3};
    for (uint32_t i = 0; i < 6; ++i) ix[indexAt + i] = static_cast<uint16_t>(mesh[i] + (absolute ? vertexAt : 0));
    indices->Unlock();

    frame = 2;
    device->SetStreamSource(0, pool, 0, 16);
    device->SetIndices(indices);
    if (absolute) device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, vertexAt + 4, indexAt, 2); // a wider declared range
    else device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(vertexAt), 0, 4, indexAt, 2);
    ring->Lock(ringAt, 48, &p, ringAt ? D3DLOCK_NOOVERWRITE : D3DLOCK_DISCARD);
    for (uint32_t i = 0; i < 48; ++i) static_cast<uint8_t *>(p)[i] = static_cast<uint8_t>(i * 5 + seed);
    ring->Unlock();
    device->SetStreamSource(0, ring, ringAt, 16);
    device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);

    frame = 3;
    pool->Release();
    ring->Release();
    indices->Release();
    device->Release();
    d3d->Release();
    d3dlog::ResetForTests();
    return log;
}

} // namespace

DIFF_TEST(HostDevLog_Note, 0)
{
    // The frame clock's lines: written at once, whole, one line each, beside the open frame's records.
    std::string out;
    uint32_t frame = 3;
    devlog::Recorder rec([&] { return frame; }, [&](std::string_view s) { out.append(s.data(), s.size()); });
    rec.Record(devlog::Kind::Stream, "A 1");
    rec.Note("clk 2 t=66666 main=1:00 | polls=0");
    rec.Note("clk 3 x\ny");
    frame = 4;
    rec.Record(devlog::Kind::Stream, "B 2");
    rec.Finish();
    const std::vector<std::string> got = Lines(out);
    if (got.size() != 4 || got[0] != "clk 2 t=66666 main=1:00 | polls=0" || got[1] != "clk 3 x y" ||
        got[2].rfind("frame 3 stream 1 ", 0) != 0 || got[3].rfind("frame 4 stream 1 ", 0) != 0)
        ctx.Fail("note lines: %s", out.c_str());
    ++ctx.cases;
}

DIFF_TEST(HostDevLog_Positions, 0)
{
    // Frame 2 (the draws and the ring upload, all stream records) is the same text wherever the mesh lies.
    const std::string a = PoolRun(1, 0, 0, 0, false, 0), b = PoolRun(2, 9, 30, 96, false, 0), c = PoolRun(3, 17, 12, 48, true, 0);
    const std::vector<std::string> fa = FrameLines(a, 2), fb = FrameLines(b, 2), fc = FrameLines(c, 2);
    std::string all;
    for (const auto &l : fa) all += l + "\n";
    if (fa.empty() || fa != fb || fa != fc) {
        std::string other;
        for (const auto &l : fb) other += l + "\n";
        ctx.Fail("frame 2 differs with the positions:\n%s---\n%s", all.c_str(), other.c_str());
    }
    ++ctx.cases;

    // What it says: the vertices the indices reach, the indices relative to the smallest, no positions or ring flags.
    uint8_t vertices[64];
    for (uint32_t i = 0; i < 64; ++i) vertices[i] = static_cast<uint8_t>(i * 3);
    const uint32_t relative[6] = {0, 1, 2, 2, 1, 3};
    uint8_t ringBytes[48];
    for (uint32_t i = 0; i < 48; ++i) ringBytes[i] = static_cast<uint8_t>(i * 5);
    if (!Has(all, "DrawIndexedPrimitive 4 2 n=4 s0=V:") || !Has(all, "/16 #" + Hex12(devlog::Hash(vertices, sizeof(vertices))) + " ib=I:") ||
        !Has(all, " #" + Hex12(devlog::Hash(relative, sizeof(relative)))) || !Has(all, "SetStreamSource 0 V:") ||
        !Has(all, " * 16") || !Has(all, "Upload Vd0:") || !Has(all, " 48 0x0 #" + Hex12(devlog::Hash(ringBytes, sizeof(ringBytes)))) ||
        !Has(all, "DrawPrimitive 4 1 s0=Vd0:"))
        ctx.Fail("frame 2 records:\n%s", all.c_str());
    // Static uploads: no offset, no "from" (the name is the parameters'), the bytes and their hash.
    std::string all1;
    for (const auto &l : FrameLines(a, 1)) all1 += l + "\n";
    if (!Has(all1, "Upload V:") || Has(all1, " from ") || !Has(all1, " 64 0x0 #")) ctx.Fail("frame 1 records:\n%s", all1.c_str());
    ++ctx.cases;

    // Other mesh contents: other draw records.
    const std::vector<std::string> fd = FrameLines(PoolRun(4, 9, 30, 96, false, 1), 2);
    if (fd == fa) ctx.Fail("a different mesh logs the same draws");
    ++ctx.cases;
}
