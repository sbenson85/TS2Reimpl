// cTSUIPaintingService in the standalone shape (lane KS). Built for size, as the game's (0x6c0f3d..0x6c22cb are packed).
//
// The objects the slots talk to are other classes' (the texture manager, the image and drawing objects the framework
// creates, the dialog service's UI object, the headline font); their slots are called by number where nothing names them
// yet. Image slots used (cIGZBuffer; the argument counts are cGZBuffer's bodies' `ret N`, lane KV): 3 Init (w, h, format),
// 6 / 7 Lock / Unlock (flags), 9 Width, 10 Height, 12 its rectangle, 15 its format (hidden result pointer), 18 Fill (value,
// 0), 21 GetPixel (x, y), 30 ConvertRGBValueToNative (r, g, b), 32 ConvertRGBAValueToNative (r, g, b, a), 33
// ConvertNativeValueToRGBA (value, &r, &g, &b, &a: one byte each). The texture's slot 20 sets a pixel.
// A slot call nested in another's arguments (GetPixel in ConvertNativeValueToRGBA's, a colour in Fill's or the font's
// slot 52, the lot's key in the UI object's slot 59) takes only its own arguments: the original pushes the outer call's
// trailing arguments before it makes the inner call.
#include "sg/cTextureManager.h"
#include "ui/cTSUIPaintingService.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "gz/cIGZMessage2Standard.h"
#include "gz/cIGZString.h"
#include "gz/cRZCursor.h" // ColorFormatBits
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersB.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/StringKeyParser.h"
#include "rz/cRZString.h"

#include <bit>
#include <new>
#include <string>

namespace {

using UnknownRef = cRZAutoRefCount<cIGZUnknown>;

constexpr uint32_t kImageIid = 0x20732180;      // the image interface (cIGZImage face)
constexpr uint32_t kImageClass = 0xc470d325;    // a blank image
constexpr uint32_t kDrawerClass = 0xae6320e;    // the 2D drawing object
constexpr uint32_t kDrawerIid = 0x2ae63219;
constexpr uint32_t kTextureClass = 0x1c4a276c;  // a blank texture
constexpr uint32_t kTextureIid = 0xfc49f6ee;
constexpr uint32_t kTextureHolderClass = 0x2a956dad; // what the texture manager registers
constexpr uint32_t kTextureHolderIid = 0x2a956da8;
constexpr uint32_t kOverlayType = 0x4bbd01b6;   // the overlay image's resource type and group (0x743877)
constexpr uint32_t kOverlayGroup = 0x499db772;
constexpr uint32_t kHeadlineFont = 0xadd1afd3;  // the font service's headline font id
constexpr uint32_t kStandardMessageIid = 0x65297976;
constexpr uint32_t kLockPixels = 0x800, kLockColor = 0x8010, kLockFill = 0x8001;

constexpr uint32_t kThumbnailNameFormat = 0xdec858; // the thumbnail texture's name (the source's id, %08x)
constexpr uint32_t kTextNameFormat = 0xdec878;      // the text texture's name (the text, %s)
constexpr uint32_t kMinusSign = 0xd9f8f8, kPlusSign = 0xda6db4;
constexpr uint32_t kWhite = 0x102f0fc; // the game's constant white (1, 1, 1)
constexpr uint32_t kZero = 0x10dd69c;  // the game's constant zero vector

// A result tested in al (the original's `test al, al` on a slot it declares as a byte).
bool Byte(uint32_t value) { return static_cast<uint8_t>(value) != 0; }

// round-to-nearest of v, the original's way: v + 1.5 * 2^23 as a float, then its mantissa bits.
int32_t RoundBits(float v) { return std::bit_cast<int32_t>(v + 12582912.0f) - 0x4b400000; }

// The texture manager (0xae8750: a cached getter, sg/cTextureManager.h).
void *TextureManager() { return nGZSceneGraph::TextureManager(); }
// The dialog service (0x20e031: a cached getter, still game code).
void *DialogService() { return OriginalFunction<void *(__cdecl *)(), 0x20e031>()(); }
void *LanguageService() { return CachedServiceGetter<0x10f51c8, 0x441e5070, 0x54b7d5>(); } // 0x9c5a5d
cIGZCOM *GZCOM() { return static_cast<cIGZCOM *>(GetDirectorFrameworkObject()); }

// The texture manager's lookup by name: slot 25 answers whether a texture of that name exists, slot 17 its handle.
bool HasTexture(void *manager, const char *name) { return RawSlot<25, bool>(manager, name); }
uint32_t TextureHandle(void *manager, const char *name) { return RawSlot<17>(manager, name); }

// 0x743877 (cdecl): the overlay image (resource type, interface, out, group).
void MakeOverlay(UnknownRef &overlay)
{
    OriginalFunction<void(__cdecl *)(uint32_t, uint32_t, void **, uint32_t), 0x743877>()(kOverlayType, kImageIid,
                                                                                        overlay.AsPPVoid(), kOverlayGroup);
}
// 0x741d03 (cdecl): the overlay blended onto an image.
void BlendOverlay(cIGZUnknown *overlay, cIGZUnknown *image)
{
    OriginalFunction<void(__cdecl *)(cIGZUnknown *, cIGZUnknown *), 0x741d03>()(overlay, image);
}

struct ImageFormat {
    uint32_t mKind;
    uint32_t mBitsPerPixel;
};
struct ImageRect {
    int32_t left, top, right, bottom;
};

// A cRZString on the stack as the original builds it inline (the game's vtable, refcount 0), destroyed with cRZString's
// destructor body.
struct LocalString {
    union {
        cRZString mText;
    };
    LocalString()
    {
        ::new (&mText.mString) std::string();
        mText.mVTable = GameAddress<void>(cRZString::kVTable);
        mText.mRefCount = 0;
    }
    LocalString(const LocalString &) = delete;
    ~LocalString() { mText.Destruct(); }
};

} // namespace

