#include "gz/cRZCursor.h"

#include "game/game.h"
#include "game/import.h"
#include "game/vtable.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZGraphicSystem.h"
#include "rz/RZTextConvert.h"

#include <cstring>
#include <new>
#include <vector>

namespace {

// ---- Win32, through the game's import slots ----
struct IconInfo { // ICONINFO
    int32_t isIcon;
    uint32_t hotX;
    uint32_t hotY;
    void *mask;
    void *colour;
};
struct Bitmap { // BITMAP
    int32_t type;
    int32_t width;
    int32_t height;
    int32_t widthBytes;
    uint16_t planes;
    uint16_t bitsPerPixel;
    void *bits;
};
struct BitmapInfoHeader { // BITMAPINFOHEADER
    uint32_t size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bitCount;
    uint32_t compression;
    uint32_t sizeImage;
    int32_t xPelsPerMeter;
    int32_t yPelsPerMeter;
    uint32_t coloursUsed;
    uint32_t coloursImportant;
};
struct CursorInfo { // CURSORINFO
    uint32_t size;
    uint32_t flags;
    void *cursor;
    int32_t x;
    int32_t y;
};
static_assert(sizeof(IconInfo) == 0x14, "ICONINFO");
static_assert(sizeof(Bitmap) == 0x18, "BITMAP");
static_assert(sizeof(BitmapInfoHeader) == 0x28, "BITMAPINFOHEADER");
static_assert(sizeof(CursorInfo) == 0x14, "CURSORINFO");

using LoadCursorFromFileFn = void *(__stdcall *)(const char *);
using LoadImageFn = void *(__stdcall *)(void *, const char *, uint32_t, int32_t, int32_t, uint32_t);
using GetIconInfoFn = int32_t(__stdcall *)(void *, IconInfo *);
using GetDCFn = void *(__stdcall *)(void *);
using GetObjectFn = int32_t(__stdcall *)(void *, int32_t, void *);
using GetDIBitsFn = int32_t(__stdcall *)(void *, void *, uint32_t, uint32_t, void *, BitmapInfoHeader *, uint32_t);
using ReleaseDCFn = int32_t(__stdcall *)(void *, void *);
using DeleteObjectFn = int32_t(__stdcall *)(void *);
using DestroyCursorFn = int32_t(__stdcall *)(void *);
using LoadCursorFn = void *(__stdcall *)(void *, const char *);
using SetCursorFn = void *(__stdcall *)(void *);
using ShowCursorFn = int32_t(__stdcall *)(int32_t);
using GetCursorInfoFn = int32_t(__stdcall *)(CursorInfo *);

void *LoadCursorFromFile(const char *path) { return ImportFunction<LoadCursorFromFileFn, 0xd9a4cc>()(path); }
void *LoadImageFromFile(const char *path, uint32_t type, uint32_t flags)
{
    return ImportFunction<LoadImageFn, 0xd9a4c4>()(nullptr, path, type, 0, 0, flags);
}
bool GetIconInfo(void *icon, IconInfo *info) { return ImportFunction<GetIconInfoFn, 0xd9a584>()(icon, info) != 0; }
void *GetScreenDC() { return ImportFunction<GetDCFn, 0xd9a518>()(nullptr); }
bool GetObject(void *object, int32_t size, void *out) { return ImportFunction<GetObjectFn, 0xd9a098>()(object, size, out) != 0; }
bool GetDIBits(void *dc, void *bitmap, uint32_t lines, void *bits, BitmapInfoHeader *header)
{
    return ImportFunction<GetDIBitsFn, 0xd9a094>()(dc, bitmap, 0, lines, bits, header, 0) != 0;
}
void ReleaseScreenDC(void *dc) { ImportFunction<ReleaseDCFn, 0xd9a514>()(nullptr, dc); }
void DeleteObject(void *object) { ImportFunction<DeleteObjectFn, 0xd9a0a8>()(object); }
void DestroyCursor(void *cursor) { ImportFunction<DestroyCursorFn, 0xd9a5ac>()(cursor); }
void *LoadSystemCursor(uint32_t id)
{
    return ImportFunction<LoadCursorFn, 0xd9a4c0>()(nullptr, reinterpret_cast<const char *>(uintptr_t(id)));
}
void SetSystemCursor(void *cursor) { ImportFunction<SetCursorFn, 0xd9a5b0>()(cursor); }
int32_t ShowSystemCursor(bool show) { return ImportFunction<ShowCursorFn, 0xd9a4b0>()(show ? 1 : 0); }
bool GetCursorInfo(CursorInfo *info) { return ImportFunction<GetCursorInfoFn, 0xd9a4d4>()(info) != 0; }

constexpr uint32_t kUnknownIid = 1;

// LoadImageA's IMAGE_CURSOR and LR_LOADFROMFILE | LR_DEFAULTSIZE.
constexpr uint32_t kImageCursor = 2;
constexpr uint32_t kLoadFromFileDefaultSize = 0x50;
// GetDIBits' header: 32-bit top-down rows of the bitmap, BI_RGB.
constexpr uint32_t kHeaderSize = 0x28;
constexpr uint16_t kBitCount = 32;
// CURSORINFO's CURSOR_SHOWING.
constexpr uint32_t kCursorShowing = 1;

// The SDK's system cursor ids (IDC_*).
constexpr uint32_t kIdcArrow = 32512;
constexpr uint32_t kIdcIBeam = 32513;
constexpr uint32_t kIdcWait = 32514;
constexpr uint32_t kIdcCross = 32515;
constexpr uint32_t kIdcUpArrow = 32516;
constexpr uint32_t kIdcSizeNwse = 32642;
constexpr uint32_t kIdcSizeNesw = 32643;
constexpr uint32_t kIdcSizeWe = 32644;
constexpr uint32_t kIdcSizeNs = 32645;
constexpr uint32_t kIdcSizeAll = 32646;
constexpr uint32_t kIdcNo = 32648;
constexpr uint32_t kIdcHand = 32649;
constexpr uint32_t kIdcHelp = 32651;

// The cursor buffer: colour format 9 (32-bit with alpha), locked for writing.
constexpr uint32_t kCursorFormat = 9;
constexpr uint32_t kLockFlags = 0x8040;
// Every colour pixel is opaque; a mask pixel with any colour bit makes it transparent (zero).
constexpr uint32_t kOpaque = 0xff000000;
constexpr uint32_t kColourBits = 0x00ffffff;

// The IDC_* of a system cursor kind, 0 for kinds without one.
uint32_t SystemCursorId(uint32_t kind)
{
    switch (kind) {
    case 1: return kIdcArrow;
    case 2: return kIdcCross;
    case 3: return kIdcIBeam;
    case 5: return kIdcNo;
    case 6: return kIdcSizeAll;
    case 7: return kIdcSizeNesw;
    case 8: return kIdcSizeNs;
    case 9: return kIdcSizeNwse;
    case 10: return kIdcSizeWe;
    case 0xb: return kIdcUpArrow;
    case 0xf: return kIdcWait;
    case 0x10: return kIdcHelp;
    case 0x11: return kIdcHand;
    default: return 0;
    }
}

// A cRZString on the stack, built and destroyed by the hook-shaped members.
struct LocalString {
    union {
        cRZString s;
    };
    LocalString() { s.Construct(); }
    ~LocalString() { s.Destruct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
};

} // namespace

// ---- the graphics system helpers ----

ScopedRef *AcquireGraphicSystem(ScopedRef *ref)
{
    ref->mObject = nullptr;
    if (void *const framework = GetServiceObject())
        As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kGraphicSystemServiceId, kGraphicSystemIid, &ref->mObject);
    return ref;
}

