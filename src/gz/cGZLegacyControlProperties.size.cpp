// The attribute readers and the common window properties of cGZLegacyControl (lane MD, foundations F5b):
// src/gz/cGZLegacyControl.h. Built for size, as the game's.
#include "gz/cGZLegacyControl.h"

#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "gz/cGZUIScriptService.h"        // cIGZUIScriptService
#include "gz/cIGZFrameWork.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "ifaces/cIGZWin.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/cIGZVariant.h"
#include "rz/cRZIniFile.h" // cRZIniStringHelpers::SplitOff
#include "rz/cRZVariant.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <new>

#pragma warning(disable : 4996) // sscanf, as the game calls it

namespace {

constexpr uint32_t kUnknownIid = 1;
constexpr uint32_t kTextVariant = 0x800e;
constexpr char kTextSeparator = ' ';
constexpr uint32_t kTextResourceType = 0x2026960b;
constexpr uint32_t kResourceManagerId = 0x056b906e;
constexpr uint32_t kResourceManagerIid = 0x656b8efc;
constexpr uint32_t kStringIid = 0x089b7dc8;
constexpr uint32_t kUntypedBytes = 0x8000; // the bytes variants' tag (cRZVariant ORs in the array bit)

// ---- the attribute ids of the common properties ----
constexpr uint32_t kIdAttribute = 0x100;           // uint32: the window id
constexpr uint32_t kAreaAttribute = 0x101;         // text "( l, t, r, b)": the anchor area
constexpr uint32_t kPositionAttribute = 0x102;     // text "(x,y)"
constexpr uint32_t kSizeAttribute = 0x103;         // text "(w,h)"
constexpr uint32_t kColorAttribute = 0x104;        // text "(r,g,b)"
constexpr uint32_t kCaptionAttribute = 0x105;      // text
constexpr uint32_t kCaptionKeyAttribute = 0x106;   // text "{group,instance}" (hex): a localized caption
constexpr uint32_t kHiddenFlagAttribute = 0x107;   // any value: flag 0x20000 set
constexpr uint32_t kFirstTipAttribute = 0x108;     // texts 0x108..0x10f: the tooltips
constexpr uint32_t kTipCount = 8;
constexpr uint32_t kTypedPropertyAttribute = 0x110; // texts "id,type,value" (applied late)

// ---- the window properties they set ----
constexpr uint32_t kCaptionKeyProperty = 0x0b31771a;  // the caption's resource key (16 bytes)
constexpr uint32_t kCaptionPairProperty = 0x3aca6906; // the caption key's two words (8 bytes), always set
constexpr uint32_t kTipsProperty = 0xbaca6879;        // the tooltip list (a variant holding a cGZLegacyTextList)

// ---- flags ----
constexpr uint32_t kHiddenFlag = 0x20000;

// The flags every new window starts with (SetFlag mask, value), in the original's order.
struct FlagDefault {
    uint32_t mask;
    uint32_t value;
};
constexpr FlagDefault kFlagDefaults[] = {
    {0x1, 1},
    {0x10000, 1},
    {0x800, 0},
    {0x20000, 1},
    {0x40000, 1},
    {0x4, 0},
    {0x40, 0},
    {0x8000, 1},
};

// A uint32 attribute that sets a flag when not 0 and clears it when 0, in the original's order.
struct FlagAttribute {
    uint32_t attribute;
    uint32_t mask;
};
constexpr FlagAttribute kApplyFlags[] = {
    {0xf01a, 0x1},
    {0xf01c, 0x100},
    {0xf01e, 0x800},
    {0xf025, 0x8000},
    {0xf026, 0x4},
    {0xf027, 0x40},
    {0xf023, 0x80000},
    {0xf024, 0x200000},
    {0xf028, 0x8000000},
    {0xf029, 0x80},
    {0xf02a, 0x20},
    {0xf02b, 0x10},
    {0xf02c, 0x2000000},
    {0xf02d, 0x800000},
    {0xf02e, 0x1000000},
};
// Those of the pass after the window is placed.
constexpr FlagAttribute kLateFlags[] = {
    {0xf01b, 0x2},
    {0xf01f, 0x10000},
    {0xf020, 0x20000},
    {0xf021, 0x40000},
};

cIGZVariant *ValueOf(const cGZScriptAttribute &attribute) { return reinterpret_cast<cIGZVariant *>(attribute.mValue); }

void ApplyFlagAttributes(ifaces::cIGZWin *window, const cGZScriptAttributes *attributes, const FlagAttribute *first,
                         const FlagAttribute *last)
{
    uint32_t value = 0; // one word for every lookup, as the original's
    for (const FlagAttribute *flag = first; flag != last; ++flag)
        if (FindUint32Attribute(attributes, flag->attribute, &value)) window->SetFlag(flag->mask, value != 0);
}

// The variant a typed property's text makes: the text as a Char variant converted to the named type (a temporary,
// destroyed after the typed variant is built), or the text itself for "string"; null for an unknown type. The
// allocation comes first, as the original's.
cRZVariant *MakeTypedValue(const cRZString &type, const char *text)
{
    auto is = [&type](const char *name) { return type.IsEqual(name, 0xffffffff, true); };
    if (is("bool")) {
        // The converted byte is stored as it is (0x9c7ce2 takes the byte), not normalised to 0 / 1.
        cRZVariant *const made = new cRZVariant(false);
        made->mBool = cRZVariant(text).ValBool();
        return made;
    }
    if (is("uint8")) return new cRZVariant(cRZVariant(text).ValUint8());
    if (is("sint8")) return new cRZVariant(cRZVariant(text).ValSint8());
    if (is("uint16")) return new cRZVariant(cRZVariant(text).ValUint16());
    if (is("sint16")) return new cRZVariant(cRZVariant(text).ValSint16());
    if (is("uint32")) return new cRZVariant(cRZVariant(text).ValUint32());
    if (is("sint32")) return new cRZVariant(cRZVariant(text).ValSint32());
    if (is("uint64")) return new cRZVariant(cRZVariant(text).ValUint64());
    if (is("sint64")) return new cRZVariant(cRZVariant(text).ValSint64());
    if (is("float32")) return new cRZVariant(cRZVariant(text).ValFloat32());
    if (is("float64")) return new cRZVariant(cRZVariant(text).ValFloat64());
    if (is("string")) return new cRZVariant(text);
    return nullptr;
}

// One "id,type,value" text: the id (strtoul, any base) and the type split off the front; with an id not 0, a type
// and a value left, the typed variant goes to the window's SetProperty (not released).
void ApplyTypedProperty(ifaces::cIGZWin *window, cIGZVariant *value)
{
    LegacyText text;
    value->GetValString(*text.mText.AsInterface(), 0, kTextSeparator);
    auto *const split = reinterpret_cast<cRZIniStringHelpers *>(&text.mText);

    union Part {
        cRZString mText;
        Part() {}
        ~Part() { mText.Destruct(); }
    };
    uint32_t id;
    {
        Part head;
        split->SplitOff(&head.mText, ",");
        id = std::strtoul(head.mText.ToChar(), nullptr, 0);
    }
    Part type;
    split->SplitOff(&type.mText, ",");
    if (id == 0 || type.mText.IsEmpty() || text.mText.IsEmpty()) return;
    if (cRZVariant *const typed = MakeTypedValue(type.mText, text.Chars())) window->SetProperty(id, typed);
}

} // namespace