// ---- construction ----------------------------------------------------------------------------------------------------

cTSUIPaintingService::cTSUIPaintingService() : rz::cRZSystemService(kServiceId, 0)
{
    mMessageIds.insert(kMessageObjectChanged);
    mMessageIds.insert(kMessageClearDirty);
}

cTSUIPaintingService::~cTSUIPaintingService() noexcept = default;

cITSUIPaintingService *__cdecl cTSUIPaintingService::Create()
{
    return new cTSUIPaintingService;
}

// ---- cIGZUnknown -----------------------------------------------------------------------------------------------------

bool cTSUIPaintingService::QueryInterface(uint32_t riid, void **ppv)
{
    if (!ppv) return false;
    if (riid != cITSUIPaintingService::kIID) return rz::cRZSystemService::QueryInterface(riid, ppv);
    *ppv = static_cast<cITSUIPaintingService *>(this);
    AddRef();
    return true;
}

uint32_t cTSUIPaintingService::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cTSUIPaintingService::Release() { return rz::cRZSystemService::Release(); }

// ---- cIGZSystemService -----------------------------------------------------------------------------------------------

// 0x6c2219: the overlay dropped, the message ids registered with the message server.
bool cTSUIPaintingService::Init()
{
    mOverlay.Assign(nullptr);
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)
        ->CallSlot5ForEachKey(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZMessageTarget2 *>(this))),
                              nullptr);
    return true;
}

// 0x6c2295: the overlay dropped, the colour cache and the dirty ids emptied, the message ids unregistered.
bool cTSUIPaintingService::Shutdown()
{
    mOverlay.Assign(nullptr);
    mLotColors.clear();
    mDirtyThumbnails.clear();
    reinterpret_cast<KeyMapWalker *>(&mMessageIds)
        ->CallSlot6ForEachKey(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZMessageTarget2 *>(this))),
                              nullptr);
    return true;
}

// ---- cITSUIPaintingService -------------------------------------------------------------------------------------------

