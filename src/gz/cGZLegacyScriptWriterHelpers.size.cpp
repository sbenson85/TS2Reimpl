// The legacy script writer's formatting helpers (lane FI, foundations): src/gz/cGZLegacyScriptWriter.h. Built for size,
// as the game's.
#include "gz/cGZLegacyScriptWriter.h"

#include "gz/cGZLegacyControl.h" // LegacyText
#include "gz/cGZLegacyHandlersInternal.h" // UIDefaults
#include "ifaces/cIGZWin.h"
#include "rz/cIGZVariant.h"

#include <cstring>

namespace {

constexpr char kYes[] = "yes";
constexpr char kNo[] = "no";
constexpr uint32_t kTextVariant = 0x800e;
constexpr uint32_t kUint32Variant = 6;
constexpr uint32_t kUntypedBit = 0x8000;
constexpr uint32_t kUnnamedFontId = 0x68963c4c; // the one font id written as nothing when the defaults have no name
constexpr uint32_t kFontProperty = 0xfaa3be85;
constexpr uint32_t kImagePairProperty = 0xdaa3b860;
constexpr uint32_t kKeySize = 16;

// Storage for a cRZString another function constructs (EscapeScriptText); the holder destroys it explicitly.
struct RawText {
    alignas(cRZString) unsigned char bytes[sizeof(cRZString)];
    cRZString *Get() { return reinterpret_cast<cRZString *>(bytes); }
};

const char *Chars(const cRZString *s) { return s->mString.c_str(); }

// The formatted text of one attribute, appended to the script's std::string (as the original: never through the
// script's cIGZString slots).
template <class... Args> void AppendFormatted(cRZString *out, const char *format, Args... args)
{
    LegacyText piece;
    piece.mText.Sprintf(format, args...);
    out->mString.append(piece.mText.mString);
}

// A UI defaults font entry (slot 37's answer): slot 5 gives its name.
class cIGZUIFontEntry {
public:
    virtual void vf0() = 0;
    virtual void vf1() = 0;
    virtual void vf2() = 0;
    virtual void vf3() = 0;
    virtual void vf4() = 0;
    virtual const char *GetName() = 0; // 5
};

cIGZUIFontEntry *FontEntry(uint32_t fontId)
{
    return static_cast<cIGZUIFontEntry *>(UIDefaults()->FindFontEntry(fontId));
}

} // namespace

void __cdecl AppendTextAttribute(cRZString *out, const cRZString *name, const cRZString *value)
{
    AppendFormatted(out, "%s=%s ", Chars(name), Chars(value));
}

void __cdecl AppendFlagAttributes(cRZString *out, uint32_t flags, uint32_t defaults, const LegacyFlagName *table)
{
    const uint32_t changed = flags ^ defaults;
    for (const LegacyFlagName *row = table; row->name; ++row) {
        if (!(row->mask & changed) && !row->always) continue;
        LegacyText value;
        value.mText.FromChar((row->mask & flags) ? kYes : kNo);
        LegacyText name;
        name.mText.FromChar(row->name);
        AppendTextAttribute(out, &name.mText, &value.mText);
    }
}

void __cdecl AppendBoolAttribute(cRZString *out, const cRZString *name, bool value)
{
    AppendFormatted(out, "%s=%s ", Chars(name), value ? kYes : kNo);
}

void __cdecl AppendChangedBoolAttribute(cRZString *out, const cRZString *name, bool value, bool defaultValue)
{
    if (value == defaultValue) return;
    AppendFormatted(out, "%s=%s ", Chars(name), value ? kYes : kNo);
}

void __cdecl AppendColorAttribute(cRZString *out, const cRZString *name, uint8_t r, uint8_t g, uint8_t b)
{
    AppendFormatted(out, "%s=(%u,%u,%u) ", Chars(name), uint32_t(r), uint32_t(g), uint32_t(b));
}

void __cdecl AppendColor2Attribute(cRZString *out, const cRZString *name, uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1,
                                   uint8_t g1, uint8_t b1)
{
    AppendFormatted(out, "%s=(%u,%u,%u)(%u,%u,%u) ", Chars(name), uint32_t(r0), uint32_t(g0), uint32_t(b0), uint32_t(r1),
                    uint32_t(g1), uint32_t(b1));
}

void __cdecl AppendColor4Attribute(cRZString *out, const cRZString *name, uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1,
                                   uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2, uint8_t r3, uint8_t g3,
                                   uint8_t b3)
{
    AppendFormatted(out, "%s=(%u,%u,%u)(%u,%u,%u)(%u,%u,%u)(%u,%u,%u) ", Chars(name), uint32_t(r0), uint32_t(g0),
                    uint32_t(b0), uint32_t(r1), uint32_t(g1), uint32_t(b1), uint32_t(r2), uint32_t(g2), uint32_t(b2),
                    uint32_t(r3), uint32_t(g3), uint32_t(b3));
}

void __cdecl AppendHexAttribute(cRZString *out, const cRZString *name, uint32_t value)
{
    AppendFormatted(out, "%s=0x%08x ", Chars(name), value);
}

void __cdecl AppendKeyAttribute(cRZString *out, const cRZString *name, uint32_t group, uint32_t instance)
{
    AppendFormatted(out, "%s={%08x,%08x} ", Chars(name), group, instance);
}

void __cdecl AppendIntAttribute(cRZString *out, const cRZString *name, int32_t value)
{
    AppendFormatted(out, "%s=%d ", Chars(name), value);
}

