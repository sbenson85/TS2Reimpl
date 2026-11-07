#pragma once
// The shader programs (lane NR, rendering R3b step 3): nGZGraphic4::cShaderProgramsManagerDX, cShaderProgramInputsDX,
// cVertexShaderDX and cPixelShaderDX. docs/subsystems/rendering.md, R3b.
//
// The manager turns shader text into Direct3D shaders through D3DX (statically linked, third-party: called where it
// lies): AssembleProgram (assembly text), CompileProgram (HLSL with an entry point and a profile) and the fragment
// linker (AddFragments gathers fragment text; BeginLink, AddFragment per fragment name, then LinkProgram links them into
// one shader per adapter). A made program is shared: the manager keeps every vertex and pixel shader sorted by a key
// (the device, the fragment names it was linked from, its function bytes, its processing) and answers an equal one
// when there is one. Compiled and linked programs come with their inputs (cShaderProgramInputsDX): the constants and
// samplers of the program's constant table, sorted by name.
//
// Standalone shape (docs/standalone-shape.md): the manager is a static singleton answered by its getter 0x1758c0 (the
// graphic system's slot 19 jumps to it; 0x18ddf0 caches that answer). The getter is hooked and answers ours, so the
// manager's vtable is never reached; the shaders and inputs only the manager makes are ours too: every slot is in
// effect through its callers.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "gz/cSurfaceDX.h" // cIDeviceResource
#include "rz/cRZString.h"

class cIGZString;
#include "game/owned.h"
#include "rz/RZFastFixedAllocators.h"

// The classes' fixed-size pools (operator new / delete).
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x28>, 10ca2fc);
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x28>, 10ca308);

namespace ifaces {
class cIGZOStream;
}

