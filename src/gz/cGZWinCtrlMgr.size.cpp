// cGZWinCtrlMgr (lane KO): the whole class in the standalone shape (src/gz/cGZWinCtrlMgr.h). Built for size in the game.
#include "gz/cGZWinCtrlMgr.h"

#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "ifaces/cIGZFrameWork.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject, ScopedRef
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "rz/cRZString.h"
#include "ui/cGZWinBtn.h"
#include "ui/cGZWinText.h"

#include <new>

namespace {

// The window face a control hands out (its slot 3), and the cIGZWin slots called on it here.
enum WinSlot : int {
    kWinSetSize = 62,         // (width, height)
    kWinSetArea = 64,         // (left, right, top, bottom): CreateOptionGroup's rectangle
    kWinSetParent = 79,       // (parent)
    kWinSetCaption = 90,      // (cIGZString *)
    kWinSetStyle = 92,        // (style)
    kWinSetBackground = 119,  // (colour value)
    kWinSetColorRGB = 120,    // (r, g, b): low bytes only (gz/cGZWin.h)
};

// Slots every control here answers.
enum CtrlSlot : int {
    kCtrlAsWin = 3, // the window face
};

// cIGZWinBtn (db/methods/cGZWinBtn.jsonl).
enum BtnSlot : int {
    kBtnSetImage = 12,     // (kind, image, rect)
    kBtnSetFont = 19,      // (font)
    kBtnSetColor = 23,     // (index, colour)
    kBtnSetAlignment = 27, // (alignment)
    kBtnSetMargins2 = 30,  // (horizontal, vertical)
};

// The image objects' slots (CreateBitmap fits the window to the image; the default button takes its rectangle).
enum ImageSlot : int {
    kImageWidth = 9,
    kImageHeight = 10,
    kImageRect = 11, // (int32_t rect[4]): fills all four words
};

// The fonts' slots used here.
enum FontSlot : int {
    kFontValue = 12,  // CreateText hands it to the label's slot 10
    kFontHeight = 37, // (0, 0): the line height the default controls size themselves by
};

// The colour service's slots (0xc416025c).
enum ColorServiceSlot : int {
    kColorFromRGB = 34, // (r, g, b) -> value: GetColor passes the packed word shifted, the callee takes the low bytes
    kColorToRGB = 36,   // (value, uint8_t *r, uint8_t *g, uint8_t *b)
};

// The class object `classId` queried for `iid` from the COM object. Its result is a 32-bit word (gz/cIGZCOM.h); the
// creators here test its low byte.
bool GetClassObject(uint32_t classId, uint32_t iid, cIGZUnknown **out)
{
    return static_cast<uint8_t>(static_cast<cIGZCOM *>(GetDirectorFrameworkObject())
                                    ->GetClassObject(classId, iid, reinterpret_cast<void **>(out))) != 0;
}

// The fallback when the COM object has no class: new, the control's constructor (`this` back), the face at `offset`,
// counted once (the caller's reference).
cIGZUnknown *Counted(void *object, uint32_t offset)
{
    auto *face = reinterpret_cast<cIGZUnknown *>(static_cast<uint8_t *>(object) + offset);
    face->AddRef();
    return face;
}

// Constructors of controls nobody has replaced yet: `this` only, or `this` plus one argument (ret 4).
// The RVA is spelled at each call so tools/origsyms.py finds it.
using Ctor0 = void *(__thiscall *)(void *);
using Ctor1 = void *(__thiscall *)(void *, uint32_t);

cIGZUnknown *NewButton()
{
    return Counted(static_cast<cGZWinBtn *>(::operator new(cGZWinBtn::kSize))->Construct(), 0);
}

void *WindowOf(cIGZUnknown *control) { return RawSlot<kCtrlAsWin, void *>(control); }

// The service `id` from the framework into `ref` (0x7ae7f4 / 0x212e8c).
void QueryService(ScopedRef &ref, uint32_t id, uint32_t iid)
{
    ref.mObject = nullptr;
    if (void *const framework = GetServiceObject())
        As<ifaces::cIGZFrameWork>(framework)->GetSystemService(id, iid, &ref.mObject);
}

// Set: the new object counted, the old one released and erased, the new one inserted (through a counted pair).
void SetEntry(cGZWinCtrlMgr::ObjectTable &table, uint32_t id, cIGZUnknown *object)
{
    if (object) object->AddRef();
    const auto it = table.find(id);
    if (it != table.end()) {
        if (it->second) it->second->Release();
        table.erase(it);
    }
    if (object) {
        object->AddRef();
        table.insert({id, object});
        object->Release();
    }
}

cIGZUnknown *GetEntry(const cGZWinCtrlMgr::ObjectTable &table, uint32_t id)
{
    const auto it = table.find(id);
    return it == table.end() ? nullptr : it->second;
}

void ReleaseAll(cGZWinCtrlMgr::ObjectTable &table)
{
    for (const auto &entry : table)
        if (entry.second) entry.second->Release();
    table.clear();
}

// The control type ids CreateDefaultControl knows.
enum : uint32_t {
    kTypeScrollbarH = 0x28963e2c,
    kTypeClass598 = 0x28963e36,
    kTypeLineInput = 0x48963e28,
    kTypeScrollbarV = 0x48963e30,
    kTypeText = 0x48963e33,
    kTypeButtonKind3 = 0x68963e21,
    kTypeSizedClass231a = 0x68963e45,
    kTypeButtonA = 0x68963e48,
    kTypeButtonKind2 = 0x88963e15,
    kTypeWindow = 0x88963e39,
    kTypeButtonB = 0xa8963e4b,
    kTypeButtonImage2 = 0xc8963e1d,
    kTypeOptionGroup = 0xc8963e3c,
    kTypeButtonImage3 = 0xe8963e24,
    kTypeClass231a = 0xe8963e41,
    kTypeBitmap = 0xe8963e4d,
};

} // namespace

