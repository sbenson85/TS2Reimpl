#pragma once
// d3dfx: the shared fake Direct3D 9 of the scenegraph + rendering campaign (docs/subsystems/rendering.md, "Shared
// fixture"). Every test of code on the device boundary runs both sides against these fakes and compares one log.
//
// - Fakes of the Direct3D interfaces are drawfx fakes (tests/drawfx_core.h) of the COM slot tables generated from the
//   SDK's d3d9.h (tests/fx_d3d9.h, tools/d3dslots.py): `__stdcall` thunks, every call logged with its slot name and
//   its arguments normalised by kind (matrices, viewports, rects, lights, materials and shader-constant arrays by
//   content; pointers also by where they point, so a pointer into the object under test must be in a registered
//   range or at one address on both sides). Results per slot with Fake::Returns, out-parameters with Fake::On.
//       drawfx::Fake device;  device.Reset<fx_d3d9::IDirect3DDevice9>(d3dfx::kDeviceId);
//   Slot numbers by name: fx_d3d9::dev::SetViewport.
// - ErrorLog: while alive, LogD3DError's side effects (the cRZStd report and the error-string service it asks) are
//   notes in the same log, so where a failure is reported relative to the device calls is compared too.
// - Answers: per-slot HRESULTs chosen once per case and given to both sides.
// - ErrorLog::Answer: the fake framework's GetSystemService hands out a given fake for an id pair (lane NC; anything
//   else gets the error-string service's held object, as before).
// - Resources (lane NC, for the device and R3): surfaces (GetDesc, LockRect / UnlockRect over a buffer), textures
//   (GetLevelDesc, LockRect), queries (GetData busy for a set number of calls), and Creates: the device's Create* slots
//   handing out such fakes through their out-parameters.
#include "drawfx_core.h"
#include "fx_d3d9.h"
#include "rz/MiscHelpersA.h"

#include <d3d9.h>

#include <array>
#include <cstring>
#include <random>
#include <vector>