// 0x6c1ea4. The lots come from the app's slot 16 object (slot 40: how many of a kind, slot 41: one of them); a lot's slot 7
// is its name, and its virtual base (through the vbptr at +4) the key the UI object's slot 41 loads the thumbnail by.
void cTSUIPaintingService::GetLotColor(int32_t kind, const char *name, float *rgb)
{
    if (!rgb || !name) return;
    const uint32_t key = StringKeyParser::Crc32(name, 1);
    const auto cached = mLotColors.find(key);
    if (cached != mLotColors.end()) {
        const Color &c = cached->second;
        rgb[0] = c.r, rgb[1] = c.g, rgb[2] = c.b;
        return;
    }
    void *const app = GetCachedSingleton();
    void *lots = nullptr;
    if (app && (lots = RawSlot<16, void *>(app)) != nullptr) {
        // ORIGINAL-BUG: a kind other than 1 or 2 passes the value of the `rgb` pointer as the list kind.
        const uint32_t list = kind == 1 ? 1 : kind == 2 ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rgb));
        const uint32_t count = RawSlot<40>(lots, list);
        for (uint32_t i = 0; i < count; ++i) {
            void *const lot = RawSlot<41, void *>(lots, list, i);
            if (!lot || !RawSlot<7, const char *>(lot)) continue;
            if (key != StringKeyParser::Crc32(RawSlot<7, const char *>(lot), 1)) continue;
            void *const dialogs = DialogService();
            if (!dialogs) continue;
            void *const ui = RawSlot<4, void *>(dialogs);
            if (!ui) continue;
            UnknownRef image;
            auto *const lotBase = static_cast<uint8_t *>(lot) + 4;
            const void *const lotKey = lotBase + (*reinterpret_cast<const int32_t *const *>(lotBase))[2];
            if (!RawSlot<41, bool>(ui, lotKey, kImageIid, image.AsPPVoid())) continue;
            if (!RawSlot<6, bool>(image.Get(), kLockColor)) continue;
            uint32_t opaque = 0;
            float sumR = 0.0f, sumG = 0.0f, sumB = 0.0f;
            for (uint32_t y = 0; y < RawSlot<10>(image.Get()); ++y) {
                for (uint32_t x = 0; x < RawSlot<9>(image.Get()); ++x) {
                    uint8_t r, g, b, a;
                    // The answer is not checked: a failed conversion leaves the bytes as they were (lane KV).
                    RawSlot<33, bool>(image.Get(), RawSlot<21>(image.Get(), x, y), &r, &g, &b, &a);
                    if (a) {
                        ++opaque;
                        sumR = static_cast<float>(r) / 255.0f + sumR;
                        sumG = static_cast<float>(g) / 255.0f + sumG;
                        sumB = static_cast<float>(b) / 255.0f + sumB;
                    }
                }
            }
            RawSlot<7>(image.Get(), kLockColor);
            Color color;
            if (opaque == 0) {
                color = *GameAddress<const Color>(kZero);
            } else {
                const float scale = static_cast<float>(1.0 / static_cast<double>(opaque));
                color = {sumR * scale, sumG * scale, sumB * scale};
            }
            mLotColors[key] = color;
            rgb[0] = color.r, rgb[1] = color.g, rgb[2] = color.b;
            return;
        }
    }
    const Color &white = *GameAddress<const Color>(kWhite);
    rgb[0] = white.r * 0.5f, rgb[1] = white.g * 0.5f, rgb[2] = white.b * 0.5f;
}

