// nTSCASCore::cTSCASBodyPartSystem (lane JY): the text-driven slots: the record texts (slot 66) and the outfit
// properties applied from a property set (slot 28).
#include "cas/cTSCASBodyPartSystem.h"

#include "game/original.h"
#include "game/vtable.h"
#include "misc/HexParse.h"
#include "misc/UtilitiesE.h"
#include "rz/cRZIniFile.h"

#include <cstdlib>

namespace nTSCASCore {

namespace {

// The image's texts (read where they are rather than copied).
constexpr uint32_t kRvaListDelimiter = 0xdaa6c0;  // one character
constexpr uint32_t kRvaHairTone = 0xeb9f50;       // property names
constexpr uint32_t kRvaSkinTone = 0xeb9f5c;
constexpr uint32_t kRvaOverlayCount = 0xdbc964;
constexpr uint32_t kRvaOverlayFormat = 0xda6db8;  // the overlay property's name from a prefix and an index
constexpr uint32_t kRvaOverlayPrefix = 0xdbc974;

const char *Text(uint32_t rva)
{
    return GameAddress<const char>(rva);
}

// A property set's value by name (its slot 24), null when it has none.
const char *Property(void *properties, const char *name)
{
    return VirtualCall<const char *(__thiscall *)(void *, const char *), 24>(properties, name);
}


// SplitOff's out: raw memory until the callee constructs it.
struct SplitText {
    union {
        cRZString mText;
    };
    SplitText() {}
    SplitText(const SplitText &) = delete;
    ~SplitText() { mText.Destruct(); }
};

} // namespace

// ---- Slot 66: the record texts ----------------------------------------------------------------------------------------

void BodyPartSystemIface::CollectRecordTexts(ServiceStringVector *out)
{
    out->clear();
    const char *const delimiter = Text(kRvaListDelimiter);
    for (size_t i = 0; i < mRecords->size(); ++i) {
        const SurfaceRecord &record = (*mRecords)[i];
        LocalRZString text;
        std::string &s = text.mText.mString;
        for (int k = 0; k < 4; ++k) {
            if (k != 0) s.append(delimiter, 1);
            s.append(record.mText[k].c_str(), record.mText[k].size());
        }
        out->push_back(ServiceString(text.mText.ToChar()));
    }
}

// ---- Slot 28: outfit properties ---------------------------------------------------------------------------------------

void BodyPartSystemIface::ApplyOutfitProperties(uint32_t id, uint8_t reset)
{
    if (reset != 0) VirtualCall<void(__thiscall *)(void *), 16>(this);
    void *const queried = GetCachedQueriedObject();
    const uint32_t key = VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 19>(queried, id);
    MaterialEntryRef set; // the whole 0x14 bytes: slot 33 writes all of them
    VirtualCall<void(__thiscall *)(void *, MaterialEntryRef *, uint32_t), 33>(queried, &set, key);
    uint8_t guid[16];
    if (const char *hair = Property(set.mProperties, Text(kRvaHairTone))) {
        hexparse::ParseGuidText(guid, hair);
        VirtualCall<void(__thiscall *)(void *, const void *), 74>(this, guid);
    }
    if (const char *skin = Property(set.mProperties, Text(kRvaSkinTone))) {
        hexparse::ParseGuidText(guid, skin);
        VirtualCall<void(__thiscall *)(void *, const void *), 54>(this, guid);
    }
    const char *const countText = Property(set.mProperties, Text(kRvaOverlayCount));
    if (countText == nullptr) return;
    const int32_t count = std::atoi(countText);
    for (int32_t i = 0; i < count; ++i) {
        LocalRZString name;
        name.mText.Sprintf(Text(kRvaOverlayFormat), Text(kRvaOverlayPrefix), i);
        const char *const value = Property(set.mProperties, name.mText.mString.c_str());
        if (value == nullptr) continue;
        LocalRZString list(value);
        SplitText first;
        reinterpret_cast<cRZIniStringHelpers *>(&list.mText)->SplitOff(&first.mText, Text(kRvaListDelimiter));
        VirtualCall<void(__thiscall *)(void *, const char *, uint32_t, uint32_t), 38>(this, first.mText.ToChar(), 1, 0);
    }
}

} // namespace nTSCASCore