namespace d3dfx {

inline constexpr uint32_t kDeviceId = 0xd3dd0001;

// HRESULTs a slot can answer: success, S_FALSE (positive: not a failure), D3DERR_INVALIDCALL and E_FAIL.
inline constexpr uint32_t kResults[] = {0, 1, 0x8876086c, 0x80004005};

// Per-slot results of one fake for one case: chosen once, applied to each side's fresh fake.
struct Answers {
    std::array<uint32_t, drawfx::kMaxSlots> ret{};
    // Each of the first `count` slots fails (or answers S_FALSE) one time in `oneIn`, otherwise succeeds.
    void Random(std::mt19937 &gen, unsigned count, unsigned oneIn = 3)
    {
        ret.fill(0);
        for (unsigned i = 0; i < count; ++i) ret[i] = gen() % oneIn == 0 ? kResults[gen() % 4] : 0;
    }
    void Apply(drawfx::Fake &f) const
    {
        for (unsigned i = 0; i < f.iface->count; ++i) f.Returns(i, ret[i]);
    }
};

// The last call on fakes of interface I in a log, or null.
inline const drawfx::Call *LastCall(const drawfx::Log &log, const drawfx::Iface &iface)
{
    for (auto it = log.rbegin(); it != log.rend(); ++it)
        if (it->iface == &iface) return &*it;
    return nullptr;
}
inline unsigned Calls(const drawfx::Log &log, const drawfx::Iface &iface)
{
    unsigned n = 0;
    for (const drawfx::Call &c : log) n += c.iface == &iface;
    return n;
}
// Notes with this label in a log (ErrorLog's "LogD3DError" lines).
inline unsigned Notes(const drawfx::Log &log, const char *label)
{
    unsigned n = 0;
    for (const drawfx::Call &c : log) n += !c.iface && std::strcmp(c.note, label) == 0;
    return n;
}

namespace detail {
struct Object {
    void *const *vtable;
    uint32_t pad;
};
inline void *gServiceVTable[8];
inline void *gHeldVTable[32];
inline Object gService{gServiceVTable, 0}, gHeld{gHeldVTable, 0};

// ErrorLog::Answer's table: the fake handed out for an id pair (counted when handed out).
struct ServiceAnswer {
    uint32_t first, second;
    drawfx::Fake *fake;
};
inline std::vector<ServiceAnswer> gServiceAnswers;

// The framework (0x10c9378): slot 5 hands out the fake answering the id pair (ErrorLog::Answer), else the held object,
// whose slots 2, 7 and 30 LogD3DError's QueryServiceValue calls (src/rz/MiscHelpersA.cpp).
inline uint32_t __fastcall ServiceQuery(void *, void *, uint32_t first, uint32_t second, void **out)
{
    drawfx::Note("service.Query", {first, second});
    for (const ServiceAnswer &a : gServiceAnswers)
        if (a.first == first && a.second == second) {
            ++a.fake->refs;
            *out = a.fake;
            return 0;
        }
    *out = &gHeld;
    return 0;
}
inline uint32_t __fastcall HeldRelease(void *, void *)
{
    drawfx::Note("service.held.Release", {});
    return 0;
}
inline uint32_t __fastcall HeldSlot7(void *, void *, uint32_t a, uint32_t b, uint32_t c)
{
    drawfx::Note("service.held.slot7", {drawfx::Norm(a), drawfx::Norm(b), drawfx::Norm(c)});
    return 0;
}
inline uint32_t __fastcall HeldSlot30(void *, void *, uint32_t a)
{
    drawfx::Note("service.held.slot30", {drawfx::Norm(a)});
    return 0;
}
// cRZStd's slot 2 (the varargs report LogD3DError makes): the line's fields.
inline void __cdecl Report(cRZStd *self, const char *category, uint32_t number, const char *format, void *value)
{
    drawfx::Note("LogD3DError", {self->mFlags, self->mFirst, self->mSecond, drawfx::P(category), number,
                                 drawfx::P(format), drawfx::Norm(drawfx::P(value))});
}
} // namespace detail

// While alive: cRZStd's slot 2 records the report and the service global points at the fake service.
class ErrorLog {
public:
    explicit ErrorLog(difftest::Context &ctx)
        : slot_(ctx.image.At<void *>(cRZStd::kVTable + 8)), service_(ctx.image.At<void *>(kServiceRva))
    {
        detail::gServiceVTable[5] = reinterpret_cast<void *>(&detail::ServiceQuery);
        detail::gHeldVTable[2] = reinterpret_cast<void *>(&detail::HeldRelease);
        detail::gHeldVTable[7] = reinterpret_cast<void *>(&detail::HeldSlot7);
        detail::gHeldVTable[30] = reinterpret_cast<void *>(&detail::HeldSlot30);
        DWORD old;
        VirtualProtect(slot_, 4, PAGE_READWRITE, &old);
        savedSlot_ = *slot_;
        savedService_ = *service_;
        *slot_ = reinterpret_cast<void *>(&detail::Report);
        *service_ = &detail::gService;
        detail::gServiceAnswers.clear();
    }
    ~ErrorLog()
    {
        *slot_ = savedSlot_;
        *service_ = savedService_;
        detail::gServiceAnswers.clear();
    }
    ErrorLog(const ErrorLog &) = delete;
    ErrorLog &operator=(const ErrorLog &) = delete;

    // While alive, the framework's GetSystemService(first, second) hands out `fake` (its count raised by one).
    void Answer(uint32_t first, uint32_t second, drawfx::Fake &fake)
    {
        for (detail::ServiceAnswer &a : detail::gServiceAnswers)
            if (a.first == first && a.second == second) {
                a.fake = &fake;
                return;
            }
        detail::gServiceAnswers.push_back({first, second, &fake});
    }