ifaces::cIGZGraphicSystem *GetGraphicSystem()
{
    auto *const cell = &ts2_rva_10f51a4;
    if (!*cell) {
        ScopedRef ref;
        AcquireGraphicSystem(&ref);
        *cell = static_cast<ifaces::cIGZGraphicSystem *>(ref.mObject);
        ref.ReleaseObject();
    }
    return *cell;
}

uint32_t __stdcall ColorFormatBits(uint32_t format) { return cGZColorType(format).bits; }

// ---- cRZCursor ----

cRZCursor::cRZCursor()
    : mRefCount(0), mUseSystemCursor(false), mGraphics(nullptr), mBuffer(nullptr), mHotX(0), mHotY(0), mHandle(nullptr)
{
    mPath.Construct();
    if (GetServiceObject()) {
        mGraphics = GetGraphicSystem();
        if (mGraphics) mUseSystemCursor = mGraphics->UseSystemCursor();
    }
}

cRZCursor::cRZCursor(const cIGZString &path) : cRZCursor() { cRZCursor::LoadCursor(path); }

cRZCursor::~cRZCursor()
{
    if (mHandle) ReplaceHandle(nullptr);
    if (mBuffer) {
        mBuffer->Release();
        mBuffer = nullptr;
    }
    mPath.Destruct();
}