cGZWinCtrlMgr::cGZWinCtrlMgr()
    : rz::cRZSystemService(kServiceId, kPriority), mInitialized(false), mWordD4(kDefaultWordD4)
{
}

cGZWinCtrlMgr::~cGZWinCtrlMgr() noexcept { cGZWinCtrlMgr::Shutdown(); }

cIGZSystemService *__cdecl cGZWinCtrlMgr::Create() { return new cGZWinCtrlMgr; }

bool cGZWinCtrlMgr::QueryInterface(uint32_t riid, void **out)
{
    if (riid == cIGZWinCtrlMgr::kIid) {
        *out = static_cast<cIGZWinCtrlMgr *>(this);
        AddRef();
        return true;
    }
    return rz::cRZSystemService::QueryInterface(riid, out);
}

bool cGZWinCtrlMgr::Init()
{
    if (mInitialized) return true;
    mInitialized = true;
    ScopedRef colorService, soundManager;
    QueryService(colorService, kColorServiceId, kColorServiceIid);
    mColorService = WinCtrlRef(static_cast<cIGZUnknown *>(colorService.mObject));
    QueryService(soundManager, cGZSndMgr::kServiceId, cIGZSndMgr::kIid);
    mSoundManager = cRZAutoRefCount<cIGZSndMgr>(static_cast<cIGZSndMgr *>(soundManager.mObject));

    // The standard colours.
    SetColor(0x68963f7d, 0, 0, 0);
    SetColor(0x28963fc5, 0x80, 0, 0);
    SetColor(0xa8963f82, 0, 0x80, 0);
    SetColor(0x48963f84, 0x80, 0x80, 0x40);
    SetColor(0x68963f8a, 0, 0, 0x80);
    SetColor(0xa8963f8c, 0x80, 0, 0x80);
    SetColor(0x68963f8f, 0, 0x80, 0x80);
    SetColor(0x08963f91, 0x80, 0x80, 0x80);
    SetColor(0x28963f93, 0xc0, 0xc0, 0xc0);
    SetColor(0xe8963f95, 0xff, 0, 0);
    SetColor(0x28963f98, 0, 0xff, 0);
    SetColor(0xe8963f9a, 0xff, 0xff, 0);
    SetColor(0xc8963fa2, 0, 0, 0xff);
    SetColor(0x08963fa5, 0, 0xff, 0xff);
    SetColor(0x28963fa8, 0xff, 0xff, 0xff);
    SetColor(0x88963fab, 0x40, 0x40, 0x40);
    SetColor(0xa8963fad, 0x40, 0x40, 0x40);
    SetColor(0x08963faf, 0xc0, 0xc0, 0xc0);
    SetColor(0xa8963fb2, 0xff, 0xff, 0xff);
    SetColor(0x88963fb4, 0xc0, 0xc0, 0xc0);
    SetColor(0x08963fb9, 0xa0, 0xa0, 0xa0);
    SetColor(0x28963fbc, 0xff, 0xff, 0xff);
    soundManager.ReleaseObject();
    colorService.ReleaseObject();
    return true;
}

