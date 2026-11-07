#pragma once
#include "gz/cGZSndMgr.h" // cIGZSndMgr
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "ts/cTSFamilyRelationshipArray.h" // cRZAutoRefCount

#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>

// cGZWinCtrlMgr (lane KO): the window control manager service (service id 0xc2c2eb0f, priority 0, interface 0x22c2eb1f).
// It creates the standard controls (buttons, labels, option groups, bitmaps, sliders, scroll bars, spinners, line inputs and
// the default controls by type id, through registered creators first), and holds the look they share: named colours
// (id -> r, g, b), fonts, images and two more id -> object tables, plus the sound manager for control sounds.
//
// Faces (RTTI order): +0 rz::cRZSystemService (cIGZSystemService, game vtable 0xe56030, 13 slots), +4 its cRZUnknownMT
// (0xe56068, 6), +0x1c cIGZWinCtrlMgr (0xe56084, 43). 0xd8 bytes. The COM director (cGZWinCOMDirector 0x92f3a7) registers
// the factory 0x92f732 for class 0xc2c2eb0f; it is the only creator (new 0xd8, a tail jump to the constructor 0x94ac21, the
// +0 face). With the factory hooked to Create, the game's vtables are on no object: the constructor 0x94ac21, the
// destructor 0x94acd4, the deleting destructor 0x94ad7f and every slot are in effect through it
// (db/status/cGZWinCtrlMgr.jsonl).
//
// Control objects are other classes' (cGZWinBtn, cGZWinText and the hook-free others): their slots are called by number
// here, with the method's name in a comment where its class has the fact.

using WinCtrlRef = cRZAutoRefCount<cIGZUnknown>;

