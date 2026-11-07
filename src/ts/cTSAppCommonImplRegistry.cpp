// ts::cTSAppCommonImpl: the two registry handlers RegisterRegistryHandlers (slot 17) registers (lane MB, F4e): the
// resource factories (CN_PRF, 0xca5db0) and the data-base segments (CN_PDBS, 0xca5170, with the segment builder 0xca4f70).
// Each is called once per key and value of its registry table, __cdecl, with the application object as the context
// (not read).
#include "ts/cTSAppCommonImpl.h"

#include "app/AppInit.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/cIGZPersistDBSegment.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "misc/HexParse.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/StaticGetters.h"
#include "rz/StringKeyParser.h"
#include "rz/cRZFile.h"
#include "rz/cRZIniFile.h"
#include "rz/cRZLanguageManager.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace ts {
namespace {

constexpr uint32_t kResourceManagerService = 0x56b906e;
constexpr uint32_t kResourceManagerIid = 0x656b8efc;
constexpr uint32_t kSegmentIid = 0x656b8f0c;           // cIGZPersistDBSegment, asked of the class object
constexpr uint32_t kDirectoryFilesIid = 0x856c8cf7;    // cIGZPersistDBSegmentDirectoryFiles
constexpr uint32_t kMultiPackedFilesIid = 0xe90f85f3;  // cIGZDBSegmentMultiPackedFiles
constexpr uint32_t kOptionDataRoot = 0x4bcfce5e;       // each product's data directory (mask: the product)
constexpr int kDirectoryFilesSlot15 = 15;              // called with (1, 1) on a directory-files segment; not named yet
constexpr char kBeginGroup[] = "BEGIN_GROUP";          // the table's group markers (0x1085f08 / 0x1085f28): skipped
constexpr char kEndGroup[] = "END_GROUP";
constexpr char kClassDelimiter[] = ";";                // 0xdab158: "<class id>;<path>"
constexpr char kLanguageToken[] = "%Language%";        // 0xeb5ad4
constexpr char kDataDirectoryToken[] = "%DataDirectory%"; // 0xdaaa5c
constexpr char kDefaultLanguageName[] = "English";

// 0xb39360 (not ours): the running product's mask bit (0x20000).
using ProductMask = uint32_t(__cdecl *)();

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

// The string's text by its slots 7 and 6 (length first, as the original).
std::string TextOf(cIGZString *string)
{
    const uint32_t length = string->Strlen();
    return std::string(string->Data(), length);
}

// The resource manager (service 0x56b906e) through the framework; null without one. The caller releases it.
ifaces::cIGZPersistResourceManager *ResourceManager()
{
    void *manager = nullptr;
    auto *framework = reinterpret_cast<cIGZFrameWork *>(static_cast<uintptr_t>(RZGetGlobal10C9378()));
    if (framework) framework->GetSystemService(kResourceManagerService, kResourceManagerIid, &manager);
    return static_cast<ifaces::cIGZPersistResourceManager *>(manager);
}

// Release (slot 2 of every GZ interface; the resource manager's face leaves it unnamed).
void ReleaseIf(void *object)
{
    if (object) static_cast<cIGZUnknown *>(object)->Release();
}

// prefix + each directory of the option + suffix, for the directories of the products `mask` selects (the list emptied
// and refilled by GetOptionList), last to first: the segment for every such path that exists.
void AddDataDirectorySegments(OptionStringList &list, uint32_t classId, uint32_t typeId, const std::string &prefix,
                              const std::string &suffix, uint32_t mask)
{
    appinit::CommonImplSlot5(kOptionDataRoot, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&list)), 1, 0, mask);
    for (auto directory = list.rbegin(); directory != list.rend(); ++directory) {
        Text path;
        path.s.mString = prefix + directory->c_str() + suffix;
        if (cRZFile::PathExists(path.Face())) cTSAppCommonImplAdapters::AddPathSegment(classId, typeId, &path.Face());
    }
}

} // namespace

// ---- 0xca5db0: a resource factory: the key is the resource type, the value the factory's class id (each a number,
// hexadecimal after 0x), registered with the resource manager (no null test on it, as the original).

void __cdecl cTSAppCommonImplAdapters::OnResourceFactoryEntry(cIGZString *key, cIGZString *value, void *)
{
    const uint32_t type = StringKeyParser::ParseNumber(key->ToChar());
    const uint32_t factoryId = StringKeyParser::ParseNumber(value->ToChar());
    ifaces::cIGZPersistResourceManager *manager = ResourceManager();
    manager->RegisterFactory(factoryId, type, nullptr);
    ReleaseIf(manager);
}