bool cGZWinCtrlMgr::Shutdown()
{
    mColors.clear();
    ReleaseAll(mFonts);
    ReleaseAll(mImages);
    ReleaseAll(mObjects8C);
    ReleaseAll(mObjectsAC);
    while (mCreators.size() != 0) static_cast<cIGZWinCtrlMgr *>(this)->RemoveCreator(mCreators.front());
    mColorService.Assign(nullptr);
    mSoundManager.Assign(nullptr);
    mInitialized = false;
    return true;
}

cIGZUnknown *cGZWinCtrlMgr::GetFont(uint32_t id) { return GetEntry(mFonts, id); }
void cGZWinCtrlMgr::SetFont(uint32_t id, cIGZUnknown *font) { SetEntry(mFonts, id, font); }
cIGZUnknown *cGZWinCtrlMgr::GetImage(uint32_t id) { return GetEntry(mImages, id); }
void cGZWinCtrlMgr::SetImage(uint32_t id, cIGZUnknown *image) { SetEntry(mImages, id, image); }
cIGZUnknown *cGZWinCtrlMgr::GetObject8C(uint32_t id) { return GetEntry(mObjects8C, id); }
void cGZWinCtrlMgr::SetObject8C(uint32_t id, cIGZUnknown *object) { SetEntry(mObjects8C, id, object); }
cIGZUnknown *cGZWinCtrlMgr::GetObjectAC(uint32_t id) { return GetEntry(mObjectsAC, id); }
void cGZWinCtrlMgr::SetObjectAC(uint32_t id, cIGZUnknown *object) { SetEntry(mObjectsAC, id, object); }

cIGZUnknown *cGZWinCtrlMgr::GetFontOrDefault(uint32_t id)
{
    cIGZWinCtrlMgr *const self = this;
    cIGZUnknown *const font = self->GetFont(id);
    return font ? font : self->GetFont(kDefaultFont);
}

uint32_t cGZWinCtrlMgr::FindObjectAC(cIGZUnknown *matcher)
{
    for (const auto &entry : mObjectsAC)
        if (RawSlot<12, bool>(matcher, entry.second, 1u)) return entry.first;
    return 0;
}

uint32_t cGZWinCtrlMgr::GetWordD4() { return mWordD4; }
void cGZWinCtrlMgr::SetWordD4(uint32_t value) { mWordD4 = value; }

// --- colours

void cGZWinCtrlMgr::GetColorRGB(uint32_t id, uint8_t *r, uint8_t *g, uint8_t *b)
{
    const auto it = mColors.find(id);
    if (it == mColors.end()) {
        *b = 0;
        *g = 0;
        *r = 0;
        return;
    }
    *r = static_cast<uint8_t>(it->second >> 16);
    *g = static_cast<uint8_t>(it->second >> 8);
    *b = static_cast<uint8_t>(it->second);
}

uint32_t cGZWinCtrlMgr::GetColor(uint32_t id)
{
    const auto it = mColors.find(id);
    if (it == mColors.end() || !mColorService) return 0;
    const uint32_t rgb = it->second;
    return RawSlot<kColorFromRGB>(mColorService.Get(), rgb >> 16, rgb >> 8, rgb);
}

