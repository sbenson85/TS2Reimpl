// The legacy script writer's common attributes (lane WA, foundations): src/gz/cGZLegacyScriptWriter.h. Built for size, as
// the game's.
#include "gz/cGZLegacyScriptWriter.h"

#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZLegacyScriptWriterInternal.h"
#include "ifaces/cIGZWin.h"

#include <cstdint>

namespace {

using legacy_script::AsWin;
using legacy_script::Of;
using legacy_script::Text;

constexpr uint32_t kCaptionResourceProperty = 0x0b31771a;
constexpr uint32_t kTooltipProperty = 0xbaca6879;
constexpr uint32_t kTooltipSourceHasTexts = 0x10;

// A copy of a counted text, the way the original builds it (0x144f2a), destroyed at the end of the scope.
struct CopiedText {
    union {
        cRZString mText;
    };
    explicit CopiedText(const cIGZString &source) { mText.Construct(source); }
    CopiedText(const CopiedText &) = delete;
    CopiedText &operator=(const CopiedText &) = delete;
    ~CopiedText() { mText.Destruct(); }
};

// What the window's tooltip property answers (iid 0xbaca6879): a counted source whose slot 3 gives flags (0x10: has
// texts) and whose slot 62 (1, &set) gives a counted set; the set holds, at +4, eight cRZStrings 0x20 bytes apart.
struct TooltipSet {
    void *vtable;
    cRZString *texts; // +4
};

// The tooltip texts' attribute names, in the order of the texts.
constexpr const char *kTooltipNames[8] = {"comments",    "commentslmd", "commentsrmd", "commentslmu",
                                          "commentsrmu", "commentsmm",  "commentsku",  "commentskd"};

// The yes/no flags the window writes whatever their value (mask and attribute name), then the ones written only when
// they are set (the default is "no").
struct WindowFlag {
    uint32_t mask;
    const char *name;
};
constexpr WindowFlag kAlwaysFlags[] = {
    {0x1, "winflag_visible"},          {0x2, "winflag_enabled"},        {0x100, "winflag_moveable"},
    {0x800, "winflag_sortable"},       {0x10000, "winflag_pbuff"},      {0x20000, "winflag_pbufftrans"},
    {0x40000, "winflag_pbufferase"},   {0x4, "winflag_alphablend"},     {0x8000, "winflag_acceptfocus"},
    {0x80000, "winflag_mousetrans"},   {0x200000, "winflag_ignoremouse"},
};
constexpr WindowFlag kChangedFlags[] = {
    {0x40, "winflag_premulalpha"},       {0x8000000, "winflag_delayedplot"},   {0x80, "winflag_container"},
    {0x20, "winflag_fadeeffects"},       {0x10, "winflag_relativeright"},      {0x2000000, "winflag_relativewidth"},
    {0x800000, "winflag_relativebottom"}, {0x1000000, "winflag_relativeheight"},
};

// Slot 128: calls `visitor(window, id, value, context)` for each property of the window until one answers false.
using PropertyVisitor = bool(__cdecl *)(void *window, uint32_t id, void *value, void *context);

} // namespace

static_assert(sizeof(cRZString) == 0x20, "the tooltip texts are 0x20 bytes apart");

void __cdecl WriteWindowAttributes(ifaces::cIGZWin *window, cRZString *out)
{
    if (window->GetID() != 0) AppendHexAttribute(out, Of(Text("id")), window->GetID());

    // The area: the getters are called bottom first.
    const int32_t bottom = window->GetBottom();
    const int32_t right = window->GetRight();
    const int32_t top = window->GetTop();
    const int32_t left = window->GetLeft();
    AppendRectAttribute(out, Of(Text("area")), left, top, right, bottom);

    // Slot 117 gives three bytes (the fill colour) and its answer is none.
    uint8_t red, green, blue;
    RawSlot<117, void>(window, &red, &green, &blue);
    AppendColorAttribute(out, Of(Text("fillcolor")), red, green, blue);

    if (RawSlot<89, cIGZString *>(window)->Strlen() != 0) {
        CopiedText caption(*RawSlot<89, cIGZString *>(window));
        AppendQuotedAttribute(out, Of(Text("caption")), &caption.mText);
    }

    uint32_t resource[4] = {};
    if (GetKeyProperty(window, kCaptionResourceProperty, resource) && (resource[2] != 0 || resource[1] != 0))
        AppendKeyAttribute(out, Of(Text("captionres")), resource[1], resource[2]);

    // The tooltip texts: the property's source, when it has texts, gives a set of eight; each non-empty one is written.
    void *source = nullptr;
    if (window->GetProperty(kTooltipProperty, &source)) {
        if (RawSlot<3>(source) & kTooltipSourceHasTexts) {
            TooltipSet *set = nullptr;
            if (RawSlot<62, bool>(source, uint32_t(1), &set)) {
                for (int i = 0; i < 8; ++i) {
                    cRZString *tooltipText = reinterpret_cast<cRZString *>(reinterpret_cast<uint8_t *>(set->texts) + 0x20 * i);
                    if (tooltipText->Strlen() != 0) AppendQuotedAttribute(out, Of(Text(kTooltipNames[i])), tooltipText);
                }
                static_cast<cIGZUnknown *>(static_cast<void *>(set))->Release();
            }
        }
        static_cast<cIGZUnknown *>(source)->Release();
    }

    RawSlot<128, bool>(window, OriginalAddress<PropertyVisitor, 0x86e7aa>(), out);

    for (const WindowFlag &flag : kAlwaysFlags) {
        Text name(flag.name);
        AppendBoolAttribute(out, Of(name), window->IsFlagSet(flag.mask));
    }
    for (const WindowFlag &flag : kChangedFlags) {
        Text name(flag.name);
        AppendChangedBoolAttribute(out, Of(name), window->IsFlagSet(flag.mask), false);
    }
}