void __cdecl AppendImageItemAttribute(cRZString *out, const cRZString *name, uint32_t id, const cRZString *text,
                                      uint32_t flags, uint32_t group, uint32_t instance, int32_t left, int32_t top,
                                      int32_t right, int32_t bottom)
{
    LegacyText piece;
    {
        RawText escaped;
        EscapeScriptText(escaped.Get(), text);
        if (group && instance)
            piece.mText.Sprintf("%s=\"%u:%s:%u:{%08x,%08x}:(%d,%d,%d,%d)\" ", Chars(name), id, Chars(escaped.Get()), flags,
                                group, instance, left, top, right, bottom);
        else
            piece.mText.Sprintf("%s=\"%u:%s:%u::\" ", Chars(name), id, Chars(escaped.Get()), flags);
        escaped.Get()->Destruct();
    }
    out->mString.append(piece.mText.mString);
}

void __cdecl AppendNumberedPairAttribute(cRZString *out, const cRZString *name, uint32_t id, int32_t x, int32_t y)
{
    AppendFormatted(out, "%s=\"%u:(%d,%d)\" ", Chars(name), id, x, y);
}

void __cdecl AppendPairAttribute(cRZString *out, const cRZString *name, int32_t x, int32_t y)
{
    AppendFormatted(out, "%s=(%d,%d) ", Chars(name), x, y);
}

void __cdecl AppendRectAttribute(cRZString *out, const cRZString *name, int32_t left, int32_t top, int32_t right,
                                 int32_t bottom)
{
    AppendFormatted(out, "%s=(%d,%d,%d,%d) ", Chars(name), left, top, right, bottom);
}

void __cdecl AppendQuotedAttribute(cRZString *out, const cRZString *name, const cRZString *text)
{
    out->mString.append(name->mString);
    out->mString.append("=\"", 2);
    RawText escaped;
    EscapeScriptText(escaped.Get(), text);
    out->mString.append(escaped.Get()->mString);
    escaped.Get()->Destruct();
    out->mString.append("\" ", 2);
}

void __cdecl AppendKey3Attribute(cRZString *out, const cRZString *name, const uint32_t *key)
{
    AppendFormatted(out, "%s={%08x,%08x,%08x} ", Chars(name), key[0], key[1], key[2]);
}

void __cdecl AppendWordAttribute(cRZString *out, const cRZString *name)
{
    out->mString.append(name->mString);
    out->mString.append(" ", 1);
}

cRZString *__cdecl EscapeScriptText(cRZString *out, const cRZString *text)
{
    static constexpr char kSpecial[] = "&<>'\"";
    out->Construct(*text, 0, uint32_t(-1));
    std::string &s = out->mString;
    for (size_t at = s.find_first_of(kSpecial, 0); at != std::string::npos;) {
        const char *entity;
        switch (s[at]) {
        case '"': entity = "&quot;"; break;
        case '&': entity = "&amp;"; break;
        case '\'': entity = "&apos;"; break;
        case '<': entity = "&lt;"; break;
        case '>': entity = "&gt;"; break;
        default: entity = nullptr; break;
        }
        size_t step = 1;
        if (entity) {
            step = std::strlen(entity);
            s.replace(at, 1, entity, step);
        }
        at = s.find_first_of(kSpecial, at + step);
    }
    return out;
}

bool __cdecl FormatFontId(uint32_t fontId, cIGZString *out)
{
    if (cIGZUIFontEntry *font = FontEntry(fontId)) {
        out->Sprintf("%s", font->GetName());
        return true;
    }
    if (fontId == kUnnamedFontId) return false;
    out->Sprintf("0x%08x", fontId);
    return true;
}

bool __cdecl FormatFontProperty(ifaces::cIGZWin *window, cIGZString *out, int32_t index)
{
    cIGZVariant *value = nullptr;
    if (!window->GetProperty(kFontProperty + index, &value)) return false;
    bool written = false;
    if (value->GetType() == kUint32Variant) {
        const uint32_t fontId = *value->AsUint32Ref();
        if (cIGZUIFontEntry *font = FontEntry(fontId))
            out->Sprintf("%s", font->GetName());
        else
            out->Sprintf("%d", fontId);
        written = true;
    } else if (value->GetType() == kTextVariant) {
        value->GetValString(*out, 0, ' ');
        written = true;
    }
    value->Release();
    return written;
}

bool __cdecl GetImagePairProperty(ifaces::cIGZWin *window, uint32_t *group, uint32_t *instance, int32_t index)
{
    cIGZVariant *value = nullptr;
    if (!window->GetProperty(kImagePairProperty + index, &value)) return false;
    bool found = false;
    if (value->GetType() & kUntypedBit) {
        const uint32_t *words = value->GetUint32Array();
        found = true;
        *group = words[0];
        *instance = words[1];
    }
    value->Release();
    return found;
}

bool __cdecl GetKeyProperty(ifaces::cIGZWin *window, uint32_t id, uint32_t *key)
{
    if (!window) return false;
    cIGZVariant *value;
    if (!window->GetProperty(id, &value)) return false;
    const bool found = value->GetCount() == kKeySize;
    if (found) {
        uint32_t size = kKeySize;
        value->GetValVoid(key, &size);
    }
    value->Release();
    return found;
}

bool __cdecl GetKeyListEntry(ifaces::cIGZWin *window, uint32_t id, int32_t index, uint32_t *entry)
{
    if (!window) return false;
    cIGZVariant *value;
    if (!window->GetProperty(id, &value)) return false;
    bool found = false;
    if (value->GetType() == kUntypedBit) {
        const uint32_t count = value->GetCount() / kKeySize;
        if (index >= 0 && index < int32_t(count)) {
            const uint32_t *at = static_cast<const uint32_t *>(value->GetVoidArray()) + index * 4;
            found = true;
            entry[0] = at[0];
            entry[1] = at[1];
            entry[2] = at[2];
            entry[3] = at[3];
        }
    }
    value->Release();
    return found;
}