    static constexpr uint32_t kServiceRva = 0x10c9378;

private:
    void **slot_;
    void **service_;
    void *savedSlot_ = nullptr;
    void *savedService_ = nullptr;
};

// ---- Resources (lane NC) ----
// Slot numbers of the resource interfaces the handlers below answer (d3d9.h's order).
namespace surf {
inline constexpr unsigned GetDesc = 12, LockRect = 13, UnlockRect = 14;
}
namespace tex {
inline constexpr unsigned GetLevelDesc = 17, GetSurfaceLevel = 18, LockRect = 19, UnlockRect = 20;
}
namespace query {
inline constexpr unsigned Issue = 6, GetData = 7;
}

// A surface's (or a texture level's) description and memory. On success GetDesc copies `desc` and LockRect writes
// `pitch` and `bits` (the whole surface: the rectangle argument is logged, not applied); on failure (the slot's
// Returns) neither writes, as Direct3D leaves the out-parameter alone.
struct SurfaceData {
    D3DSURFACE_DESC desc{};
    int32_t pitch = 64;
    uint8_t bits[1024]{};
    drawfx::Fake *level = nullptr; // a texture's GetSurfaceLevel answer (AddRef'd), or null
};

namespace detail {
inline SurfaceData &DataOf(drawfx::Fake &self) { return *static_cast<SurfaceData *>(self.owner); }
template <class T> T *Out(uint32_t word) { return reinterpret_cast<T *>(uintptr_t(word)); }
inline uint32_t Describe(drawfx::Fake &self, unsigned slot, uint32_t out)
{
    if (self.ret[slot] == 0 && out) std::memcpy(Out<D3DSURFACE_DESC>(out), &DataOf(self).desc, sizeof(D3DSURFACE_DESC));
    return self.ret[slot];
}
inline uint32_t Lock(drawfx::Fake &self, unsigned slot, uint32_t out)
{
    if (self.ret[slot] == 0 && out) {
        D3DLOCKED_RECT *locked = Out<D3DLOCKED_RECT>(out);
        locked->Pitch = DataOf(self).pitch;
        locked->pBits = DataOf(self).bits;
    }
    return self.ret[slot];
}
inline uint32_t SurfaceGetDesc(drawfx::Fake &self, const uint32_t *a) { return Describe(self, surf::GetDesc, a[0]); }
inline uint32_t SurfaceLockRect(drawfx::Fake &self, const uint32_t *a) { return Lock(self, surf::LockRect, a[0]); }
inline uint32_t TextureGetLevelDesc(drawfx::Fake &self, const uint32_t *a)
{
    return Describe(self, tex::GetLevelDesc, a[1]);
}
inline uint32_t TextureLockRect(drawfx::Fake &self, const uint32_t *a) { return Lock(self, tex::LockRect, a[1]); }
inline uint32_t TextureGetSurfaceLevel(drawfx::Fake &self, const uint32_t *a)
{
    if (self.ret[tex::GetSurfaceLevel] != 0 || !a[1]) return self.ret[tex::GetSurfaceLevel];
    drawfx::Fake *level = DataOf(self).level;
    if (level) ++level->refs;
    *Out<void *>(a[1]) = level;
    return 0;
}
} // namespace detail

// Makes `fake` a surface answering from `data` (which must outlive it).
inline void MakeSurface(drawfx::Fake &fake, uint32_t id, SurfaceData &data)
{
    fake.Reset<fx_d3d9::IDirect3DSurface9>(id);
    fake.owner = &data;
    fake.On(surf::GetDesc, &detail::SurfaceGetDesc);
    fake.On(surf::LockRect, &detail::SurfaceLockRect);
}
// Makes `fake` a texture whose every level answers from `data` (GetSurfaceLevel hands out data.level).
inline void MakeTexture(drawfx::Fake &fake, uint32_t id, SurfaceData &data)
{
    fake.Reset<fx_d3d9::IDirect3DTexture9>(id);
    fake.owner = &data;
    fake.On(tex::GetLevelDesc, &detail::TextureGetLevelDesc);
    fake.On(tex::GetSurfaceLevel, &detail::TextureGetSurfaceLevel);
    fake.On(tex::LockRect, &detail::TextureLockRect);
}

// A query: GetData answers S_FALSE `busy` times, then copies `result` (up to the size asked) and answers S_OK. It
// ignores Returns on purpose: a polling caller loops until S_OK, so a programmed S_FALSE would never end.
struct QueryData {
    unsigned busy = 0;
    uint8_t result[8]{};
};
namespace detail {
inline uint32_t QueryGetData(drawfx::Fake &self, const uint32_t *a)
{
    QueryData &q = *static_cast<QueryData *>(self.owner);
    if (q.busy) {
        --q.busy;
        return S_FALSE;
    }
    if (a[0]) std::memcpy(Out<void>(a[0]), q.result, a[1] < sizeof(q.result) ? a[1] : sizeof(q.result));
    return S_OK;
}
} // namespace detail
inline void MakeQuery(drawfx::Fake &fake, uint32_t id, QueryData &data)
{
    fake.Reset<fx_d3d9::IDirect3DQuery9>(id);
    fake.owner = &data;
    fake.On(query::GetData, &detail::QueryGetData);
}

// The device's Create* slots: each call hands out the next fake queued for that slot (AddRef'd) through its
// out-parameter and answers S_OK; with nothing queued, or when the slot's Returns is a failure, it writes null and
// answers that failure (E_OUTOFMEMORY when nothing is queued and no failure was programmed). One Creates per device
// fake (its `owner` points here; the device fake must not use `owner` otherwise).
class Creates {
public:
    // The slot's index of the out-parameter (the arguments after `this`).
    static constexpr unsigned OutArg(unsigned slot)
    {
        switch (slot) {
        case fx_d3d9::dev::CreateAdditionalSwapChain: return 1;
        case fx_d3d9::dev::CreateTexture: return 6;
        case fx_d3d9::dev::CreateVolumeTexture: return 7;
        case fx_d3d9::dev::CreateCubeTexture: return 5;
        case fx_d3d9::dev::CreateVertexBuffer: return 4;
        case fx_d3d9::dev::CreateIndexBuffer: return 4;
        case fx_d3d9::dev::CreateRenderTarget: return 6;
        case fx_d3d9::dev::CreateDepthStencilSurface: return 6;
        case fx_d3d9::dev::CreateOffscreenPlainSurface: return 4;
        case fx_d3d9::dev::CreateStateBlock: return 1;
        case fx_d3d9::dev::CreateVertexDeclaration: return 1;
        case fx_d3d9::dev::CreateVertexShader: return 1;
        case fx_d3d9::dev::CreatePixelShader: return 1;
        case fx_d3d9::dev::CreateQuery: return 1;
        default: return ~0u;
        }
    }
    explicit Creates(drawfx::Fake &device) : device_(device) { device.owner = this; }
    Creates(const Creates &) = delete;
    Creates &operator=(const Creates &) = delete;

