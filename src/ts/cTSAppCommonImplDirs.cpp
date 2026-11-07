// ts::cTSAppCommonImpl: the directory slots (lanes EV, FC). Built for speed, as the game's (0xca8710, 0xca75c0 and 0xca6910
// are 16-byte aligned).
#include "ts/cTSAppCommonImpl.h"

#include "app/AppInit.h"
#include "app/AppServices2.h"
#include "app/ProductInfo.h"
#include "app/IniFile.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/GZCOM.h"
#include "rz/cRZFile.h"
#include "rz/cRZIniFile.h"
#include "gz/cIGZFrameWork.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/StringKeyParser.h"
#include "rz/cRZCmdLine.h"
#include "rz/cRZFileName.h"
#include "rz/cTSIniFile.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace ts {
namespace {

// A cRZString on the stack, built and torn down by its own (hook-shaped) members.
struct Text {
    union {
        cRZString s;
    };
    Text() { s.Construct(); }
    explicit Text(const char *text) { s.Construct(text); }
    Text(const Text &) = delete;
    Text &operator=(const Text &) = delete;
    ~Text() { s.Destruct(); }
    cIGZString &Face() { return *s.AsInterface(); }
};

cIGZCmdLine *CommandLine()
{
    auto *framework = reinterpret_cast<cIGZFrameWork *>(static_cast<uintptr_t>(RZGetGlobal10C9378()));
    return framework->CommandLine();
}

// A switch's parameter (cIGZCmdLine slot 8, quotes trimmed); the name is a temporary cRZString, as in the original.
bool Switch(cIGZCmdLine *commandLine, const char *name, Text &parameter)
{
    Text key(name);
    return commandLine->IsSwitchPresent(key.Face(), parameter.Face(), true);
}

// 0x1c2732 (__cdecl, not ours yet): out = _makepath(drive, dir, name, ext) of the four strings' texts (null: empty).
using MakePath = void(__cdecl *)(cRZString *out, const cRZString *drive, const cRZString *dir, const cRZString *name,
                                 const cRZString *ext);

// 0xb39360 (not ours yet): the current product's mask bit (mov eax, 0x20000).
using ProductMask = uint32_t(__cdecl *)();

// The fixed options of SetupUserDataDirectories, in the original's order: key, the option it is relative to, the text,
// whether it goes to the overrides, and the product mask (kCurrent: 0xb39360's).
struct FixedOption {
    uint32_t key, alias;
    const char *text;
    uint32_t toOverrides;
    int32_t mask;
};
constexpr int32_t kCurrent = 0x7fffffff;

constexpr FixedOption kDataRoots[] = {
    {0xec43275c, 0x4bcfce5e, "Sys/", 0, -1},
    {0x2ce990aa, 0x4bcfce5e, "Control/", 0, -1},
    {0xec432753, 0x4bcfce5e, "Res/", 0, -1},
};

constexpr FixedOption kFixedOptions[] = {
    {0xade2e3ba, 0x2bcfce83, "Config/", 0, -1},
    {0x4c4324c5, 0x2bcfce83, "Neighborhoods/", 0, -1},
    {0x8d470e6b, 0x2bcfce83, "Collections/", 0, -1},
    {0xf1803568, 0x2bcfce83, "PetBreeds/", 0, -1},
    {0x3188384d, 0x2bcfce83, "PetCoats/", 0, -1},
    {0x8ce431ee, 0x2bcfce83, "Storytelling/", 0, -1},
    {0x0be3ca74, 0x2bcfce83, "Thumbnails/", 0, -1},
    {0x8c5aedd5, 0x2bcfce83, "LotCatalog/", 0, -1},
    {0xac4d4399, 0x2bcfce83, "Export/", 0, -1},
    {0x2c900c4c, 0x2bcfce83, "Downloads/", 0, -1},
    {0xad58ce39, 0x2bcfce83, "SavedSims/", 0, -1},
    {0xece41771, 0x2bcfce83, "Teleport/", 0, -1},
    {0x4df92c03, 0x2bcfce83, "PackObject/", 0, -1},
    {0xcce43221, 0x2bcfce83, "Movies/", 0, -1},
    {0xeceb26f2, 0x2bcfce83, "Music/", 0, -1},
    {0x6d9b87bc, 0x2bcfce83, "SC4Terrains/", 0, -1},
    {0xcd93cb0d, 0x2bcfce83, "Groups.cache", 0, -1},
    {0x4dd17c4c, 0x2bcfce83, "Cameras/", 0, -1},
    {0xce02aeb0, 0x2bcfce83, "HTTPCache/", 0, -1},
    {0x10ede041, 0x2bcfce83, "Paintings/", 0, -1},
    {0x51a396fe, 0x2bcfce83, "PackagedLots/", 0, -1},
    {0x71a39707, 0x2bcfce83, "PackagedSims/", 0, -1},
    {0xf1a54fd7, 0x2bcfce83, "LockedBins/", 0, -1},
    {0x71dd9f92, 0x2bcfce83, "Accessory.cache", 0, -1},
    {0xedb70c55, 0xec432753, "ContentRegistry", 0, -1},
    {0xedb70568, 0x2bcfce83, "ContentRegistry", 0, -1},
    {0xd431eedd, 0xf4876cc9, "Packed/Objects/", 0, -1},
    {0xf431eeea, 0xf4876cc9, "Packed/Skins/", 0, -1},
    {0x3505f6b5, 0xf4876cc9, "Packed/Patterns/", 0, -1},
    {0x543599d6, 0xf4876cc9, "Downloads/Objects/", 0, -1},
    {0x74876d49, 0xf4876cc9, "Downloads/Skins/", 0, -1},
    {0x3505f6c3, 0xf4876cc9, "Downloads/Patterns/", 0, -1},
    {0x6ced4870, 0x2ced4743, "Characters/", 0, -1},
    {0x6ced477c, 0x2ced4743, "Lots/", 0, -1},
    {0xec4f2a83, 0x6cd89f8d, "UI/Fonts/", 1, -1},
    {0xeceb24ed, 0x6cd89f8d, "Movies/", 1, -1},
    {0xcc4368f3, 0x6cd89f8d, "Sims3D/", 1, 1},
    {0xcc4368f3, 0x6cd89f8d, "3D/", 1, -2},
    {0x8c4f2dba, 0x6cd89f8d, "Bins/", 1, -1},
    {0xcbe2f142, 0x6cd89f8d, "Skins/", 1, -1},
    {0xeceb24ed, 0xec432753, "Movies/", 0, -1},
    {0x6ca89b67, 0x4bcfce5e, "SimScript/", 0, -1},
    {0xec9f78aa, 0xec432753, "Effects/", 0, -1},
    {0xcc9f7591, 0xec432753, "Config/", 0, kCurrent},
    {0xac4f3639, 0xec432753, "LotTemplates/", 0, -1},
    {0x8be74d63, 0xec432753, "EdithHelp/", 0, 1},
    {0xcc4368f3, 0xec432753, "Sims3D/", 0, 1},
    {0xcc4368f3, 0xec432753, "3D/", 0, -2},
    {0x4ea750fe, 0xec432753, "Cameras/", 0, -1},
    {0xcc9f6e91, 0xec432753, "Text/", 0, kCurrent},
    {0x2bd051f9, 0xec432753, "Catalog/", 0, -1},
    {0xcdefbd7d, 0xec432753, "Lights/", 0, -1},
    {0x2bd0234d, 0xec432753, "Objects/", 0, kCurrent},
    {0x91ec9a5a, 0xec432753, "Overrides/", 0, kCurrent},
    {0x2f46701b, 0xec432753, "ObjectScripts/", 0, kCurrent},
    {0xccd9e001, 0xec432753, "Tutorial/", 0, -1},
    {0xccbe1c05, 0xec432753, "Sound/", 0, -1},
    {0xccbe1c05, 0x6cd89f8d, "Sound/", 1, -1},
    {0xcca89cbe, 0x4bcfce5e, "Metrics/", 0, -1},
    {0x6cbe1ca3, 0xec432753, "UI/", 0, -1},
    {0x4dd1e035, 0xec432753, "Wants/", 0, kCurrent},
    {0xcdd17ed5, 0xec432753, "Materials/", 0, kCurrent},
    {0xee5c0210, 0xec432753, "Terrain/", 0, -1},
    {0x2e5c023e, 0xec432753, "Lighting/", 0, -1},
    {0x2c891701, 0xec432753, "GlobalLots/", 0, kCurrent},
    {0xce9b3252, 0xec432753, "NeighborhoodTemplate/", 0, -2},
    {0x313fa8b2, 0xec432753, "StuffPack/", 0, -1},
    {0x0cad1a88, 0x8d470e6b, "Icons/", 0, -1},
    {0x8bd04049, 0xcc9f7591, "globalStartup.cheat", 0, -1},
    {0x4c9f74fe, 0xcc9f7591, "Graphics Rules.sgr", 0, -1},
    {0xcc9f7985, 0xcc9f7591, "TS2Config.txt", 0, -1},
    {0x8c4f2dba, 0x2bd051f9, "Bins/", 0, -1},
    {0xec8f9a06, 0x2bd051f9, "Patterns/", 0, -1},
    {0x8cc63107, 0x2bd051f9, "Scripts/", 0, -1},
    {0x6c69ac4a, 0x2bd051f9, "Materials/", 0, -1},
    {0xcbe2f142, 0x2bd051f9, "Skins/", 0, -1},
    {0x8ded6280, 0xcbe2f142, "FaceInfo.package", 0, -1},
    {0x8ded6288, 0xcbe2f142, "Jewelry.package", 0, -1},
    {0xb19c133d, 0x2bd051f9, "KissingPoints/", 0, -1},
    {0x0defbdbb, 0xcdefbd7d, "Objects/", 0, -1},
    {0x4d9263ea, 0xec8f9a06, "Fences/", 0, -1},
    {0xed926481, 0xec8f9a06, "Floors/", 0, -1},
    {0xed926475, 0xec8f9a06, "Roofs/", 0, -1},
    {0xcd926464, 0xec8f9a06, "Walls/", 0, -1},
    {0x2bd02363, 0x2bd0234d, "Global/", 0, -1},
    {0x0cd9e045, 0xccd9e001, "EpisodeDefinitions.txt", 0, -1},
    {0xec4f2a83, 0x6cbe1ca3, "Fonts/", 0, -1},
    {0xcbe74f30, 0x6cbe1ca3, "Cursors/", 0, -1},
    {0x0d7eb18b, 0x4dd1e035, "Definitions/", 0, -1},
    {0x0d7ebaf1, 0x4dd1e035, "Trees/", 0, -1},
    {0x2bd047c9, 0x2bcfce83, "breakPoints.edith", 0, -1},
    {0x4be75253, 0xec43275c, "Default.ini", 0, kCurrent},
    {0xac4f294e, 0xec43275c, "GraphicsOptionsDialog.ini", 0, kCurrent},
    {0x4c4f2aeb, 0xec43275c, "NeighborhoodTerrain.ini", 0, kCurrent},
    {0x4c9faa54, 0xec43275c, "Neighborhood.ini", 0, kCurrent},
    {0x0c4f28ba, 0xec43275c, "Terrain.ini", 0, kCurrent},
    {0x0be7523c, 0xec43275c, "ReleaseResource.ini", 0, kCurrent},
    {0xcc9a696d, 0xcc9f7591, "objectImposters.txt", 0, -1},
    {0x6bd04212, 0x8cc63107, "floorPatterns.txt", 0, -1},
    {0xcbd77a21, 0x8cc63107, "roofpatterns.txt", 0, -1},
    {0xebd04231, 0x8cc63107, "roofs.txt", 0, -1},
    {0xabd04234, 0x8cc63107, "fireplaces.txt", 0, -1},
    {0xf03a7ee3, 0x8cc63107, "awnings.txt", 0, -1},
    {0x4bd0420e, 0x8cc63107, "terrainSculptBrushes.txt", 0, -1},
    {0x8c7194f2, 0x8cc63107, "userToolProxies.txt", 0, -1},
    {0x6c657966, 0x8cc63107, "neighborhoodObjects.txt", 0, -1},
    {0xebd04239, 0x8cc63107, "fences.txt", 0, -1},
    {0x2bd0423c, 0x8cc63107, "walls.txt", 0, -1},
    {0x6bd0423b, 0x8cc63107, "wallPatterns.txt", 0, -1},
    {0x2bd04237, 0x8cc63107, "modularStairs.txt", 0, -1},
    {0xabd04238, 0x8cc63107, "deckFoundations.txt", 0, -1},
    {0xebd0423e, 0x8cc63107, "terrainPaints.txt", 0, -1},
    {0xcbd04239, 0x8cc63107, "fenceArches.txt", 0, -1},
    {0xac9f786b, 0xec9f78aa, "config.pfx", 0, -1},
    {0x6c59eae5, 0x0be3ca74, "DesignModeThumbnails.package", 0, -1},
    {0x0c7e9981, 0x0be3ca74, "CASThumbnails.package", 0, -1},
    {0xcc2a2628, 0x0be3ca74, "ObjectThumbnails.package", 0, -1},
    {0x0c30cf23, 0x0be3ca74, "BuildModeThumbnails.package", 0, -1},
    {0x4d5340e0, 0x0be3ca74, "CANHObjectsThumbnails.package", 0, -1},
    {0x6c9f7a1f, 0xec432753, "Lights/Lighting.txt", 0, -1},
    {0x4c9f79fd, 0xec432753, "Lights/LightRig.txt", 0, -1},
    {0xac9f7a5a, 0xcc9f7591, "LODConfig.txt", 0, -1},
    {0x0c9f79c6, 0xcdd17ed5, "config.matShad", 0, -1},
    {0x2ea751e4, 0x4ea750fe, "cameras.txt", 0, -1},
    {0x8c9f7a34, 0x4dd17c4c, "cameras.txt", 0, -1},
    {0x4bd040bc, 0xade2e3ba, "userStartup.cheat", 0, -1},
};

// ---- the resource-configuration ini (0xca6c10)

constexpr uint32_t kOptionResourceIni = 0x0be7523c; // ReleaseResource.ini
constexpr uint32_t kOptionDefaultIni = 0x4be75253;  // Default.ini

// The ini reader's member that is not ours yet: the constructor 0x9c5df3 (an empty reader, ret 0). The section walk is
// ours (cRZIniFile::ForEachKey, slot 13, called directly like the original): the callback gets each key and value of
// the section (cdecl, with the context as a third argument).
using IniCallback = void(__cdecl *)(cIGZString *key, cIGZString *value, void *context);
using IniConstruct = void *(__thiscall *)(void *ini);

// The registry service (0x25076b9e, interface 0xa237613c): slot 6 turns a key and value of the ini into an entry (the
// game words 0x1085efc / 0x1085ef8 or the text "CN_DBSDF" select the table), slot 3 adds it.
constexpr uint32_t kRegistryService = 0x25076b9e, kRegistryServiceIid = 0xa237613c;
constexpr uint32_t kResourceFactoriesWord = 0x1085efc, kDBSegmentsWord = 0x1085ef8;

struct cIRegistryService : cIGZUnknown {
    virtual bool Vf3(cIGZString *entry) = 0;                                                             // slot 3
    virtual bool Vf4() = 0;
    virtual bool Vf5() = 0;
    virtual bool MakeEntry(uint32_t table, cIGZString *key, cIGZString *value, cIGZString *entry) = 0;   // slot 6
};

// 0xca74b0 / 0xca7020 / 0xca7130 (cdecl): one key and value of the section, through the registry service. The service is
// assumed present (the original dereferences it unconditionally); it is released at the end.
void RegisterIniEntry(uint32_t table, cIGZString *key, cIGZString *value)
{
    cIRegistryService *service = nullptr;
    auto *framework = reinterpret_cast<cIGZFrameWork *>(static_cast<uintptr_t>(RZGetGlobal10C9378()));
    if (framework) framework->GetSystemService(kRegistryService, kRegistryServiceIid, reinterpret_cast<void **>(&service));
    {
        Text entry;
        if (service->MakeEntry(table, key, value, &entry.Face())) service->Vf3(&entry.Face());
    }
    if (service) service->Release();
}

// The first path of the option's list that exists (the list is filled through the cached cITSAppCommon, 0x1d0c60); the
// last one tried stays in `path` when none does.
bool FindExisting(uint32_t option, OptionStringList &list, Text &path)
{
    appinit::CommonImplSlot5(option, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&list)), 1, 0, 0xffffffff);
    for (const OptionString &candidate : list) {
        path.s.mString.assign(candidate.c_str(), std::strlen(candidate.c_str()));
        if (cRZFile::PathExists(path.Face())) return true;
    }
    return false;
}

