#pragma once
#include "rz/cRZString.h"
#include "rz/GameAllocator.h" // GZServiceAllocator
#include "ts/cITSAppFW.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <string>

// cTSAppCommonImpl in the standalone shape (lane EP; docs/subsystems/startup.md, handoff item A1): the application's
// option store. RVAs 1.18.0.187.
//
// The two containers are MSVC 14.40 std::map<uint32_t, OptionValue> (the game's map code is the library's: the lower
// bound 0x1463b0 is a library match for every std::map<unsigned, X>, and the tree node is _Tree_node: left, parent,
// right, colour, nil byte, then the pair at +0x10). An option is a key (+0x10), an alias key (+0x14: the option is
// relative to that option's value) and a std::list of entries (+0x18: head, +0x1c: size) whose elements are a cRZString
// and a product mask (0x24 bytes, 0x2c-byte list nodes). The first map (+0x04) holds the defaults, the second (+0x0c)
// the overrides: AddOption's fourth argument picks the map, and the readers try the overrides first unless their
// fourth argument says "defaults only".
//
// The readers' list output (GetOptionList / ReadOptionList) is a std::list<std::string> whose nodes and buffers come
// from the allocator service (0x20-byte nodes, AllocateListNodes 0x1e7190 / FreeThroughAllocatorService 0x197100).
namespace as2 {
void *__stdcall AllocateViaAllocatorService(uint32_t size, uint32_t unused); // 0x1bf795 (src/app/AppServices2.h)
void __stdcall FreeViaAllocatorService(void *mem, uint32_t unused);         // 0x1bf84e
} // namespace as2

namespace ts {

// The option string lists' memory comes from the allocator service (the same service and cache as the game's
// list/string helpers 0x1e7190, 0x177110, 0x197100, 0x152b80).
using OptionString = std::basic_string<char, std::char_traits<char>, GZServiceAllocator<char>>;
using OptionStringList = std::list<OptionString, GZServiceAllocator<OptionString>>;
static_assert(sizeof(OptionString) == 0x18 && sizeof(OptionStringList) == 8, "option string list layout");

// One entry of an option: its text and the product mask it applies to (the readers take the entries whose mask meets
// theirs). The text is a hook-shaped cRZString (game vtable), built by its own Construct/Destruct members.
struct OptionEntry {
    union {
        cRZString mText; // +0x00
    };
    uint32_t mMask;      // +0x20

    OptionEntry(const cRZString &text, uint32_t mask); // the text copied (std::string(text, 0, npos), as the original)
    OptionEntry(const OptionEntry &other);
    OptionEntry &operator=(const OptionEntry &) = delete;
    ~OptionEntry();

    // std::find's test (0xca4ac0): the masks equal, then this text's IsEqual(other's text, case-sensitive), slot 11.
    bool operator==(const OptionEntry &other) const;
};
static_assert(sizeof(OptionEntry) == 0x24, "OptionEntry: cRZString, mask");

struct OptionValue {
    uint32_t mAlias = 0;                // +0x14 of the tree node: 0, or the key this option is relative to
    std::list<OptionEntry> mEntries;    // +0x18
};
using OptionMap = std::map<uint32_t, OptionValue>;
static_assert(sizeof(OptionMap) == 8, "std::map: head, size");
static_assert(sizeof(std::_Tree_node<OptionMap::value_type, void *>) == 0x20, "the game's 0x20-byte tree node");

// cTSAppCommonImpl (0x18 bytes; game vtable 0xeb5468, 18 slots; constructor 0xca4cb0, destructor body 0xca4de0,
// deleting destructor 0xca4f50 at slot 10). In the game it is only a base of cTSAppFW (+0x44), whose constructor is its
// one creation site; its own vtable has _purecall at slots 1, 2 and 13. Here those three slots call _purecall too, so
// the class can be built on its own (its constructor hook, the tests).
class cTSAppCommonImpl : public cITSAppCommon {
public:
    cTSAppCommonImpl();            // 0xca4cb0
    ~cTSAppCommonImpl() override;  // 0xca4de0; deleting 0xca4f50 (slot 10)

    bool QueryInterface(uint32_t riid, void **out) override; // 0xca6ff0 (0xebabd35c -> this)
    uint32_t AddRef() override;                              // _purecall in the game's vtable
    uint32_t Release() override;                             // _purecall

