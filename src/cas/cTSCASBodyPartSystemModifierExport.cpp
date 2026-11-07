// nTSCASCore::cTSCASBodyPartSystem (lane KA): slot 42, which writes the modifier stack out as modifier texts, the
// inverse of slot 41 (cTSCASBodyPartSystemModifiers.cpp). The face's `this` is the subobject at +0x1c; see
// cTSCASBodyPartSystem.h.
#include "cas/cTSCASBodyPartSystem.h"

#include "game/original.h"
#include "game/vtable.h"

#include <string>
#include <utility>
#include <vector>

namespace nTSCASCore {

namespace {

// The codes a modifier text carries (what slot 41's parser 0xb5ebe0 reads back).
constexpr int32_t kCodeMorph = 1;       // record kind 1
constexpr int32_t kCodeWeight = 2;      // record kind 2
constexpr int32_t kCodeGroup = 3;       // record kind 0
constexpr int32_t kCodeMergedWeight = 4; // record kind 7

struct ModifierText {
    std::string mName;
    int32_t mCode;
    float mWeight;
};

// The game vector's strings destroyed in place (their slot 33, flags 0) and the vector emptied.
void ClearStrings(StringVector *strings)
{
    if (strings->mFirst == strings->mLast) return;
    for (cRZString *s = strings->mFirst; s != strings->mLast; ++s) VirtualCall<void(__thiscall *)(void *, uint32_t), 33>(s, 0);
    strings->mLast = strings->mFirst;
}

} // namespace

void BodyPartSystemIface::ExportModifierTexts(StringVector *out)
{
    ClearStrings(out);
    std::vector<ModifierText> texts;
    std::string group; // the last group's name: a group equal to it is not repeated
    for (const BodyPartEntry *e = mStack->mFirst; e != mStack->mLast; ++e) {
        if (e->mWeight == 0.0f && e->mKind != 0) continue;
        std::string name(e->Name());
        int32_t code;
        switch (e->mKind) {
        case 1:
            code = kCodeMorph;
            // A full-weight morph before any group starts the list again.
            if (e->mWeight >= 1.0f && group.empty()) texts.clear();
            break;
        case 7: code = kCodeMergedWeight; break;
        case 2: code = kCodeWeight; break;
        case 0:
            if (name == group) continue;
            code = kCodeGroup;
            group = name;
            break;
        default: continue;
        }
        texts.push_back({std::move(name), code, e->mWeight});
    }
    for (const ModifierText &t : texts) {
        LocalRZString name(t.mName.c_str());
        LocalRZString text;
        // "%s:%d:%f" through the text's Sprintf; always true.
        if (OriginalFunction<bool(__cdecl *)(const cRZString *, const int32_t *, const float *, cRZString *), 0xb5f0b0>()(
                &name.mText, &t.mCode, &t.mWeight, &text.mText))
            OriginalFunction<void(__thiscall *)(StringVector *, const cRZString *), 0x3a05e0>()(out, &text.mText);
    }
}

} // namespace nTSCASCore
