// nGZSceneGraph::cLevelInfo as a whole class (lane SX). Layout and the switch: src/sg/cLevelInfo.h.
#include "sg/cLevelInfo.h"

#include "game/import_slots.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName

#include <cstring>

// The constructors' hook targets (cLevelInfo_Constructor / SizedConstructor, cLevelInfo.h).
#pragma comment(linker, "/alternatename:_cLevelInfo_Constructor=??0cLevelInfo@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_cLevelInfo_SizedConstructor=??0cLevelInfo@nGZSceneGraph@@QAE@HHH@Z")

namespace nGZSceneGraph {

static_assert(sizeof(cLevelInfo) == cLevelInfo::kSize, "cLevelInfo size (0x54)");
static_assert(offsetof(cLevelInfo, mWidth) == 0x2c, "width");
static_assert(offsetof(cLevelInfo, mHeight) == 0x30, "height");
static_assert(offsetof(cLevelInfo, mPitch) == 0x34, "pitch");
static_assert(offsetof(cLevelInfo, mByteCount) == 0x38, "byte count");
static_assert(offsetof(cLevelInfo, mFlag) == 0x3c, "flag");
static_assert(offsetof(cLevelInfo, mPixels) == 0x40, "pixels");

namespace {
// The imports, called through their import slots (VirtualAlloc 0xd9a1e4, VirtualFree 0xd9a1e8; game/import_slots.h).
using VirtualAllocFn = void *(__stdcall *)(void *, uint32_t, uint32_t, uint32_t);
using VirtualFreeFn = int(__stdcall *)(void *, uint32_t, uint32_t);
constexpr uint32_t kMemCommitReserveTopDown = 0x103000; // MEM_COMMIT | MEM_RESERVE | MEM_TOP_DOWN
constexpr uint32_t kPageReadWrite = 4;
constexpr uint32_t kMemRelease = 0x8000;
} // namespace

// ---- the pixel buffer ----

void LevelPixels::FreeBlock()
{
    if (mSize < mThreshold) delete[] mData;
    else ImportSlot<VirtualFreeFn, &ts2_rva_d9a1e8>()(mData, 0, kMemRelease);
}

void LevelPixels::Release()
{
    if (mSize != 0) {
        FreeBlock();
        mData = nullptr;
        mSize = 0;
    }
}

void LevelPixels::Resize(uint32_t n)
{
    if (mSize == n) return;
    FreeBlock();
    mData = nullptr;
    mSize = 0;
    if (n == 0) return;
    if (n >= mThreshold)
        mData = static_cast<uint8_t *>(ImportSlot<VirtualAllocFn, &ts2_rva_d9a1e4>()(nullptr, n, kMemCommitReserveTopDown, kPageReadWrite));
    else
        mData = new uint8_t[n];
    mSize = n;
}

// ---- construction ----

cLevelInfo::cLevelInfo() = default;

// The pitch and the byte count by format (signed: a width or height of 0 gives the DXT formats one block less).
cLevelInfo::cLevelInfo(int32_t width, int32_t height, int32_t format) : mWidth(width), mHeight(height)
{
    switch (format) {
    case kFormatRgba32:
        mPitch = mWidth * 4;
        mByteCount = static_cast<uint32_t>(mHeight * mWidth * 4);
        break;
    case kFormatRgb24:
    case kFormatRgb24B:
        mPitch = mWidth * 3;
        mByteCount = static_cast<uint32_t>(mHeight * mWidth * 3);
        break;
    case kFormatByte:
    case kFormatByteB:
        mPitch = mWidth;
        mByteCount = static_cast<uint32_t>(mHeight * mWidth);
        break;
    case kFormatDxt1:
        mPitch = ((mWidth - 1) >> 2) * 8 + 8;
        mByteCount = static_cast<uint32_t>((((mHeight - 1) >> 2) + 1) * mPitch);
        break;
    case kFormatDxt3:
    case kFormatDxt5:
        mPitch = (((mWidth - 1) >> 2) + 1) * 16;
        mByteCount = static_cast<uint32_t>((((mHeight - 1) >> 2) + 1) * mPitch);
        break;
    case kFormatWord:
        mPitch = mWidth * 2;
        mByteCount = static_cast<uint32_t>(mHeight * mWidth * 2);
        break;
    default:
        break;
    }
}

// The pixels (LevelPixels' destructor), then the cSGResource part.
cLevelInfo::~cLevelInfo() = default;

cISGResource *__cdecl cLevelInfo::Create() { return (new cLevelInfo)->Face(); }

// ---- cIGZUnknown ----

bool cLevelInfo::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == kIidSelf) {
        *out = static_cast<cSGResource *>(this);
        AddRef();
        return true;
    }
    return cSGResource::QueryInterface(iid, out);
}