    // The next call of `slot` hands out `fake` (queued in order).
    void Queue(unsigned slot, drawfx::Fake &fake)
    {
        queue_.push_back({slot, &fake});
        device_.On(slot, &Create);
    }
    unsigned Pending() const { return unsigned(queue_.size()); }

private:
    struct Entry {
        unsigned slot;
        drawfx::Fake *fake;
    };
    static unsigned SlotOf(drawfx::Fake &self, const uint32_t *args)
    {
        // The dispatcher passes no slot to handlers: the last logged call on this fake is this one.
        (void)args;
        for (auto it = drawfx::gLog.rbegin(); it != drawfx::gLog.rend(); ++it)
            if (it->object == self.id && it->iface == self.iface) return it->slot;
        return ~0u;
    }
    static uint32_t Create(drawfx::Fake &self, const uint32_t *args)
    {
        Creates &c = *static_cast<Creates *>(self.owner);
        const unsigned slot = SlotOf(self, args);
        void **out = detail::Out<void *>(args[OutArg(slot)]);
        if (self.ret[slot] != 0 && int32_t(self.ret[slot]) < 0) {
            if (out) *out = nullptr;
            return self.ret[slot];
        }
        for (auto it = c.queue_.begin(); it != c.queue_.end(); ++it)
            if (it->slot == slot) {
                drawfx::Fake *fake = it->fake;
                c.queue_.erase(it);
                ++fake->refs;
                if (out) *out = fake;
                return S_OK;
            }
        if (out) *out = nullptr;
        return uint32_t(E_OUTOFMEMORY);
    }