// cIGZWinCtrlMgr (iid 0x22c2eb1f), 43 slots. "parent" and "caption" are what the created control's window face gets through
// its slots 79 and 90; the creators hand out the control with one reference (the caller's).
class cIGZWinCtrlMgr : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x22c2eb1f;

    // Slots 3 / 4: the adjustor thunks 0x94c19b / 0x94c66b to the service's Init / Shutdown (one override each).
    virtual bool Init() = 0;     // slot 3
    virtual bool Shutdown() = 0; // slot 4
    // id -> object tables (not counted on the way out; Set counts the new one and releases the old one).
    virtual cIGZUnknown *GetFont(uint32_t id) = 0;                     // slot 5, 0x94c69e
    virtual void SetFont(uint32_t id, cIGZUnknown *font) = 0;          // slot 6, 0x94c4d7
    virtual cIGZUnknown *GetImage(uint32_t id) = 0;                    // slot 7, 0x94c673
    virtual void SetImage(uint32_t id, cIGZUnknown *image) = 0;        // slot 8, 0x94c455
    // Buttons: slot 10 with the image kind 2, 3 or 4 (slots 9 / 20, 11 / 21, 12 / 22 / 23 share three bodies).
    virtual cIGZUnknown *CreateButtonKind2(void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                                           uint32_t style) = 0; // slot 9, 0x94ada1
    // 0x94adbe (ret 0x18): a button (class 0x22ecfc47, else cGZWinBtn): parent, caption, the font 0x68963c4c, the image
    // (the image 0xe8963ef1 when null) of `imageKind` with `imageRect`, the colour 0x28963f93, and `style` (window slot 92).
    virtual cIGZUnknown *CreateButton(uint32_t imageKind, void *parent, void *caption, cIGZUnknown *image,
                                      int32_t *imageRect, uint32_t style) = 0; // slot 10
    virtual cIGZUnknown *CreateButtonKind3(void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                                           uint32_t style) = 0; // slot 11, 0x94b791
    virtual cIGZUnknown *CreateButtonKind4(void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                                           uint32_t style) = 0; // slot 12, 0x94aeb3
    virtual cIGZUnknown *CreateText(void *parent, void *caption) = 0;                     // slot 13, 0x94b7ae
    virtual cIGZUnknown *CreateOptionGroup(void *parent, void *caption, int32_t *rect) = 0; // slot 14, 0x94b95e
    virtual cIGZUnknown *CreateBitmap(void *parent, cIGZUnknown *image, bool fitImage) = 0; // slot 15, 0x94bd40
    virtual cIGZUnknown *CreateSlider(void *parent, uint32_t orientation, uint32_t a, uint32_t b, uint32_t c) = 0; // 16
    virtual cIGZUnknown *CreateScrollbar(void *parent, uint32_t orientation, uint32_t a, uint32_t b,
                                         uint32_t c) = 0; // slot 17, 0x94ba91
    virtual cIGZUnknown *CreateSpinner(void *parent, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                                       uint32_t e) = 0;                              // slot 18, 0x94bc1b
    virtual cIGZUnknown *CreateLineInput(void *parent, uint32_t a) = 0;              // slot 19, 0x94b895
    virtual cIGZUnknown *CreateButtonKind2B(void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                                            uint32_t style) = 0; // slot 20 (0x94ada1)
    virtual cIGZUnknown *CreateButtonKind3B(void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                                            uint32_t style) = 0; // slot 21 (0x94b791)
    virtual cIGZUnknown *CreateButtonKind4B(void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                                            uint32_t style) = 0; // slot 22 (0x94aeb3)
    virtual cIGZUnknown *CreateButtonKind4C(void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                                            uint32_t style) = 0; // slot 23 (0x94aeb3)
    // Control creators (iid 0xa2ed067f), asked first by CreateControl, newest first.
    virtual bool AddCreator(cIGZUnknown *creator) = 0;    // slot 24, 0x94c1dd
    virtual bool RemoveCreator(cIGZUnknown *creator) = 0; // slot 25, 0x94c235
    // 0x94aed0: the first creator whose slot 6 answers true, else CreateDefaultControl.
    virtual bool CreateControl(uint32_t type, uint32_t iid, void **out) = 0; // slot 26
    // The sound manager's slots 4 to 7 (nothing without one).
    virtual void SetSoundDefaultFlags(uint32_t flags) = 0;          // slot 27, 0x94c572
    virtual uint32_t GetSoundDefaultFlags() = 0;                    // slot 28, 0x94bf47
    virtual uint32_t GetSoundAlias(uint32_t name) = 0;              // slot 29, 0x94c6c9
    virtual void SetSoundAlias(uint32_t name, uint32_t id) = 0;     // slot 30, 0x94c559 (the id's high word 0)
    // Named colours: 0x00rrggbb by id.
    virtual void GetColorRGB(uint32_t id, uint8_t *r, uint8_t *g, uint8_t *b) = 0; // slot 31, 0x94be2a (0, 0, 0 if none)
    virtual uint32_t GetColor(uint32_t id) = 0; // slot 32, 0x94bde5: the colour service's value (slot 34), 0 if none
    virtual void SetColor(uint32_t id, uint8_t r, uint8_t g, uint8_t b) = 0;        // slot 33, 0x94c286
    virtual void SetColorFromValue(uint32_t id, uint32_t value) = 0; // slot 34, 0x94c2e8: the colour service's slot 36
    virtual cIGZUnknown *GetObject8C(uint32_t id) = 0;               // slot 35, 0x94beaf
    virtual void SetObject8C(uint32_t id, cIGZUnknown *object) = 0;  // slot 36, 0x94c335
    virtual cIGZUnknown *GetObjectAC(uint32_t id) = 0;               // slot 37, 0x94beda
    // 0x94bf0b: the id of the first object (list order) for which `matcher`'s slot 12 (object, 1) answers true, else 0.
    virtual uint32_t FindObjectAC(cIGZUnknown *matcher) = 0;         // slot 38
    virtual void SetObjectAC(uint32_t id, cIGZUnknown *object) = 0;  // slot 39, 0x94c3cd
    virtual uint32_t GetWordD4() = 0;                                // slot 40, folded 0x254680 (+0xd4, 750)
    virtual void SetWordD4(uint32_t value) = 0;                      // slot 41, 0x94c3c0 (folded with cTSCANHEntry's)
    // 0x94af1f (2,162 bytes): the control of a standard type id, made and set up here, queried for `iid` into `out`.
    virtual bool CreateDefaultControl(uint32_t type, uint32_t iid, void **out) = 0; // slot 42
};

