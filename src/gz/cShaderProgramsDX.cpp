// The shader programs (lane NR, rendering R3b step 3): src/gz/cShaderProgramsDX.h.
#include "gz/cShaderProgramsDX.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceEnum.h"
#include "gz/cIGZString.h"
#include "gz/cVertexFormat.h" // cIVertexDeclarationsManagerDX (the semantic renames)
#include "ifaces/cIDeviceResourceManager.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZOStream.h"
#include "rz/MiscHelpersA.h" // LogD3DError
#include "rz/RZAutoRef.h"
#include "rz/RZFastFixedAllocators.h"
#include "rz/RZFileStreams.h" // cRZTextODSStream
#include "rz/Singleton_20ddfb.h" // GetServiceObject

#include "game/owned.h"
TS2_OWNED_STATIC(const char, d9f140)[]; // the source file of the shader programs' reports

namespace nGZGraphic4 {

using namespace shader_detail;
using d3dx::ID3DXBuffer;

namespace {

// The LogD3DError lines (the source file is ts2_rva_d9f140, GZGraphic4ShaderProgramDX.cpp).
constexpr uint32_t kLineAssemble = 0x38c, kLineCompile = 0x3ca, kLineGather = 0x426, kLineCreationParameters = 0x4b7,
                   kLineCreateLinker = 0x4be, kLineAddFragments = 0x4c6, kLineFragmentDesc = 0x4ef,
                   kLineConstantTable = 0x1a9, kLineTableDesc = 0x1ac, kLineConstantDesc = 0x1b3,
                   kLineSamplerDesc = 0x20f;
constexpr HRESULT kInvalidCall = static_cast<HRESULT>(0x8876086c); // D3DERR_INVALIDCALL
constexpr HRESULT kDuplicateFragment = static_cast<HRESULT>(0x88760b5b);

// The framework's graphic service (cGraphicSystemDX's cIGraphicSystemDX face).
constexpr uint32_t kGraphicServiceId = 0x85522803, kGraphicServiceIid = 0xc55289ba;

void Log(HRESULT hr, uint32_t line)
{
    LogD3DError(static_cast<uint32_t>(hr), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ts2_rva_d9f140)),
                line);
}
void Check(HRESULT hr, uint32_t line)
{
    if (FAILED(hr)) Log(hr, line);
}

// D3DX (statically linked, third-party code: called where it lies).
using AssembleFn = HRESULT(__stdcall *)(const char *, UINT, const void *, void *, DWORD, ID3DXBuffer **, ID3DXBuffer **);
using CompileFn = HRESULT(__stdcall *)(const char *, UINT, const void *, void *, const char *, const char *, DWORD,
                                       ID3DXBuffer **, ID3DXBuffer **, d3dx::ID3DXConstantTable **);
using GatherFn = HRESULT(__stdcall *)(const char *, UINT, const void *, void *, DWORD, ID3DXBuffer **, ID3DXBuffer **);
using CreateLinkerFn = HRESULT(__stdcall *)(IDirect3DDevice9 *, UINT, d3dx::ID3DXFragmentLinker **);
using DisassembleFn = HRESULT(__stdcall *)(const DWORD *, BOOL, const char *, ID3DXBuffer **);
using ConstantTableFn = HRESULT(__stdcall *)(const DWORD *, d3dx::ID3DXConstantTable **);

HRESULT AssembleShader(const char *text, UINT size, const void *defines, DWORD flags, ID3DXBuffer **shader,
                       ID3DXBuffer **errors)
{
    return OriginalFunction<AssembleFn, 0x1e89e>()(text, size, defines, nullptr, flags, shader, errors);
}
HRESULT CompileShader(const char *text, UINT size, const void *defines, const char *entry, const char *profile,
                      DWORD flags, ID3DXBuffer **shader, ID3DXBuffer **errors)
{
    return OriginalFunction<CompileFn, 0x1e981>()(text, size, defines, nullptr, entry, profile, flags, shader, errors,
                                                  nullptr);
}
HRESULT GatherFragments(const char *text, UINT size, const void *defines, DWORD flags, ID3DXBuffer **fragments,
                        ID3DXBuffer **errors)
{
    return OriginalFunction<GatherFn, 0x22220>()(text, size, defines, nullptr, flags, fragments, errors);
}
HRESULT CreateFragmentLinker(IDirect3DDevice9 *device, d3dx::ID3DXFragmentLinker **linker)
{
    return OriginalFunction<CreateLinkerFn, 0x28c0c>()(device, 0, linker);
}
HRESULT DisassembleShader(const DWORD *function, ID3DXBuffer **text)
{
    return OriginalFunction<DisassembleFn, 0x2168f>()(function, FALSE, nullptr, text);
}
HRESULT GetShaderConstantTable(const DWORD *function, d3dx::ID3DXConstantTable **table)
{
    return OriginalFunction<ConstantTableFn, 0x1eca8>()(function, table);
}