bool cRZCursor::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kUnknownIid && riid != kIid) return false;
    *out = static_cast<cIGZCursor *>(this);
    AddRef();
    return true;
}

uint32_t cRZCursor::AddRef() { return ++mRefCount; }

uint32_t cRZCursor::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

bool cRZCursor::Slot3() { return true; }
bool cRZCursor::Slot4() { return true; }

void cRZCursor::SetCursor(bool set)
{
    if (!set) return;
    if (!mHandle) {
        if (mGraphics) mGraphics->SetCursor(mBuffer, mHotX, mHotY);
        return;
    }
    // A handle, but the graphics system draws the cursor itself: the image without its hot spot.
    if (!mUseSystemCursor && mGraphics && !mGraphics->UseSystemCursor()) {
        mGraphics->SetCursor(mBuffer, 0, 0);
        return;
    }
    SetSystemCursor(mHandle);
}

void cRZCursor::ShowCursor(bool show) { ShowSystemCursor(show); }

void cRZCursor::GetCursorHotSpot(int32_t *x, int32_t *y)
{
    *x = mHotX;
    *y = mHotY;
}

void cRZCursor::SetCursorHotSpot(int32_t x, int32_t y)
{
    mHotX = static_cast<int16_t>(x);
    mHotY = static_cast<int16_t>(y);
}

bool cRZCursor::LoadCursor(const cIGZString &path)
{
    *mPath.AsInterface() = path; // through the string's vtable, as the original
    SetCursorHandle(nullptr);
    LocalString text;
    RZText::ConvertStringToString(*mPath.AsInterface(), *text.s.AsInterface(), RZText::kSystemCodePage);
    if (mUseSystemCursor) {
        mHandle = LoadCursorFromFile(text.s.ToChar());
        return mHandle != nullptr;
    }
    LoadImageIntoBuffer(text.s.ToChar());
    return mBuffer != nullptr;
}

void cRZCursor::LoadImageIntoBuffer(const char *path)
{
    void *const icon = LoadImageFromFile(path, kImageCursor, kLoadFromFileDefaultSize);
    if (!icon) return;
    IconInfo info;
    if (GetIconInfo(icon, &info)) {
        mHotX = static_cast<int16_t>(info.hotX);
        mHotY = static_cast<int16_t>(info.hotY);
        if (void *const dc = GetScreenDC()) {
            Bitmap mask{};
            if (GetObject(info.mask, sizeof(mask), &mask)) {
                BitmapInfoHeader header{};
                header.size = kHeaderSize;
                header.width = mask.width;
                header.height = mask.height;
                header.planes = 1;
                header.bitCount = kBitCount;
                header.sizeImage = static_cast<uint32_t>(mask.height * mask.width * 4);
                // ORIGINAL-BUG: a buffer of an earlier load is overwritten without its Release (it leaks). The graphics
                // system is assumed.
                if (GetGraphicSystem()->CreateBuffer(&mBuffer)) {
                    const int32_t width = header.width;
                    // Without a colour bitmap the mask holds both halves (AND over XOR): the image is half as high.
                    const int32_t height = info.colour ? header.height : header.height >> 1;
                    bool done = false;
                    if (mBuffer->Init(width, height, kCursorFormat, ColorFormatBits(kCursorFormat)) &&
                        mBuffer->Lock(kLockFlags)) {
                        auto *const surface = static_cast<uint8_t *>(mBuffer->GetColorSurfaceBits());
                        const uint32_t stride = mBuffer->GetColorSurfaceStride();
                        const uint32_t count = static_cast<uint32_t>(height * width);
                        std::vector<uint32_t> pixels(count * 2);
                        uint32_t *const colour = pixels.data();
                        uint32_t *const andMask = colour + count;                        if (GetDIBits(dc, info.mask, header.height, info.colour ? andMask : colour, &header) &&
                            (!info.colour || GetDIBits(dc, info.colour, header.height, colour, &header))) {
                            for (uint32_t i = 0; i < count; ++i) colour[i] |= kOpaque;
                            // The bitmaps' rows are bottom-up, the buffer's top-down.
                            for (int32_t row = 0; row < height; ++row)
                                std::memcpy(surface + (height - 1 - row) * stride, colour + row * width, width * 4);
                            for (int32_t row = 0; row < height; ++row) {
                                auto *const line = reinterpret_cast<uint32_t *>(surface + (height - 1 - row) * stride);
                                const uint32_t *const maskRow = andMask + row * width;
                                for (int32_t x = 0; x < header.width; ++x)
                                    if (maskRow[x] & kColourBits) line[x] = 0;
                            }
                            done = true;
                        }                        mBuffer->Unlock(kLockFlags);
                    }
                    if (!done) {
                        mBuffer->Shutdown();
                        mBuffer->Release();
                        mBuffer = nullptr;
                    }
                }
            }
            ReleaseScreenDC(dc);
        }
        if (info.mask) DeleteObject(info.mask);
        if (info.colour) DeleteObject(info.colour);
    }
    DestroyCursor(icon);
}