class cGZWinCtrlMgr : public rz::cRZSystemService, public cIGZWinCtrlMgr {
public:
    static constexpr uint32_t kSize = 0xd8;
    static constexpr uint32_t kServiceId = 0xc2c2eb0f;
    static constexpr int32_t kPriority = 0;
    static constexpr uint32_t kDefaultWordD4 = 750;
    static constexpr uint32_t kColorServiceId = 0xc416025c, kColorServiceIid = 0x73283c; // getter 0x7ae7f4
    static constexpr uint32_t kCreatorIid = 0xa2ed067f;
    static constexpr uint32_t kDefaultFont = 0x68963c4c;

    cGZWinCtrlMgr();            // 0x94ac21
    ~cGZWinCtrlMgr() noexcept override; // 0x94acd4 (Shutdown first); deleting 0x94ad7f (+4 slot 3, thunk 0x94ad77)

    cGZWinCtrlMgr(const cGZWinCtrlMgr &) = delete;
    cGZWinCtrlMgr &operator=(const cGZWinCtrlMgr &) = delete;

    // 0x92f732 (the factory the director registers): new, construct, the +0 face.
    static cIGZSystemService *__cdecl Create();

    // cIGZUnknown (every face; +4 / +0x1c through the thunks 0x94c1cd / 0x94c1d5). AddRef / Release: cRZSystemService's.
    bool QueryInterface(uint32_t riid, void **out) override; // 0x94c1a3
    uint32_t AddRef() override { return rz::cRZSystemService::AddRef(); }
    uint32_t Release() override { return rz::cRZSystemService::Release(); }

    // cIGZSystemService and cIGZWinCtrlMgr slots 3 / 4
    bool Init() override;     // 0x94bf56
    bool Shutdown() override; // 0x94c581