void cGZWinCtrlMgr::SetColor(uint32_t id, uint8_t r, uint8_t g, uint8_t b)
{
    const uint32_t rgb = uint32_t(r) << 16 | uint32_t(g) << 8 | b;
    const auto it = mColors.find(id);
    if (it != mColors.end()) mColors.erase(it);
    mColors.insert({id, rgb});
}

void cGZWinCtrlMgr::SetColorFromValue(uint32_t id, uint32_t value)
{
    uint8_t r = 0, g = 0, b = 0;
    if (mColorService) RawSlot<kColorToRGB>(mColorService.Get(), value, &r, &g, &b);
    static_cast<cIGZWinCtrlMgr *>(this)->SetColor(id, r, g, b);
}

// --- the sound manager

void cGZWinCtrlMgr::SetSoundDefaultFlags(uint32_t flags)
{
    if (mSoundManager) mSoundManager->SetDefaultFlags(flags);
}

uint32_t cGZWinCtrlMgr::GetSoundDefaultFlags() { return mSoundManager ? mSoundManager->GetDefaultFlags() : 0; }

uint32_t cGZWinCtrlMgr::GetSoundAlias(uint32_t name) { return mSoundManager ? mSoundManager->GetAlias(name) : 0; }

void cGZWinCtrlMgr::SetSoundAlias(uint32_t name, uint32_t id)
{
    if (mSoundManager) mSoundManager->SetAlias(name, id);
}

// --- creators

bool cGZWinCtrlMgr::AddCreator(cIGZUnknown *creator)
{
    if (!creator) return false;
    void *held = nullptr; // the reference the list keeps
    if (!creator->QueryInterface(kCreatorIid, &held)) return false;
    static_cast<cIGZWinCtrlMgr *>(this)->RemoveCreator(creator);
    if (!RawSlot<4, bool>(creator)) return false;
    mCreators.push_front(creator);
    return true;
}

bool cGZWinCtrlMgr::RemoveCreator(cIGZUnknown *creator)
{
    for (auto it = mCreators.begin(); it != mCreators.end(); ++it) {
        if (*it != creator) continue;
        RawSlot<5>(*it);
        (*it)->Release();
        mCreators.erase(it);
        return true;
    }
    return false;
}

bool cGZWinCtrlMgr::CreateControl(uint32_t type, uint32_t iid, void **out)
{
    for (cIGZUnknown *creator : mCreators)
        if (RawSlot<6, bool>(creator, type, iid, out)) return true;
    return static_cast<cIGZWinCtrlMgr *>(this)->CreateDefaultControl(type, iid, out);
}

// --- the control creators

cIGZUnknown *cGZWinCtrlMgr::CreateButton(uint32_t imageKind, void *parent, void *caption, cIGZUnknown *image,
                                         int32_t *imageRect, uint32_t style)
{
    cIGZWinCtrlMgr *const self = this;
    cIGZUnknown *button = nullptr;
    if (!GetClassObject(0x22ecfc47, 0x8810, &button)) button = NewButton();
    void *const window = WindowOf(button);
    cIGZUnknown *const font = self->GetFont(0x68963c54);
    if (!image) image = self->GetImage(0xe8963ef1);
    RawSlot<kWinSetParent>(window, parent);
    RawSlot<kWinSetCaption>(window, caption);
    RawSlot<kWinSetBackground>(window, self->GetColor(0x28963f93));
    RawSlot<kBtnSetFont>(button, font);
    RawSlot<kBtnSetImage>(button, imageKind, image, imageRect);
    RawSlot<kWinSetStyle>(window, style);
    return button;
}