void cRZCursor::SetCursorHandle(void *handle) { ReplaceHandle(handle); }

void cRZCursor::ReplaceHandle(void *handle)
{
    if (mHandle == handle) return;
    if (mHandle) DestroyCursor(mHandle);
    mHandle = handle;
}

void cRZCursor::SetCursorBuffer(cIGZBuffer *buffer)
{
    if (mBuffer == buffer) return;
    if (buffer) buffer->AddRef();
    if (mBuffer) mBuffer->Release();
    mBuffer = buffer;
}

bool cRZCursor::UsesSystemCursor() { return mUseSystemCursor; }

cIGZBuffer *cRZCursor::GetCursorBuffer() { return mBuffer; }

// ---- cRZSystemCursorW95 ----

cRZSystemCursorW95::cRZSystemCursorW95(uint32_t kind)
{
    if (const uint32_t id = SystemCursorId(kind)) mHandle = LoadSystemCursor(id);
}

// The system's shared cursor is not destroyed: the handle is forgotten before the cursor's destructor runs.
cRZSystemCursorW95::~cRZSystemCursorW95() { mHandle = nullptr; }

bool cRZSystemCursorW95::LoadCursor(const cIGZString &) { return false; }

void cRZSystemCursorW95::SetCursorHandle(void *handle) { mHandle = handle; }

// ---- cRZCursorManager ----

cRZCursorManager::cRZCursorManager() : mInitialized(false), mRefCount(0), mCurrent(nullptr) {}

// ORIGINAL-BUG: the listed cursors, the auto stack's and the current one are not released (only Shutdown does).
cRZCursorManager::~cRZCursorManager()
{
    // the members do the work
}

bool cRZCursorManager::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kUnknownIid && riid != kIid) return false;
    *out = static_cast<cIGZCursorManager *>(this);
    AddRef();
    return true;
}

uint32_t cRZCursorManager::AddRef() { return ++mRefCount; }

uint32_t cRZCursorManager::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

bool cRZCursorManager::Init()
{
    if (mInitialized) return true;
    cRZSystemCursorW95 *const arrow = new cRZSystemCursorW95(kArrow);
    AddCursor(arrow, kArrow);
    SetCurrentCursor(arrow);
    AddCursor(new cRZSystemCursorW95(kIBeam), kIBeam);
    AddCursor(new cRZSystemCursorW95(kWait), kWait);
    AddCursor(new cRZSystemCursorW95(kHelp), kHelp);
    AddCursor(new cRZSystemCursorW95(kHand), kHand);
    mInitialized = true;
    return true;
}

