#pragma once
#include "gz/cIGZPropertySet.h" // GZServiceString, cIGZPropertySetString
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <set>

// The application's property files (lane MA, F4d; RVAs 1.18.0.187): the 0xf0-byte object cTSAppFW keeps at +0x60. No
// vtable and no RTTI, so the name is ours. cTSAppFW::PreFrameworkInit builds it (0xcb0890 with true), sets its paths
// (0xcb0f40), loads the properties (0xcb18a0) and the Config directory's (0xcb1790); Init loads the global file into its
// own set (0xcb1780); DoMessage 0xcd and Shutdown save (0xcb28b0); Shutdown destroys it (0xcb0ad0). The properties go to
// the framework's property-set service (+0xc, class 0xac1887e). Nothing else in the image reaches these functions.
//
// Files: <data>savedProps.txt and <data>userSavedProps.txt name the keys that are saved (one per line, '#' comments),
// <data>globalProps.xml the global properties, <user>userProps.xml the user's (read, and written by Save with the saved
// keys only), <app>Config/globalProps.xml the Config directory's. <data> is the first existing directory of the
// application's list 0xcc9f7591, <app> that of list 0x6cd89f8d, <user> the text option 0xade2e3ba, unless the override
// directory (+0xd0) is set: then it is all three (nothing in the image sets it).
namespace ts {

// The saved key names: case-insensitive (_stricmp) std::set of allocator-service strings. The game's set code is the
// library's (lower bound 0x2eb8c0, node string copy 0x315060, tree erase 0xcb0810; 0x28-byte nodes from operator new).
struct KeyNameLess {
    bool operator()(const GZServiceString &a, const GZServiceString &b) const;
};
using KeyNameSet = std::set<GZServiceString, KeyNameLess>;
static_assert(sizeof(KeyNameSet) == 8, "std::set: head, size");

class cTSAppPropertyFiles {
public:
    static constexpr uint32_t kSize = 0xf0;
    static constexpr uint32_t kPropertyServiceClass = 0xac1887e; // the framework's system service (slot 5)
    static constexpr uint32_t kDataDirectoriesOption = 0xcc9f7591;   // list option (CommonImpl slot 5)
    static constexpr uint32_t kConfigDirectoriesOption = 0x6cd89f8d; // list option (CommonImpl slot 5)
    static constexpr uint32_t kUserDirectoryOption = 0xade2e3ba;     // text option (CommonImpl slot 4)

    // 0xcb0890 (__thiscall, ret 4): both flags from `enabled`, the set empty, the strings empty, the property-set
    // service asked for (only when there is a framework). Returns this.
    cTSAppPropertyFiles *Construct(bool enabled);
    // 0xcb0ad0: the service released (the member cleared first), the strings from the last to the first, the set.
    void Destruct();

    // 0xcb0f40: the seven paths (see above).
    void SetupPaths();
    // 0xcb18a0: the command line's switches (propsNoSave / cardConfig clear mSave, propsNoLoad / cardConfig clear
    // mLoadUser), then either the files of the propertyList switch (';'-separated; a relative one is taken in the data
    // directory) or the global file and, with mLoadUser, the user's; everything copied to the service's set.
    void LoadProperties();
    // 0xcb1780 (ret 4): the global file into `target`.
    void LoadGlobalInto(cIGZPropertySetString *target);
    // 0xcb1790: the Config directory's file without its propertyRevision, copied to the service's set.
    void LoadConfigProperties();
    // 0xcb28b0: with mSave and saved key names, the service's properties under those names written to the user's file
    // (unless it exists and is read-only).
    void Save();

    // 0xcb1e60 (ret 8): the user's file; taken (its saved keys only) when `target` has no propertyRevision, or the same
    // one as the file. Returns false when the file was not read or the revisions differ.
    bool LoadUserProperties(cIGZPropertySetString *target, const cRZString *path);
    // 0xcb2020 (ret 8): each ';'-separated file of `list`.
    void LoadPropertyList(cIGZPropertySetString *target, const cRZString *list);
    // 0xcb2240 (ret 4): when `names` is empty, the names of both saved-key files.
    void LoadSavedKeyNames(KeyNameSet *names);

    // 0xcb0da0 (ret 8; ecx not read): every property of `from` set on `to`.
    static void __stdcall CopyAll(cIGZPropertySetString *from, cIGZPropertySetString *to);
    // 0xcb0e50 (ret 0xc): the properties of `from` whose keys are in `names`.
    static void __stdcall CopyListed(cIGZPropertySetString *from, cIGZPropertySetString *to, KeyNameSet *names);
    // 0xcb1cf0 (ret 8): the XML property file at `path` read into a local set and copied to `target`. False when the
    // file is missing or not read.
    static bool __stdcall LoadXml(cIGZPropertySetString *target, const cRZString *path);
    // 0xcb2400 (ret 8): `names` cleared, then each line of the text file at `path`, trimmed, that is not empty and does
    // not start with '#'. An exception while adding one line is swallowed (the original's catch (...)).
    static void __stdcall ReadKeyNames(const char *path, KeyNameSet *names);

    bool mSave;                            // +0x00 Save writes only when set
    bool mLoadUser;                        // +0x01 the user's file is loaded only when set
    KeyNameSet mSavedKeys;                 // +0x04
    cIGZPropertySetString *mProperties;    // +0x0c the framework's property-set service (counted)
    union { cRZString mSavedKeysPath; };      // +0x10 <data>savedProps.txt
    union { cRZString mUserSavedKeysPath; };  // +0x30 <data>userSavedProps.txt
    union { cRZString mUserPropsPath; };      // +0x50 <user>userProps.xml
    union { cRZString mGlobalPropsPath; };    // +0x70 <data>globalProps.xml
    union { cRZString mConfigPropsPath; };    // +0x90 <app>Config/globalProps.xml
    union { cRZString mDataDirectory; };      // +0xb0
    union { cRZString mOverrideDirectory; };  // +0xd0 never set in the image
};
static_assert(sizeof(cTSAppPropertyFiles) == cTSAppPropertyFiles::kSize, "cTSAppPropertyFiles size");
static_assert(offsetof(cTSAppPropertyFiles, mSavedKeys) == 0x04, "cTSAppPropertyFiles layout");
static_assert(offsetof(cTSAppPropertyFiles, mProperties) == 0x0c, "cTSAppPropertyFiles layout");
static_assert(offsetof(cTSAppPropertyFiles, mSavedKeysPath) == 0x10, "cTSAppPropertyFiles layout");
static_assert(offsetof(cTSAppPropertyFiles, mUserPropsPath) == 0x50, "cTSAppPropertyFiles layout");
static_assert(offsetof(cTSAppPropertyFiles, mDataDirectory) == 0xb0, "cTSAppPropertyFiles layout");
static_assert(offsetof(cTSAppPropertyFiles, mOverrideDirectory) == 0xd0, "cTSAppPropertyFiles layout");

} // namespace ts