namespace nGZGraphic4 {

class cIDeviceDX;

// ---- the D3DX interfaces the manager uses (d3dx9shader.h's layouts; COM, __stdcall) ----
namespace d3dx {

struct ID3DXBuffer : public IUnknown {
    virtual void *__stdcall GetBufferPointer() = 0; // 3
    virtual DWORD __stdcall GetBufferSize() = 0;    // 4
};

using Handle = const char *; // D3DXHANDLE

struct ConstantTableDesc { // D3DXCONSTANTTABLE_DESC
    const char *Creator;
    DWORD Version;
    UINT Constants;
};

struct ConstantDesc { // D3DXCONSTANT_DESC (0x30 bytes)
    const char *Name;
    uint32_t RegisterSet;   // 0 bool, 1 int4, 2 float4, 3 sampler
    UINT RegisterIndex;
    UINT RegisterCount;
    uint32_t Class;         // 0 scalar, 1 vector, 2 matrix rows, 3 matrix columns, 4 object, 5 struct
    uint32_t Type;          // 0 void, 1 bool, 2 int, 3 float, ... 10-14 the samplers
    UINT Rows;
    UINT Columns;
    UINT Elements;
    UINT StructMembers;
    UINT Bytes;
    const void *DefaultValue;
};
static_assert(sizeof(ConstantDesc) == 0x30, "D3DXCONSTANT_DESC");

struct ID3DXConstantTable : public ID3DXBuffer {
    virtual HRESULT __stdcall GetDesc(ConstantTableDesc *desc) = 0;                             // 5
    virtual HRESULT __stdcall GetConstantDesc(Handle constant, ConstantDesc *desc, UINT *count) = 0; // 6
    virtual UINT __stdcall GetSamplerIndex(Handle constant) = 0;                                // 7
    virtual Handle __stdcall GetConstant(Handle constant, UINT index) = 0;                      // 8
};

struct FragmentDesc { // D3DXFRAGMENT_DESC
    const char *Name;
    DWORD Target; // the shader version token
};

struct ID3DXFragmentLinker : public IUnknown {
    virtual HRESULT __stdcall GetDevice(IDirect3DDevice9 **device) = 0;              // 3
    virtual UINT __stdcall GetNumberOfFragments() = 0;                               // 4
    virtual Handle __stdcall GetFragmentHandleByIndex(UINT index) = 0;               // 5
    virtual Handle __stdcall GetFragmentHandleByName(const char *name) = 0;          // 6
    virtual HRESULT __stdcall GetFragmentDesc(Handle fragment, FragmentDesc *desc) = 0; // 7
    virtual HRESULT __stdcall AddFragments(const DWORD *fragments) = 0;              // 8
    virtual HRESULT __stdcall GetAllFragments(ID3DXBuffer **buffer) = 0;             // 9
    virtual HRESULT __stdcall GetFragment(Handle fragment, ID3DXBuffer **buffer) = 0; // 10
    virtual HRESULT __stdcall LinkShader(const char *profile, DWORD flags, const Handle *fragments, UINT count,
                                         ID3DXBuffer **buffer, ID3DXBuffer **errors) = 0; // 11
};

} // namespace d3dx

// Interface ids.
constexpr uint32_t kIidShaderProgramsManager = 0xcba41a4a;
constexpr uint32_t kIidShaderProgramInputs = 0x2c38f4b6;
constexpr uint32_t kIidShaderProgram = 0x5b82db15;
constexpr uint32_t kIidShaderProgramDX = 0xccc09644;
constexpr uint32_t kIidVertexShader = 0x5b840091;
constexpr uint32_t kIidPixelShader = 0xcc433302;
constexpr uint32_t kIidShaderResource = 0xda02e09f;  // cIAllocDeviceResource's (gz/cSurfaceDX.h)
constexpr uint32_t kIidD3DVertexShader = 0x09b1b574; // IDirect3DVertexShader9 (+0xc, not counted)
constexpr uint32_t kIidD3DPixelShader = 0xcc433307;  // IDirect3DPixelShader9 (+0xc, not counted)

// What a program is (its slot 10; 8 bytes).
struct cShaderProgramInfo {
    uint32_t kind;    // +0: 0 vertex, 1 pixel
    uint8_t major;    // +4: the version token's
    uint8_t minor;    // +5 (0xff: a software vertex shader)
    uint8_t software; // +6: made for software vertex processing
    uint8_t hardware; // +7: runs with hardware vertex processing (cDeviceStateDX::SetVertexShader reads it)
};
static_assert(sizeof(cShaderProgramInfo) == 8, "cShaderProgramInfo");

// nGZGraphic4::cIShaderProgram / cIShaderProgramDX / cIVertexShaderDX / cIPixelShaderDX (slots 10-15; where the faces
// split is a guess).
class cIShaderProgram : public cIDeviceResource {
public:
    virtual cShaderProgramInfo *GetInfo() = 0;  // 10
    virtual cIDeviceDX *GetDeviceDX() = 0;      // 11
    virtual const DWORD *GetFunction() = 0;     // 12: the tokens (the D3DX buffer's)
    virtual uint32_t GetFunctionSize() = 0;     // 13: in bytes
};
class cIShaderProgramDX : public cIShaderProgram {
public:
    virtual IUnknown *GetD3DShader() = 0;       // 14: IDirect3DVertexShader9 / IDirect3DPixelShader9 (not counted)
};
class cIVertexShaderDX : public cIShaderProgramDX {
public:
    virtual ~cIVertexShaderDX() = default;      // 15
};
class cIPixelShaderDX : public cIShaderProgramDX {
public:
    virtual ~cIPixelShaderDX() = default;       // 15
};

// A program constant as the inputs hand it out (0x2c bytes).
struct cShaderConstant {
    const char *name;        // +0 (the inputs' own copy)
    uint32_t registerSet;    // +4: D3DX's
    uint32_t registerIndex;
    uint32_t registerCount;
    uint32_t type;           // +0x10: D3DX's parameter type (0-3)
    uint32_t shape;          // +0x14: 0 scalar, 1 vector, 2 matrix (columns), 3 struct
    uint32_t vectorColumns;  // +0x18: a vector's
    uint32_t matrixRows;     // +0x1c: a matrix's
    uint32_t matrixColumns;  // +0x20
    uint32_t structMembers;  // +0x24
    uint32_t elements;       // +0x28
};
static_assert(sizeof(cShaderConstant) == 0x2c, "cShaderConstant");

// A sampler (12 bytes): its name, the number of sampler registers and their indices.
struct cShaderSampler {
    const char *name;
    uint32_t count;
    const uint32_t *registers;
};
static_assert(sizeof(cShaderSampler) == 12, "cShaderSampler");

// nGZGraphic4::cIShaderProgramInputs.
class cIShaderProgramInputs : public cIGZUnknown {
public:
    virtual uint32_t GetConstantCount() = 0;                    // 3
    virtual cShaderConstant FindConstant(const char *name) = 0; // 4: all zero when there is none
    virtual cShaderConstant GetConstant(uint32_t i) = 0;        // 5
    virtual uint32_t GetSamplerCount() = 0;                     // 6
    virtual cShaderSampler FindSampler(const char *name) = 0;   // 7: all zero when there is none
    virtual cShaderSampler GetSampler(uint32_t i) = 0;          // 8
    virtual ~cIShaderProgramInputs() = default;                 // 9
};

// nGZGraphic4::cIShaderProgramsManager. The make slots answer 0 (made), 1 (D3DX refused the text: its messages go into
// `errors` when given) or 2 (the device isn't ready, or the program can't be made on it).
class cIShaderProgramsManager : public cIGZUnknown {
public:
    // 3: assembly text.
    virtual uint32_t AssembleProgram(cIDeviceDX *device, const char *text, uint32_t size, cIShaderProgramDX **out,
                                     cIGZString *errors, uint32_t flags) = 0;
    // 4: HLSL, with its inputs.
    virtual uint32_t CompileProgram(cIDeviceDX *device, const char *entry, const char *profile, const char *text,
                                    uint32_t size, cIShaderProgramDX **out, cIShaderProgramInputs **inputs,
                                    cIGZString *errors, uint32_t flags) = 0;
    virtual void Reset() = 0; // 5: drops the fragments and the linkers
    // 6: fragment text (D3DXGatherFragments); the linkers are dropped, to be made again with it.
    virtual uint32_t AddFragments(const char *text, uint32_t size, cIGZString *errors, uint32_t flags) = 0;
    virtual void BeginLink() = 0;                      // 7
    virtual void AddFragment(const char *name) = 0;    // 8: one to link
    // 9: the fragments added since BeginLink, linked.
    virtual uint32_t LinkProgram(cIDeviceDX *device, cIShaderProgramDX **out, cIShaderProgramInputs **inputs,
                                 cIGZString *errors, uint32_t flags) = 0;
    virtual bool IsLinking() = 0;                      // 10
    virtual bool Disassemble(cIGZString *out, cIGZUnknown *program) = 0; // 11
    virtual void Dump(ifaces::cIGZOStream *stream) = 0; // 12: counts and sizes as text (null: the debugger output)
    virtual ~cIShaderProgramsManager() = default;      // 13
};

// The flags of the make slots: bit 1 debug information (D3DX's D3DXSHADER_DEBUG), bit 2 software vertex processing
// (compiled, gathered and linked text gets D3DXSHADER_SKIPVALIDATION without it).
namespace shader_detail {
inline uint32_t D3DXFlags(uint32_t flags) { return (~(flags * 4) & 4) | ((flags >> 1) & 1); }
inline bool Software(uint32_t flags) { return ((flags >> 2) & 1) != 0; }

// A counted pointer as the original's vectors of them hold it: counted when copied, released when destroyed, and never
// moved (no move constructor), so a vector growing copies each element (counted again) and releases the old ones, as
// the original's reallocations do (0x16f200, 0x158c50): the D3DX buffers' fakes see those calls.
template <class T> struct CountedRef {
    T *p = nullptr;
    CountedRef() = default;
    explicit CountedRef(T *q) : p(q)
    {
        if (p) p->AddRef();
    }
    CountedRef(const CountedRef &o) : p(o.p)
    {
        if (p) p->AddRef();
    }
    CountedRef &operator=(const CountedRef &o)
    {
        if (o.p) o.p->AddRef();
        T *old = p;
        p = o.p;
        if (old) old->Release();
        return *this;
    }
    ~CountedRef()
    {
        if (p) p->Release();
    }
};

// The order the manager keeps its programs in (0x1747d0 over the 0x14-byte keys 0x1738a0 / 0x174920 / 0x1749b0 build):
// device, fragment count, then software and hardware, then with no fragments the function's size and bytes, with
// fragments the fragment names (by address). The buffers' sizes and bytes are read through their slots, so the
// comparisons' calls are part of the behaviour.
struct Key {
    void *device;
    uint32_t count;
    const CountedRef<cRZString> *names;
    d3dx::ID3DXBuffer *buffer;
    uint8_t software;
    uint8_t hardware;
};
static_assert(sizeof(Key) == 0x14, "the original's key");
bool KeyLess(const Key &a, const Key &b);
} // namespace shader_detail

class cShaderProgramsManagerDX;

// The programs' common part (0x28 bytes): +4 count, +8 the device (cIDeviceDX, not counted), +0xc the Direct3D shader
// (counted), +0x10 the D3DX buffer of the function (counted), +0x14 the fragment names it was linked from (counted),
// +0x20 the info. Made only by the manager (CreateProgram), from the fixed-size pools 0x10ca2fc / 0x10ca308.
template <class Face, class D3DShader> class cShaderProgramT : public Face {
public:
    uint32_t AddRef() override { return ++mRefs; }
    uint32_t Release() override;
    cIDeviceDX *GetDevice() override { return mDevice; }
    cIDeviceDX *GetDeviceConst() override { return mDevice; }
    bool IsCreated() override { return mShader != nullptr; }
    bool SurvivesReset() override { return false; }
    bool SurvivesModeChange() override { return false; }
    cShaderProgramInfo *GetInfo() override { return &mInfo; }
    cIDeviceDX *GetDeviceDX() override { return mDevice; }
    const DWORD *GetFunction() override { return static_cast<const DWORD *>(mFunction->GetBufferPointer()); }
    uint32_t GetFunctionSize() override { return mFunction->GetBufferSize(); }
    IUnknown *GetD3DShader() override { return mShader; }

    uint32_t mRefs = 0;                    // +4
    cIDeviceDX *mDevice = nullptr;         // +8
    D3DShader *mShader = nullptr;          // +0xc
    d3dx::ID3DXBuffer *mFunction = nullptr; // +0x10
    std::vector<shader_detail::CountedRef<cRZString>> mNames; // +0x14
    cShaderProgramInfo mInfo{};            // +0x20

protected:
    // ReleaseResource's body (also the destructor's), up to leaving the manager's list (each leaf's own): unregister
    // from the device's resource manager, release the shader and the function, drop the names.
    void Drop();
    // The destructor's rest: the names, the function and the shader released.
    void Destroy();
};

class cVertexShaderDX final : public cShaderProgramT<cIVertexShaderDX, IDirect3DVertexShader9> {
public:
    static constexpr uint32_t kPoolRva = 0x10ca2fc; // RZFastFixed0x28
    static rz::cRZFastFixedAllocator2<0x28> &Pool() { return ts2_rva_10ca2fc; }
    bool QueryInterface(uint32_t riid, void **out) override;
    void ReleaseResource() override;
    uint32_t GetMemorySize() override;
    ~cVertexShaderDX() override;
    static void *operator new(size_t size);
    static void operator delete(void *block, size_t size);
};
static_assert(sizeof(cVertexShaderDX) == 0x28, "cVertexShaderDX size");

class cPixelShaderDX final : public cShaderProgramT<cIPixelShaderDX, IDirect3DPixelShader9> {
public:
    static constexpr uint32_t kPoolRva = 0x10ca308; // RZFastFixed0x28
    static rz::cRZFastFixedAllocator2<0x28> &Pool() { return ts2_rva_10ca308; }
    bool QueryInterface(uint32_t riid, void **out) override;
    void ReleaseResource() override;
    uint32_t GetMemorySize() override;
    ~cPixelShaderDX() override;
    static void *operator new(size_t size);
    static void operator delete(void *block, size_t size);
};
static_assert(sizeof(cPixelShaderDX) == 0x28, "cPixelShaderDX size");

// The inputs' records: what slots 4 / 5 and 7 / 8 hand out, followed by the name (and the sampler's registers) the
// handed-out pointers point into.
struct cShaderConstantRecord {
    cShaderConstant desc; // +0
    std::string name;     // +0x2c
};
static_assert(sizeof(cShaderConstantRecord) == 0x44, "constant record");
struct cShaderSamplerRecord {
    cShaderSampler desc;     // +0
    std::string name;        // +0xc
    uint32_t registers[16];  // +0x24
};
static_assert(sizeof(cShaderSamplerRecord) == 100, "sampler record");

class cShaderProgramInputsDX final : public cIShaderProgramInputs {
public:
    // 0x173950: the constants and samplers of the program `function` (its constant table), sorted by name.
    cShaderProgramInputsDX(const DWORD *function, uint32_t words);

    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t AddRef() override { return ++mRefs; }
    uint32_t Release() override;
    uint32_t GetConstantCount() override { return static_cast<uint32_t>(mConstants.size()); }
    cShaderConstant FindConstant(const char *name) override;
    cShaderConstant GetConstant(uint32_t i) override { return mConstants[i].desc; }
    uint32_t GetSamplerCount() override { return static_cast<uint32_t>(mSamplers.size()); }
    cShaderSampler FindSampler(const char *name) override;
    cShaderSampler GetSampler(uint32_t i) override { return mSamplers[i].desc; }
    ~cShaderProgramInputsDX() override = default;