// The defines every make slot passes to D3DX: the vertex declarations manager's semantic renames, asked through the
// graphic service (its slot 27 answers the manager; the service is held until the slot ends).
struct ServiceDefines {
    RZAutoRef graphics{nullptr};
    ServiceDefines()
    {
        if (void *const framework = GetServiceObject())
            As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kGraphicServiceId, kGraphicServiceIid, &graphics.mObject);
    }
    ServiceDefines(const ServiceDefines &) = delete;
    ServiceDefines &operator=(const ServiceDefines &) = delete;
    ~ServiceDefines() { graphics.ReleaseObject(); }
    const void *Get() const
    {
        // cIGraphicSystemDX slot 27 (lane NQ's pending fact GetVertexDeclarationsManager27: the jump to the declarations
        // manager's getter). No null check, as the original.
        void *const declarations = RawSlot<27, void *>(graphics.mObject);
        return static_cast<cIVertexDeclarationsManagerDX *>(declarations)->GetSemanticRenames();
    }
};

// D3DX's messages into `errors` (when there are any and the caller wants them).
void ErrorText(cIGZString *errors, ID3DXBuffer *messages)
{
    if (!errors || !messages || messages->GetBufferSize() == 0) return;
    const DWORD size = messages->GetBufferSize();
    const char *text = static_cast<const char *>(messages->GetBufferPointer());
    errors->FromChar(text, size);
}

void ReleaseLocal(ID3DXBuffer *&b)
{
    if (b) {
        b->Release();
        b = nullptr;
    }
}

ifaces::cIDeviceResourceManager *ResourceManager(cIDeviceDX *device)
{
    return As<ifaces::cIDeviceResourceManager>(device->GetResourceManager());
}
cIDeviceEnum *Enum(cIDeviceDX *device) { return As<cIDeviceEnum>(device->GetDeviceEnumConst()); }

// The device as its cIDeviceDX face (QueryInterface 0x553b7cf, counted), released at the end of the scope.
struct DeviceDX {
    cIDeviceDX *p = nullptr;
    explicit DeviceDX(cIDeviceDX *device) { device->QueryInterface(cDeviceDX::kIidDeviceDX, reinterpret_cast<void **>(&p)); }
    DeviceDX(const DeviceDX &) = delete;
    DeviceDX &operator=(const DeviceDX &) = delete;
    ~DeviceDX()
    {
        if (p) p->Release();
    }
};

// A program's key, as 0x1749b0 (vertex: the program's own processing) and 0x174920 (pixel: hardware) build it.
template <class Shader> Key KeyOf(const Shader *s, bool pixel)
{
    Key k{};
    k.device = s->mDevice;
    k.count = static_cast<uint32_t>(s->mNames.size());
    k.names = s->mNames.empty() ? nullptr : s->mNames.data();
    k.buffer = s->mFunction;
    k.software = pixel ? 0 : s->mInfo.software;
    k.hardware = pixel ? 1 : s->mInfo.hardware;
    return k;
}
template <class Shader> struct KeyCompare {
    bool pixel;
    bool operator()(const Shader *s, const Key &k) const { return KeyLess(KeyOf(s, pixel), k); }
    bool operator()(const Key &k, const Shader *s) const { return KeyLess(k, KeyOf(s, pixel)); }
    bool operator()(const Shader *a, const Shader *b) const { return KeyLess(KeyOf(a, pixel), KeyOf(b, pixel)); }
};
template <class Shader> std::pair<typename std::vector<Shader *>::iterator, typename std::vector<Shader *>::iterator>
Range(std::vector<Shader *> &list, const Key &key, bool pixel)
{
    return std::equal_range(list.begin(), list.end(), key, KeyCompare<Shader>{pixel});
}

} // namespace

// ---- the key ----