constexpr uint32_t kOptionDataRoot = 0x4bcfce5e;  // a product's data directory (TSData)
constexpr uint32_t kOptionDownloads = 0x4e36c5a3; // the TS2Downloads directory
constexpr uint32_t kOptionLogs = 0x4d4896ba;      // the log directory
constexpr uint32_t kOptionUserData = 0x2bcfce83;  // the user-data root ("Documents/EA Games/...")

// ---- the product directories (0xca75c0)

constexpr uint32_t kOptionExecutableDir = 0x6bcfce93; // the executable's directory
constexpr uint32_t kOptionProductIni = 0x6c253729;    // an ini whose 'Directories' section maps "ep<n>dir" to directories
constexpr uint32_t kProductSlots = 32;                 // product n's mask is 1 << n
constexpr uint32_t kRunningProduct = 17;               // this executable's product (mask 0x20000, as 0xb39360)
constexpr const char *kProductSwitch = "ep%ddir";      // the switch and ini key naming product n's directory

// 0x9c59d9 (not ours yet): the file-system service's cIGZFileSystem face (0x2073215d / 0xe0203660), cached in 0x10f51c4.
// Its slot 4 is the application directory, slot 19 SetDataDirectory.
using GetFileSystem = void *(__cdecl *)();
constexpr int kFileSystemAppDirectory = 4, kFileSystemSetDataDirectory = 19;