    uint32_t mRefs = 0;                            // +4
    std::vector<cShaderConstantRecord> mConstants; // +8
    std::vector<cShaderSamplerRecord> mSamplers;   // +0x14
};
static_assert(sizeof(cShaderProgramInputsDX) == 0x20, "cShaderProgramInputsDX size");

class cShaderProgramsManagerDX final : public cIShaderProgramsManager {
public:
    static constexpr uint32_t kGetterRva = 0x1758c0;
    static cShaderProgramsManagerDX *Instance(); // the getter (hooked)

    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t AddRef() override { return 1; } // a static object: not counted
    uint32_t Release() override { return 1; }
    uint32_t AssembleProgram(cIDeviceDX *device, const char *text, uint32_t size, cIShaderProgramDX **out,
                             cIGZString *errors, uint32_t flags) override;
    uint32_t CompileProgram(cIDeviceDX *device, const char *entry, const char *profile, const char *text, uint32_t size,
                            cIShaderProgramDX **out, cIShaderProgramInputs **inputs, cIGZString *errors,
                            uint32_t flags) override;
    void Reset() override;
    uint32_t AddFragments(const char *text, uint32_t size, cIGZString *errors, uint32_t flags) override;
    void BeginLink() override;
    void AddFragment(const char *name) override;
    uint32_t LinkProgram(cIDeviceDX *device, cIShaderProgramDX **out, cIShaderProgramInputs **inputs, cIGZString *errors,
                         uint32_t flags) override;
    bool IsLinking() override { return mLinking; }
    bool Disassemble(cIGZString *out, cIGZUnknown *program) override;
    void Dump(ifaces::cIGZOStream *stream) override;
    ~cShaderProgramsManagerDX() override = default;