cIGZUnknown *cGZWinCtrlMgr::CreateButtonKind2(void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                                              uint32_t style)
{
    return static_cast<cIGZWinCtrlMgr *>(this)->CreateButton(2, parent, caption, image, imageRect, style);
}
cIGZUnknown *cGZWinCtrlMgr::CreateButtonKind3(void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                                              uint32_t style)
{
    return static_cast<cIGZWinCtrlMgr *>(this)->CreateButton(3, parent, caption, image, imageRect, style);
}
cIGZUnknown *cGZWinCtrlMgr::CreateButtonKind4(void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                                              uint32_t style)
{
    return static_cast<cIGZWinCtrlMgr *>(this)->CreateButton(4, parent, caption, image, imageRect, style);
}
cIGZUnknown *cGZWinCtrlMgr::CreateButtonKind2B(void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                                               uint32_t style)
{
    return static_cast<cIGZWinCtrlMgr *>(this)->CreateButton(2, parent, caption, image, imageRect, style);
}
cIGZUnknown *cGZWinCtrlMgr::CreateButtonKind3B(void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                                               uint32_t style)
{
    return static_cast<cIGZWinCtrlMgr *>(this)->CreateButton(3, parent, caption, image, imageRect, style);
}
cIGZUnknown *cGZWinCtrlMgr::CreateButtonKind4B(void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                                               uint32_t style)
{
    return static_cast<cIGZWinCtrlMgr *>(this)->CreateButton(4, parent, caption, image, imageRect, style);
}
cIGZUnknown *cGZWinCtrlMgr::CreateButtonKind4C(void *parent, void *caption, cIGZUnknown *image, int32_t *imageRect,
                                               uint32_t style)
{
    return static_cast<cIGZWinCtrlMgr *>(this)->CreateButton(4, parent, caption, image, imageRect, style);
}

cIGZUnknown *cGZWinCtrlMgr::CreateText(void *parent, void *caption)
{
    cIGZWinCtrlMgr *const self = this;
    cIGZUnknown *text = nullptr;
    if (!GetClassObject(0x592, 0x212cdc1f, &text))
        text = Counted(static_cast<cGZWinText *>(::operator new(cGZWinText::kSize))->Construct(), 0xf8);
    void *const window = WindowOf(text);
    cIGZUnknown *const font = self->GetFont(kDefaultFont);
    const uint32_t fontValue = font ? RawSlot<kFontValue>(font) : 0;
    RawSlot<kWinSetParent>(window, parent);
    RawSlot<kWinSetCaption>(window, caption);
    RawSlot<18>(text, font);
    RawSlot<10>(text, fontValue);
    RawSlot<21>(text, 1u);
    RawSlot<16>(text, 0u);
    RawSlot<7>(text, 2u, 0u);
    RawSlot<5>(text, 0u, 0u);
    return text;
}

cIGZUnknown *cGZWinCtrlMgr::CreateOptionGroup(void *parent, void *caption, int32_t *rect)
{
    cIGZWinCtrlMgr *const self = this;
    cIGZUnknown *group = nullptr;
    if (!GetClassObject(0xa1336cc1, 0xa1336cc0, &group))
        group = Counted(OriginalFunction<Ctor0, 0x95b137>()(::operator new(0x14c)), 0xf8);
    void *const window = WindowOf(group);
    RawSlot<kWinSetParent>(window, parent);
    RawSlot<kWinSetCaption>(window, caption);
    if (rect) RawSlot<kWinSetArea>(window, rect[0], rect[2], rect[1], rect[3]);
    RawSlot<5>(group, 1u, 1u);
    RawSlot<5>(group, 4u, 0u);
    RawSlot<5>(group, 8u, 1u);
    const uint32_t c1 = self->GetColor(0xa8963fad);
    const uint32_t c2 = self->GetColor(0x08963faf);
    const uint32_t c3 = self->GetColor(0xa8963fad);
    const uint32_t c4 = self->GetColor(0xa8963fad);
    RawSlot<29>(group, c4, c3, c2, c1);
    RawSlot<36>(group, 0u, rect ? rect[3] - rect[1] : 0x80);
    return group;
}