// The executable's directory with the '/out/' rule (slot 17 when nothing else names it): a build tree ".../out/..." is cut
// at its last "/out/"; otherwise the parent directory ('../', made absolute) if it exists, else the executable's own.
void DirectoryFromExecutable(Text &directory, const Text &executable)
{
    directory.s.mString.assign(executable.s.mString.c_str(), executable.s.mString.size());
    const int32_t out = directory.s.RFind("/out/", 0xffffffff, true);
    if (out != -1) {
        directory.s.mString.erase(static_cast<size_t>(out));
        return;
    }
    Text parent;
    parent.s.mString = directory.s.mString + "../";
    inifile::AbsolutePathString(&parent.s, &parent.s);
    cRZFileName::NormaliseSeparators(&parent.s, '/');
    if (as2::DirectoryExists(&parent.s)) directory.s.mString.assign(parent.s.mString.c_str(), parent.s.mString.size());
}

// The development layout: the executable's '../TSData' if it exists, else the 'ep0dir' switch + 'TSData'; both product
// masks 1 and 0x20000 get it when the path exists.
void AddDevelopmentDataDirectory(cTSAppCommonImpl &app, const Text &executable)
{
    Text directory;
    Text sibling;
    sibling.s.mString = executable.s.mString + "../";
    inifile::AbsolutePathString(&sibling.s, &sibling.s);
    cRZFileName::NormaliseSeparators(&sibling.s, '/');
    sibling.s.mString.append("TSData", 6);
    cRZFileName::NormaliseSeparators(&sibling.s, '/');
    if (as2::DirectoryExists(&sibling.s)) {
        directory.s.mString.assign(sibling.s.mString.c_str(), sibling.s.mString.size());
    } else {
        Text name;
        name.s.Sprintf(kProductSwitch, 0);
        CommandLine()->IsSwitchPresent(name.Face(), directory.Face(), true);
        cRZFileName::NormaliseSeparators(&directory.s, '/');
        directory.s.mString.append("TSData", 6);
        cRZFileName::NormaliseSeparators(&directory.s, '/');
    }
    if (cRZFile::PathExists(directory.Face())) {
        app.AddOption(kOptionDataRoot, 0, directory.s.ToChar(), 0, 1);
        app.AddOption(kOptionDataRoot, 0, directory.s.ToChar(), 0, 0x20000);
    }
}