    cShaderProgramsManagerDX();
    cShaderProgramsManagerDX(const cShaderProgramsManagerDX &) = delete;
    cShaderProgramsManagerDX &operator=(const cShaderProgramsManagerDX &) = delete;

    // 0x176100 (in effect through the make slots): the program of the D3DX buffer `function` on `device`, the shared
    // one when an equal one exists (counted into *out). False when the device can't run it.
    bool CreateProgram(cIDeviceDX *device, d3dx::ID3DXBuffer *function, bool software, cIGZString *errors,
                       cIShaderProgramDX **out);

    struct Linkers {
        d3dx::ID3DXFragmentLinker *p[6] = {};
        ~Linkers(); // last to first
        void ReleaseAll(); // first to last, each emptied
    };
    // The fragment names, ordered by size then text. Each is counted when it goes in and released when the set is
    // emptied (Reset, the destructor: the original's tree erase 0x16f520 releases each node's name).
    struct NameLess {
        bool operator()(const cRZString *a, const cRZString *b) const;
    };
    struct Names : std::set<cRZString *, NameLess> {
        Names() = default;
        Names(const Names &) = delete;
        Names &operator=(const Names &) = delete;
        ~Names() { Clear(); }
        void Clear();
    };

    // 0x176e40 (lane RB): the fragment linker of adapter `index` released and emptied (cGraphicSystemDX's device events).
    void ReleaseLinker(uint32_t index);

    Linkers mLinkers;                            // +4: per adapter ordinal (counted)
    std::vector<cVertexShaderDX *> mVertex;      // +0x1c: sorted by key (not counted)
    std::vector<cPixelShaderDX *> mPixel;        // +0x28
    std::vector<shader_detail::CountedRef<d3dx::ID3DXBuffer>> mFragments; // +0x34: what AddFragments gathered
    Names mNames;                                // +0x40 (counted)
    std::vector<shader_detail::CountedRef<cRZString>> mLink; // +0x48: the names to link
    bool mLinking = false;                       // +0x54
};
static_assert(sizeof(cShaderProgramsManagerDX) == 0x58, "cShaderProgramsManagerDX size");

} // namespace nGZGraphic4