    // slot 3, 0xca7230: adds `text` to the option `key` of the overrides (toOverrides's low byte set) or the defaults; a new option
    // takes `alias`, an existing one keeps its own; an entry equal to one already there is not added again. A null text
    // erases the option from that map. The result is undefined (eax of the last call); our `false`.
    bool AddOption(uint32_t key, uint32_t alias, const char *text, uint32_t toOverrides, int32_t mask) override;
    // slot 4, 0xca5e40: empties `out`, then ReadOptionText.
    bool GetOptionText(uint32_t id, cIGZString *out, uint32_t a, uint32_t defaultsOnly, int32_t mask) override;
    // slot 5, 0xca5e80: empties the list (OptionStringList *), then ReadOptionList; false for a null list.
    bool GetOptionList(uint32_t id, void *list, uint32_t a, uint32_t defaultsOnly, int32_t mask) override;
    bool HasOption(uint32_t key) override;                  // slot 6, 0xca6bd0: the key is in the overrides
    // slot 7, 0xca69f0: no override's text (GetOptionText(key, 1, 0, -1)) is a path prefix of `name` (0x1c2269).
    bool IsNameUnused(cIGZString *name) override;
    bool IsOptionNameUnused(uint32_t id) override;          // slot 8, 0xca6920
    void SetAppName(const char *text) override;             // slot 9, shared 0x300a0 (does nothing)
    // slot 11, 0xca5ed0: appends the first entry meeting `mask` to `out` (after the alias option's text when there is
    // an alias, which must exist); true when something was appended.
    bool ReadOptionText(uint32_t id, cIGZString *out, uint32_t a, uint32_t defaultsOnly, int32_t mask) override;
    // slot 12, 0xca6200: appends every entry meeting `mask` (each prefixed with every text of the alias option, read
    // with the entry's mask) to the list; true when something was appended.
    bool ReadOptionList(uint32_t id, void *list, uint32_t a, uint32_t defaultsOnly, int32_t mask) override;
    bool InitOptions() override;                            // slot 13, _purecall
    // slot 14, 0xca75c0 (lane FC; cTSAppCommonImplDirs.cpp): the executable's directory (option 0x6bcfce93) and each
    // product's TSData directory (option 0x4bcfce5e, mask 1 << product slot) from the command line ('ep', 'ep<n>dir'),
    // the 'Directories' section of the ini at option 0x6c253729, the product paths (src/app/ProductInfo.h) and the file
    // system. True.
    bool FindProductDirectories() override;
    // slot 15, 0xca8710 (lane EV): false when InitOptions (slot 13) fails; else the downloads and log directories (command
    // line, else derived from option 0x4bcfce5e) and the fixed table of user-data, resource and file options. True.
    // cTSAppFW overrides it (0xc2a7de).
    bool SetupUserDataDirectories() override;
    // slot 16, 0xca6c10 (lane EV): finds the resource-configuration ini (options 0xbe7523c, then 0x4be75253: the first
    // path that exists) and reads its sections ResourceFactories, DBSegments and DBSegmentDirectoryFiles. True.
    bool ReadResourceConfiguration() override;
    void RegisterRegistryHandlers() override;               // slot 17, 0xca5d20

    // 0xca6910 (ret 4; only FindProductDirectories calls it): any of `mask`'s bits set in mFlags.
    bool HasFlags(uint32_t mask) const;

    // mFlags bit 0: the development layout (the data directory is the executable's '../TSData', no product
    // paths). The constructor clears the word; the shipping game takes the other route.
    static constexpr uint32_t kFlagDevelopmentLayout = 1;

