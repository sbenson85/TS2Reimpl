#include "app/StaticInit.h"
#include "app/StaticInitAllocators.h"
#include "app/StaticInitAny.h"
#include "app/StaticInitAtExit.h"
#include "app/StaticInitConstructed.h"
#include "app/StaticInitContainers.h"
#include "app/StaticInitParser.h"
#include "app/StaticInitH5.h"
#include "app/StaticInitHT.h"

#include "game/game.h"
#include "rz/StringKeyParser.h"

#include <algorithm>
#include <array>
#include <vector>

namespace staticinit {

namespace {

constexpr uint32_t kKeyTag = 0xff000000; // the top byte every key here carries

// Slot order. Storage and initialiser RVAs from db/initializers.jsonl (kind "key-hash"); each entry's code was
// checked against the one instruction pattern of its form.
constexpr NameKey kNameKeys[] = {
    {384, 0x13d9ab, 0x10de520, "ui_primary_button"},
    {385, 0x13da0f, 0x10de524, "ui_secondary_button"},
    {386, 0x13da5a, 0x10de528, "ui_tertiary_button"},
    {387, 0x13d8e3, 0x10de52c, "ui_pane_primary"},
    {388, 0x13d979, 0x10de530, "ui_piemenu_primary"},
    {389, 0x13d992, 0x10de534, "ui_piemenu_secondary"},
    {390, 0x13d9c4, 0x10de538, "ui_queue"},
    {391, 0x13d9dd, 0x10de53c, "ui_queue_delete"},
    {392, 0x13d802, 0x10de540, "ui_error"},
    {393, 0x13dad7, 0x10de544, "ui_undo"},
    {394, 0x13d9f6, 0x10de548, "ui_redo"},
    {395, 0x13d627, 0x10de54c, "ui_button_ffw"},
    {396, 0x13d640, 0x10de550, "ui_button_fw"},
    {397, 0x13d659, 0x10de554, "ui_button_pause"},
    {398, 0x13d672, 0x10de558, "ui_button_play"},
    {399, 0x13d68b, 0x10de55c, "ui_button_rotate_lp"},
    {400, 0x13d6a4, 0x10de560, "ui_button_zoom_in"},
    {401, 0x13d6bd, 0x10de564, "ui_button_zoom_out"},
    {402, 0x13d947, 0x10de568, "ui_panel_open"},
    {403, 0x13d8fc, 0x10de56c, "ui_panel_close"},
    {404, 0x13d92e, 0x10de570, "ui_panel_expand"},
    {405, 0x13d915, 0x10de574, "ui_panel_collapse"},
    {406, 0x13d960, 0x10de578, "ui_panel_shift"},
    {407, 0x13dabe, 0x10de57c, "ui_text_notification_open"},
    {408, 0x13daa5, 0x10de580, "ui_text_notification_close"},
    {409, 0x13da8c, 0x10de584, "ui_text_description_open"},
    {410, 0x13da73, 0x10de588, "ui_text_description_close"},
    {411, 0x13da41, 0x10de58c, "ui_softwindow_open"},
    {412, 0x13da28, 0x10de590, "ui_softwindow_close"},
    {413, 0x13d84d, 0x10de594, "ui_hardwindow_open"},
    {414, 0x13d834, 0x10de598, "ui_hardwindow_close"},
    {415, 0x13d352, 0x10de59c, "ui_album_open"},
    {416, 0x13d339, 0x10de5a0, "ui_album_close"},
    {417, 0x13d6d6, 0x10de5a4, "ui_cas_camera"},
    {418, 0x13d7e9, 0x10de5a8, "ui_cas_randomize_mup"},
    {419, 0x13d753, 0x10de5ac, "ui_cas_personalityone"},
    {420, 0x13d7d0, 0x10de5b0, "ui_cas_personalitytwo"},
    {421, 0x13d7b7, 0x10de5b4, "ui_cas_personalitythree"},
    {422, 0x13d721, 0x10de5b8, "ui_cas_personalityfour"},
    {423, 0x13d708, 0x10de5bc, "ui_cas_personalityfive"},
    {424, 0x13d785, 0x10de5c0, "ui_cas_personalitysix"},
    {425, 0x13d76c, 0x10de5c4, "ui_cas_personalityseven"},
    {426, 0x13d6ef, 0x10de5c8, "ui_cas_personalityeight"},
    {427, 0x13d73a, 0x10de5cc, "ui_cas_personalitynine"},
    {428, 0x13d79e, 0x10de5d0, "ui_cas_personalityten"},
    {429, 0x13d866, 0x10de5d4, "ui_object_buy"},
    {430, 0x13d8ca, 0x10de5d8, "ui_object_sell"},
    {431, 0x13d898, 0x10de5dc, "ui_object_plop"},
    {432, 0x13d8b1, 0x10de5e0, "ui_object_rotate"},
    {433, 0x13d87f, 0x10de5e4, "ui_object_pickup"},
    {434, 0x13d52d, 0x10de5e8, "ui_build_terrain_lp"},
    {435, 0x13d39d, 0x10de5ec, "ui_build_deck_mdown"},
    {436, 0x13d3b6, 0x10de5f0, "ui_build_deck_mup"},
    {437, 0x13d3cf, 0x10de5f4, "ui_build_deck_section"},
    {438, 0x13d3e8, 0x10de5f8, "ui_build_door_green"},
    {439, 0x13d401, 0x10de5fc, "ui_build_door_plop"},
    {440, 0x13d41a, 0x10de600, "ui_build_flooring_plop"},
    {441, 0x13d433, 0x10de604, "ui_build_garden_plop"},
    {442, 0x13d44c, 0x10de608, "ui_build_pool_mdown"},
    {443, 0x13d465, 0x10de60c, "ui_build_pool_mup"},
    {444, 0x13d497, 0x10de610, "ui_build_roof_mdown"},
    {445, 0x13d4b0, 0x10de614, "ui_build_roof_mup"},
    {446, 0x13d4c9, 0x10de618, "ui_build_stair_green"},
    {447, 0x13d4e2, 0x10de61c, "ui_build_stair_plop"},
    {448, 0x13d47e, 0x10de620, "ui_build_rail_plop"},
    {449, 0x13d4fb, 0x10de624, "ui_build_terrain_level_mdown"},
    {450, 0x13d514, 0x10de628, "ui_build_terrain_level_mup"},
    {451, 0x13d546, 0x10de62c, "ui_build_terrain_paint_lp"},
    {452, 0x13d5aa, 0x10de630, "ui_build_wallcovering_appear"},
    {453, 0x13d5c3, 0x10de634, "ui_build_wallcovering_plop"},
    {454, 0x13d5dc, 0x10de638, "ui_build_walldelete_mdown"},
    {455, 0x13d5f5, 0x10de63c, "ui_build_walldelete_section"},
    {456, 0x13d55f, 0x10de640, "ui_build_wall_mdown"},
    {457, 0x13d578, 0x10de644, "ui_build_wall_mup"},
    {458, 0x13d591, 0x10de648, "ui_build_wall_section"},
    {459, 0x13d60e, 0x10de64c, "ui_build_water_lp"},
    {460, 0x13d81b, 0x10de650, "ui_flatten_lot"},
    {461, 0x13d384, 0x10de654, "ui_aspiration_powerup"},
    {462, 0x13d36b, 0x10de658, "ui_aspiration_powerdown"},
    {463, 0x13d307, 0x10de65c, "sting_datemeter_bad"},
    {464, 0x13d320, 0x10de660, "sting_datemeter_good"},
};

constexpr NameKeyPair kNameKeyPairs[] = {
    {115, 0x13852c, 0x10cc428, "Listener"},
    {116, 0x138594, 0x10cc470, "stopallsounds"},
    {117, 0x138560, 0x10cc478, "startmusicdipping"},
    {118, 0x1385c8, 0x10cc480, "stopmusicdipping"},
    {119, 0x1385fc, 0x10cc4a0, "musicCategories"},
    {120, 0x138630, 0x10cc4c8, "SoundManager"},
    {121, 0x138664, 0x10cc540, "AmbienceSceneNodeConstants"},
    {122, 0x138698, 0x10cc548, "PrimitiveClassIds"},
    {123, 0x1386cc, 0x10cc550, "AmbienceSystem"},
    {124, 0x138700, 0x10cc570, "events"},
    {127, 0x13876a, 0x10cc690, "defaultlayer"},
    {128, 0x13879e, 0x10cc6d8, "aliases"},
    {129, 0x1387d2, 0x10cc6e0, "shoetypes"},
    {130, 0x138806, 0x10cc6e8, "stringRanges"},
    {329, 0x13caf9, 0x10dc6f0, "build"},
    {330, 0x13cb2d, 0x10dc6f8, "buy"},
    {331, 0x13cb95, 0x10dc700, "nhood"},
    {332, 0x13cb61, 0x10dc708, "cas"},
    {333, 0x13cbc9, 0x10dc798, "build"},
    {334, 0x13cbfd, 0x10dc7a0, "buy"},
    {335, 0x13cc65, 0x10dc7a8, "nhood"},
    {336, 0x13cc31, 0x10dc7b0, "cas"},
    {377, 0x13d1f5, 0x10de038, "build"},
    {378, 0x13d229, 0x10de040, "buy"},
    {379, 0x13d291, 0x10de048, "nhood"},
    {380, 0x13d25d, 0x10de050, "cas"},
    {480, 0x13dd51, 0x10deaa0, "build"},
    {481, 0x13dd85, 0x10deaa8, "buy"},
    {482, 0x13dded, 0x10deab0, "nhood"},
    {483, 0x13ddb9, 0x10deab8, "cas"},
};

constexpr size_t kNameKeyCount = std::size(kNameKeys);
constexpr size_t kNameKeyPairCount = std::size(kNameKeyPairs);

// Both tables merged by slot: the skip list.
constexpr auto kOwned = [] {
    std::array<winmainstart::OwnedInitialiser, kNameKeyCount + kNameKeyPairCount> out{};
    size_t a = 0, b = 0;
    for (auto &o : out) {
        if (b == kNameKeyPairCount || (a < kNameKeyCount && kNameKeys[a].slot < kNameKeyPairs[b].slot)) {
            o = {kNameKeys[a].slot, kNameKeys[a].imageRva, &InitNameKey, &kNameKeys[a]};
            ++a;
        } else {
            o = {kNameKeyPairs[b].slot, kNameKeyPairs[b].imageRva, &InitNameKeyPair, &kNameKeyPairs[b]};
            ++b;
        }
    }
    return out;
}();

constexpr bool SortedBySlot()
{
    for (size_t i = 1; i < kOwned.size(); ++i) {
        if (kOwned[i - 1].slot >= kOwned[i].slot) return false;
    }
    return true;
}
static_assert(SortedBySlot(), "owned XC entries: one per slot, in slot order");

} // namespace

void __cdecl InitNameKey(const void *nameKey)
{
    const auto *key = static_cast<const NameKey *>(nameKey);
    *GameAddress<uint32_t>(key->storage) = StringKeyParser::Crc24(key->name, 1) | kKeyTag;
}

void __cdecl InitNameKeyPair(const void *nameKeyPair)
{
    const auto *key = static_cast<const NameKeyPair *>(nameKeyPair);
    uint32_t first = StringKeyParser::Crc24(key->name, 0) | kKeyTag;
    uint32_t second = StringKeyParser::Crc32(key->name, 1);
    uint32_t *storage = GameAddress<uint32_t>(key->storage);
    storage[1] = second;
    storage[0] = first;
}

const NameKey *NameKeys(size_t *count)
{
    *count = kNameKeyCount;
    return kNameKeys;
}

const NameKeyPair *NameKeyPairs(size_t *count)
{
    *count = kNameKeyPairCount;
    return kNameKeyPairs;
}

const winmainstart::OwnedInitialiser *OwnedNameKeys(size_t *count)
{
    *count = kOwned.size();
    return kOwned.data();
}

// The kinds' lists merged by slot, once (the host asks before its XC step; the tests check one entry per slot).
const winmainstart::OwnedInitialiser *OwnedInitialisers(size_t *count)
{
    static const std::vector<winmainstart::OwnedInitialiser> all = [] {
        std::vector<winmainstart::OwnedInitialiser> v;
        for (auto kind : {&OwnedNameKeys, &OwnedObjects, &OwnedValues, &OwnedStrings, &OwnedAnys, &OwnedRZStrings,
                          &OwnedAtExits, &OwnedConstructed, &OwnedHF, &OwnedAllocators, &OwnedParsers, &OwnedH5, &OwnedHT}) {
            size_t n = 0;
            const winmainstart::OwnedInitialiser *p = kind(&n);
            v.insert(v.end(), p, p + n);
        }
        std::sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return a.slot < b.slot; });
        return v;
    }();
    *count = all.size();
    return all.data();
}

} // namespace staticinit
