// nGZSceneGraph::DrawMissingTextureName (lane VV): src/sg/DrawMissingTextureName.h.
#include "sg/DrawMissingTextureName.h"

#include "gz/PixelFormat.h" // kIidARGB
#include "gz/cIGZFont.h"
#include "gz/cIGZFontSys.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cSurfaceLockAsGZBuffer.h"
#include "rz/RZAutoRef.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/cRZString.h"

#include <cstring>
#include <new>
#include <string>
#include <utility>

#undef DrawText // windows.h's (through the surface headers), not cIGZFont's slot 52

namespace nGZSceneGraph {

namespace {

using nGZGraphic4::cILockableSurface;
using nGZGraphic4::cIPixelFormat;
using nGZGraphic4::cIPixelFormatARGBFace;

constexpr uint32_t kRed = 0xffff0000;   // as the ARGB face reads it: the bytes blue 0, green 0, red 0xff, alpha 0xff
constexpr uint32_t kFontColour2 = 0x8000000; // the font's own second colour
constexpr const char *kFontName = "Lucida Console";
constexpr uint32_t kFontSize = 14;

// A cRZString on the stack as the image builds the font's name (the string's vtable, the text, count 0).
struct LocalRZString {
    union {
        cRZString mText;
    };
    explicit LocalRZString(const char *text)
    {
        ::new (&mText.mString) std::string(text);
        mText.mVTable = cRZString::VTable();
        mText.mRefCount = 0;
    }
    LocalRZString(const LocalRZString &) = delete;
    LocalRZString &operator=(const LocalRZString &) = delete;
    ~LocalRZString() { mText.Destruct(); }
};

// The font system's "Lucida Console" at size 14 (shared), or null. The service is asked for and let go around the
// lookup; the image calls the lookup on whatever it got.
cIGZFont *FindNameFont()
{
    RZAutoRef fonts;
    fonts.mObject = nullptr;
    if (void *const framework = GetServiceObject())
        static_cast<cIGZFrameWork *>(framework)->GetSystemService(cIGZFontSys::kServiceId, cIGZFontSys::kIid,
                                                                   &fonts.mObject);
    cIGZFont *font;
    {
        LocalRZString family(kFontName);
        font = static_cast<cIGZFontSys *>(fonts.mObject)->FindFont(family.mText.AsInterface(), 0, kFontSize, 0, true);
    }
    fonts.ReleaseObject();
    return font;
}

// Pixel `size` bytes wide at byte offset `at` from the bits.
template <class Pixel> void Put(uint8_t *bits, uint32_t at, uint32_t value)
{
    *reinterpret_cast<Pixel *>(bits + at) = Pixel(value);
}

// Both diagonals of the largest square from the top corners: `count` pixels down each.
template <class Pixel> void Cross(uint8_t *bits, uint32_t pitch, uint32_t width, uint32_t count, uint32_t value)
{
    constexpr uint32_t size = sizeof(Pixel);
    for (uint32_t i = 0; i < count; ++i) {
        Put<Pixel>(bits, (pitch + size) * i, value);
        Put<Pixel>(bits, (pitch - size) * i + width * size - size, value);
    }
}

} // namespace

void __cdecl DrawMissingTextureName(cILockableSurface *surface, cIGZString *name)
{
    nRZGraphic4::cSurfaceLockAsGZBuffer buffer(surface);
    const nGZGraphic4::cSurfaceLock &lock = buffer.mLock;

    for (uint32_t row = 0; row < lock.rows; ++row)
        std::memset(static_cast<uint8_t *>(lock.bits) + uint32_t(lock.pitch) * row, 0xff, lock.rowBytes);

    // ORIGINAL-BUG: the ARGB face is released as soon as it is found and used afterwards (and used unchecked, so a format
    // without one would crash here); harmless for the pixel formats, whose counts are fixed.
    cIPixelFormatARGBFace *argb = nullptr;
    static_cast<cIPixelFormat *>(surface->GetPixelFormat())->QueryInterface(nGZGraphic4::kIidARGB,
                                                                            reinterpret_cast<void **>(&argb));
    if (argb) argb->Release();
    const uint32_t red = kRed;
    const uint32_t mark = argb->PackColor(reinterpret_cast<const uint8_t *>(&red));

    const uint32_t width = uint32_t(lock.rect.right - lock.rect.left);
    const uint32_t rows = lock.rows;
    const uint32_t count = rows < width ? rows : width;
    uint8_t *const bits = static_cast<uint8_t *>(lock.bits);
    if (argb->GetBitsPerPixel() == 16) Cross<uint16_t>(bits, uint32_t(lock.pitch), width, count, mark);
    else Cross<uint32_t>(bits, uint32_t(lock.pitch), width, count, mark);

    if (name->Strlen() == 0) return;
    cIGZFont *const font = FindNameFont();
    if (!font) return;

    // The name's length is asked before its text, each time.
    uint32_t length = name->Strlen();
    const uint32_t step = font->GetWidth(name->ToChar(), length);
    const uint32_t lineStep = uint32_t(font->GetLineSpacing());
    uint32_t colour = buffer.cSurfaceLockAsGZBuffer::ConvertRGBValueToNative(0, 0, 0);      // black
    uint32_t other = buffer.cSurfaceLockAsGZBuffer::ConvertRGBValueToNative(0, 0, 0xff);    // blue
    uint32_t offset = 0;
    for (uint32_t y = 0; y < rows; y += lineStep) {
        length = name->Strlen();
        font->DrawText(&buffer, 0, int32_t(y), name->ToChar() + offset, length - offset, colour, kFontColour2, nullptr,
                       nullptr);
        length = name->Strlen();
        for (uint32_t x = font->GetWidth(name->ToChar() + offset, length - offset); x < width; x += step) {
            std::swap(colour, other);
            length = name->Strlen();
            font->DrawText(&buffer, int32_t(x), int32_t(y), name->ToChar(), length, colour, kFontColour2, nullptr,
                           nullptr);
        }
        offset = (offset + 2) % name->Strlen();
    }
    font->Release();
}

} // namespace nGZSceneGraph