uint32_t cLevelInfo::AddRef() { return cSGResource::AddRef(); }
uint32_t cLevelInfo::Release() { return cSGResource::Release(); }

// ---- persistence ----

// The record, the resource part, the four size words, then the pixels (the buffer sized to the byte count first).
void cLevelInfo::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cSGResource::Read(reader);
    mWidth = static_cast<int32_t>(reader->GetUint32());
    mHeight = static_cast<int32_t>(reader->GetUint32());
    mPitch = static_cast<int32_t>(reader->GetUint32());
    mByteCount = reader->GetUint32();
    mPixels.Resize(mByteCount);
    reader->GetVoid(mPixels.mData, mPixels.mSize);
    if (info.mName) info.mName->Release();
}

cPersistTypeInfo cLevelInfo::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cLevelInfo");
    info.mWord4 = kClassId;
    info.mVersion = kVersion;
    return info;
}

// The record (released at once), the resource part, the four size words, the pixels.
void cLevelInfo::Write(ifaces::cIGZPersistWriter *writer)
{
    {
        cPersistTypeInfo info = cLevelInfo::GetTypeInfo();
        writer->WriteTypeInfo(&info);
        if (info.mName) info.mName->Release();
    }
    cSGResource::WriteResource(writer);
    writer->SetUint32(static_cast<uint32_t>(mWidth));
    writer->SetUint32(static_cast<uint32_t>(mHeight));
    writer->SetUint32(static_cast<uint32_t>(mPitch));
    writer->SetUint32(mByteCount);
    writer->SetVoid(mPixels.mData, mPixels.mSize);
}

uint32_t cLevelInfo::GetOwnerInfo() { return cSGResource::GetOwnerInfo(); }

const void *cLevelInfo::GetRecord18() { return "_lifo"; }
const void *cLevelInfo::GetRecord19() { return "6li"; }

// ---- own slots ----

cISGResource *cLevelInfo::GetResourceConst() { return Face(); }
cISGResource *cLevelInfo::GetResource() { return Face(); }
void cLevelInfo::SetFlag15(uint8_t value) { mFlag = value; }
uint8_t cLevelInfo::GetFlag16() { return mFlag; }

// ORIGINAL-BUG: the pitch is taken from the other level's byte count, not its pitch (the game's copy does so).
cLevelInfo *cLevelInfo::Assign(const cLevelInfo &other)
{
    mWidth = other.mWidth;
    mHeight = other.mHeight;
    mPitch = static_cast<int32_t>(other.mByteCount);
    mByteCount = other.mByteCount;
    mPixels.Resize(other.mPixels.mSize);
    if (mPixels.mSize != 0) std::memcpy(mPixels.mData, other.mPixels.mData, mPixels.mSize);
    mFlag = other.mFlag;
    return this;
}

cLevelInfo *__fastcall cLevelInfoAdapters::Assign(cLevelInfo *self, void *, const cLevelInfo *other)
{
    return self->cLevelInfo::Assign(*other);
}

} // namespace nGZSceneGraph
