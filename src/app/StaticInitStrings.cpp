// Owned XC entries of the "std-string" kind (lane GQ; docs/subsystems/startup.md "Stage 3: the static
// initialisers"): a std::string constant and its atexit destructor. Three forms in the image:
//   * Construct (31 entries, all " \t\n\r"): the library constructor basic_string(const char *) (0x141136) on the
//     global; the thunk calls _Tidy_deallocate (0x141455). Ours: the same library constructor in place.
//   * Characters (32): size and capacity (15) are constant-initialised in .data; the entry copies the `size`
//     characters into the inline buffer and nothing else (the terminator is .data's zero). Ours copies the same.
//   * Heap (22, longer than 15): the entry allocates capacity + 1 (32) bytes (0x141344), copies the characters with
//     their terminator, and stores the pointer, the size and the capacity (31); the rest of the union is untouched.
//     Ours does the same.
// Each thunk empties the string (frees a heap buffer; size 0, capacity 15, terminator): our thunk runs our
// std::string destructor, which does exactly that. Both sides allocate from the process heap (E1 facts), so the
// game may free or reassign what ours allocated. Standalone, each becomes a `const std::string`.
#include "app/StaticInit.h"

#include "game/game.h"

#include <array>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace staticinit {

namespace {

// Slot order. Forms and texts from what each image entry writes (a junk-filled run of each, two patterns) and
// the .data around the Characters entries; the difftest checks each against its entry.
constexpr StringStatic kStrings[] = {
    {111, 0x1384bb, 0x10cb81c, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd907a5
    {169, 0x1392a0, 0x10d9318, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd91ab0
    {191, 0x139580, 0x10d96c4, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd91e20
    {207, 0x13a7c0, 0x10d9fe0, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd92520
    {215, 0x13a930, 0x10da3ec, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd92800
    {235, 0x13bbf0, 0x1014f88, StringForm::Characters, "$Neighbor", 9, 15}, // atexit 0xd93b70
    {236, 0x13b740, 0x1014fa0, StringForm::Characters, "$FamilyAssets", 13, 15}, // atexit 0xd933f0
    {237, 0x13b770, 0x1014fb8, StringForm::Characters, "$Family", 7, 15}, // atexit 0xd93450
    {238, 0x13b9c0, 0x1014fd0, StringForm::Characters, "$LastName", 9, 15}, // atexit 0xd937b0
    {239, 0x13b8a0, 0x1014fe8, StringForm::Characters, "$FullName", 9, 15}, // atexit 0xd935d0
    {240, 0x13bab0, 0x1015000, StringForm::Characters, "$Me", 3, 15}, // atexit 0xd93930
    {241, 0x13bc80, 0x1015018, StringForm::Characters, "$Object", 7, 15}, // atexit 0xd93c30
    {242, 0x13b9f0, 0x1015030, StringForm::Characters, "$Local", 6, 15}, // atexit 0xd93810
    {243, 0x13bae0, 0x1015048, StringForm::Characters, "$Money", 6, 15}, // atexit 0xd93990
    {244, 0x13bf20, 0x1015060, StringForm::Characters, "$TimeLocal", 10, 15}, // atexit 0xd93f30
    {245, 0x13b860, 0x1015078, StringForm::Characters, "$FullNameLocal", 14, 15}, // atexit 0xd93570
    {246, 0x13bb50, 0x1015090, StringForm::Characters, "$NameLocal", 10, 15}, // atexit 0xd93a50
    {247, 0x13bb10, 0x10150a8, StringForm::Characters, "$NameAttrib", 11, 15}, // atexit 0xd939f0
    {248, 0x13bbb0, 0x10150c0, StringForm::Characters, "$NeighborLocal", 14, 15}, // atexit 0xd93b10
    {249, 0x13b620, 0x10150d8, StringForm::Characters, "$Attrib", 7, 15}, // atexit 0xd931b0
    {250, 0x13b6b0, 0x10150f0, StringForm::Characters, "$CatalogLocal", 13, 15}, // atexit 0xd932d0
    {251, 0x13c120, 0x1015108, StringForm::Characters, "$TokenNameLocal", 15, 15}, // atexit 0xd94120
    {252, 0x13b710, 0x1015120, StringForm::Characters, "$DayLocal", 9, 15}, // atexit 0xd93390
    {253, 0x13b990, 0x1015138, StringForm::Characters, "$JobTitle", 9, 15}, // atexit 0xd93750
    {254, 0x13bd10, 0x1015150, StringForm::Characters, "$PrivateString", 14, 15}, // atexit 0xd93cf0
    {255, 0x13b5c0, 0x10dad08, StringForm::Heap, "$AspirationString", 17, 31}, // atexit 0xd93150
    {256, 0x13b650, 0x1015168, StringForm::Characters, "$BehaviorString", 15, 15}, // atexit 0xd93210
    {257, 0x13be90, 0x10dad20, StringForm::Heap, "$SemiGlobalString", 17, 31}, // atexit 0xd93e70
    {258, 0x13b930, 0x1015180, StringForm::Characters, "$GlobalString", 13, 15}, // atexit 0xd93690
    {259, 0x13ba20, 0x1015198, StringForm::Characters, "$LotNameLocal", 13, 15}, // atexit 0xd93870
    {260, 0x13ba50, 0x10dad38, StringForm::Heap, "$LotNameLocalByLotId", 20, 31}, // atexit 0xd938d0
    {261, 0x13c1d0, 0x10dad50, StringForm::Heap, "$TokenPersonalCountedLocalName", 30, 31}, // atexit 0xd941e0
    {262, 0x13c2c0, 0x10dad68, StringForm::Heap, "$TokenPersonalSingularLocalName", 31, 31}, // atexit 0xd942a0
    {263, 0x13bfd0, 0x10dad80, StringForm::Heap, "$TokenFamilyCountedLocalName", 28, 31}, // atexit 0xd94000
    {264, 0x13c0b0, 0x10dad98, StringForm::Heap, "$TokenFamilySingularLocalName", 29, 31}, // atexit 0xd940c0
    {265, 0x13c160, 0x10dadb0, StringForm::Heap, "$TokenPersonalCountedLocalDesc", 30, 31}, // atexit 0xd94180
    {266, 0x13c240, 0x10dadc8, StringForm::Heap, "$TokenPersonalSingularLocalDesc", 31, 31}, // atexit 0xd94240
    {267, 0x13bf50, 0x10dade0, StringForm::Heap, "$TokenFamilyCountedLocalDesc", 28, 31}, // atexit 0xd93f90
    {268, 0x13c040, 0x10dadf8, StringForm::Heap, "$TokenFamilySingularLocalDesc", 29, 31}, // atexit 0xd94060
    {269, 0x13b6e0, 0x10151b0, StringForm::Characters, "$DayFlags", 9, 15}, // atexit 0xd93330
    {270, 0x13b690, 0x10151c8, StringForm::Characters, "$Cash", 5, 15}, // atexit 0xd93270
    {271, 0x13bef0, 0x10151e0, StringForm::Characters, "$SkillName", 10, 15}, // atexit 0xd93ed0
    {272, 0x13c400, 0x10151f8, StringForm::Characters, "$WordPoints", 11, 15}, // atexit 0xd943c0
    {273, 0x13bc20, 0x10dae10, StringForm::Heap, "$NeighborhoodLocal", 18, 31}, // atexit 0xd93bd0
    {274, 0x13bcb0, 0x10dae28, StringForm::Heap, "$PrimaryNeighborhood", 20, 31}, // atexit 0xd93c90
    {275, 0x13c340, 0x10dae40, StringForm::Heap, "$WantDescriptionLocal", 21, 31}, // atexit 0xd94300
    {276, 0x13c3a0, 0x10dae58, StringForm::Heap, "$WantHelpTextLocal", 18, 31}, // atexit 0xd94360
    {277, 0x13b7a0, 0x10dae70, StringForm::Heap, "$FearDescriptionLocal", 21, 31}, // atexit 0xd934b0
    {278, 0x13b800, 0x10dae88, StringForm::Heap, "$FearHelpTextLocal", 18, 31}, // atexit 0xd93510
    {279, 0x13b960, 0x1015210, StringForm::Characters, "$GroupName", 10, 15}, // atexit 0xd936f0
    {280, 0x13bdb0, 0x10daea0, StringForm::Heap, "$PropertyPrivateString", 22, 31}, // atexit 0xd93db0
    {281, 0x13be20, 0x10daeb8, StringForm::Heap, "$PropertySemiGlobalString", 25, 31}, // atexit 0xd93e10
    {282, 0x13bd50, 0x10daed0, StringForm::Heap, "$PropertyGlobalString", 21, 31}, // atexit 0xd93d50
    {283, 0x13b8d0, 0x10daee8, StringForm::Heap, "$GUIDCatalogNameLocal", 21, 31}, // atexit 0xd93630
    {284, 0x13bb80, 0x1015228, StringForm::Characters, "$NameParam", 10, 15}, // atexit 0xd93ab0
    {343, 0x13cce1, 0x10dc8dc, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd9479b
    {381, 0x13d2c5, 0x10de188, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd94c39
    {382, 0x13d2e0, 0x10de1f8, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd94c43
    {477, 0x13dd1b, 0x10de968, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd94ddf
    {479, 0x13dd36, 0x10de9c0, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd94de9
    {490, 0x13dee9, 0x10df208, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd94e6b
    {632, 0x13f580, 0x10faae4, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd96880
    {698, 0x140160, 0x10fc85c, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd97970
    {713, 0x1402a0, 0x10fd220, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd97f00
    {722, 0x140370, 0x10fd2bc, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd98410
    {736, 0x140568, 0x10fd6e0, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd9883c
    {737, 0x140583, 0x10fd710, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd98846
    {738, 0x14059e, 0x10fd738, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd98850
    {739, 0x1405b9, 0x10fd760, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd9885a
    {740, 0x1405d4, 0x10fd788, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd98864
    {741, 0x1405ef, 0x10fd7b0, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd9886e
    {742, 0x14060a, 0x10fd7d8, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd98878
    {743, 0x140625, 0x10fd800, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd98882
    {744, 0x140640, 0x10fd828, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd9888c
    {745, 0x14065b, 0x10fd850, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd98896
    {746, 0x140676, 0x10fd878, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd988a0
    {747, 0x140691, 0x10fd8a0, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd988aa
    {748, 0x1406ac, 0x10fd8c8, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd988b4
    {749, 0x1406c7, 0x10fd8f0, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd988be
    {750, 0x1406e2, 0x10fd918, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd988c8
    {751, 0x1406fd, 0x10fd940, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd988d2
    {752, 0x140718, 0x10fd968, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd988dc
    {753, 0x140733, 0x10fd990, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd988e6
    {783, 0x140ad0, 0x110773c, StringForm::Construct, " \t\n\r", 4, 15}, // atexit 0xd98e70
    {794, 0x140e10, 0x10899f0, StringForm::Characters, "option", 6, 15}, // atexit 0xd99260
};

void DestroyString(const StringStatic &e)
{
    std::destroy_at(GameAddress<std::string>(e.storage));
}

template <size_t I> void __cdecl StringThunk() { DestroyString(kStrings[I]); }

using Thunk = void(__cdecl *)();
template <size_t... I> constexpr std::array<Thunk, sizeof...(I)> StringThunks(std::index_sequence<I...>) { return {&StringThunk<I>...}; }
constexpr auto kStringThunks = StringThunks(std::make_index_sequence<std::size(kStrings)>());

constexpr auto kOwned = [] {
    std::array<winmainstart::OwnedInitialiser, std::size(kStrings)> out{};
    for (size_t i = 0; i < std::size(kStrings); ++i) out[i] = {kStrings[i].slot, kStrings[i].imageRva, &InitString, &kStrings[i]};
    return out;
}();

} // namespace

void __cdecl InitString(const void *stringStatic)
{
    const auto &e = *static_cast<const StringStatic *>(stringStatic);
    uint8_t *object = GameAddress<uint8_t>(e.storage);
    switch (e.form) {
    case StringForm::Construct:
        ::new (object) std::string(e.text);
        break;
    case StringForm::Characters:
        // The inline buffer is the first member of an MSVC std::string; size and capacity stay as .data has them.
        std::memcpy(object, e.text, e.size);
        break;
    case StringForm::Heap: {
        char *buffer = static_cast<char *>(::operator new(e.capacity + 1));
        std::memcpy(buffer, e.text, e.size + 1);
        std::memcpy(object, &buffer, sizeof(buffer));      // _Bx._Ptr
        std::memcpy(object + 0x10, &e.size, sizeof(e.size)); // _Mysize
        std::memcpy(object + 0x14, &e.capacity, sizeof(e.capacity)); // _Myres
        break;
    }
    }
    BoundAtExit()(kStringThunks[&e - kStrings]);
}

const StringStatic *StringStatics(size_t *count)
{
    *count = std::size(kStrings);
    return kStrings;
}

const winmainstart::OwnedInitialiser *OwnedStrings(size_t *count)
{
    *count = kOwned.size();
    return kOwned.data();
}

} // namespace staticinit