// The installed layout: the products named by the 'ep' switch (all without it), each one's directory from its 'ep<n>dir'
// switch, else the product ini's 'Directories' section, else the product paths (GetProductPaths, 0xcb4690).
void AddProductDataDirectories(cTSAppCommonImpl &app, const Text &executable)
{
    // GetProductPaths (0xcb4690) writes the products' key and the base game's directory (the executable's, three levels up;
    // nothing is read from the registry).
    char registryKey[0x104] = {};
    uint32_t registryKeySize = sizeof(registryKey);
    char baseBuffer[0x104] = {};
    uint32_t baseSize = sizeof(baseBuffer);
    product::GetProductPaths("Sims2.exe", registryKey, &registryKeySize, baseBuffer, &baseSize, nullptr, nullptr, nullptr);
    Text baseDirectory;
    if (baseSize != 0) ConvertBytesToString(baseBuffer, baseSize, -1, &baseDirectory.Face());

    char **listed = nullptr;
    int32_t listedCount;
    const bool haveList = product::InstalledProductDirectories(registryKey, &listed, &listedCount);
    std::vector<std::string> installed; // product n's directory at n - 1
    if (haveList) {
        if (listedCount != 0) installed.resize(static_cast<uint32_t>(listedCount));
        for (int32_t i = 0; i < listedCount; ++i) {
            if (!listed[i]) continue;
            Text converted;
            ConvertBytesToString(listed[i], static_cast<uint32_t>(std::strlen(listed[i])), -1, &converted.Face());
            installed[i] = converted.s.mString;
        }
    }

    cIGZCmdLine *commandLine = CommandLine();
    bool enabled[kProductSlots];
    enabled[0] = true;
    Text key; // the 'ep' switch's list, then each product's switch and ini key
    bool listedProducts;
    {
        Text name("ep");
        listedProducts = commandLine->IsSwitchPresent(name.Face(), key.Face(), true);
    }
    if (!listedProducts) {
        for (uint32_t slot = 1; slot < kProductSlots; ++slot) enabled[slot] = true;
    } else {
        for (uint32_t slot = 1; slot < kProductSlots; ++slot) enabled[slot] = false;
        Text token;
        auto *list = reinterpret_cast<cRZStringTokenizer *>(&key.s);
        while (list->NextToken(',', &token.s)) {
            // atoi (0x830972; its wrapper is byte-identical to _wtoi's, but the parser it calls, 0x82fdbf, reads chars).
            const int product = std::atoi(token.s.ToChar());
            if (static_cast<uint32_t>(product - 1) < kProductSlots - 1) enabled[product] = true;
        }
    }

    for (uint32_t slot = 0; slot < kProductSlots; ++slot) {
        if (!enabled[slot]) continue;
        key.s.Sprintf(kProductSwitch, slot);
        Text directory;
        if (!commandLine->IsSwitchPresent(key.Face(), directory.Face(), true)) {
            bool fromIni = false;
            Text iniPath;
            appinit::CommonImplSlot4(kOptionProductIni, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&iniPath.s)), 1, 0, 0xffffffff);
            if (cRZFile::PathExists(iniPath.Face())) {
                Text section("Directories");
                alignas(cTSIniFile) unsigned char iniBytes[sizeof(cTSIniFile)];
                auto *ini = reinterpret_cast<cTSIniFile *>(iniBytes);
                ini->Construct(iniPath.Face(), nullptr);
                {
                    Text value;
                    if (ini->GetString(section.Face(), key.Face(), &value.Face())) {
                        cRZFileName::NormaliseSeparators(&value.s, '/');
                        directory.s.mString.assign(value.s.mString.c_str(), value.s.mString.size());
                        fromIni = true;
                    }
                }
                ini->Destruct();
            }
            if (!fromIni) {
                if (slot == 0 && baseDirectory.s.Strlen() != 0) {
                    directory.s.FromChar(baseDirectory.s.ToChar());
                } else if (haveList && slot >= 1) {
                    if (slot == kRunningProduct) DirectoryFromExecutable(directory, executable);
                    else if (slot <= static_cast<uint32_t>(listedCount) && !installed[slot - 1].empty())
                        directory.s.FromChar(installed[slot - 1].c_str());
                } else if (slot == kRunningProduct) {
                    DirectoryFromExecutable(directory, executable);
                }
            }
        }

        if (directory.s.Strlen() != 0) { // (the original also clears enabled[slot] when empty; nothing reads it again)
            cRZFileName::NormaliseSeparators(&directory.s, '/');
            directory.s.mString.append("TSData", 6);
            if (slot == kRunningProduct) {
                Text nonShip;
                nonShip.s.mString = directory.s.mString + "NonShip";
                cRZFileName::NormaliseSeparators(&nonShip.s, '/');
                if (cRZFile::PathExists(nonShip.Face())) app.AddOption(kOptionDataRoot, 0, nonShip.s.ToChar(), 0, 0x20000);
            }
            cRZFileName::NormaliseSeparators(&directory.s, '/');
            if (cRZFile::PathExists(directory.Face()))
                app.AddOption(kOptionDataRoot, 0, directory.s.ToChar(), 0, static_cast<int32_t>(1u << slot));
        }
        if (slot == kRunningProduct) {
            void *fileSystem = OriginalFunction<GetFileSystem, 0x9c59d9>()();
            VirtualCall<bool(__thiscall *)(void *, const cIGZString *), kFileSystemSetDataDirectory>(fileSystem, &directory.Face());
        }
    }

    if (haveList) {
        for (int32_t i = 0; i < listedCount; ++i) std::free(listed[i]);
        if (listed) std::free(listed);
    }
}

} // namespace

