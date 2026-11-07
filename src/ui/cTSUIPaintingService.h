#pragma once
// cTSUIPaintingService (lane KS): the UI's texture painter (system service id 0x1835d96f, priority 0; COM class built by the
// creator 0x6c1e61, which the cTSUICOMDirector constructor registers). 0x58 bytes, built for size (/O1). It turns images
// into textures held by the texture manager (by name): one image as is (slot 5), layers composed onto a 128x128 canvas
// (slot 4), a balloon thumbnail of an object (slot 6), a line of text in the headline font (slot 8) or a signed number
// (slot 7); and it hands out the average colour of a lot's thumbnail, cached by the CRC of the lot's name (slot 3).
//
// Faces (RTTI order): +0 cRZSystemService / cIGZSystemService (game vtable 0xdec7cc, 13 slots), +4 cRZUnknownMT (0xdec804,
// 6), +0x1c cITSUIPaintingService (0xdec820, 9), +0x20 cIGZMessageTarget2 (0xdec848, 4).
//
// Standalone shape (docs/standalone-shape.md): the creator 0x6c1e61 (`new`, the painting face returned) is the only caller
// of the constructor 0x6c0f3d, and the vtables are referenced only by that constructor and the destructor 0x6c0ff9, so with
// the creator hooked every object carries ours. Nothing else in the image reaches the class's code.
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "ts/cTSFamilyRelationshipArray.h" // cRZAutoRefCount

#include <cstddef>
#include <cstdint>
#include <set>
#include <unordered_map>
#include <vector>

class cIGZString;

// One layer ComposeLayers draws onto its canvas (0x14 bytes; the caller's vector is passed by value).
struct cTSUIPaintLayer {
    cIGZUnknown *mImage; // +0  the image (the cIGZImage face, iid 0x20732180); null layers are skipped
    bool mOverlay;       // +4  the overlay image is blended onto the scaled copy first
    float mScale;        // +8  the copy's size: round(scale * 128) on both axes
    int32_t mVAlign;     // +0xc  1 top, 2 bottom, 3 centred; anything else top
    int32_t mHAlign;     // +0x10 4 left, 5 right, 6 centred; anything else left
};
static_assert(sizeof(cTSUIPaintLayer) == 0x14, "cTSUIPaintLayer layout");
// The layers' vector: its memory is the allocator service's (the original's by-value destructor 0x5a9640 frees through it).
using cTSUIPaintLayers = std::vector<cTSUIPaintLayer, GZServiceAllocator<cTSUIPaintLayer>>;
// Passed by value in three words (first / last / end): ComposeLayers 0x6c1595 pops 0x14 with the name and the flag (lane KV).
static_assert(sizeof(cTSUIPaintLayers) == 0xc, "cTSUIPaintLayers is the original's 12-byte vector");

// cITSUIPaintingService (iid 0x6834ba24). The texture slots answer the texture manager's handle for the name, or -1 (0 for
// ComposeLayers) when something they need is missing. Slot names are lane KS's (tentative, from the bodies).
class cITSUIPaintingService : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x6834ba24;

    // slot 3 (ret 0xc): the average colour (r, g, b in 0..1) of the opaque pixels of the thumbnail of the lot named `name`
    // (by CRC32, lowercased), cached per name; white * 0.5 when no lot of that name has a thumbnail. `kind` 1 / 2 pick the
    // lot list (see the body).
    virtual void GetLotColor(int32_t kind, const char *name, float *rgb) = 0;
    // slot 4 (ret 0x14): the layers drawn onto a new 128x128 canvas (the first layer's format), made a texture named `name`.
    virtual uint32_t ComposeLayers(cTSUIPaintLayers layers, cIGZString *name, bool rebuild) = 0;
    // slot 5 (ret 0xc): `image` copied pixel by pixel into a new texture registered under `name`.
    virtual uint32_t CreateTexture(cIGZUnknown *image, cIGZString *name, bool rebuild) = 0;
    // slot 6 (ret 0x10): the 128x128 thumbnail of `source` made a texture named after its id.
    virtual uint32_t CreateThumbnailTexture(cIGZString *name, cIGZUnknown *source, bool fromLot, uint32_t option) = 0;
    // slot 7 (ret 8): the number with its sign ("+" / "-" and the language's digits) through CreateTextTexture.
    virtual uint32_t CreateNumberTexture(int32_t value, cIGZString *name) = 0;
    // slot 8 (ret 8): `text` in the headline font, centred on a 256x256 canvas, made a texture named after it.
    virtual uint32_t CreateTextTexture(cIGZString *text, cIGZString *name) = 0;
};