bool shader_detail::KeyLess(const Key &a, const Key &b)
{
    if (a.device != b.device) return reinterpret_cast<uintptr_t>(a.device) < reinterpret_cast<uintptr_t>(b.device);
    if (a.count != b.count) return a.count < b.count;
    if (a.software != b.software) return a.software < b.software;
    if (a.hardware != b.hardware) return a.hardware < b.hardware;
    if (a.count == 0) {
        if (a.buffer->GetBufferSize() == b.buffer->GetBufferSize()) {
            const DWORD size = b.buffer->GetBufferSize();
            const void *bBytes = b.buffer->GetBufferPointer();
            const void *aBytes = a.buffer->GetBufferPointer();
            return std::memcmp(aBytes, bBytes, size) < 0;
        }
        const DWORD aSize = a.buffer->GetBufferSize();
        const DWORD bSize = b.buffer->GetBufferSize();
        return aSize < bSize;
    }
    for (uint32_t i = 0; i < a.count; ++i)
        if (a.names[i].p != b.names[i].p)
            return reinterpret_cast<uintptr_t>(a.names[i].p) < reinterpret_cast<uintptr_t>(b.names[i].p);
    return false;
}

// ---- the programs ----

template <class Face, class D3DShader> uint32_t cShaderProgramT<Face, D3DShader>::Release()
{
    if (--mRefs != 0) return mRefs;
    delete this;
    return 0;
}

template <class Face, class D3DShader> void cShaderProgramT<Face, D3DShader>::Drop()
{
    ResourceManager(mDevice)->Erase(this);
    mDevice = nullptr;
    if (mShader) {
        mShader->Release();
        mShader = nullptr;
    }
    if (mFunction) {
        mFunction->Release();
        mFunction = nullptr;
    }
    mNames.clear();
}

template <class Face, class D3DShader> void cShaderProgramT<Face, D3DShader>::Destroy()
{
    std::vector<CountedRef<cRZString>>().swap(mNames);
    if (mFunction) {
        mFunction->Release();
        mFunction = nullptr;
    }
    if (mShader) {
        mShader->Release();
        mShader = nullptr;
    }
}

template class cShaderProgramT<cIVertexShaderDX, IDirect3DVertexShader9>;
template class cShaderProgramT<cIPixelShaderDX, IDirect3DPixelShader9>;

namespace {
// Leaving the manager's list (the original finds the program with no check that it is there: every program the
// manager makes is in its list until this runs).
template <class Shader> void Leave(std::vector<Shader *> &list, Shader *s)
{
    const auto at = std::find(list.begin(), list.end(), s);
    if (at != list.end()) list.erase(at);
}
} // namespace