bool cRZCursorManager::Shutdown()
{
    if (!mInitialized) return true;
    AssignCurrent(nullptr);
    while (!mAutoStack.empty()) {
        mAutoStack.back()->Release();
        mAutoStack.pop_back();
    }
    ReleaseAll();
    mInitialized = false;
    return true;
}

void cRZCursorManager::ReleaseAll()
{
    for (auto &entry : mCursors) entry.second->Release();
    mCursors.clear();
}

bool cRZCursorManager::AddCursorFromFile(const cIGZString &path, uint32_t id)
{
    return AddCursor(new cRZCursor(path), id);
}

bool cRZCursorManager::AddCursor(cIGZCursor *cursor, uint32_t id)
{
    if (cursor) cursor->AddRef();
    const auto found = mCursors.find(id);
    if (found != mCursors.end()) {
        if (found->second) found->second->Release();
        mCursors.erase(found);
    }
    if (cursor) mCursors.emplace(id, cursor);
    return true;
}

bool cRZCursorManager::RemoveCursor(uint32_t id)
{
    const auto found = mCursors.find(id);
    if (found == mCursors.end()) return false;
    found->second->Release();
    mCursors.erase(found);
    return true;
}

cIGZCursor *cRZCursorManager::GetCurrentCursor() { return mCurrent; }

cIGZCursor *cRZCursorManager::GetCursor(uint32_t id)
{
    const auto found = mCursors.find(id);
    return found != mCursors.end() ? found->second : nullptr;
}

// The cursor is assumed (a null one crashes the original).
bool cRZCursorManager::SetCurrentCursor(cIGZCursor *cursor)
{
    AssignCurrent(cursor);
    cursor->SetCursor(true);
    return true;
}

bool cRZCursorManager::SetCurrentCursorById(uint32_t id)
{
    cIGZCursor *const cursor = GetCursor(id);
    if (!cursor) return false;
    cursor->SetCursor(true);
    AssignCurrent(cursor);
    return true;
}

// The current cursor is assumed.
bool cRZCursorManager::PushAutoCursor(uint32_t id)
{
    cIGZCursor *const previous = mCurrent;
    previous->AddRef();
    if (SetCurrentCursorById(id)) {
        mAutoStack.push_back(previous);
        return true;
    }
    previous->Release();
    return false;
}

bool cRZCursorManager::PopAutoCursor()
{
    if (mAutoStack.empty()) return false;
    cIGZCursor *const cursor = mAutoStack.back();
    mAutoStack.pop_back();
    SetCurrentCursor(cursor);
    cursor->Release();
    return true;
}

// ShowCursor's display count to 0: the direction is fixed by the first answer.
void cRZCursorManager::ShowCursor()
{
    const int32_t count = ShowSystemCursor(true);
    if (count == 0) return;
    const bool up = count < 1;
    while (ShowSystemCursor(up) != 0) {
    }
}

// The display count to -1 likewise.
void cRZCursorManager::HideCursor()
{
    const int32_t count = ShowSystemCursor(false);
    if (count == -1) return;
    const bool up = count < -1;
    while (ShowSystemCursor(up) != -1) {
    }
}

bool cRZCursorManager::IsCursorVisible()
{
    CursorInfo info;
    info.size = sizeof(info);
    GetCursorInfo(&info);
    return info.flags == kCursorShowing;
}

void cRZCursorManager::AssignCurrent(cIGZCursor *cursor)
{
    if (cursor == mCurrent) return;
    if (cursor) cursor->AddRef();
    if (mCurrent) mCurrent->Release();
    mCurrent = cursor;
}

// ---- adapters ----

cRZCursor *__cdecl cRZCursorAdapters::Create() { return new cRZCursor; }

cRZCursor *__fastcall cRZCursorAdapters::Construct(void *memory, void *, const cIGZString *path)
{
    return ::new (memory) cRZCursor(*path);
}

cRZCursorManager *__fastcall cRZCursorAdapters::ConstructManager(void *memory, void *)
{
    return ::new (memory) cRZCursorManager;
}

ScopedRef *__fastcall cRZCursorAdapters::AcquireGraphicSystem(ScopedRef *ref, void *) { return ::AcquireGraphicSystem(ref); }