class cTSUIPaintingService : public rz::cRZSystemService, public cITSUIPaintingService, public cIGZMessageTarget2 {
public:
    static constexpr uint32_t kVTable = 0xdec7cc; // the game's, for tests
    static constexpr uint32_t kVTable4 = 0xdec804;
    static constexpr uint32_t kPaintingVTable = 0xdec820;
    static constexpr uint32_t kMessageVTable = 0xdec848;
    static constexpr uint32_t kSize = 0x58;
    static constexpr uint32_t kServiceId = 0x1835d96f;
    static constexpr uint32_t kMessageClearDirty = 0xc5; // drops every id waiting for a new thumbnail
    static constexpr uint32_t kMessageObjectChanged = 0xc7; // with data1 kDirtyKind: data2 waits for a new thumbnail
    static constexpr uint32_t kDirtyKind = 0x6c27daef;

    cTSUIPaintingService();                  // 0x6c0f3d
    ~cTSUIPaintingService() noexcept override; // 0x6c0ff9 (noexcept as the base's; cRZAutoRefCount's is noexcept(false)); deleting 0x6c1073 (+4 slot 3, thunk 0x6c106b)

    // 0x6c1e61 (cdecl): a new service, its painting face returned (null when new failed).
    static cITSUIPaintingService *__cdecl Create();

    // cIGZUnknown (every face; the secondary faces through the compiler's thunks)
    bool QueryInterface(uint32_t riid, void **ppv) override; // 0x6c2240 (thunks 0x6c227d, 0x6c2285, 0x6c228d)
    uint32_t AddRef() override;                              // folded 0x1cc320 (cRZSystemService's)
    uint32_t Release() override;                             // folded 0x1cd7a0 (cRZSystemService's)

    // cIGZSystemService
    bool Init() override;     // 0x6c2219
    bool Shutdown() override; // 0x6c2295

    // cITSUIPaintingService
    void GetLotColor(int32_t kind, const char *name, float *rgb) override;                       // 0x6c1ea4
    uint32_t ComposeLayers(cTSUIPaintLayers layers, cIGZString *name, bool rebuild) override; // 0x6c1595
    uint32_t CreateTexture(cIGZUnknown *image, cIGZString *name, bool rebuild) override;          // 0x6c1325
    uint32_t CreateThumbnailTexture(cIGZString *name, cIGZUnknown *source, bool fromLot, uint32_t option) override; // 0x6c1092
    uint32_t CreateNumberTexture(int32_t value, cIGZString *name) override;                      // 0x6c1c3b
    uint32_t CreateTextTexture(cIGZString *text, cIGZString *name) override;                     // 0x6c1917

    // cIGZMessageTarget2
    bool DoMessage(void *message) override; // 0x6c1d9e

    struct Color {
        float r, g, b;
    };

    std::unordered_map<uint32_t, Color> mLotColors; // +0x24 CRC32 of the lot name -> average colour
    cRZAutoRefCount<cIGZUnknown> mOverlay;          // +0x44 the overlay image (made on first use, dropped by Init / Shutdown)
    std::set<uint32_t> mMessageIds;                 // +0x48 0xc7, 0xc5: registered with the message server by Init
    std::set<int32_t> mDirtyThumbnails;             // +0x50 thumbnail ids to rebuild (DoMessage 0xc7, CreateThumbnailTexture)
};
static_assert(sizeof(cTSUIPaintingService) == cTSUIPaintingService::kSize, "cTSUIPaintingService size");
static_assert(offsetof(cTSUIPaintingService, mLotColors) == 0x24, "cTSUIPaintingService layout");
static_assert(offsetof(cTSUIPaintingService, mOverlay) == 0x44, "cTSUIPaintingService layout");
static_assert(offsetof(cTSUIPaintingService, mMessageIds) == 0x48, "cTSUIPaintingService layout");
static_assert(offsetof(cTSUIPaintingService, mDirtyThumbnails) == 0x50, "cTSUIPaintingService layout");