bool cVertexShaderDX::QueryInterface(uint32_t riid, void **out)
{
    if (riid == kIidD3DVertexShader) {
        *out = mShader; // not counted, and before the null check
        return true;
    }
    if (out && (riid == 1 || riid == kIidVertexShader || riid == kIidShaderProgram || riid == kIidShaderProgramDX ||
                riid == kIidShaderResource)) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

void cVertexShaderDX::ReleaseResource()
{
    if (!IsCreated()) return;
    Drop();
    Leave(cShaderProgramsManagerDX::Instance()->mVertex, this);
}

uint32_t cVertexShaderDX::GetMemorySize() { return mShader ? mFunction->GetBufferSize() * 2 + sizeof(*this) : sizeof(*this); }

cVertexShaderDX::~cVertexShaderDX()
{
    if (mShader) {
        Drop();
        Leave(cShaderProgramsManagerDX::Instance()->mVertex, this);
    }
    Destroy();
}

void *cVertexShaderDX::operator new(size_t) { return reinterpret_cast<RZFixedAllocatorBase *>(&Pool())->Alloc(); }
void cVertexShaderDX::operator delete(void *block, size_t) { reinterpret_cast<RZFastFixed0x28 *>(&Pool())->Free(block); }

bool cPixelShaderDX::QueryInterface(uint32_t riid, void **out)
{
    if (riid == kIidD3DPixelShader) {
        *out = mShader;
        return true;
    }
    if (out && (riid == 1 || riid == kIidPixelShader || riid == kIidShaderProgram || riid == kIidShaderProgramDX ||
                riid == kIidShaderResource)) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

void cPixelShaderDX::ReleaseResource()
{
    if (!IsCreated()) return;
    Drop();
    Leave(cShaderProgramsManagerDX::Instance()->mPixel, this);
}

// No shader: nothing (the vertex program's answers its own size).
uint32_t cPixelShaderDX::GetMemorySize() { return mShader ? mFunction->GetBufferSize() * 2 + sizeof(*this) : 0; }

cPixelShaderDX::~cPixelShaderDX()
{
    if (mShader) {
        Drop();
        Leave(cShaderProgramsManagerDX::Instance()->mPixel, this);
    }
    Destroy();
}

void *cPixelShaderDX::operator new(size_t) { return reinterpret_cast<RZFixedAllocatorBase *>(&Pool())->Alloc(); }
void cPixelShaderDX::operator delete(void *block, size_t) { reinterpret_cast<RZFastFixed0x28 *>(&Pool())->Free(block); }

// ---- the inputs ----

namespace {
bool NameBefore(const char *a, const char *b) { return std::strcmp(a, b) < 0; }
template <class Record> struct ByName {
    bool operator()(const Record &r, const char *name) const { return NameBefore(r.name.c_str(), name); }
    bool operator()(const char *name, const Record &r) const { return NameBefore(name, r.name.c_str()); }
    bool operator()(const Record &a, const Record &b) const { return NameBefore(a.name.c_str(), b.name.c_str()); }
};
} // namespace

// The original also sums the function's words into four statics (0x10ca2ec, 0x10ca350 / 358 / 360, each written once
// behind its own guard); nothing in the image reads them (xrefs: only this constructor), so they are left out.
cShaderProgramInputsDX::cShaderProgramInputsDX(const DWORD *function, uint32_t)
{
    d3dx::ID3DXConstantTable *table = nullptr;
    Check(GetShaderConstantTable(function, &table), kLineConstantTable);
    d3dx::ConstantTableDesc tableDesc;
    Check(table->GetDesc(&tableDesc), kLineTableDesc);
    for (UINT i = 0; i < tableDesc.Constants; ++i) {
        const d3dx::Handle handle = table->GetConstant(nullptr, i);
        d3dx::ConstantDesc d;
        UINT count = 1;
        Check(table->GetConstantDesc(handle, &d, &count), kLineConstantDesc);
        switch (d.RegisterSet) {
        case 0:
        case 1:
        case 2: {
            if (d.Type > 3) break;
            cShaderConstantRecord r{};
            r.desc.registerSet = d.RegisterSet;
            r.desc.registerIndex = d.RegisterIndex;
            r.desc.registerCount = d.RegisterCount;
            r.desc.type = d.Type;
            r.desc.structMembers = d.StructMembers;
            r.desc.elements = d.Elements;
            r.name = d.Name;
            switch (d.Class) {
            case 0: r.desc.shape = 0; break;
            case 1:
                r.desc.shape = 1;
                r.desc.vectorColumns = d.Columns;
                break;
            case 3:
                r.desc.shape = 2;
                r.desc.matrixRows = d.Rows;
                r.desc.matrixColumns = d.Columns;
                break;
            case 5: r.desc.shape = 3; break;
            default: continue; // matrix rows and objects are left out
            }
            mConstants.push_back(r);
            break;
        }
        case 3: {
            if (d.Type < 10 || d.Type > 14 || d.Class != 4) break;
            cShaderSamplerRecord r{};
            r.desc.count = count;
            r.name = d.Name;
            d3dx::ConstantDesc registers[16];
            UINT used = 16;
            Check(table->GetConstantDesc(handle, registers, &used), kLineSamplerDesc);
            // The original leaves the registers past `used` as its stack held them; ours are zero.
            for (UINT k = 0; k < used; ++k) r.registers[k] = registers[k].RegisterIndex;
            mSamplers.push_back(r);
            break;
        }
        default: break;
        }
    }
    std::sort(mConstants.begin(), mConstants.end(), ByName<cShaderConstantRecord>{});
    std::sort(mSamplers.begin(), mSamplers.end(), ByName<cShaderSamplerRecord>{});
    for (cShaderConstantRecord &r : mConstants) r.desc.name = r.name.c_str();
    for (cShaderSamplerRecord &r : mSamplers) {
        r.desc.name = r.name.c_str();
        r.desc.registers = r.registers;
    }
    if (table) table->Release();
}

bool cShaderProgramInputsDX::QueryInterface(uint32_t riid, void **out)
{
    if (out && (riid == 1 || riid == kIidShaderProgramInputs)) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

uint32_t cShaderProgramInputsDX::Release()
{
    if (--mRefs != 0) return mRefs;
    delete this;
    return 0;
}

cShaderConstant cShaderProgramInputsDX::FindConstant(const char *name)
{
    const auto range = std::equal_range(mConstants.begin(), mConstants.end(), name, ByName<cShaderConstantRecord>{});
    if (range.first == range.second) return cShaderConstant{};
    return range.first->desc;
}

cShaderSampler cShaderProgramInputsDX::FindSampler(const char *name)
{
    const auto range = std::equal_range(mSamplers.begin(), mSamplers.end(), name, ByName<cShaderSamplerRecord>{});
    if (range.first == range.second) return cShaderSampler{};
    return range.first->desc;
}

// ---- the manager ----

cShaderProgramsManagerDX::Linkers::~Linkers()
{
    for (int i = 5; i >= 0; --i)
        if (p[i]) p[i]->Release();
}

void cShaderProgramsManagerDX::Linkers::ReleaseAll()
{
    for (d3dx::ID3DXFragmentLinker *&l : p) {
        if (l) {
            l->Release();
            l = nullptr;
        }
    }
}

void cShaderProgramsManagerDX::ReleaseLinker(uint32_t index)
{
    d3dx::ID3DXFragmentLinker *&linker = mLinkers.p[index];
    if (linker) {
        linker->Release();
        linker = nullptr;
    }
}

bool cShaderProgramsManagerDX::NameLess::operator()(const cRZString *a, const cRZString *b) const
{
    if (a->mString.size() != b->mString.size()) return a->mString.size() < b->mString.size();
    return a->mString < b->mString;
}

void cShaderProgramsManagerDX::Names::Clear()
{
    for (cRZString *name : *this) name->Release();
    clear();
}

cShaderProgramsManagerDX::cShaderProgramsManagerDX()
{
    mVertex.reserve(16);
    mPixel.reserve(16);
    mLink.reserve(16);
}

cShaderProgramsManagerDX *cShaderProgramsManagerDX::Instance()
{
    static cShaderProgramsManagerDX instance;
    return &instance;
}

bool cShaderProgramsManagerDX::QueryInterface(uint32_t riid, void **out)
{
    if (out && (riid == 1 || riid == kIidShaderProgramsManager)) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

bool cShaderProgramsManagerDX::CreateProgram(cIDeviceDX *device, ID3DXBuffer *function, bool software, cIGZString *errors,
                                             cIShaderProgramDX **out)
{
    if (!device->IsDeviceReady()) return false;
    DeviceDX dx(device);
    const auto *tokens = static_cast<const DWORD *>(function->GetBufferPointer());
    const uint32_t version = tokens[0] & 0xffff;
    const uint8_t major = static_cast<uint8_t>(tokens[0] >> 8), minor = static_cast<uint8_t>(tokens[0]);
    Key key{};
    key.device = dx.p;
    key.count = static_cast<uint32_t>(mLink.size());
    key.names = mLink.empty() ? nullptr : mLink.data();
    key.buffer = function;

    if ((tokens[0] & 0xffff0000) == 0xfffe0000) {
        bool hardware = false;
        bool checkSoftware = true;
        if (!software && minor != 0xff) {
            cIDeviceEnum *first = Enum(device);
            cIDeviceEnum *second = Enum(device);
            const uint32_t major4 = first->GetLimit(4);
            const uint32_t caps = second->GetLimit(5) | (major4 << 8);
            hardware = version <= caps;
            checkSoftware = caps < version;
        }
        if (checkSoftware && !Enum(device)->HasProcessingCap(3)) {
            if (errors) errors->FromChar("Software vertex processing not supported by device");
            return false;
        }
        key.software = software;
        key.hardware = hardware;
        const auto range = Range(mVertex, key, false);
        if (range.first != range.second) {
            *out = *range.first;
            (*out)->AddRef();
            return true;
        }
        IDirect3DVertexShader9 *made = nullptr;
        if (dx.p->GetD3DDevice()->CreateVertexShader(tokens, &made) != 0) {
            if (errors) errors->FromChar("Failed CreateVertexShader");
            if (made) made->Release();
            return false;
        }
        cVertexShaderDX *const s = new cVertexShaderDX;
        s->AddRef();
        s->mDevice = dx.p;
        made->AddRef();
        s->mShader = made;
        function->AddRef();
        function->AddRef();
        s->mFunction = function;
        function->Release();
        s->mNames = mLink;
        s->mInfo = {0, major, minor, static_cast<uint8_t>(software), static_cast<uint8_t>(hardware)};
        made->Release();
        ResourceManager(device)->Insert(s);
        mVertex.insert(range.first, s);
        *out = s;
        s->AddRef();
        s->Release();
        return true;
    }

    if (software) {
        if (errors) errors->FromChar("Software processing not supported for pixel programs");
        return false;
    }
    cIDeviceEnum *first = Enum(device);
    cIDeviceEnum *second = Enum(device);
    const uint32_t major8 = first->GetLimit(8);
    const uint32_t caps = second->GetLimit(9) | (major8 << 8);
    if (version > caps) {
        if (errors) errors->FromChar("Pixel shader program version not supported");
        return false;
    }
    key.software = 0;
    key.hardware = 1;
    const auto range = Range(mPixel, key, true);
    if (range.first != range.second) {
        *out = *range.first;
        (*out)->AddRef();
        return true;
    }
    IDirect3DPixelShader9 *made = nullptr;
    if (dx.p->GetD3DDevice()->CreatePixelShader(tokens, &made) != 0) {
        if (errors) errors->FromChar("Failed CreatePixelShader");
        if (made) made->Release();
        return false;
    }
    cPixelShaderDX *const s = new cPixelShaderDX;
    s->AddRef();
    s->mDevice = dx.p;
    made->AddRef();
    s->mShader = made;
    function->AddRef();
    function->AddRef();
    s->mFunction = function;
    function->Release();
    s->mNames = mLink;
    s->mInfo = {1, major, minor, 0, 1};
    made->Release();
    ResourceManager(device)->Insert(s);
    mPixel.insert(range.first, s);
    *out = s;
    s->AddRef();
    s->Release();
    return true;
}

uint32_t cShaderProgramsManagerDX::AssembleProgram(cIDeviceDX *device, const char *text, uint32_t size,
                                                   cIShaderProgramDX **out, cIGZString *errors, uint32_t flags)
{
    mLink.clear();
    mLinking = false;
    *out = nullptr;
    if (!device->IsDeviceReady()) return 2;
    ServiceDefines service;
    ID3DXBuffer *function = nullptr, *messages = nullptr;
    const void *defines = service.Get();
    const HRESULT hr = AssembleShader(text, size, defines, (flags >> 1) & 1, &function, errors ? &messages : nullptr);
    uint32_t result = 1;
    if (hr == kInvalidCall) {
        Log(hr, kLineAssemble);
        ErrorText(errors, messages);
    } else if (hr == 0) {
        result = CreateProgram(device, function, Software(flags), errors, out) ? 0 : 2;
    } else {
        ErrorText(errors, messages);
    }
    ReleaseLocal(messages);
    ReleaseLocal(function);
    return result;
}

uint32_t cShaderProgramsManagerDX::CompileProgram(cIDeviceDX *device, const char *entry, const char *profile,
                                                  const char *text, uint32_t size, cIShaderProgramDX **out,
                                                  cIShaderProgramInputs **inputs, cIGZString *errors, uint32_t flags)
{
    mLink.clear();
    mLinking = false;
    *out = nullptr;
    *inputs = nullptr;
    if (!device->IsDeviceReady()) return 2;
    ServiceDefines service;
    ID3DXBuffer *function = nullptr, *messages = nullptr;
    const void *defines = service.Get();
    const HRESULT hr =
        CompileShader(text, size, defines, entry, profile, D3DXFlags(flags), &function, errors ? &messages : nullptr);
    uint32_t result = 1;
    if (hr == kInvalidCall) {
        Log(hr, kLineCompile);
        ErrorText(errors, messages);
    } else if (hr == 0) {
        if (!CreateProgram(device, function, Software(flags), errors, out)) {
            result = 2;
        } else {
            const DWORD bytes = function->GetBufferSize();
            const auto *tokens = static_cast<const DWORD *>(function->GetBufferPointer());
            cShaderProgramInputsDX *const made = new cShaderProgramInputsDX(tokens, bytes >> 2);
            *inputs = made;
            made->AddRef();
            result = 0;
        }
    } else {
        ErrorText(errors, messages);
    }
    ReleaseLocal(messages);
    ReleaseLocal(function);
    return result;
}

void cShaderProgramsManagerDX::Reset()
{
    mLink.clear();
    mLinking = false;
    mLinkers.ReleaseAll();
    mFragments.clear();
    mNames.Clear();
    for (cVertexShaderDX *s : mVertex) s->mNames.clear();
    for (cPixelShaderDX *s : mPixel) s->mNames.clear();
    std::sort(mVertex.begin(), mVertex.end(), KeyCompare<cVertexShaderDX>{false});
    std::sort(mPixel.begin(), mPixel.end(), KeyCompare<cPixelShaderDX>{true});
}

uint32_t cShaderProgramsManagerDX::AddFragments(const char *text, uint32_t size, cIGZString *errors, uint32_t flags)
{
    mLink.clear();
    mLinking = false;
    ServiceDefines service;
    ID3DXBuffer *fragments = nullptr, *messages = nullptr;
    const void *defines = service.Get();
    const HRESULT hr =
        GatherFragments(text, size, defines, D3DXFlags(flags), &fragments, errors ? &messages : nullptr);
    uint32_t result = 1;
    if (hr == kInvalidCall) {
        Log(hr, kLineGather);
        ErrorText(errors, messages);
    } else if (hr == 0) {
        mFragments.emplace_back(fragments);
        mLinkers.ReleaseAll();
        result = 0;
    } else {
        ErrorText(errors, messages);
    }
    ReleaseLocal(messages);
    ReleaseLocal(fragments);
    return result;
}

void cShaderProgramsManagerDX::BeginLink()
{
    mLink.clear();
    mLinking = true;
}

void cShaderProgramsManagerDX::AddFragment(const char *name)
{
    cRZString *const made = static_cast<cRZString *>(::operator new(sizeof(cRZString)))->Construct(name);
    made->AddRef();
    const auto inserted = mNames.insert(made);
    if (inserted.second) made->AddRef();
    mLink.emplace_back(*inserted.first);
    made->Release();
}

uint32_t cShaderProgramsManagerDX::LinkProgram(cIDeviceDX *device, cIShaderProgramDX **out,
                                               cIShaderProgramInputs **inputs, cIGZString *errors, uint32_t flags)
{
    *out = nullptr;
    *inputs = nullptr;
    mLinking = false;
    if (!device->IsDeviceReady()) return 2;
    if (mLink.empty()) {
        if (errors) errors->FromChar("No fragments added");
        return 1;
    }
    DeviceDX dx(device);
    const bool software = Software(flags);
    Key key{};
    key.device = dx.p;
    key.count = static_cast<uint32_t>(mLink.size());
    key.names = mLink.data();
    key.buffer = nullptr;
    if (!software) {
        // A hardware program: a pixel program, a vertex program for hardware processing, then one for software
        // processing linked without it.
        key.hardware = 1;
        auto pixel = Range(mPixel, key, true);
        if (pixel.first != pixel.second) {
            *out = *pixel.first;
        } else {
            auto vertex = Range(mVertex, key, false);
            if (vertex.first != vertex.second) {
                *out = *vertex.first;
            } else {
                key.hardware = 0;
                vertex = Range(mVertex, key, false);
                if (vertex.first != vertex.second) *out = *vertex.first;
            }
        }
    } else {
        key.software = 1;
        const auto vertex = Range(mVertex, key, false);
        if (vertex.first != vertex.second) *out = *vertex.first;
    }
    if (*out) {
        mLink.clear();
        (*out)->AddRef();
        return 0;
    }

    D3DDEVICE_CREATION_PARAMETERS creation;
    Check(dx.p->GetD3DDevice()->GetCreationParameters(&creation), kLineCreationParameters);
    const UINT adapter = creation.AdapterOrdinal; // not checked against the six linkers, as the original
    if (!mLinkers.p[adapter]) {
        Check(CreateFragmentLinker(dx.p->GetD3DDevice(), &mLinkers.p[adapter]), kLineCreateLinker);
        for (const CountedRef<ID3DXBuffer> &f : mFragments) {
            d3dx::ID3DXFragmentLinker *const linker = mLinkers.p[adapter];
            const HRESULT hr = linker->AddFragments(static_cast<const DWORD *>(f.p->GetBufferPointer()));
            if (hr == kInvalidCall || hr == E_FAIL) Log(hr, kLineAddFragments);
            if (hr != 0) {
                if (errors) errors->FromChar(hr == kDuplicateFragment ? "Duplicate named fragment" : "Unknown error");
                return 1;
            }
        }
    }

    const uint32_t count = static_cast<uint32_t>(mLink.size());
    std::vector<d3dx::Handle> handles(count);
    for (uint32_t i = 0; i < count; ++i) {
        d3dx::ID3DXFragmentLinker *const linker = mLinkers.p[adapter];
        cRZString *const name = mLink[i].p;
        const d3dx::Handle handle = linker->GetFragmentHandleByName(name->AsInterface()->ToChar());
        if (!handle) {
            if (errors) {
                errors->FromChar("Unknown fragment: ");
                errors->Append(name->AsInterface()->ToChar(), static_cast<uint32_t>(name->mString.size()));
            }
            return 1;
        }
        handles[i] = handle;
    }

    d3dx::FragmentDesc desc;
    Check(mLinkers.p[adapter]->GetFragmentDesc(handles[0], &desc), kLineFragmentDesc);
    const char *const prefix = (desc.Target & 0xffff0000) == 0xffff0000 ? "ps" : "vs";
    const uint32_t major = (desc.Target >> 8) & 0xff, minor = desc.Target & 0xff;
    // Seven characters at most and no room promised for the end: the original's "vs_2_sw" / "ps_3_sw" profiles leave
    // their eighth byte as the stack held it (ORIGINAL-BUG); ours is zero, so they end there (snprintf into eight bytes
    // keeps seven characters and ends them, as a seven-byte write into a zeroed buffer would).
    char profile[8] = {};
    if (minor == 0xff)
        std::snprintf(profile, sizeof(profile), "%s_%u_sw", prefix, major);
    else if (major == 2 && minor == 1)
        std::snprintf(profile, sizeof(profile), "%s_2_x", prefix);
    else
        std::snprintf(profile, sizeof(profile), "%s_%u_%u", prefix, major, minor);

    ID3DXBuffer *function = nullptr, *messages = nullptr;
    const HRESULT hr =
        mLinkers.p[adapter]->LinkShader(profile, D3DXFlags(flags), handles.data(), count, &function, &messages);
    uint32_t result = 1;
    if (hr == 0) {
        if (!CreateProgram(device, function, software, errors, out)) {
            result = 2;
        } else {
            mLink.clear();
            const DWORD bytes = function->GetBufferSize();
            const auto *tokens = static_cast<const DWORD *>(function->GetBufferPointer());
            cShaderProgramInputsDX *const made = new cShaderProgramInputsDX(tokens, bytes >> 2);
            *inputs = made;
            made->AddRef();
            result = 0;
        }
    } else {
        ErrorText(errors, messages);
    }
    ReleaseLocal(messages);
    ReleaseLocal(function);
    return result;
}

bool cShaderProgramsManagerDX::Disassemble(cIGZString *out, cIGZUnknown *program)
{
    out->Erase(0, 0xffffffff);
    cIShaderProgramDX *shader = nullptr;
    program->QueryInterface(kIidShaderProgramDX, reinterpret_cast<void **>(&shader));
    const DWORD *const function = shader->GetFunction(); // no null check, as the original
    ID3DXBuffer *text = nullptr;
    const HRESULT hr = DisassembleShader(function, &text);
    if (hr >= 0) {
        const char *const chars = static_cast<const char *>(text->GetBufferPointer());
        const DWORD size = text->GetBufferSize();
        out->FromChar(chars, size);
    }
    ReleaseLocal(text);
    if (shader) shader->Release();
    return hr >= 0;
}

namespace {
// "<label> alloc size: %.1fKB, avg: %.1fbytes" over `bytes` and `count` programs, in floats as the original computes it.
void SizeLine(cRZString &line, const char *format, uint32_t bytes, uint32_t count)
{
    const float total = static_cast<float>(static_cast<double>(bytes));
    const float kb = total * 0.0009765625f;
    const float average = total / static_cast<float>(static_cast<double>(count ? count : 1));
    line.Sprintf(format, static_cast<double>(kb), static_cast<double>(average));
}
} // namespace

void cShaderProgramsManagerDX::Dump(ifaces::cIGZOStream *stream)
{
    if (!stream)
        stream = reinterpret_cast<ifaces::cIGZOStream *>(
            static_cast<cRZTextODSStream *>(::operator new(sizeof(cRZTextODSStream)))->Construct());
    stream->AddRef();
    stream->SetRZCharStr("================================================================================\n");
    stream->SetRZCharStr("=                            Shader programs manager                           =\n");
    stream->SetRZCharStr("================================================================================\n");

    uint32_t bytes = 0;
    for (const CountedRef<ID3DXBuffer> &f : mFragments) bytes += f.p->GetBufferSize();
    uint32_t count = static_cast<uint32_t>(mFragments.size());
    stream->SetRZCharStr("Fragment buffers count: ");
    stream->SetUint32(count);
    stream->SetRZCharStr("\n");
    cRZString line;
    line.Construct();
    SizeLine(line, "Fragment buffers alloc size: %.1fKB, avg: %.1fbytes\n", bytes, count);
    stream->SetRZCharStr(line.mString.c_str());

    uint32_t hardware = 0;
    bytes = 0;
    for (cVertexShaderDX *s : mVertex) {
        hardware += s->GetInfo()->hardware;
        bytes += s->mFunction->GetBufferSize();
    }
    count = static_cast<uint32_t>(mVertex.size());
    stream->SetRZCharStr("Vertex program count: ");
    stream->SetUint32(count);
    stream->SetRZCharStr(", HW processing: ");
    stream->SetUint32(hardware);
    stream->SetRZCharStr("\n");
    SizeLine(line, "Vertex program alloc size: %.1fKB, avg: %.1fbytes\n", bytes, count);
    stream->SetRZCharStr(line.mString.c_str());

    count = static_cast<uint32_t>(mPixel.size());
    stream->SetRZCharStr("Pixel program count: ");
    stream->SetUint32(count);
    stream->SetRZCharStr("\n");
    bytes = 0;
    for (cPixelShaderDX *s : mPixel) bytes += s->mFunction->GetBufferSize();
    SizeLine(line, "Pixel program alloc size: %.1fKB, avg: %.1fbytes\n", bytes, count);
    stream->SetRZCharStr(line.mString.c_str());
    line.Destruct();
    stream->Release();
}

} // namespace nGZGraphic4