    drawfx::Fake &device_;
    std::vector<Entry> queue_;
};

// ---- General resources (lane NJ, R3; for R1c and later) ----
// Unlike SurfaceData above, these write their out-parameters whatever they answer: the engine reads a description or
// a locked rectangle without checking the result (LogD3DError and carry on), so a fake that wrote nothing on failure
// would leave each side reading its own stack.
//   d3dfx::Level level;            // one surface: description, results, pixel buffer
//   d3dfx::MakeLevel(fake, id, level);
//   d3dfx::Resource tex;           // a texture or cube texture: level count, LOD, its levels' surfaces
//   d3dfx::MakeResource(fake, id, tex);
//   d3dfx::Script creates(device); creates.Add(fx_d3d9::dev::CreateTexture, hr, &fake);
namespace res {
// IDirect3DSurface9 (d3d9.h order).
inline constexpr unsigned GetDevice = 3, SetPriority = 7, GetPriority = 8, PreLoad = 9, GetType = 10, GetContainer = 11,
                          GetDesc = 12, LockRect = 13, UnlockRect = 14;
// IDirect3DTexture9 / IDirect3DCubeTexture9.
inline constexpr unsigned SetLOD = 11, GetLOD = 12, GetLevelCount = 13, GetLevelDesc = 17, GetSurfaceLevel = 18,
                          TexLockRect = 19, TexUnlockRect = 20;
} // namespace res

inline constexpr unsigned kLevelBytes = 8192;

// A surface (or a texture level): GetDesc copies `desc` and answers descHr; LockRect writes pitch and the buffer's
// address and answers lockHr (counted in `locks`, UnlockRect takes one off and answers unlockHr); GetContainer writes
// null and answers E_NOINTERFACE; GetDevice hands out `device` (counted). GetPriority answers `priority`, SetPriority
// stores its argument there and answers the old value.
struct Level {
    D3DSURFACE_DESC desc{};
    uint32_t descHr = 0, lockHr = 0, unlockHr = 0;
    int32_t pitch = 16;
    uint32_t priority = 0;
    int32_t locks = 0;
    drawfx::Fake *device = nullptr;
    uint8_t bits[kLevelBytes]{};
};

// A texture or cube texture: GetLevelCount answers `levels`, GetLOD `lod`; GetLevelDesc(level) copies the level's
// description (face 0's for the cube) and answers its descHr; GetSurfaceLevel(level) / GetCubeMapSurface(face, level)
// hand out that level's fake (counted, null when there is none) and answer surfaceHr. Levels past kMaxLevels answer
// the last one.
struct Resource {
    static constexpr unsigned kMaxLevels = 16;
    bool cube = false;
    uint32_t levels = 1, lod = 0, surfaceHr = 0;
    drawfx::Fake *surface[6][kMaxLevels]{};
    Level *level[6][kMaxLevels]{};
};

namespace detail {
inline Level &LevelOf(drawfx::Fake &self) { return *static_cast<Level *>(self.owner); }
inline Resource &ResourceOf(drawfx::Fake &self) { return *static_cast<Resource *>(self.owner); }
inline uint32_t LevelGetDesc(drawfx::Fake &self, const uint32_t *a)
{
    Level &l = LevelOf(self);
    if (a[0]) std::memcpy(Out<D3DSURFACE_DESC>(a[0]), &l.desc, sizeof(D3DSURFACE_DESC));
    return l.descHr;
}
inline uint32_t LevelLockRect(drawfx::Fake &self, const uint32_t *a)
{
    Level &l = LevelOf(self);
    if (a[0]) {
        D3DLOCKED_RECT *locked = Out<D3DLOCKED_RECT>(a[0]);
        locked->Pitch = l.pitch;
        locked->pBits = l.bits;
    }
    ++l.locks;
    return l.lockHr;
}
inline uint32_t LevelUnlockRect(drawfx::Fake &self, const uint32_t *)
{
    Level &l = LevelOf(self);
    --l.locks;
    return l.unlockHr;
}
inline uint32_t LevelGetContainer(drawfx::Fake &, const uint32_t *a)
{
    if (a[1]) *Out<void *>(a[1]) = nullptr;
    return uint32_t(E_NOINTERFACE);
}
inline uint32_t LevelGetDevice(drawfx::Fake &self, const uint32_t *a)
{
    drawfx::Fake *device = LevelOf(self).device;
    if (device) ++device->refs;
    if (a[0]) *Out<void *>(a[0]) = device;
    return device ? 0 : uint32_t(D3DERR_INVALIDCALL);
}
inline uint32_t LevelSetPriority(drawfx::Fake &self, const uint32_t *a)
{
    Level &l = LevelOf(self);
    const uint32_t old = l.priority;
    l.priority = a[0];
    return old;
}
inline uint32_t LevelGetPriority(drawfx::Fake &self, const uint32_t *) { return LevelOf(self).priority; }
inline unsigned Clamp(uint32_t level) { return level < Resource::kMaxLevels ? level : Resource::kMaxLevels - 1; }
inline uint32_t ResourceLevelDesc(drawfx::Fake &self, const uint32_t *a)
{
    Level *l = ResourceOf(self).level[0][Clamp(a[0])];
    if (!l) return uint32_t(D3DERR_INVALIDCALL);
    if (a[1]) std::memcpy(Out<D3DSURFACE_DESC>(a[1]), &l->desc, sizeof(D3DSURFACE_DESC));
    return l->descHr;
}
inline uint32_t ResourceSurface(drawfx::Fake &self, const uint32_t *a)
{
    Resource &r = ResourceOf(self);
    const unsigned face = r.cube ? (a[0] < 6 ? a[0] : 5) : 0;
    const uint32_t level = r.cube ? a[1] : a[0];
    const uint32_t out = r.cube ? a[2] : a[1];
    drawfx::Fake *s = r.surface[face][Clamp(level)];
    if (s) ++s->refs;
    if (out) *Out<void *>(out) = s;
    return r.surfaceHr;
}
inline uint32_t ResourceLevelCount(drawfx::Fake &self, const uint32_t *) { return ResourceOf(self).levels; }
inline uint32_t ResourceLOD(drawfx::Fake &self, const uint32_t *) { return ResourceOf(self).lod; }
} // namespace detail

// Makes `fake` a surface answering from `level` (which must outlive it).
inline void MakeLevel(drawfx::Fake &fake, uint32_t id, Level &level)
{
    fake.Reset<fx_d3d9::IDirect3DSurface9>(id);
    fake.owner = &level;
    fake.On(res::GetDesc, &detail::LevelGetDesc);
    fake.On(res::LockRect, &detail::LevelLockRect);
    fake.On(res::UnlockRect, &detail::LevelUnlockRect);
    fake.On(res::GetContainer, &detail::LevelGetContainer);
    fake.On(res::GetDevice, &detail::LevelGetDevice);
    fake.On(res::SetPriority, &detail::LevelSetPriority);
    fake.On(res::GetPriority, &detail::LevelGetPriority);
}
// Makes `fake` a texture (or, with resource.cube, a cube texture) answering from `resource`.
inline void MakeResource(drawfx::Fake &fake, uint32_t id, Resource &resource)
{
    if (resource.cube)
        fake.Reset<fx_d3d9::IDirect3DCubeTexture9>(id);
    else
        fake.Reset<fx_d3d9::IDirect3DTexture9>(id);
    fake.owner = &resource;
    fake.On(res::GetLevelCount, &detail::ResourceLevelCount);
    fake.On(res::GetLOD, &detail::ResourceLOD);
    fake.On(res::GetLevelDesc, &detail::ResourceLevelDesc);
    fake.On(res::GetSurfaceLevel, &detail::ResourceSurface);
}

// The device's Create* slots by script: each call takes the next entry scripted for its slot and hands out its fake
// (counted; null when the entry has none) through the out-parameter, answering the entry's HRESULT, so a failure can
// still hand out an object (or a success none). With nothing scripted for the slot: null and E_OUTOFMEMORY. Uses the
// device fake's `owner` (not with Creates).
class Script {
public:
    explicit Script(drawfx::Fake &device) : device_(device) { device.owner = this; }
    Script(const Script &) = delete;
    Script &operator=(const Script &) = delete;