// 0x6c1595. Unless `rebuild`, a texture already registered under the name is handed out. Otherwise a 128x128 canvas in the
// first layer's format, the drawing object on it, and each layer scaled (0x7444d5) to round(scale * 128), the overlay
// blended on when the layer asks, then drawn at its alignment: the first with blend mode 0, the rest with 3.
uint32_t cTSUIPaintingService::ComposeLayers(cTSUIPaintLayers layers, cIGZString *name, bool rebuild)
{
    uint32_t result = 0;
    if (!rebuild) {
        void *const manager = TextureManager();
        if (HasTexture(manager, name->ToChar())) return TextureHandle(TextureManager(), name->ToChar());
    }
    if (layers.empty() || !layers.front().mImage) return result;
    UnknownRef drawer;
    if (!Byte(GZCOM()->GetClassObject(kDrawerClass, kDrawerIid, drawer.AsPPVoid()))) return result;
    UnknownRef canvas;
    if (!Byte(GZCOM()->GetClassObject(kImageClass, kImageIid, canvas.AsPPVoid()))) return result;
    ImageFormat format;
    const ImageFormat *const first = RawSlot<15, const ImageFormat *>(layers.front().mImage, &format);
    if (!RawSlot<3, bool>(canvas.Get(), 0x80u, 0x80u, first->mKind, first->mBitsPerPixel)) return result;
    if (!mOverlay) MakeOverlay(mOverlay);
    uint32_t blend = 0;
    RawSlot<17>(drawer.Get(), canvas.Get());
    RawSlot<24>(drawer.Get(), 1u);
    for (const cTSUIPaintLayer &layer : layers) {
        cIGZUnknown *const source = layer.mImage;
        if (!source) continue;
        UnknownRef scaled(source);
        const int32_t size = RoundBits(layer.mScale * 128.0f);
        OriginalFunction<void(__cdecl *)(cIGZUnknown *, int32_t, int32_t, uint32_t, void **, uint32_t), 0x7444d5>()(
            source, size, size, kImageIid, scaled.AsPPVoid(), 0);
        if (layer.mOverlay && mOverlay) BlendOverlay(mOverlay.Get(), scaled.Get());
        RawSlot<22>(drawer.Get(), blend);
        RawSlot<23>(drawer.Get(), 0u);
        RawSlot<21>(drawer.Get(), -1);
        // ORIGINAL-BUG: the scaled copy is used unchecked (0x7444d5 failing leaves it null).
        ImageRect rect = *RawSlot<12, const ImageRect *>(scaled.Get());
        uint32_t y = 0;
        switch (layer.mVAlign) {
        case 1: y = 0; break;
        case 2: y = RawSlot<10>(canvas.Get()), y -= RawSlot<10>(scaled.Get()); break;
        case 3: y = RawSlot<10>(canvas.Get()), y = (y - RawSlot<10>(scaled.Get())) >> 1; break;
        }
        uint32_t x = 0;
        switch (layer.mHAlign) {
        case 4: x = 0; break;
        case 5: x = RawSlot<9>(canvas.Get()), x -= RawSlot<9>(scaled.Get()); break;
        case 6: x = RawSlot<9>(canvas.Get()), x = (x - RawSlot<9>(scaled.Get())) >> 1; break;
        }
        const int32_t dx = static_cast<int32_t>(x) - rect.left;
        const int32_t dy = static_cast<int32_t>(y) - rect.top;
        rect.right += dx;
        rect.top += dy;
        rect.bottom += dy;
        rect.left += dx;
        RawSlot<39>(drawer.Get(), scaled.Get(), RawSlot<12, const ImageRect *>(scaled.Get()), &rect);
        blend = 3;
    }
    return CreateTexture(canvas.Get(), name, rebuild);
}

// 0x6c1325. Unless `rebuild`, a texture already registered under the name is handed out. Otherwise a texture of the image's
// size gets every pixel the image's slot 33 accepts (a, r, g, b in 0..1), and a texture holder made of it is registered
// with the texture manager under the name (slot 20), whose answer is returned.
uint32_t cTSUIPaintingService::CreateTexture(cIGZUnknown *image, cIGZString *name, bool rebuild)
{
    if (!rebuild) {
        void *const manager = TextureManager();
        if (HasTexture(manager, name->ToChar())) return TextureHandle(TextureManager(), name->ToChar());
    }
    if (!image) return UINT32_MAX;
    const uint32_t width = RawSlot<9>(image);
    const uint32_t height = RawSlot<10>(image);
    UnknownRef texture;
    if (!Byte(GZCOM()->GetClassObject(kTextureClass, kTextureIid, texture.AsPPVoid()))) return UINT32_MAX;
    RawSlot<6>(texture.Get(), width, height, 1u, 1u, 1.0f, 1u);
    if (RawSlot<6, bool>(image, kLockPixels)) {
        for (uint32_t x = 0; x < width; ++x) {
            for (uint32_t y = 0; y < height; ++y) {
                uint8_t r, g, b, a;
                if (RawSlot<33, bool>(image, RawSlot<21>(image, x, y), &r, &g, &b, &a)) {
                    float color[4] = {static_cast<float>(a) / 255.0f, static_cast<float>(r) / 255.0f,
                                      static_cast<float>(g) / 255.0f, static_cast<float>(b) / 255.0f};
                    RawSlot<20>(texture.Get(), x, y, color, 0u);
                }
            }
        }
        RawSlot<7>(image, kLockPixels);
    }
    UnknownRef holder;
    if (!Byte(GZCOM()->GetClassObject(kTextureHolderClass, kTextureHolderIid, holder.AsPPVoid()))) return UINT32_MAX;
    RawSlot<9>(holder.Get(), texture.Get(), 1u);
    void *const manager = TextureManager();
    return RawSlot<20>(manager, holder.Get(), name->ToChar(), 1u);
}