// ---- the readers ------------------------------------------------------------------------------------------------------

bool __cdecl FindTextAttribute(const cGZScriptAttributes *attributes, uint32_t id, cRZString *out)
{
    out->mString.clear();
    const uint32_t count = attributes->Size();
    for (uint32_t i = 0; i < count; ++i) {
        const cGZScriptAttribute &attribute = attributes->mBegin[i];
        if (attribute.mId != id || !attribute.mValue) continue;
        cIGZVariant *const value = ValueOf(attribute);
        if (value->GetType() == kTextVariant) return value->GetValString(*out->AsInterface(), 0, kTextSeparator);
    }
    return false;
}

bool __cdecl FindPairAttribute(const cGZScriptAttributes *attributes, uint32_t id, int32_t *first, int32_t *second)
{
    LegacyText text;
    return FindTextAttribute(attributes, id, &text.mText) && std::sscanf(text.Chars(), "(%d,%d)", first, second) == 2;
}

bool __cdecl FindColorAttribute(const cGZScriptAttributes *attributes, uint32_t id, uint8_t *red, uint8_t *green,
                                uint8_t *blue)
{
    LegacyText text;
    if (!FindTextAttribute(attributes, id, &text.mText)) return false;
    int r, g, b;
    if (std::sscanf(text.Chars(), "(%d,%d,%d)", &r, &g, &b) != 3) return false;
    *red = static_cast<uint8_t>(r);
    *green = static_cast<uint8_t>(g);
    *blue = static_cast<uint8_t>(b);
    return true;
}

