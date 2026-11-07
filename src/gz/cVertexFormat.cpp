// The vertex formats and their Direct3D declarations (lane NJ, rendering R3 part 3): src/gz/cVertexFormat.h.
#include "gz/cVertexFormat.h"

#include <algorithm>

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cDeviceDX.h"
#include "gz/cIGZString.h"
#include "ifaces/cIDeviceResourceManager.h"
#include "rz/MiscHelpersA.h" // LogD3DError
#include "rz/RZFastFixedAllocators.h"
#include "rz/cRZString.h"

namespace nGZGraphic4 {

namespace format_detail {

// 0xfecf9c: bytes per component type (one to four floats, four bytes, a colour).
constexpr uint32_t kTypeBytes[6] = {4, 8, 12, 16, 4, 4};

// 0x16d5c0: EndFormat's sort key. The common layouts first (position, weights, indices, normal, the two colours, the
// first eight texture coordinates with their usual component counts), then each usage in a band of 100 by its index;
// unknown usages first of all (-1).
int32_t SortKey(const cVertexElement &e)
{
    const uint32_t index = e.index;
    switch (e.usage) {
    case 0x5b830781:
        if (index == 0 && e.type == 2) return 0;
        break;
    case 0x3bd70105:
        if (index == 0 && e.type <= 2) return 1;
        break;
    case 0xfbd70111:
        if (index == 0 && e.type == 4) return 2;
        break;
    case 0x3b83078b:
        if (index == 0 && e.type == 2) return 3;
        break;
    case 0xdb830795:
        if (index == 0 && e.type == 5) return 5;
        if (index == 1 && e.type == 5) return 6;
        break;
    case 0xbb8307ab:
        if (index < 8 && e.type <= 3) return static_cast<int32_t>(index) + 7;
        break;
    }
    int32_t band;
    switch (e.usage) {
    case 0x5b830781: band = 100; break;
    case 0x3bd70105: band = 200; break;
    case 0xfbd70111: band = 300; break;
    case 0x3b83078b: band = 400; break;
    case 0xdb830795: band = 500; break;
    case 0xbb8307ab: band = 600; break;
    case 0x89d92ba0: band = 700; break;
    case 0x69d92b93: band = 800; break;
    case 0xdcf2cfdc: band = 900; break;
    case 0x5cf2cfe1: band = 1000; break;
    case 0xcb6f3a6a: band = 1100; break;
    case 0xeb720693: band = 1200; break;
    case 0xcb7206a1: band = 1300; break;
    case 0x114113c3: band = 1400; break;
    case 0x114113cd: band = 1500; break;
    default: return -1;
    }
    return band + static_cast<int32_t>(index);
}

// A cRZString local (built and destroyed by hand, as the original's inlined code does).
struct LocalString {
    union {
        cRZString s;
    };
    LocalString() { s.Construct(); }
    explicit LocalString(const char *text) { s.Construct(text); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
    ~LocalString() { s.Destruct(); }
    const cIGZString &Z() const { return *reinterpret_cast<const cIGZString *>(&s); }
};

} // namespace format_detail

using namespace format_detail;

const char *VertexUsageName(uint32_t usage)
{
    switch (usage) {
    case 0x5b830781: return "pos";
    case 0x3bd70105: return "weights";
    case 0xfbd70111: return "blendidx";
    case 0x3b83078b: return "norm";
    case 0xdb830795: return "col";
    case 0xbb8307ab: return "tc";
    case 0x89d92ba0: return "tangent";
    case 0x69d92b93: return "binorm";
    case 0xdcf2cfdc: return "targetidx";
    case 0x5cf2cfe1: return "posdelta";
    case 0xcb6f3a6a: return "normdelta";
    case 0xeb720693: return "coldelta";
    case 0xcb7206a1: return "uvdelta";
    case 0x114113c3: return "vertexid";
    case 0x114113cd: return "seamvertexid";
    default: return "UNKNOWN";
    }
}

// ---- cVertexFormat ----

bool cVertexFormat::QueryInterface(uint32_t riid, void **out)
{
    if (out && (riid == 1 || riid == kIidVertexFormat)) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

uint32_t cVertexFormat::Release()
{
    if (--mRefs != 0) return mRefs;
    delete this;
    return 0;
}

uint32_t cVertexFormat::GetTotalElementCount()
{
    uint32_t n = 0;
    for (const Stream &s : mStreams) n += static_cast<uint32_t>(s.size());
    return n;
}

cVertexElement *cVertexFormat::GetElement(cVertexElement *out, uint32_t stream, uint32_t i)
{
    *out = mStreams[stream][i];
    return out;
}

// The element FindElement answers when there is none: no usage, type 6, the stream count as its stream.
cVertexElement *cVertexFormat::None(cVertexElement *out)
{
    const uint8_t streams = static_cast<uint8_t>(GetStreamCount());
    *out = cVertexElement{};
    out->type = 6;
    out->stream = streams;
    return out;
}

cVertexElement *cVertexFormat::FindElement(cVertexElement *out, uint32_t stream, uint32_t usage, uint8_t index)
{
    for (const cVertexElement &e : mStreams[stream])
        if (e.usage == usage && e.index == index) {
            *out = e;
            return out;
        }
    return None(out);
}

cVertexElement *cVertexFormat::FindElementAny(cVertexElement *out, uint32_t usage, uint8_t index)
{
    for (const Stream &s : mStreams)
        for (const cVertexElement &e : s)
            if (e.usage == usage && e.index == index) {
                *out = e;
                return out;
            }
    return None(out);
}

int32_t cVertexFormat::GetMaxIndex(uint32_t usage)
{
    int32_t best = -1;
    for (const Stream &s : mStreams)
        for (const cVertexElement &e : s)
            if (e.usage == usage && best < static_cast<int32_t>(e.index)) best = e.index;
    return best;
}

uint32_t cVertexFormat::GetStreamSize(uint32_t stream)
{
    uint32_t bytes = 0;
    for (int32_t i = 0; i < static_cast<int32_t>(GetElementCount(stream)); ++i)
        bytes += GetElementSize(stream, static_cast<uint32_t>(i));
    return bytes;
}

uint32_t cVertexFormat::GetElementSize(uint32_t stream, uint32_t i)
{
    cVertexElement e;
    // The original indexes its six-entry table (0xfecf9c) with any type: types past 5 read the words after it, the
    // formats manager's static object, which our build never uses. Ours answers 0 for them (no component).
    const uint32_t type = GetElement(&e, stream, i)->type;
    return type < 6 ? kTypeBytes[type] : 0;
}

uint32_t cVertexFormat::GetElementOffset(uint32_t stream, int32_t i)
{
    uint32_t bytes = 0;
    for (int32_t k = 0; k < i; ++k) bytes += GetElementSize(stream, static_cast<uint32_t>(k));
    return bytes;
}

uint32_t cVertexFormat::GetOffset(uint32_t stream, uint32_t usage, uint8_t index)
{
    cVertexElement e;
    FindElement(&e, stream, usage, index);
    return GetElementOffset(e.stream, e.position);
}

uint32_t cVertexFormat::GetOffsetAny(uint32_t usage, uint8_t index)
{
    cVertexElement e;
    FindElementAny(&e, usage, index);
    return GetElementOffset(e.stream, e.position);
}

void cVertexFormat::Describe(cIGZString *out)
{
    static const char *const kTypeText[6] = {":1f", ":2f", ":3f", ":4f", ":ub4", ":argb"};
    LocalString text;
    out->Erase(0, 0xffffffffu);
    text.s.Sprintf("id:%d", mId);
    out->Append(text.Z());
    for (uint32_t s = 0; s < GetStreamCount(); ++s) {
        text.s.Sprintf("{%d", s);
        out->Append(text.Z());
        for (uint32_t i = 0; i < GetElementCount(s); ++i) {
            cVertexElement e;
            GetElement(&e, s, i);
            text.s.Sprintf(" %s%d", VertexUsageName(e.usage), e.index);
            out->Append(text.Z());
            if (e.type < 6) {
                LocalString type(kTypeText[e.type]);
                out->Append(type.Z());
            }
        }
        out->Append("}", 1);
    }
}

// ---- cVertexFormatsManager ----

cVertexFormatsManager::cVertexFormatsManager()
{
    mBuilder = new cVertexFormat;
    mBuilder->AddRef();
    mFormats.push_back(nullptr);
}

cVertexFormatsManager::~cVertexFormatsManager()
{
    if (mBuilder) mBuilder->Release();
    for (cIVertexFormat *f : mFormats)
        if (f) f->Release();
}

cVertexFormatsManager *cVertexFormatsManager::Instance()
{
    static cVertexFormatsManager instance;
    return &instance;
}

bool cVertexFormatsManager::QueryInterface(uint32_t riid, void **out)
{
    if (out && (riid == 1 || riid == kIidVertexFormatsManager)) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

void cVertexFormatsManager::BeginFormat()
{
    for (cIVertexFormat *&f : mFormats) {
        if (!f) continue;
        // Held by nobody else: dropped.
        f->AddRef();
        if (f->Release() == 1) {
            cIVertexFormat *held = f;
            f = nullptr;
            held->Release();
        }
    }
    mBuilding = true;
}

void cVertexFormatsManager::AddElement(uint32_t stream, uint32_t type, uint32_t usage, uint8_t index)
{
    std::vector<cVertexFormat::Stream> &streams = mBuilder->mStreams;
    if (stream >= streams.size()) streams.resize(stream + 1);
    cVertexFormat::Stream &s = streams[stream];
    cVertexElement e{};
    e.usage = usage;
    e.index = index;
    e.type = type;
    e.stream = static_cast<uint8_t>(stream);
    e.position = static_cast<uint8_t>(s.size());
    s.push_back(e);
}

namespace {
// Two formats with the same streams of the same usages, indices and types.
bool SameLayout(cIVertexFormat *built, cIVertexFormat *other)
{
    if (other->GetStreamCount() != built->GetStreamCount()) return false;
    for (int32_t s = 0; s < static_cast<int32_t>(other->GetStreamCount()); ++s) {
        if (other->GetElementCount(s) != built->GetElementCount(s)) return false;
        for (int32_t i = 0; i < static_cast<int32_t>(other->GetElementCount(s)); ++i) {
            cVertexElement a, b;
            other->GetElement(&a, s, i);
            built->GetElement(&b, s, i);
            if (a.usage != b.usage || a.index != b.index || a.type != b.type) return false;
        }
    }
    return true;
}
} // namespace

void cVertexFormatsManager::EndFormat(cIVertexFormat **out)
{
    mBuilding = false;
    for (cVertexFormat::Stream &s : mBuilder->mStreams) {
        std::sort(s.begin(), s.end(),
                  [](const cVertexElement &a, const cVertexElement &b) { return SortKey(a) < SortKey(b); });
        uint8_t position = 0;
        for (cVertexElement &e : s) e.position = position++;
    }
    const uint32_t count = static_cast<uint32_t>(mFormats.size());
    uint32_t slot = count; // the first empty entry past 0
    for (uint32_t i = 1; i < count; ++i) {
        cIVertexFormat *f = mFormats[i];
        if (!f) {
            slot = std::min(slot, i);
        } else if (SameLayout(mBuilder, f)) {
            mBuilder->mStreams.clear();
            *out = mFormats[i];
            (*out)->AddRef();
            return;
        }
    }
    if (slot == mFormats.size()) mFormats.push_back(nullptr);
    cVertexFormat *made = new cVertexFormat;
    made->mId = mBuilder->mId;
    made->mStreams = mBuilder->mStreams;
    cIVertexFormat *&entry = mFormats[slot];
    if (entry != made) {
        made->AddRef();
        cIVertexFormat *old = entry;
        entry = made;
        if (old) old->Release();
    }
    static_cast<cVertexFormat *>(mFormats[slot])->mId = slot;
    mBuilder->mStreams.clear();
    *out = mFormats[slot];
    (*out)->AddRef();
}

// ---- cVertexDeclarationDX ----

namespace {
void Check(HRESULT hr, uint32_t line)
{
    if (FAILED(hr))
        LogD3DError(static_cast<uint32_t>(hr),
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ts2_rva_d9ea18)),
                    line);
}
ifaces::cIDeviceResourceManager *Manager(cIDeviceDX *device)
{
    return As<ifaces::cIDeviceResourceManager>(device->GetResourceManager());
}
// The declarations manager. The original asks 0x18dd70, which caches what the graphic system's slot 18 answers: a
// jump to the hooked getter, so ours.
cVertexDeclarationsManagerDX *Declarations() { return cVertexDeclarationsManagerDX::Instance(); }
} // namespace

void *cVertexDeclarationDX::operator new(size_t) { return reinterpret_cast<RZFixedAllocatorBase *>(&Pool())->Alloc(); }
void cVertexDeclarationDX::operator delete(void *block, size_t) { reinterpret_cast<RZFastFixed0x58 *>(&Pool())->Free(block); }

bool cVertexDeclarationDX::QueryInterface(uint32_t riid, void **out)
{
    if (riid == kIidD3DVertexDeclaration) {
        *out = mDeclaration;
        return true;
    }
    if (out && (riid == 1 || riid == kIidVertexDeclaration || riid == kIidVertexDeclarationDX || riid == 0xda02e09f)) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

uint32_t cVertexDeclarationDX::Release()
{
    if (--mRefs != 0) return mRefs;
    delete this;
    return 0;
}

// Unregister from the device's resource manager, release the declaration and empty its entry in the manager.
void cVertexDeclarationDX::Drop()
{
    Manager(mDevice)->Erase(this);
    if (mDeclaration) {
        mDeclaration->Release();
        mDeclaration = nullptr;
    }
    mDevice = nullptr;
    Declarations()->mDeclarations[mId] = nullptr;
    mId = 0;
}

void cVertexDeclarationDX::ReleaseResource()
{
    if (IsCreated()) Drop();
}

uint32_t cVertexDeclarationDX::GetMemorySize()
{
    UINT count = 0;
    if (mDeclaration) {
        D3DVERTEXELEMENT9 elements[65];
        Check(mDeclaration->GetDeclaration(elements, &count), 0x9e);
    }
    return count * sizeof(D3DVERTEXELEMENT9) + sizeof(cVertexDeclarationDX);
}

cVertexDeclarationDX::~cVertexDeclarationDX()
{
    if (mDeclaration) Drop();
    if (mFormat) mFormat->Release();
}

// ---- cVertexDeclarationsManagerDX ----

cVertexDeclarationsManagerDX::cVertexDeclarationsManagerDX() { mDeclarations.push_back(nullptr); }

cVertexDeclarationsManagerDX *cVertexDeclarationsManagerDX::Instance()
{
    static cVertexDeclarationsManagerDX instance;
    return &instance;
}

bool cVertexDeclarationsManagerDX::QueryInterface(uint32_t riid, void **out)
{
    if (out && (riid == 1 || riid == kIidVertexDeclarationsManager || riid == kIidVertexDeclarationsManagerDX)) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

const char *const *cVertexDeclarationsManagerDX::GetSemanticRenames()
{
    // Pairs (declaration text, the name it stands for in shaders), then two nulls.
    static const char *const kRenames[] = {
        "dcl_blendweights", "dcl_blendweight", "dcl_blendweights0", "dcl_blendweight",
        "BLENDWEIGHTS", "BLENDWEIGHT", "BLENDWEIGHTS0", "BLENDWEIGHT",
        "dcl_targetindices", "dcl_blendindices4", "dcl_targetindices0", "dcl_blendindices4",
        "dcl_targetindices1", "dcl_blendindices5",
        "TARGETINDICES", "BLENDINDICES4", "TARGETINDICES0", "BLENDINDICES4", "TARGETINDICES1", "BLENDINDICES5",
        "dcl_positiondelta", "dcl_position4", "dcl_positiondelta0", "dcl_position4",
        "dcl_positiondelta1", "dcl_position5", "dcl_positiondelta2", "dcl_position6",
        "dcl_positiondelta3", "dcl_position7",
        "POSITIONDELTA", "POSITION4", "POSITIONDELTA0", "POSITION4", "POSITIONDELTA1", "POSITION5",
        "POSITIONDELTA2", "POSITION6", "POSITIONDELTA3", "POSITION7",
        "dcl_normaldelta", "dcl_normal4", "dcl_normaldelta0", "dcl_normal4", "dcl_normaldelta1", "dcl_normal5",
        "dcl_normaldelta2", "dcl_normal6", "dcl_normaldelta3", "dcl_normal7",
        "NORMALDELTA", "NORMAL4", "NORMALDELTA0", "NORMAL4", "NORMALDELTA1", "NORMAL5",
        "NORMALDELTA2", "NORMAL6", "NORMALDELTA3", "NORMAL7",
        "dcl_colordelta", "dcl_color4", "dcl_colordelta0", "dcl_color4", "dcl_colordelta1", "dcl_color5",
        "dcl_colordelta2", "dcl_color6", "dcl_colordelta3", "dcl_color7",
        "COLORDELTA", "COLOR4", "COLORDELTA0", "COLOR4", "COLORDELTA1", "COLOR5",
        "COLORDELTA2", "COLOR6", "COLORDELTA3", "COLOR7",
        "dcl_texcoorddelta", "dcl_texcoord6", "dcl_texcoorddelta0", "dcl_texcoord6",
        "dcl_texcoorddelta1", "dcl_texcoord7", "dcl_texcoorddelta2", "dcl_texcoord8",
        "dcl_texcoorddelta3", "dcl_texcoord9",
        "TEXCOORDDELTA", "TEXCOORD6", "TEXCOORDDELTA0", "TEXCOORD6", "TEXCOORDDELTA1", "TEXCOORD7",
        "TEXCOORDDELTA2", "TEXCOORD8", "TEXCOORDDELTA3", "TEXCOORD9",
        "dcl_vertexid", "dcl_blendindices6", "VERTEXID", "BLENDINDICES6",
        "dcl_seamvertexid", "dcl_blendindices7", "SEAMVERTEXID", "BLENDINDICES7",
        nullptr, nullptr,
    };
    return kRenames;
}

namespace {
// The Direct3D element for one of the format's: the type by component type (types past 5 unused), the usage and
// usage index by usage id (the engine's extra positions, normals, colours, indices and texture coordinates go to the
// spare indices 4-7).
void Convert(const cVertexElement &e, D3DVERTEXELEMENT9 &d)
{
    static constexpr BYTE kTypes[6] = {D3DDECLTYPE_FLOAT1, D3DDECLTYPE_FLOAT2, D3DDECLTYPE_FLOAT3,
                                       D3DDECLTYPE_FLOAT4, D3DDECLTYPE_UBYTE4, D3DDECLTYPE_D3DCOLOR};
    d.Type = e.type < 6 ? kTypes[e.type] : static_cast<BYTE>(D3DDECLTYPE_UNUSED);
    BYTE usage = D3DDECLUSAGE_POSITION, index = e.index;
    switch (e.usage) {
    case 0x5cf2cfe1: index += 4; break;                                 // POSITION 4+
    case 0x3bd70105: usage = D3DDECLUSAGE_BLENDWEIGHT; break;
    case 0xfbd70111: usage = D3DDECLUSAGE_BLENDINDICES; break;
    case 0xdcf2cfdc: usage = D3DDECLUSAGE_BLENDINDICES; index += 4; break;
    case 0x114113c3: usage = D3DDECLUSAGE_BLENDINDICES; index += 6; break;
    case 0x114113cd: usage = D3DDECLUSAGE_BLENDINDICES; index += 7; break;
    case 0x3b83078b: usage = D3DDECLUSAGE_NORMAL; break;
    case 0xcb6f3a6a: usage = D3DDECLUSAGE_NORMAL; index += 4; break;
    case 0xbb8307ab: usage = D3DDECLUSAGE_TEXCOORD; break;
    case 0xcb7206a1: usage = D3DDECLUSAGE_TEXCOORD; index += 6; break;
    case 0x89d92ba0: usage = D3DDECLUSAGE_TANGENT; break;
    case 0x69d92b93: usage = D3DDECLUSAGE_BINORMAL; break;
    case 0xdb830795: usage = D3DDECLUSAGE_COLOR; break;
    case 0xeb720693: usage = D3DDECLUSAGE_COLOR; index += 4; break;
    default: break; // POSITION (0x5b830781) and unknown usages
    }
    d.Usage = usage;
    d.UsageIndex = index;
}
} // namespace

bool cVertexDeclarationsManagerDX::GetDeclaration(cIDeviceDX *device, cIVertexFormat *format, cIVertexDeclarationDX **out)
{
    if (!device->IsDeviceReady()) return false;
    cIDeviceDX *deviceDX = nullptr;
    device->QueryInterface(cDeviceDX::kIidDeviceDX, reinterpret_cast<void **>(&deviceDX));
    bool done = false;
    const uint32_t count = static_cast<uint32_t>(mDeclarations.size());
    uint32_t slot = count; // the first empty entry past 0
    for (uint32_t i = 1; i < count; ++i) {
        auto *d = static_cast<cVertexDeclarationDX *>(mDeclarations[i]);
        if (!d) {
            slot = std::min(slot, i);
        } else if (d->mDevice == deviceDX && d->mFormat == format) {
            *out = d;
            d->AddRef();
            if (deviceDX) deviceDX->Release();
            return true;
        }
    }
    if (slot == count) mDeclarations.push_back(nullptr);

    for (int32_t s = 0; s < static_cast<int32_t>(format->GetStreamCount()); ++s) {
        for (int32_t i = 0; i < static_cast<int32_t>(format->GetElementCount(s)); ++i) {
            cVertexElement e;
            format->GetElement(&e, s, i);
            D3DVERTEXELEMENT9 d{};
            d.Stream = static_cast<WORD>(s);
            d.Offset = static_cast<WORD>(format->GetElementOffset(s, i));
            d.Method = D3DDECLMETHOD_DEFAULT;
            Convert(e, d);
            mElements.push_back(d);
        }
    }
    mElements.push_back(D3DVERTEXELEMENT9 D3DDECL_END());

    IDirect3DVertexDeclaration9 *made = nullptr;
    IDirect3DDevice9 *d3d = deviceDX->GetD3DDevice();
    const HRESULT hr = d3d->CreateVertexDeclaration(mElements.data(), &made);
    mElements.clear();
    const uint32_t code = static_cast<uint32_t>(hr);
    if (code != 0x8876017c && code != 0x8007000e && code != 0x88760827) {
        // ORIGINAL-BUG: any other failure still makes the declaration, without a Direct3D object if none came back;
        // such a declaration is never unregistered (its release unregisters only created ones), so its entry in the
        // list outlives it and a later search reads the freed block.
        Check(hr, 0x1b5);
        auto *d = new cVertexDeclarationDX;
        d->AddRef();
        d->mId = slot;
        d->mDevice = deviceDX;
        if (made) made->AddRef();
        if (d->mDeclaration) d->mDeclaration->Release();
        d->mDeclaration = made;
        if (d->mFormat != format) {
            format->AddRef();
            cIVertexFormat *old = d->mFormat;
            d->mFormat = format;
            if (old) old->Release();
        }
        std::fill(std::begin(d->mStreamSizes), std::end(d->mStreamSizes), 0u);
        // ORIGINAL-BUG: no bound on the stream count: a format with more than 16 streams writes past the sizes.
        for (int32_t s = 0; s < static_cast<int32_t>(format->GetStreamCount()); ++s)
            d->mStreamSizes[s] = format->GetStreamSize(s);
        Manager(device)->Insert(d);
        if (made) {
            made->Release();
            made = nullptr;
        }
        mDeclarations[slot] = d;
        *out = d;
        d->AddRef();
        d->Release();
        done = true;
    }
    if (made) made->Release();
    if (deviceDX) deviceDX->Release();
    return done;
}

} // namespace nGZGraphic4