// 0x6c1092. The name becomes the source's thumbnail name (its id, slot 28). A source with a thumbnail of its own (slot 38)
// whose id is waiting for a rebuild skips the texture manager's cache; otherwise a texture of that name is handed out. The
// image comes from the UI object (the dialog service's slot 4): its slot 10 with the overlay blended on for such sources;
// for others the lot's (the app's slot 5 manager, slot 6 by id; its slot 10 is its 16-byte key, which the UI object's
// slot 59, cTSUIThumbnailService 0x7264ef, renders: key, 0x80, 0x80, iid, &image, 1) when
// `fromLot`, else the UI object's slot 29; both with the UI object's slot 4 flag set meanwhile.
uint32_t cTSUIPaintingService::CreateThumbnailTexture(cIGZString *name, cIGZUnknown *source, bool fromLot, uint32_t option)
{
    if (!source) return UINT32_MAX;
    name->Sprintf(GameAddress<const char>(kThumbnailNameFormat), RawSlot<28>(source));
    bool rebuild = false;
    bool dirty = false;
    if (RawSlot<38, bool>(source)) {
        const int32_t id = static_cast<int32_t>(RawSlot<28>(source));
        const auto it = mDirtyThumbnails.find(id);
        if (it != mDirtyThumbnails.end()) {
            rebuild = true;
            mDirtyThumbnails.erase(it);
            dirty = true;
        }
    }
    if (!dirty) {
        void *const manager = TextureManager();
        if (HasTexture(manager, name->ToChar())) return TextureHandle(TextureManager(), name->ToChar());
    }
    void *const dialogs = DialogService();
    if (!dialogs || !RawSlot<4, void *>(dialogs)) return UINT32_MAX;
    void *const ui = RawSlot<4, void *>(dialogs);
    UnknownRef image;
    if (RawSlot<38, bool>(source)) {
        if (RawSlot<10, bool>(ui, source, 0x80u, 0x80u, kImageIid, image.AsPPVoid(), 1u, 0u)) {
            if (mOverlay || (MakeOverlay(mOverlay), mOverlay)) BlendOverlay(mOverlay.Get(), image.Get());
        }
    } else if (fromLot) {
        if (GetCachedSingleton() && RawSlot<5, void *>(GetCachedSingleton())) {
            void *const lots = RawSlot<5, void *>(GetCachedSingleton());
            void *const lot = RawSlot<6, void *>(lots, RawSlot<28>(source));
            if (lot) {
                const uint8_t saved = RawSlot<5, uint8_t>(ui);
                RawSlot<4>(ui, true);
                RawSlot<59>(ui, RawSlot<10, const void *>(lot), 0x80u, 0x80u, kImageIid, image.AsPPVoid(), 1u);
                RawSlot<4>(ui, saved);
            }
        }
    } else {
        const uint8_t saved = RawSlot<5, uint8_t>(ui);
        RawSlot<4>(ui, true);
        void *const view = RawSlot<4, void *>(dialogs);
        uint8_t done;
        RawSlot<29>(view, &done, source, 0x80u, 0x80u, kImageIid, image.AsPPVoid(), 1u, option);
        RawSlot<4>(ui, saved);
    }
    if (!image) return UINT32_MAX;
    return CreateTexture(image.Get(), name, rebuild);
}

// 0x6c1c3b. "+" or "-" and the language's digits for |value| (the language's slot 18), drawn by CreateTextTexture.
uint32_t cTSUIPaintingService::CreateNumberTexture(int32_t value, cIGZString *name)
{
    if (!LanguageService() || !RawSlot<38, void *>(LanguageService(), 0u)) return UINT32_MAX;
    LocalString sign;
    LocalString number;
    void *language;
    if (value < 0) {
        sign.mText.mString = std::string(GameAddress<const char>(kMinusSign));
        language = RawSlot<38, void *>(LanguageService(), 0u);
        value = -value;
    } else {
        sign.mText.mString = std::string(GameAddress<const char>(kPlusSign));
        language = RawSlot<38, void *>(LanguageService(), 0u);
    }
    RawSlot<18>(language, static_cast<int64_t>(value), &number.mText);
    number.mText.mString = sign.mText.mString + number.mText.mString;
    return CreateTextTexture(number.mText.AsInterface(), name);
}