bool __cdecl HasAttribute(const cGZScriptAttributes *attributes, uint32_t id)
{
    const uint32_t count = attributes->Size();
    for (uint32_t i = 0; i < count; ++i)
        if (attributes->mBegin[i].mId == id) return true;
    return false;
}

bool __cdecl LoadLocalizedText(uint32_t group, uint32_t instance, cIGZString *out)
{
    const cGZPersistResourceKey key{kTextResourceType, group, instance, 0};
    auto *const framework = static_cast<cIGZFrameWork *>(GetServiceObject());
    cIGZUIScriptService *service = nullptr;
    if (framework)
        framework->GetSystemService(cGZUIScriptService::kServiceId, cIGZUIScriptService::kIID,
                                    reinterpret_cast<void **>(&service));

    bool loaded = false;
    cIGZUnknown *const context = service ? service->GetContext() : nullptr;
    if (context) {
        loaded = static_cast<cIGZUIScriptContext *>(context)->LoadText(&key, out);
    } else {
        ifaces::cIGZPersistResourceManager *manager = nullptr;
        if (framework) framework->GetSystemService(kResourceManagerId, kResourceManagerIid, reinterpret_cast<void **>(&manager));
        cIGZString *text = nullptr;
        // No test for a missing resource manager, as the original's.
        if (static_cast<uint8_t>(manager->GetPrivateResource(&key, kStringIid, reinterpret_cast<void **>(&text), 0, 0))) {
            *out = *text;
            loaded = true;
        }
        reinterpret_cast<cIGZUnknown *>(manager)->Release(); // slot 2 (the generated interface leaves it unnamed)
        if (text) text->Release();
    }
    if (service) service->Release();
    return loaded;
}

void __cdecl SetResourceKeyProperty(ifaces::cIGZWin *window, uint32_t id, const void *key)
{
    if (window) window->SetProperty(id, new cRZVariant(key, sizeof(cGZPersistResourceKey), kUntypedBytes));
}

namespace {

constexpr uint32_t kKeyBytes = sizeof(cGZPersistResourceKey); // a list entry (16 bytes)
constexpr uint32_t kInlineKeys = 32;                          // entries the original's buffer holds before using the heap

// The original's small-buffer helper 0x87b2ac (a 32-entry array inside the caller's frame, else a heap block). It zeroes
// the entries it hands out; the caller here fills every one of them, so ours does not.
class KeyBuffer {
public:
    KeyBuffer() = default;
    KeyBuffer(const KeyBuffer &) = delete;
    KeyBuffer &operator=(const KeyBuffer &) = delete;
    ~KeyBuffer()
    {
        if (mHeap) ::operator delete(mHeap);
    }
    uint8_t *Reserve(uint32_t entries)
    {
        if (entries < kInlineKeys) return mInline;
        // The size saturates at 0xffffffff when the product overflows (the original's `mul; cmovb`).
        uint64_t bytes = uint64_t(entries) * kKeyBytes;
        if (bytes > 0xffffffffull) bytes = 0xffffffffull;
        mHeap = ::operator new(static_cast<size_t>(bytes));
        return static_cast<uint8_t *>(mHeap);
    }

private:
    uint8_t mInline[kInlineKeys * kKeyBytes];
    void *mHeap = nullptr;
};

} // namespace

bool __cdecl AppendFileBrowserKey(ifaces::cIGZWin *window, uint32_t id, int32_t index, const void *key)
{
    cIGZVariant *list = nullptr;
    if (!window->GetProperty(id, reinterpret_cast<void **>(&list))) {
        cRZVariant *const created = new cRZVariant;
        created->SetVoidArray(key, kKeyBytes);
        window->SetProperty(id, created);
        return true;
    }
    bool inserted = false;
    const int32_t count = static_cast<int32_t>(list->GetCount() / kKeyBytes);
    if (index >= 0 && index <= count) {
        const uint8_t *const old = static_cast<const uint8_t *>(list->GetVoidArray());
        if (old) {
            KeyBuffer buffer;
            uint8_t *const entries = buffer.Reserve(uint32_t(count) + 1);
            const uint32_t before = uint32_t(index) * kKeyBytes;
            if (index > 0) std::memcpy(entries, old, before);
            std::memcpy(entries + before, key, kKeyBytes);
            if (index < count) std::memcpy(entries + before + kKeyBytes, old + before, uint32_t(count - index) * kKeyBytes);
            list->SetValVoid(entries, (uint32_t(count) + 1) * kKeyBytes);
            inserted = true;
        }
    }
    list->Release();
    return inserted;
}