    void Add(unsigned slot, uint32_t hr, drawfx::Fake *fake)
    {
        entries_.push_back({slot, hr, fake});
        device_.On(slot, &Create);
    }
    unsigned Pending() const { return unsigned(entries_.size()); }

private:
    struct Entry {
        unsigned slot;
        uint32_t hr;
        drawfx::Fake *fake;
    };
    static uint32_t Create(drawfx::Fake &self, const uint32_t *args)
    {
        Script &s = *static_cast<Script *>(self.owner);
        unsigned slot = ~0u;
        for (auto it = drawfx::gLog.rbegin(); it != drawfx::gLog.rend(); ++it)
            if (it->object == self.id && it->iface == self.iface) {
                slot = it->slot;
                break;
            }
        void **out = detail::Out<void *>(args[Creates::OutArg(slot)]);
        for (auto it = s.entries_.begin(); it != s.entries_.end(); ++it)
            if (it->slot == slot) {
                const Entry e = *it;
                s.entries_.erase(it);
                if (e.fake) ++e.fake->refs;
                if (out) *out = e.fake;
                return e.hr;
            }
        if (out) *out = nullptr;
        return uint32_t(E_OUTOFMEMORY);
    }

    drawfx::Fake &device_;
    std::vector<Entry> entries_;
};

} // namespace d3dfx