// 0x6c1917. Unless the name's texture exists already: the text measured in the headline font (slot 49 width, slot 37
// height), a 256x256 image filled white, the font scaled down when the text is wider or taller than 256 (the height rounded
// up), the text drawn at the left edge, centred vertically (cGZFont slot 52, nine arguments: image, x, y, text, length,
// colour, 0x8000000, 0, 0), the font's scale put back, and the image made a texture.
uint32_t cTSUIPaintingService::CreateTextTexture(cIGZString *text, cIGZString *name)
{
    if (!text->Strlen()) return UINT32_MAX;
    name->Sprintf(GameAddress<const char>(kTextNameFormat), text->ToChar());
    void *const manager = TextureManager();
    if (HasTexture(manager, name->ToChar())) return TextureHandle(TextureManager(), name->ToChar());
    if (!GetCachedServiceObjectB()) return UINT32_MAX;
    void *const font = RawSlot<5, void *>(GetCachedServiceObjectB(), kHeadlineFont);
    if (!font) return UINT32_MAX;
    const uint32_t length = text->Strlen();
    const uint32_t textWidth = RawSlot<49>(font, text->ToChar(), length);
    const uint32_t textHeight = RawSlot<37>(font, 0u, 0u);
    UnknownRef image;
    if (!Byte(GZCOM()->GetClassObject(kImageClass, kImageIid, image.AsPPVoid()))) return UINT32_MAX;
    ImageFormat format;
    format.mKind = 9;
    format.mBitsPerPixel = ColorFormatBits(9); // 0x5ba211, ours
    if (!RawSlot<3, bool>(image.Get(), 0x100u, 0x100u, format)) return UINT32_MAX;
    if (RawSlot<6, bool>(image.Get(), kLockFill)) {
        RawSlot<18>(image.Get(), RawSlot<32>(image.Get(), 0xffu, 0xffu, 0xffu, 0u), 0u);
        RawSlot<7>(image.Get(), kLockFill);
    }
    float scaleX, scaleY;
    RawSlot<29>(font, &scaleX, &scaleY);
    const float savedX = scaleX, savedY = scaleY;
    uint32_t height = textHeight;
    if (textWidth > 0x100 || textHeight > 0x100) {
        float factor = 1.0f;
        if (textWidth > 0x100)
            factor = 256.0f / static_cast<float>(static_cast<double>(textWidth));
        else if (textHeight > 0x100)
            factor = 256.0f / static_cast<float>(static_cast<double>(textHeight));
        scaleX = scaleX * factor;
        scaleY = factor * scaleY;
        const float scaled = static_cast<float>(static_cast<double>(textHeight)) * factor;
        int32_t rounded = RoundBits(scaled + 0.5f); // ceil
        if (static_cast<float>(rounded - 1) == scaled) rounded = rounded - 1;
        height = static_cast<uint32_t>(rounded);
    }
    const uint32_t top = (0x100 - height) >> 1;
    RawSlot<30>(font, scaleX, scaleY);
    const uint32_t color = RawSlot<30>(image.Get(), 0xffu, 0xffu, 0xffu);
    const uint32_t drawLength = text->Strlen();
    const char *const chars = text->ToChar();
    RawSlot<52>(font, image.Get(), 0u, top, chars, drawLength, color, 0x8000000u, 0u, 0u);
    RawSlot<30>(font, savedX, savedY);
    return CreateTexture(image.Get(), name, false);
}

// ---- cIGZMessageTarget2 ----------------------------------------------------------------------------------------------

// 0x6c1d9e: message 0xc5 empties the dirty ids; 0xc7 with both data words, data1 kDirtyKind, adds data2.
bool cTSUIPaintingService::DoMessage(void *message)
{
    if (!message) return false;
    auto *const generic = static_cast<cIGZMessage2 *>(message);
    cRZAutoRefCount<cIGZMessage2Standard> standard;
    bool handled = false;
    if (generic->QueryInterface(kStandardMessageIid, standard.AsPPVoid())) {
        const uint32_t type = generic->GetType();
        if (type == kMessageClearDirty) {
            mDirtyThumbnails.clear();
            handled = true;
        } else if (type == kMessageObjectChanged && standard->HasData1() && standard->HasData2() &&
                   standard->GetData1() == kDirtyKind) {
            mDirtyThumbnails.insert(static_cast<int32_t>(standard->GetData2()));
            handled = true;
        }
    }
    return handled;
}
