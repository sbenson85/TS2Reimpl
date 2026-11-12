// nGZSceneGraph::cTextureTextUpdateStrategy (lane 261111m): src/sg/cTextureTextUpdateStrategy.h.
#include "sg/cTextureTextUpdateStrategy.h"

#include "gz/PixelFormat.h" // kIidARGB
#include "gz/cIGZFont.h"
#include "gz/cIGZFontSys.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cSurfaceLockAsGZBuffer.h"
#include "rz/RZAutoRef.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject

#include <cstring>
#include <new>
#include <utility>

#undef DrawText // windows.h's (through the surface headers), not cIGZFont's slot 52

namespace nGZSceneGraph {

namespace {

using nGZGraphic4::cILockableSurface;
using nGZGraphic4::cIPixelFormat;
using nGZGraphic4::cIPixelFormatARGBFace;

constexpr const char *kFontName = "DebugFont";
constexpr uint32_t kFontQuality = 1;
constexpr int32_t kLeft = 4;
constexpr uint32_t kFontColour2 = 0x8000000; // the font's own second colour

// The text as a cRZString on the stack (vtable, the text, count 0), destroyed at the end of the scope.
struct LocalName {
    union {
        cRZString mText;
    };
    explicit LocalName(const char *text)
    {
        ::new (&mText.mString) std::string(text);
        mText.mVTable = cRZString::VTable();
        mText.mRefCount = 0;
    }
    LocalName(const LocalName &) = delete;
    LocalName &operator=(const LocalName &) = delete;
    ~LocalName() { mText.Destruct(); }
};

} // namespace

cTextureTextUpdateStrategy::Line::Line()
{
    ::new (&mText.mString) std::string();
    mText.mVTable = cRZString::VTable();
    mText.mRefCount = 0;
}

cTextureTextUpdateStrategy::Line::Line(const Line &other) : mColour(other.mColour)
{
    ::new (&mText.mString) std::string(other.mText.mString);
    mText.mVTable = cRZString::VTable();
    mText.mRefCount = 0;
}

cTextureTextUpdateStrategy::Line::~Line() { mText.Destruct(); }

cTextureTextUpdateStrategy::cTextureTextUpdateStrategy(uint32_t height, uint32_t fontSize)
{
    RZAutoRef fonts;
    fonts.mObject = nullptr;
    if (void *const framework = GetServiceObject())
        static_cast<cIGZFrameWork *>(framework)->GetSystemService(cIGZFontSys::kServiceId, cIGZFontSys::kIid,
                                                                   &fonts.mObject);
    // ORIGINAL-BUG: neither the font system nor the font is checked before it is used, and a font with a line spacing
    // of 0 divides by zero below.
    {
        LocalName name(kFontName);
        mFont = static_cast<cIGZFontSys *>(fonts.mObject)->FindFont(name.mText.AsInterface(), 0, fontSize, 0, true);
    }
    fonts.ReleaseObject();
    mFont->SetQuality(kFontQuality);
    uint32_t lines = height / uint32_t(mFont->GetLineSpacing());
    if (lines == 0) lines = 1;
    mLines.resize(lines);
}

cTextureTextUpdateStrategy::~cTextureTextUpdateStrategy()
{
    std::vector<Line>().swap(mLines); // the lines go before the font, as the original's
    if (mFont) mFont->Release();
}

bool cTextureTextUpdateStrategy::QueryInterface(uint32_t, void **out)
{
    if (!out) return false;
    *out = this;
    AddRef();
    return true;
}

void cTextureTextUpdateStrategy::Update(cILockableSurface *surface, uint32_t, bool)
{
    nRZGraphic4::cSurfaceLockAsGZBuffer buffer(surface);
    const nGZGraphic4::cSurfaceLock &lock = buffer.mLock;

    // ORIGINAL-BUG: the ARGB face is released as soon as it is found and used afterwards, unchecked.
    cIPixelFormatARGBFace *argb = nullptr;
    static_cast<cIPixelFormat *>(surface->GetPixelFormat())->QueryInterface(nGZGraphic4::kIidARGB,
                                                                            reinterpret_cast<void **>(&argb));
    if (argb) argb->Release();

    for (uint32_t row = 0; row < lock.rows; ++row)
        std::memset(static_cast<uint8_t *>(lock.bits) + uint32_t(lock.pitch) * row, 0, lock.rowBytes);

    const int32_t spacing = mFont->GetLineSpacing();
    const uint32_t size = uint32_t(mLines.size());
    const uint32_t start = size <= mCount ? mCount - size : 0;
    int32_t y = 0;
    for (uint32_t i = 0; i < mLines.size(); ++i) {
        Line &line = mLines[(start + i) % mLines.size()];
        const uint32_t colour = argb->PackColor(reinterpret_cast<const uint8_t *>(&line.mColour));
        cIGZString *const text = line.mText.AsInterface();
        const uint32_t length = text->Strlen(); // the length is asked before the text
        mFont->DrawText(&buffer, kLeft, y, text->ToChar(), length, colour, kFontColour2, nullptr, nullptr);
        y += spacing;
    }
}

void cTextureTextUpdateStrategy::Push(uint32_t colour, const char *text)
{
    Line &line = mLines[mCount % mLines.size()];
    line.mColour = colour | kOpaque;
    line.mText.mString.assign(text);
    ++mCount;
}

void cTextureTextUpdateStrategy::Clear()
{
    const size_t size = mLines.size();
    mLines.clear();
    mLines.resize(size);
    mCount = 0;
}

} // namespace nGZSceneGraph