// ---- Vertex and index buffers (lane NJ, R3b) ----
//   d3dfx::Buffer vb;              // description, results, bytes
//   d3dfx::MakeBuffer(fake, id, vb);
namespace d3dfx {

namespace buf {
// IDirect3DVertexBuffer9 / IDirect3DIndexBuffer9 (d3d9.h order).
inline constexpr unsigned GetDevice = 3, SetPriority = 7, GetPriority = 8, PreLoad = 9, GetType = 10, Lock = 11,
                          Unlock = 12, GetDesc = 13;
} // namespace buf

inline constexpr unsigned kBufferBytes = 4096;

// A vertex buffer (or, with `index`, an index buffer): GetDesc copies `desc` (an index buffer's description is its first
// 0x14 bytes) and answers descHr; Lock(offset, size, out, flags) writes the address of bytes[offset] (the start for
// offsets past the end) whatever it answers, answers lockHr and counts `locks`; Unlock takes one off and answers
// unlockHr. GetPriority answers `priority`, SetPriority stores its argument there and answers the old value.
struct Buffer {
    bool index = false;
    D3DVERTEXBUFFER_DESC desc{};
    uint32_t descHr = 0, lockHr = 0, unlockHr = 0, priority = 0;
    int32_t locks = 0;
    uint8_t bytes[kBufferBytes]{};
};

namespace detail {
inline Buffer &BufferOf(drawfx::Fake &self) { return *static_cast<Buffer *>(self.owner); }
inline uint32_t BufferGetDesc(drawfx::Fake &self, const uint32_t *a)
{
    Buffer &b = BufferOf(self);
    if (a[0]) std::memcpy(Out<void>(a[0]), &b.desc, b.index ? sizeof(D3DINDEXBUFFER_DESC) : sizeof(D3DVERTEXBUFFER_DESC));
    return b.descHr;
}
inline uint32_t BufferLock(drawfx::Fake &self, const uint32_t *a)
{
    Buffer &b = BufferOf(self);
    if (a[2]) *Out<void *>(a[2]) = b.bytes + (a[0] < kBufferBytes ? a[0] : 0);
    ++b.locks;
    return b.lockHr;
}
inline uint32_t BufferUnlock(drawfx::Fake &self, const uint32_t *)
{
    Buffer &b = BufferOf(self);
    --b.locks;
    return b.unlockHr;
}
inline uint32_t BufferSetPriority(drawfx::Fake &self, const uint32_t *a)
{
    Buffer &b = BufferOf(self);
    const uint32_t old = b.priority;
    b.priority = a[0];
    return old;
}
inline uint32_t BufferGetPriority(drawfx::Fake &self, const uint32_t *) { return BufferOf(self).priority; }
} // namespace detail

// Makes `fake` a vertex or index buffer (by buffer.index) answering from `buffer` (which must outlive it).
inline void MakeBuffer(drawfx::Fake &fake, uint32_t id, Buffer &buffer)
{
    if (buffer.index)
        fake.Reset<fx_d3d9::IDirect3DIndexBuffer9>(id);
    else
        fake.Reset<fx_d3d9::IDirect3DVertexBuffer9>(id);
    fake.owner = &buffer;
    fake.On(buf::GetDesc, &detail::BufferGetDesc);
    fake.On(buf::Lock, &detail::BufferLock);
    fake.On(buf::Unlock, &detail::BufferUnlock);
    fake.On(buf::SetPriority, &detail::BufferSetPriority);
    fake.On(buf::GetPriority, &detail::BufferGetPriority);
}

} // namespace d3dfx

