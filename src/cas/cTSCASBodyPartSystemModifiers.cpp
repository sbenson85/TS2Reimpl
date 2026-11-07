// nTSCASCore::cTSCASBodyPartSystem (lane JZ): slot 41, which rebuilds the morph part of the modifier stack from a list
// of modifier texts. The face's `this` is the subobject at +0x1c; see cTSCASBodyPartSystem.h.
#include "cas/cTSCASBodyPartSystem.h"

#include "game/original.h"
#include "game/vtable.h"

#include <cstring>
#include <string>

namespace nTSCASCore {

namespace {

cTSCASBodyPartSystem *SystemOf(BodyPartSystemIface *iface)
{
    return reinterpret_cast<cTSCASBodyPartSystem *>(reinterpret_cast<uint8_t *>(iface) - 0x1c);
}

constexpr uint32_t kRvaEmptyText = 0xd9b9d0;   // the image's empty string
constexpr uint32_t kRvaAllRegions = 0xeba5c0;  // "body;brow;...": the group naming every region (29 characters)
constexpr size_t kAllRegionsLength = 0x1d;

// Kinds the modifier text parser 0xb5ebe0 reports.
enum ModifierKind : int32_t {
    kModifierMorph = 1,
    kModifierWeightMerged = 2,
    kModifierGroup = 3,
    kModifierWeight = 4,
};

void RemoveMorphs(cTSCASBodyPartSystem *system)
{
    VirtualCall<void(__thiscall *)(void *), 13>(system); // face-0 slot 13: RemoveMorphRecords
}
void PushGroup(BodyPartSystemIface *self, const char *name)
{
    VirtualCall<void(__thiscall *)(void *, const char *), 47>(self, name); // PushKind0
}

} // namespace

void BodyPartSystemIface::ApplyModifierTexts(const StringVector *texts)
{
    cTSCASBodyPartSystem *const system = SystemOf(this);
    std::string group; // the last group pushed
    RemoveMorphs(system);
    PushGroup(this, GameAddress<const char>(kRvaEmptyText));
    for (const cRZString *text = texts->mFirst; text != texts->mLast; ++text) {
        LocalRZString name;
        int32_t kind = 0;
        float weight = 0.0f;
        if (!OriginalFunction<bool(__cdecl *)(const cRZString *, cRZString *, int32_t *, float *), 0xb5ebe0>()(text, &name.mText, &kind,
                                                                                                          &weight)) {
            continue;
        }
        std::string &n = name.mText.mString;
        switch (kind) {
        case kModifierMorph:
            if (group.empty() && weight >= 1.0f) {
                RemoveMorphs(system);
                PushGroup(this, GameAddress<const char>(kRvaEmptyText));
            }
            VirtualCall<void(__thiscall *)(void *, const char *, float, uint8_t), 48>(this, n.c_str(), weight, 0); // AddMorph
            break;
        case kModifierWeight:
        case kModifierWeightMerged:
            OriginalFunction<void(__thiscall *)(void *, const char *, float, uint8_t), 0xcf5930>()(system, n.c_str(), weight,
                                                                                                  kind == kModifierWeightMerged);
            break;
        case kModifierGroup:
            if (n.size() == kAllRegionsLength && std::memcmp(n.data(), GameAddress<const char>(kRvaAllRegions), kAllRegionsLength) == 0) {
                n.clear();
            }
            if (group != n) {
                PushGroup(this, n.c_str());
                group = n;
            }
            break;
        default:
            break;
        }
    }
}

} // namespace nTSCASCore