    OptionMap mDefaults;  // +0x04 (the hook-shaped mHeadA / mSizeA)
    OptionMap mOverrides; // +0x0c (mHeadB / mSizeB)
    uint32_t mFlags;      // +0x14 cleared by the constructor

private:
    // The readers' lookup: the overrides first unless `defaultsOnly`, then the defaults; null when neither has `id`.
    OptionValue *Find(uint32_t id, uint32_t defaultsOnly);
};
static_assert(sizeof(cTSAppCommonImpl) == 0x18, "cTSAppCommonImpl size");
static_assert(offsetof(cTSAppCommonImpl, mDefaults) == 4, "cTSAppCommonImpl layout");
static_assert(offsetof(cTSAppCommonImpl, mOverrides) == 0xc, "cTSAppCommonImpl layout");
static_assert(offsetof(cTSAppCommonImpl, mFlags) == 0x14, "cTSAppCommonImpl layout");

// The RVA hooks' entry points (docs/standalone-shape.md, step 6): `this` in ecx, the dummy edx, the stack arguments.
struct cTSAppCommonImplAdapters {
    static cTSAppCommonImpl *__fastcall Construct(void *memory, void *edx);                                     // 0xca4cb0
    static void __fastcall Destruct(cTSAppCommonImpl *self, void *edx);                                         // 0xca4de0
    static cTSAppCommonImpl *__fastcall DeletingDestructor(cTSAppCommonImpl *self, void *edx, uint32_t flags);  // 0xca4f50
    static bool __fastcall QueryInterface(cTSAppCommonImpl *self, void *edx, uint32_t riid, void **out);        // 0xca6ff0
    static bool __fastcall AddOption(cTSAppCommonImpl *self, void *edx, uint32_t key, uint32_t alias, const char *text,
                                     uint32_t toOverrides, int32_t mask);                                          // 0xca7230
    static bool __fastcall GetOptionText(cTSAppCommonImpl *self, void *edx, uint32_t id, cIGZString *out, uint32_t a,
                                         uint32_t defaultsOnly, int32_t mask);                                  // 0xca5e40
    static bool __fastcall GetOptionList(cTSAppCommonImpl *self, void *edx, uint32_t id, void *list, uint32_t a,
                                         uint32_t defaultsOnly, int32_t mask);                                  // 0xca5e80
    static bool __fastcall HasOption(cTSAppCommonImpl *self, void *edx, uint32_t key);                          // 0xca6bd0
    static bool __fastcall IsNameUnused(cTSAppCommonImpl *self, void *edx, cIGZString *name);                   // 0xca69f0
    static bool __fastcall IsOptionNameUnused(cTSAppCommonImpl *self, void *edx, uint32_t id);                  // 0xca6920
    static bool __fastcall ReadOptionText(cTSAppCommonImpl *self, void *edx, uint32_t id, cIGZString *out, uint32_t a,
                                          uint32_t defaultsOnly, int32_t mask);                                 // 0xca5ed0
    static bool __fastcall ReadOptionList(cTSAppCommonImpl *self, void *edx, uint32_t id, void *list, uint32_t a,
                                          uint32_t defaultsOnly, int32_t mask);                                 // 0xca6200
    static void __fastcall RegisterRegistryHandlers(cTSAppCommonImpl *self, void *edx);                         // 0xca5d20
    // Lane EV (cTSAppCommonImplDirs.cpp).
    static bool __fastcall SetupUserDataDirectories(cTSAppCommonImpl *self, void *edx);                         // 0xca8710
    static bool __fastcall ReadResourceConfiguration(cTSAppCommonImpl *self, void *edx);                        // 0xca6c10
    // Lane FC.
    static bool __fastcall FindProductDirectories(cTSAppCommonImpl *self, void *edx);                           // 0xca75c0
    static bool __fastcall HasFlags(cTSAppCommonImpl *self, void *edx, uint32_t mask);                          // 0xca6910
    // The resource ini's section callbacks (cdecl: key, value, the context ReadResourceConfiguration passes, unused):
    // one entry each through the registry service (0x25076b9e).
    static void __cdecl OnResourceFactory(cIGZString *key, cIGZString *value, void *context);        // 0xca74b0
    static void __cdecl OnDBSegment(cIGZString *key, cIGZString *value, void *context);              // 0xca7020
    static void __cdecl OnDBSegmentDirectoryFile(cIGZString *key, cIGZString *value, void *context); // 0xca7130
    // Lane MB (cTSAppCommonImplRegistry.cpp): the handlers RegisterRegistryHandlers gives the registry tables CN_PRF and
    // CN_PDBS (cdecl: key, value, the application object, unused), and the segment builder the second one calls.
    static void __cdecl OnResourceFactoryEntry(cIGZString *key, cIGZString *value, void *context); // 0xca5db0
    static void __cdecl OnPathRegistryEntry(cIGZString *key, cIGZString *value, void *context);    // 0xca5170
    // 0xca4f70 (returns in al; its one caller tests nothing).
    static bool __cdecl AddPathSegment(uint32_t classId, uint32_t typeId, cIGZString *path);
};

} // namespace ts
