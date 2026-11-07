#pragma once
// cTSDecalEffect (lane WS, rendering row E2): the effect a decal description makes (0xe8 bytes, vtables 0xe91214, 0xe91250). A
// texture laid on the ground or the water through an overlay manager the owner gives: Start asks the manager for a decal and
// sets it up, AddToScene plays the description's curves over the decal's life, Update follows the effect's placement. RVAs
// 1.18.0.187; all /O2.
//
// The curve readings at the x87 sites are the original's: the curve (0xbb5620) returns on the x87 and the products with the
// effect's scales are x87 arithmetic, so they round to the thread's precision control; the rest is SSE.
//
// Standalone shape (docs/standalone-shape.md): only Create 0xbfd440 builds one (cTSDecalDescription::CreateEffect, ours), so
// every decal effect carries our vtables and the slots need no adapters.
#include "rz/cRZString.h"
#include "ts/cTSEffectBase.h"

#include <cstddef>
#include <cstdint>

struct cTSDecalDescription;

// The decal's numbers, read and written whole (0x28 bytes, cITSDecal's slots 4 and 5).
struct cTSDecalParams {
    fx::Vector3 mPosition; // +0x00
    float mSize;           // +0x0c
    float mRotation;       // +0x10 in turns
    float mAspect;         // +0x14
    float mRepeatU;        // +0x18
    float mRepeatV;        // +0x1c
    float mOffsetU;        // +0x20
    float mOffsetV;        // +0x24
    uint32_t mSpare;       // +0x28 nothing found reads it; GetParams and SetParams copy it (0xbdad90, 0xbdbea0)
};
static_assert(sizeof(cTSDecalParams) == 0x2c, "cTSDecalParams layout");

// The interfaces (names from the game's RTTI; the slots are the ones this class calls, the others hold their place). The
// objects are still the game's cTSDecal, cTSOverlay and cTSOverlayManager.
class cITSOverlay : public cIGZUnknown {
public:
    virtual void SetVisible(bool value) = 0;              // 3 (only the low byte is read)
    virtual void SetAlpha(float alpha) = 0;               // 4 (0xbdb420)
    virtual void SetColor(const fx::Vector3 *color) = 0;  // 5 (0xbdb490)
    virtual void SetDraw(uint32_t draw) = 0;              // 6 (0xbdb570)
};

class cITSDecal : public cIGZUnknown {
public:
    virtual void Slot3(void *) = 0;                       // 3 (0xbdb1e0; not called here)
    virtual void SetParams(const cTSDecalParams *p) = 0;  // 4
    virtual void GetParams(cTSDecalParams *p) = 0;        // 5
    virtual cITSOverlay *GetOverlay() = 0;                // 6
};

class cITSOverlayManager : public cIGZUnknown {
public:
    virtual void Slot3() = 0; // 3
    virtual void Slot4() = 0; // 4
    // 5 (0xbda440): a new decal on the texture, or null.
    virtual cITSDecal *CreateDecal(const char *texture, const fx::Vector3 *position, float size, float rotation) = 0;
    virtual void Slot6(void *) = 0;                         // 6 (0xbda820)
    virtual void Slot7(void *, void *, void *, void *, void *) = 0; // 7 (0xbda710)
    virtual void Slot8(void *, void *, void *) = 0;         // 8 (0xbda610)
    virtual void RemoveOverlay(cITSOverlay *overlay) = 0; // 9 (0xbdb390)
};

class cTSDecalEffect : public cTSEffectBase {
public:
    explicit cTSDecalEffect(const cTSDecalDescription *description); // 0xbfc3f0
    ~cTSDecalEffect() override;                                      // 0xbfc700; deleting 0xbfc800, +4 thunk 0xbfc7ed
    static cITSComponentEffect *__cdecl Create(const cTSDecalDescription *description); // 0xbfd440: null on failure

    void Attach(cIGZUnknown *owner) override;      // 0: 0xbfcbe0: not running, placement and override reset, the owner's manager
    void Start(uint32_t a) override;               // 2: 0xbfd010: the decal asked for and set up; `a`: start where the life mode says
    void Stop(uint32_t a) override;                // 3: 0xbfd3f0: the decal handed back; `a` unused
    bool IsRunning() override { return mRunning; } // 4: shared 0x150f10 (under 5 bytes)
    // 5: 0xbfc830. The first argument is the time step (a float's bits); the second the scene context, whose word +0x1c counts
    // the decals seen.
    void AddToScene(void *step, void *context) override;
    // 6: 0xbfcd90: the position, size and rotation from the two placements, unless the decal was placed by hand.
    void Update(const fx::EffectsTransform *local, const fx::EffectsTransform *parent) override;
    // 7: 0xbfcce0: 3, the alpha factor; 0xb, a rectangle (left, top, right, bottom) sets the aspect and pins the decal; 0x101, the size factor.
    bool HandleEvent(uint32_t kind, void *a, void *b) override;
    bool Query(void *, void *) override { return true; } // 8: shared 0x1477b0 (not the base's no)
    void SetFlagged(bool value) override;          // 9: 0xbfcd70: the overlay's visibility

    fx::EffectsTransform mPlacement;        // +0x14 reset by Attach, otherwise unused
    fx::EffectsTransform mPlacement2;       // +0x4c unused
    fx::Vector3 mPosition;                  // +0x84
    float mSize;                            // +0x90
    float mRotation;                        // +0x94 in turns
    float mAlphaScale;                      // +0x98 random, from the alpha variance
    float mSizeScale;                       // +0x9c random, from the size variance
    float mRotateScale;                     // +0xa0 random, from the rotation variance
    float mRepeatScale;                     // +0xa4
    float mAspectScale;                     // +0xa8
    float mAlphaFactor;                     // +0xac event 3
    float mSizeFactor;                      // +0xb0 event 0x101
    bool mPinned;                           // +0xb4 Update leaves the decal alone
    float mTime;                            // +0xb8 never set by the constructor (from Start)
    float mRate;                            // +0xbc 1 / life
    fx::CountedRef<cITSOverlayManager> mManager; // +0xc0
    fx::CountedRef<cITSDecal> mDecal;       // +0xc4
    // +0xc8 a texture to use over the description's. Only Attach touches it in the image: it stays empty in the game.
    union {
        cRZString mTexture;
    };

private:
    float AlphaAt(float t) const;
    float SizeAt(float t) const;   // with the size factor; Start's has none
    float RotationAt(float t) const;
};
static_assert(offsetof(cTSDecalEffect, mPlacement) == 0x14 && offsetof(cTSDecalEffect, mPlacement2) == 0x4c &&
                  offsetof(cTSDecalEffect, mPosition) == 0x84 && offsetof(cTSDecalEffect, mSize) == 0x90 &&
                  offsetof(cTSDecalEffect, mAlphaFactor) == 0xac && offsetof(cTSDecalEffect, mPinned) == 0xb4 &&
                  offsetof(cTSDecalEffect, mTime) == 0xb8 && offsetof(cTSDecalEffect, mManager) == 0xc0 &&
                  offsetof(cTSDecalEffect, mDecal) == 0xc4 && offsetof(cTSDecalEffect, mTexture) == 0xc8 &&
                  sizeof(cTSDecalEffect) == 0xe8,
              "cTSDecalEffect layout");