// ---- 0xca8710 (slot 15) ----

bool cTSAppCommonImpl::SetupUserDataDirectories()
{
    if (!InitOptions()) return false;
    cIGZCmdLine *commandLine = CommandLine();

    // The downloads directory: the command line's TS2DownloadsDir, else "TS2Downloads/" next to the data root (its
    // last separator dropped first, so the directory's parent).
    Text path;
    if (!Switch(commandLine, "TS2DownloadsDir", path)) {
        appinit::CommonImplSlot4(kOptionDataRoot, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&path.s)), 1, 0, 0xffffffff);
        auto *helpers = reinterpret_cast<cRZStringHelpers *>(&path.s);
        if (helpers->EndsWith("\\/", 1, 1) || helpers->EndsWith("/", 1, 1)) path.s.Erase(path.s.Strlen() - 1, 1);
        Text drive, directory;
        cRZFileName::SplitInto(&path.s, &drive.s, &directory.s, nullptr, nullptr);
        Text leaf("TS2Downloads");
        OriginalFunction<MakePath, 0x1c2732>()(&path.s, &drive.s, &directory.s, &leaf.s, nullptr);
        path.s.mString.push_back('/');
    }
    cRZFileName::NormaliseSeparators(&path.s, '/');
    AddOption(kOptionDownloads, 0, path.s.Data(), 0, -1);

    for (const FixedOption &o : kDataRoots) AddOption(o.key, o.alias, o.text, o.toOverrides, o.mask);

    // The log directory: the command line's logDir, else "Logs/" under the user data. Unless userDataMode is
    // "install", the product's key is read first (GetProductPaths, 0xcb4690; its result is not used).
    Text logs;
    if (Switch(commandLine, "logDir", logs)) {
        cRZFileName::NormaliseSeparators(&logs.s, '/');
        AddOption(kOptionLogs, 0, logs.s.Data(), 0, -1);
    } else {
        if (!Switch(commandLine, "userDataMode", logs) || !logs.s.IsEqual("install", 0xffffffff, false)) {
            char buffer[0x104] = {};
            uint32_t size = sizeof(buffer);
            product::GetProductPaths("Sims2.exe", buffer, &size, nullptr, nullptr, nullptr, nullptr, nullptr);
        }
        AddOption(kOptionLogs, kOptionUserData, "Logs/", 0, -1);
    }

    const int32_t current = static_cast<int32_t>(OriginalFunction<ProductMask, 0xb39360>()());
    for (const FixedOption &o : kFixedOptions) AddOption(o.key, o.alias, o.text, o.toOverrides, o.mask == kCurrent ? current : o.mask);
    return true;
}