// ---- 0xca5170: a data-base segment: the key is the segment's type id (hexadecimal), the value "<class id>;<path>".
// The path's first %Language% becomes the current language's name ("English" when the service has none), and a
// %DataDirectory% makes one segment per existing product data directory (the other products' first, then the running
// one's); otherwise the path makes one segment. The group markers are skipped.

void __cdecl cTSAppCommonImplAdapters::OnPathRegistryEntry(cIGZString *key, cIGZString *value, void *)
{
    const std::string keyText = TextOf(key);
    if (keyText == kBeginGroup || keyText == kEndGroup) return;
    const uint32_t typeId = hexparse::ParseHex32(keyText.c_str());

    Text path;
    path.s.mString = TextOf(value);
    auto *helpers = reinterpret_cast<cRZIniStringHelpers *>(&path.s);
    uint32_t classId;
    {
        cRZString head;
        helpers->SplitOff(&head, kClassDelimiter);
        classId = hexparse::ParseHex32(head.mString.c_str());
        head.Destruct();
    }
    GetDirectorFrameworkObject(); // (its result is not used)

    if (path.s.mString.find(kLanguageToken) != std::string::npos) {
        auto *languages = static_cast<cIGZLanguageManager *>(appinit::GetLanguageService());
        Text language(kDefaultLanguageName);
        languages->HasLanguageName(&language.Face(), 0);
        cRZString head;
        helpers->SplitOff(&head, kLanguageToken);
        path.s.mString = head.mString + language.s.mString + path.s.mString;
        head.Destruct();
    }

    if (path.s.mString.find(kDataDirectoryToken) == std::string::npos) {
        AddPathSegment(classId, typeId, &path.Face());
        return;
    }
    cRZString prefix;
    helpers->SplitOff(&prefix, kDataDirectoryToken);
    const std::string suffix = path.s.mString;
    {
        OptionStringList list;
        const uint32_t others = ~OriginalFunction<ProductMask, 0xb39360>()();
        AddDataDirectorySegments(list, classId, typeId, prefix.mString, suffix, others);
        const uint32_t running = OriginalFunction<ProductMask, 0xb39360>()();
        AddDataDirectorySegments(list, classId, typeId, prefix.mString, suffix, running);
    }
    prefix.Destruct();
}

// ---- 0xca4f70: one segment: the class object `classId` (as a cIGZPersistDBSegment), initialised, given the type id,
// the path and the resource manager's default cache, then opened for reading and added to the manager. True when added,
// and also when it would not open but is a directory-files or multi-packed-files segment whose directory is empty;
// false otherwise. A directory-files segment gets its slot 15 called with (1, 1) first.

bool __cdecl cTSAppCommonImplAdapters::AddPathSegment(uint32_t classId, uint32_t typeId, cIGZString *path)
{
    ifaces::cIGZPersistDBSegment *segment = nullptr;
    auto *com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    bool result = false;
    if (static_cast<uint8_t>(com->GetClassObject(classId, kSegmentIid, reinterpret_cast<void **>(&segment))) &&
        segment->Init()) {
        segment->SetSegmentID(&typeId);
        void *directoryFiles = nullptr;
        const bool isDirectoryFiles = segment->QueryInterface(kDirectoryFilesIid, &directoryFiles) != 0;
        if (isDirectoryFiles) VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t), kDirectoryFilesSlot15>(directoryFiles, 1, 1);
        void *multiPackedFiles = nullptr;
        const bool isMultiPackedFiles = segment->QueryInterface(kMultiPackedFilesIid, &multiPackedFiles) != 0;
        if (segment->SetPath(path)) {
            ifaces::cIGZPersistResourceManager *manager = ResourceManager();
            segment->SetSegmentCache(manager->GetDefaultSegment());
            if (segment->Open(1, 0)) {
                manager->AddSegment(segment);
                result = true;
            } else if (isDirectoryFiles || isMultiPackedFiles) {
                // ORIGINAL-BUG: meant to accept an empty directory, the original builds a DirectoryScan of the path
                // without scanning it (scanNow false) and tests its count, which is then always 0: such a segment
                // that fails to open always counts as added (not added to the manager).
                result = true;
            }
            ReleaseIf(manager);
        }
        ReleaseIf(multiPackedFiles);
        ReleaseIf(directoryFiles);
    }
    ReleaseIf(segment);
    return result;
}

} // namespace ts