cIGZUnknown *cGZWinCtrlMgr::CreateBitmap(void *parent, cIGZUnknown *image, bool fitImage)
{
    cIGZUnknown *bitmap = nullptr;
    if (!GetClassObject(0x82fe68c4, 0xc12cea13, &bitmap))
        bitmap = Counted(OriginalFunction<Ctor0, 0x93741a>()(::operator new(0x11c)), 0xf8);
    void *const window = WindowOf(bitmap);
    RawSlot<kWinSetParent>(window, parent);
    RawSlot<4>(bitmap, image);
    if (fitImage && image) {
        const uint32_t height = RawSlot<kImageHeight>(image);
        const uint32_t width = RawSlot<kImageWidth>(image);
        RawSlot<kWinSetSize>(window, width, height);
    }
    return bitmap;
}

cIGZUnknown *cGZWinCtrlMgr::CreateSlider(void *parent, uint32_t orientation, uint32_t a, uint32_t b, uint32_t c)
{
    cIGZUnknown *slider = nullptr;
    if (!GetClassObject(0x21325208, 0x21325207, &slider))
        slider = Counted(OriginalFunction<Ctor0, 0x957b0e>()(::operator new(0x144)), 0xf8);
    RawSlot<kWinSetParent>(WindowOf(slider), parent);
    RawSlot<10>(slider, orientation);
    cIGZUnknown *const thumb = 
        static_cast<cIGZWinCtrlMgr *>(this)->GetImage(orientation == 1 ? 0x88963ec1 : 0xa8963ec4);
    RawSlot<12>(slider, thumb, 0u);
    RawSlot<6>(slider, a);
    RawSlot<8>(slider, b);
    RawSlot<5>(slider, c);
    return slider;
}

cIGZUnknown *cGZWinCtrlMgr::CreateScrollbar(void *parent, uint32_t orientation, uint32_t a, uint32_t b, uint32_t c)
{
    cIGZUnknown *scrollbar = nullptr;
    if (!GetClassObject(0x61325a2e, 0x61325a2d, &scrollbar))
        scrollbar = Counted(OriginalFunction<Ctor0, 0x9586a8>()(::operator new(0x170)), 0x144);
    void *const window = WindowOf(scrollbar);
    void *const bar = RawSlot<4, void *>(scrollbar);
    cIGZUnknown *const thumb = static_cast<cIGZWinCtrlMgr *>(this)->GetImage(0xe8963ec7);
    RawSlot<kWinSetParent>(window, parent);
    RawSlot<10>(bar, orientation);
    RawSlot<12>(bar, thumb, 0u);
    RawSlot<6>(bar, a);
    RawSlot<8>(bar, b);
    RawSlot<5>(scrollbar, c);
    return scrollbar;
}

cIGZUnknown *cGZWinCtrlMgr::CreateSpinner(void *parent, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    cIGZWinCtrlMgr *const self = this;
    cIGZUnknown *spinner = nullptr;
    if (!GetClassObject(0x612ce0c4, 0x612ce0c3, &spinner))
        spinner = Counted(OriginalFunction<Ctor0, 0x95c6d1>()(::operator new(0x190)), 0xf8);
    void *const window = WindowOf(spinner);
    cIGZUnknown *const arrows = self->GetImage(0x68963ed6);
    const uint32_t light = self->GetColor(0xa8963fad);
    const uint32_t dark = self->GetColor(0x08963faf);
    RawSlot<14>(spinner, arrows, 0u);
    RawSlot<10>(spinner, b, c);
    RawSlot<9>(spinner, e);
    RawSlot<12>(spinner, d);
    RawSlot<4>(spinner, a);
    RawSlot<31>(spinner, light, light, dark, dark);
    RawSlot<23>(spinner, 2u, self->GetColor(0x28963f93));
    RawSlot<kWinSetParent>(window, parent);
    return spinner;
}

cIGZUnknown *cGZWinCtrlMgr::CreateLineInput(void *parent, uint32_t a)
{
    cIGZWinCtrlMgr *const self = this;
    cIGZUnknown *input = nullptr;
    if (!GetClassObject(0x21335c5a, 0x21335c59, &input))
        input = Counted(OriginalFunction<Ctor1, 0x95d9dd>()(::operator new(0x17c), 1), 0xf8);
    void *const window = WindowOf(input);
    RawSlot<28>(input, a);
    RawSlot<11>(input, 2u);
    RawSlot<14>(input, 0u, self->GetColor(0xa8963fad));
    RawSlot<14>(input, 1u, self->GetColor(0x08963faf));
    RawSlot<kWinSetParent>(window, parent);
    return input;
}