// ---- 0xca6c10 (slot 16) ----

bool cTSAppCommonImpl::ReadResourceConfiguration()
{
    Text path;
    OptionStringList list;
    if (!FindExisting(kOptionResourceIni, list, path)) FindExisting(kOptionDefaultIni, list, path);

    alignas(8) uint8_t memory[0x40]; // the reader's size (its deleting destructor frees 0x40)
    OriginalFunction<IniConstruct, 0x9c5df3>()(memory);
    auto *ini = reinterpret_cast<cRZIniFile *>(memory);
    ini->SetPath(path.Face());
    static const struct { const char *name; IniCallback callback; } kSections[] = {
        {"ResourceFactories", &cTSAppCommonImplAdapters::OnResourceFactory},
        {"DBSegments", &cTSAppCommonImplAdapters::OnDBSegment},
        {"DBSegmentDirectoryFiles", &cTSAppCommonImplAdapters::OnDBSegmentDirectoryFile},
    };
    for (const auto &section : kSections) {
        Text name(section.name);
        ini->ForEachKey(*name.s.AsInterface(), reinterpret_cast<cRZIniFile::KeyCallback>(section.callback), this);
    }
    ini->Destruct();
    return true;
}

// ---- 0xca75c0 (slot 14) and 0xca6910 ----