    // cIGZWinCtrlMgr
    cIGZUnknown *GetFont(uint32_t id) override;
    void SetFont(uint32_t id, cIGZUnknown *font) override;
    cIGZUnknown *GetImage(uint32_t id) override;
    void SetImage(uint32_t id, cIGZUnknown *image) override;
    cIGZUnknown *CreateButtonKind2(void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                                   uint32_t style) override;
    cIGZUnknown *CreateButton(uint32_t imageKind, void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                              uint32_t style) override;
    cIGZUnknown *CreateButtonKind3(void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                                   uint32_t style) override;
    cIGZUnknown *CreateButtonKind4(void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                                   uint32_t style) override;
    cIGZUnknown *CreateText(void *parent, void *caption) override;
    cIGZUnknown *CreateOptionGroup(void *parent, void *caption, int32_t *rect) override;
    cIGZUnknown *CreateBitmap(void *parent, cIGZUnknown *image, bool fitImage) override;
    cIGZUnknown *CreateSlider(void *parent, uint32_t orientation, uint32_t a, uint32_t b, uint32_t c) override;
    cIGZUnknown *CreateScrollbar(void *parent, uint32_t orientation, uint32_t a, uint32_t b, uint32_t c) override;
    cIGZUnknown *CreateSpinner(void *parent, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) override;
    cIGZUnknown *CreateLineInput(void *parent, uint32_t a) override;
    cIGZUnknown *CreateButtonKind2B(void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                                    uint32_t style) override;
    cIGZUnknown *CreateButtonKind3B(void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                                    uint32_t style) override;
    cIGZUnknown *CreateButtonKind4B(void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                                    uint32_t style) override;
    cIGZUnknown *CreateButtonKind4C(void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                                    uint32_t style) override;
    bool AddCreator(cIGZUnknown *creator) override;
    bool RemoveCreator(cIGZUnknown *creator) override;
    bool CreateControl(uint32_t type, uint32_t iid, void **out) override;
    void SetSoundDefaultFlags(uint32_t flags) override;
    uint32_t GetSoundDefaultFlags() override;
    uint32_t GetSoundAlias(uint32_t name) override;
    void SetSoundAlias(uint32_t name, uint32_t id) override;
    void GetColorRGB(uint32_t id, uint8_t *r, uint8_t *g, uint8_t *b) override;
    uint32_t GetColor(uint32_t id) override;
    void SetColor(uint32_t id, uint8_t r, uint8_t g, uint8_t b) override;
    void SetColorFromValue(uint32_t id, uint32_t value) override;
    cIGZUnknown *GetObject8C(uint32_t id) override;
    void SetObject8C(uint32_t id, cIGZUnknown *object) override;
    cIGZUnknown *GetObjectAC(uint32_t id) override;
    uint32_t FindObjectAC(cIGZUnknown *matcher) override;
    void SetObjectAC(uint32_t id, cIGZUnknown *object) override;
    uint32_t GetWordD4() override;
    void SetWordD4(uint32_t value) override;
    bool CreateDefaultControl(uint32_t type, uint32_t iid, void **out) override;

    // 0x94c88a (ret 4): the font `id`, else the default font 0x68963c4c.
    cIGZUnknown *GetFontOrDefault(uint32_t id);

    using ObjectTable = std::unordered_map<uint32_t, cIGZUnknown *>; // counted by hand (Set / Shutdown)

    bool mInitialized;                       // +0x20 (Init once until Shutdown)
    WinCtrlRef mColorService;                // +0x24 service 0xc416025c (Init to Shutdown)
    cRZAutoRefCount<cIGZSndMgr> mSoundManager; // +0x28 service 0x1af85bfb (Init to Shutdown)
    std::unordered_map<uint32_t, uint32_t> mColors; // +0x2c id -> 0x00rrggbb (node value byte 0xe r, 0xd g, 0xc b)
    ObjectTable mFonts;                      // +0x4c
    ObjectTable mImages;                     // +0x6c
    ObjectTable mObjects8C;                  // +0x8c
    ObjectTable mObjectsAC;                  // +0xac
    std::list<cIGZUnknown *> mCreators;      // +0xcc newest first; each holds the reference AddCreator's query took
    uint32_t mWordD4;                        // +0xd4
};

static_assert(sizeof(cGZWinCtrlMgr) == cGZWinCtrlMgr::kSize, "cGZWinCtrlMgr size");
static_assert(offsetof(cGZWinCtrlMgr, mServiceId) == 0xc, "cGZWinCtrlMgr layout: the service part at +0");
static_assert(offsetof(cGZWinCtrlMgr, mInitialized) == 0x20 && offsetof(cGZWinCtrlMgr, mColorService) == 0x24 &&
                  offsetof(cGZWinCtrlMgr, mSoundManager) == 0x28 && offsetof(cGZWinCtrlMgr, mColors) == 0x2c &&
                  offsetof(cGZWinCtrlMgr, mFonts) == 0x4c && offsetof(cGZWinCtrlMgr, mImages) == 0x6c &&
                  offsetof(cGZWinCtrlMgr, mObjects8C) == 0x8c && offsetof(cGZWinCtrlMgr, mObjectsAC) == 0xac &&
                  offsetof(cGZWinCtrlMgr, mCreators) == 0xcc && offsetof(cGZWinCtrlMgr, mWordD4) == 0xd4,
              "cGZWinCtrlMgr layout");