// --- the default controls

bool cGZWinCtrlMgr::CreateDefaultControl(uint32_t type, uint32_t iid, void **out)
{
    cIGZWinCtrlMgr *const self = this;
    cIGZUnknown *control = nullptr;
    bool done = false;

    // Made through the creators above, then queried and released.
    switch (type) {
    case kTypeButtonKind2:
    case kTypeButtonKind3:
    case kTypeButtonImage2:
    case kTypeButtonImage3:
    case kTypeText: {
        alignas(4) uint8_t storage[sizeof(cRZString)];
        auto *caption = reinterpret_cast<cRZString *>(storage);
        caption->Construct();
        if (type == kTypeButtonKind2) control = self->CreateButtonKind2(nullptr, caption, nullptr, nullptr, 0);
        else if (type == kTypeButtonKind3) control = self->CreateButtonKind3(nullptr, caption, nullptr, nullptr, 0);
        else if (type == kTypeButtonImage2) control = self->CreateButton(2, nullptr, caption, nullptr, nullptr, 0);
        else if (type == kTypeButtonImage3) control = self->CreateButton(3, nullptr, caption, nullptr, nullptr, 0);
        else control = self->CreateText(nullptr, caption);
        caption->Destruct();
        done = true;
        break;
    }
    case kTypeScrollbarH:
        control = self->CreateScrollbar(nullptr, 1, 0, 200, 100);
        done = true;
        break;
    case kTypeScrollbarV:
        control = self->CreateScrollbar(nullptr, 2, 0, 200, 100);
        done = true;
        break;
    case kTypeLineInput:
        control = self->CreateLineInput(nullptr, 1);
        done = true;
        break;
    default:
        break;
    }
    if (done) {
        const bool found = control->QueryInterface(iid, out);
        control->Release();
        return found;
    }

    if (type == kTypeSizedClass231a) {
        // A control of type 0xe8963e41 (through the creators) with its height set to 1.5 lines of the default font.
        bool found = false;
        cIGZUnknown *list = nullptr;
        if (self->CreateControl(kTypeClass231a, 0x231a1c57, reinterpret_cast<void **>(&list))) {
            found = list->QueryInterface(iid, out);
            if (found) {
                cIGZUnknown *const font = self->GetFont(kDefaultFont);
                int32_t height = 0x10;
                if (font) {
                    const auto line = static_cast<int32_t>(RawSlot<kFontHeight>(font, 0u, 0u));
                    height = static_cast<int32_t>(static_cast<float>(line) * 1.5f);
                }
                RawSlot<9>(list, font);
                RawSlot<82>(list, 0x200u);
                RawSlot<6>(list, 0u, 0u, 0x10u, height);
                RawSlot<45>(list);
            }
            list->Release();
        }
        return found;
    }

    // Made here: from the COM object, else new and the control's constructor.
    switch (type) {
    case kTypeWindow:
        if (!GetClassObject(0x4386d516, 0x5386d516, &control))
            control = Counted(OriginalFunction<Ctor0, 0x937b4c>()(::operator new(0x144)), 0xf8);
        break;
    case kTypeBitmap:
        if (!GetClassObject(0x82fe68c4, 0xc12cea13, &control))
            control = Counted(OriginalFunction<Ctor0, 0x93741a>()(::operator new(0x11c)), 0xf8);
        break;
    case kTypeButtonA:
    case kTypeButtonB:
        if (!GetClassObject(0x22ecfc47, 0x8810, &control)) control = NewButton();
        break;
    case kTypeClass598:
        if (!GetClassObject(0x598, 0x4132242b, &control))
            control = Counted(OriginalFunction<Ctor1, 0x953f2f>()(::operator new(0x190), 0), 0xf8);
        break;
    case kTypeClass231a:
        if (!GetClassObject(0x231a1c58, 0x231a1c57, &control))
            control = Counted(OriginalFunction<Ctor1, 0x94cb2e>()(::operator new(0x310), 0x11ff), 0xf8);
        break;
    case kTypeOptionGroup:
        if (!GetClassObject(0xa1336cc1, 0xa1336cc0, &control))
            control = Counted(OriginalFunction<Ctor0, 0x95b137>()(::operator new(0x14c)), 0xf8);
        break;
    default:
        return false;
    }
    const bool found = control->QueryInterface(iid, out);
    if (found) {
        switch (type) {
        case kTypeButtonA:
        case kTypeButtonB: {
            cIGZUnknown *const image = self->GetImage(type == kTypeButtonA ? 0xa8963ef3 : 0xc8963ef6);
            cIGZUnknown *const font = GetFontOrDefault(0xc8963c5c);
            int32_t rect[4];
            if (image) {
                RawSlot<kImageRect>(image, rect);
            } else {
                const int32_t height = font ? static_cast<int32_t>(RawSlot<kFontHeight>(font, 0u, 0u)) : 0x20;
                rect[0] = 0;
                rect[1] = 0;
                rect[2] = 0x80;
                rect[3] = height;
            }
            cIGZUnknown *const button = control; // receiver: cIGZWinBtn
            RawSlot<kBtnSetFont>(button, font);
            RawSlot<kBtnSetColor>(button, 4u, self->GetColor(0x08963fb9));
            RawSlot<kBtnSetColor>(button, 2u, self->GetColor(0x28963fbc));
            RawSlot<kBtnSetColor>(button, 0u, self->GetColor(0x08963fb7));
            RawSlot<kBtnSetAlignment>(button, 2u);
            RawSlot<kBtnSetMargins2>(button, 2u, 2u);
            RawSlot<kBtnSetImage>(button, 4u, image, rect);
            break;
        }
        case kTypeClass598: {
            uint8_t r, g, b;
            self->GetColorRGB(0xa8963fb2, &r, &g, &b);
            RawSlot<kWinSetColorRGB>(WindowOf(control), r, g, b);
            RawSlot<41>(control, self->GetFont(kDefaultFont));
            RawSlot<42>(control, 0u, self->GetColor(0x08963fb7));
            RawSlot<42>(control, 2u, self->GetColor(0x28963fbc));
            RawSlot<42>(control, 1u, self->GetColor(0x08963fb9));
            RawSlot<68>(control, self->GetColor(0x88963fb4));
            RawSlot<47>(control, 1.0f);
            RawSlot<52>(control, 1u);
            RawSlot<54>(control, 1u);
            const uint32_t lines = self->GetColor(0x08963faf);
            const uint32_t back = self->GetColor(0xa8963fad);
            RawSlot<70>(control, back, lines);
            break;
        }
        case kTypeClass231a: {
            RawSlot<9>(control, self->GetFont(kDefaultFont));
            RawSlot<84>(control, 0x200u);
            RawSlot<21>(control, self->GetColor(0xa8963fb2));
            RawSlot<23>(control, self->GetColor(0x88963fb4));
            RawSlot<12>(control, 4u, self->GetColor(0x28963fbc));
            const uint32_t text = self->GetColor(0x08963fb7);
            RawSlot<12>(control, 0u, text);
            RawSlot<13>(control, text);
            RawSlot<18>(control, text);
            RawSlot<39>(control, 1u, text);
            break;
        }
        case kTypeOptionGroup: {
            cIGZUnknown *const group = control; // receiver: cIGZWinOptGrp (cGZWinOptGrp +0xf8)
            RawSlot<5>(group, 1u, 1u);
            RawSlot<5>(group, 4u, 0u);
            RawSlot<5>(group, 8u, 1u);
            const uint32_t c1 = self->GetColor(0xa8963fad);
            const uint32_t c2 = self->GetColor(0x08963faf);
            const uint32_t c3 = self->GetColor(0xa8963fad);
            const uint32_t c4 = self->GetColor(0xa8963fad);
            RawSlot<29>(group, c4, c3, c2, c1);
            break;
        }
        default:
            break;
        }
    }
    control->Release();
    return found;
}