bool cTSAppCommonImpl::FindProductDirectories()
{
    void *fileSystem = OriginalFunction<GetFileSystem, 0x9c59d9>()();
    Text executable(VirtualCall<const char *(__thiscall *)(void *), kFileSystemAppDirectory>(fileSystem));
    cRZFileName::NormaliseSeparators(&executable.s, '/');
    AddOption(kOptionExecutableDir, 0, executable.s.ToChar(), 0, -1);
    if (HasFlags(kFlagDevelopmentLayout)) AddDevelopmentDataDirectory(*this, executable);
    else AddProductDataDirectories(*this, executable);
    return true;
}

bool cTSAppCommonImpl::HasFlags(uint32_t mask) const
{
    return (mFlags & mask) != 0;
}

// ---- 0xca74b0 / 0xca7020 / 0xca7130: the section callbacks

void __cdecl cTSAppCommonImplAdapters::OnResourceFactory(cIGZString *key, cIGZString *value, void *)
{
    RegisterIniEntry(*GameAddress<uint32_t>(kResourceFactoriesWord), key, value);
}
void __cdecl cTSAppCommonImplAdapters::OnDBSegment(cIGZString *key, cIGZString *value, void *)
{
    RegisterIniEntry(*GameAddress<uint32_t>(kDBSegmentsWord), key, value);
}
void __cdecl cTSAppCommonImplAdapters::OnDBSegmentDirectoryFile(cIGZString *key, cIGZString *value, void *)
{
    RegisterIniEntry(static_cast<uint32_t>(reinterpret_cast<uintptr_t>("CN_DBSDF")), key, value);
}

// ---- adapters

[[msvc::forceinline_calls]] bool __fastcall cTSAppCommonImplAdapters::SetupUserDataDirectories(cTSAppCommonImpl *self, void *)
{
    return self->cTSAppCommonImpl::SetupUserDataDirectories();
}
[[msvc::forceinline_calls]] bool __fastcall cTSAppCommonImplAdapters::ReadResourceConfiguration(cTSAppCommonImpl *self, void *)
{
    return self->cTSAppCommonImpl::ReadResourceConfiguration();
}
[[msvc::forceinline_calls]] bool __fastcall cTSAppCommonImplAdapters::FindProductDirectories(cTSAppCommonImpl *self, void *)
{
    return self->cTSAppCommonImpl::FindProductDirectories();
}
[[msvc::forceinline_calls]] bool __fastcall cTSAppCommonImplAdapters::HasFlags(cTSAppCommonImpl *self, void *, uint32_t mask)
{
    return self->HasFlags(mask);
}

} // namespace ts