// ---- the common properties --------------------------------------------------------------------------------------------

void __cdecl ApplyCommonProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    if (!apply) {
        ApplyFlagAttributes(window, attributes, std::begin(kLateFlags), std::end(kLateFlags));
        const int32_t count = static_cast<int32_t>(attributes->Size());
        for (int32_t i = 0; i < count; ++i) {
            const cGZScriptAttribute &attribute = attributes->mBegin[i];
            // No test for a null value, as the original's.
            if (attribute.mId == kTypedPropertyAttribute && ValueOf(attribute)->GetType() == kTextVariant)
                ApplyTypedProperty(window, ValueOf(attribute));
        }
        return;
    }

    for (const FlagDefault &flag : kFlagDefaults) window->SetFlag(flag.mask, flag.value);
    ApplyFlagAttributes(window, attributes, std::begin(kApplyFlags), std::end(kApplyFlags));
    uint32_t id = 0;
    if (FindUint32Attribute(attributes, kIdAttribute, &id)) window->SetID(id);

    LegacyText text;
    if (FindTextAttribute(attributes, kAreaAttribute, &text.mText)) {
        // The parse is not checked: a short text leaves the rest of the rectangle as it was (the original's is
        // uninitialised stack; ours starts at 0).
        int32_t area[4] = {};
        std::sscanf(text.Chars(), "( %d, %d, %d, %d)", &area[0], &area[1], &area[2], &area[3]);
        window->SetAnchorArea(area);
    } else {
        int32_t first = 0, second = 0;
        if (FindPairAttribute(attributes, kPositionAttribute, &first, &second)) window->MoveTo(first, second);
        if (FindPairAttribute(attributes, kSizeAttribute, &first, &second)) window->SetSize(first, second);
        window->UpdateAnchors();
    }

    uint8_t red = 0, green = 0, blue = 0;
    if (FindColorAttribute(attributes, kColorAttribute, &red, &green, &blue)) window->SetBackgroundColor(red, green, blue);

    if (FindTextAttribute(attributes, kCaptionAttribute, &text.mText)) window->SetCaption(&text.mText);

    uint32_t captionKey[2] = {0, 0}; // group, instance
    if (FindTextAttribute(attributes, kCaptionKeyAttribute, &text.mText)) {
        std::sscanf(text.Chars(), "{%x,%x}", &captionKey[0], &captionKey[1]);
        LegacyText caption;
        if (LoadLocalizedText(captionKey[0], captionKey[1], caption.mText.AsInterface())) window->SetCaption(&caption.mText);
        const cGZPersistResourceKey key{kTextResourceType, captionKey[0], captionKey[1], 0};
        SetResourceKeyProperty(window, kCaptionKeyProperty, &key);
    }
    window->SetProperty(kCaptionPairProperty, new cRZVariant(captionKey, sizeof(captionKey), kUntypedBytes));

    if (HasAttribute(attributes, kHiddenFlagAttribute)) window->SetFlag(kHiddenFlag, 1);

    LegacyText tip;
    cGZLegacyTextList *const tips = new cGZLegacyTextList;
    tips->AddRef();
    for (uint32_t i = 0; i < kTipCount; ++i) {
        FindTextAttribute(attributes, kFirstTipAttribute + i, &tip.mText); // an empty text when there is none
        tips->mTexts.PushBackAny(tip.mText);
    }
    cRZVariant *const holder = new cRZVariant();
    holder->AddRef();
    holder->SetValUnknown(tips);
    window->SetProperty(kTipsProperty, holder);
    holder->Release();
    tips->Release();
}

// ---- cGZLegacyTextList ------------------------------------------------------------------------------------------------

bool cGZLegacyTextList::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kUnknownIid) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cGZLegacyTextList::AddRef() { return ++mRefCount; }

uint32_t cGZLegacyTextList::Release()
{
    if (mRefCount > 1) return --mRefCount;
    mTexts.Tidy();
    ::operator delete(this, kSize);
    return 0;
}