// ---- Vertex declarations (lane NJ, R3 part 3) ----
//   d3dfx::Declaration d;          // the element count GetDeclaration reports, its result
//   d3dfx::MakeDeclaration(fake, id, d);
namespace d3dfx {

namespace decl {
// IDirect3DVertexDeclaration9 (d3d9.h order).
inline constexpr unsigned GetDevice = 3, GetDeclaration = 4;
} // namespace decl

// A vertex declaration: GetDeclaration(elements, count) writes `count` through its second argument whatever it answers
// (the engine reads it without checking the result) and answers `hr`; the elements are left as they were (the engine
// sizes its buffer for the most Direct3D allows and reads only the count).
struct Declaration {
    uint32_t count = 0;
    uint32_t hr = 0;
};

namespace detail {
inline uint32_t DeclarationGetDeclaration(drawfx::Fake &self, const uint32_t *a)
{
    const Declaration &d = *static_cast<Declaration *>(self.owner);
    if (a[1]) *Out<UINT>(a[1]) = d.count;
    return d.hr;
}
} // namespace detail

// Makes `fake` a vertex declaration answering from `declaration` (which must outlive it).
inline void MakeDeclaration(drawfx::Fake &fake, uint32_t id, Declaration &declaration)
{
    fake.Reset<fx_d3d9::IDirect3DVertexDeclaration9>(id);
    fake.owner = &declaration;
    fake.On(decl::GetDeclaration, &detail::DeclarationGetDeclaration);
}

} // namespace d3dfx
