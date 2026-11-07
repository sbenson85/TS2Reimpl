// ts::cTSAppFW in the standalone shape (lane FG). Built for size, as the game's (0xc26147..0xc2a7de are packed).
#include "rz/RZMessageServer.h"
#include "ts/cTSAppFW.h"

#include "app/AppBase.h"
#include "app/AppCOMDirectors.h"
#include "app/AppDebugger.h"
#include "app/AppInit.h"
#include "world/cWorldDB.h"
#include "app/AppServices.h"
#include "app/AppServices3.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZClipboardManager.h"
#include "gz/cGZPropertyManager.h"
#include "ifaces/cITSFamily.h"
#include "ifaces/cITSGameStateController.h"
#include "ifaces/cITSNeighborhoodInfo.h"
#include "gz/cGZRegistry.h"
#include "gz/cGZTimerService.h"
#include "gz/cIGZFrameWork.h"
#include "rz/COMDirectors.h"
#include "rz/DirectoryScan.h"
#include "rz/RZFrameworkHelpers.h"
#include "app/AppServices2.h"
#include "app/IniFile.h"
#include "app/ProductInfo.h"
#include "app/ReportFinish.h"
#include "app/SpecialFolders.h"
#include "app/WinMain.h"
#include "gz/GZCOM.h"
#include "gz/cGZPropertySet.h"
#include "rz/RZTextConvert.h"
#include "gz/cIGZPropertySet.h"
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersB.h"
#include "rz/StringKeyParser.h"
#include "rz/cRZFile.h"
#include "rz/cRZFileName.h"
#include "rz/RZMemory.h"
#include "rz/ServiceGetters.h"
#include "rz/StaticGetters2.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/cGZAllocatorService.h"
#include "rz/cRZCallbackSystemService.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZNetManager.h"
#include "rz/cRZNetSSLManager.h"
#include "rz/nGZLog.h"
#include "ts/DynamicLibraryList.h"
#include "ts/cTSAppFWHelpers.h"
#include "ts/cTSAppPropertyFiles.h"
#include "ts/cTSGlobals.h"
#include "ts/cTSString.h"

#define NOMINMAX
#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <new>

namespace ts {

namespace {

// Imports, called through the game's import table (the tests point these slots at fakes).
constexpr uint32_t kIatRegisterWindowMessageA = 0xd9a4e4, kIatCoInitialize = 0xd9a6d4, kIatCoUninitialize = 0xd9a6e8;
constexpr uint32_t kIatOpenEventA = 0xd9a3e4, kIatSetEvent = 0xd9a10c, kIatCloseHandle = 0xd9a17c;

template <typename Fn> Fn Import(uint32_t iatRva)
{
    return *GameAddress<Fn>(iatRva);
}

using Slot0 = uint32_t(__thiscall *)(void *);

constexpr int kCmdSlotSwitchWithValue = 8, kCmdSlotSwitchPresent = 9; // cIGZCmdLine::IsSwitchPresent(name, value, trim) / (name)
constexpr int kMutexSlotDelete = 0, kMutexSlotUnlock = 2, kMutexSlotTryLock = 3;

// cIGZCmdLine slot 8: IsSwitchPresent(name, value, trimQuotes) and slot 9: IsSwitchPresent(name), through the command
// line's own vtable. Only al is read.
uint8_t SwitchWithValue(cIGZCmdLine *commandLine, const char *name, cRZString *value)
{
    cRZString key;
    key.Construct(name);
    uint8_t found = VirtualCall<uint8_t(__thiscall *)(void *, const void *, void *, uint32_t), kCmdSlotSwitchWithValue>(commandLine, &key, value, 1);
    key.Destruct();
    return found;
}

uint8_t SwitchPresent(cIGZCmdLine *commandLine, const char *name)
{
    cRZString key;
    key.Construct(name);
    uint8_t found = VirtualCall<uint8_t(__thiscall *)(void *, const void *), kCmdSlotSwitchPresent>(commandLine, &key);
    key.Destruct();
    return found;
}

// A named mutex made the way the constructor makes them (a null from operator new, which the original tests for, is
// passed on as a null mutex).
cRZMutex *MakeMutex(const char *name)
{
    auto *mutex = static_cast<cRZMutex *>(::operator new(cRZMutex::kSize));
    if (!mutex) return nullptr;
    cRZString text;
    text.Construct(name);
    cRZMutex *made = mutex->Construct(*text.AsInterface());
    text.Destruct();
    return made;
}

// cIGZUnknown slots on objects of classes still hook-shaped.
void AddRefObject(void *object) { VirtualCall<Slot0, 1>(object); }
void ReleaseObject(void *object) { VirtualCall<Slot0, 2>(object); }
bool QueryObject(void *object, uint32_t riid, void **out)
{
    return VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(object, riid, out);
}

// A face pointer `offset` bytes into an object, null for a null object (the original's neg / sbb / and).
void *FaceOf(void *object, uint32_t offset)
{
    return object ? static_cast<char *>(object) + offset : nullptr;
}

// operator new, then the hook-shaped class's constructor member when the allocation worked.
template <typename T> T *NewObject(uint32_t size)
{
    auto *memory = static_cast<T *>(::operator new(size));
    return memory ? memory->Construct() : nullptr;
}

constexpr uint32_t kIidLogService = 0x287fb697; // the cIGZSystemService face of the log service and the file system
constexpr uint32_t kIidMessageStandard = 0x65297976;

// The application's message ids (DoMessage).
constexpr uint32_t kMsgStateQuery = 0xc3, kMsgReleaseObject60 = 0xcd, kMsgFlag4 = 0xce;
constexpr uint32_t kMsgFatal = 0xb627ad3, kMsgCrashData = 0xb627ad4, kMsgIgnoredA = 0xb627ad2, kMsgIgnoredB = 0xb627ad5;
constexpr uint32_t kMsgStringTables = 0xce1505fd;
constexpr uint32_t kStateQueryKey = 0x89b7dc8;

constexpr uint32_t kAppNameText = 0xe99000; // InitOptions's text (a game string constant)

// ---- OnTick / Shutdown

constexpr uint32_t kIatSleep = 0xd9a144;
constexpr uint32_t kSettingsService = 0x0ac1887e, kSettingsIid = 0x4bb0d269; // the property manager (0x1cbaf3)
constexpr uint32_t kViewService = 0xfb6372ee, kViewIid = 0x5b637209;         // 0x18ae10
constexpr uint32_t kEsperService = 0x8d127822, kEsperIid = 0xad127830;       // 0xc260e1
constexpr uint32_t kAnyUint32VTable = 0xd9f534, kAnyVTable = 0xd9c654;
constexpr uint32_t kOnTickSleepTimeText = 0xe98c4c, kEsperSessionText = 0xe98c1c;
constexpr uint32_t kTickStateStep = 0xbea28c2;
constexpr uint32_t kMsgShutdown = 0xb466ab3;
constexpr uint32_t kMemorySnapshotVTable = 0xda31e0;

// The framework service's slot 5 (GetSystemService(id, iid, out)) into a fresh reference, null when there is no service
// object: the 37-byte getters 0x1cbaf3, 0x18ae10, 0xc260e1.
void *QueryFrameworkService(uint32_t id, uint32_t iid)
{
    void *out = nullptr;
    if (void *service = GetServiceObject())
        VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 5>(service, id, iid, &out);
    return out;
}

// A local reference the frame releases when an exception escapes; Release() is the normal path's explicit release.
struct LocalReference {
    void *mObject;
    explicit LocalReference(void *object) : mObject(object) {}
    LocalReference(const LocalReference &) = delete;
    LocalReference &operator=(const LocalReference &) = delete;
    ~LocalReference()
    {
        if (mObject) ReleaseObject(mObject);
    }
    void Release()
    {
        void *object = mObject;
        mObject = nullptr;
        if (object) ReleaseObject(object);
    }
};

// A uint32 value as the settings service fills it (cGZAny<uint32>, game vtable).
struct UintValue {
    const void *mVTable;
    uint32_t mValue;
};

// The memory snapshot Shutdown takes (0x18 bytes, game vtable 0xda31e0; 0x1bbf19 fills it from the process memory
// counters when psapi answers). Nothing reads it.
struct MemorySnapshot {
    const void *mVTable;
    uint32_t mCounters[5];
};

// A standard message made as 0x1c033d does, from RZMalloc (0x2c bytes); null when the allocation failed.
cRZMessage2Standard *NewStandardMessage()
{
    auto *raw = static_cast<cRZMessage2Standard *>(RZMemory::Malloc(cRZMessage2Standard::kSize));
    if (raw) {
        raw->mVTable = &ts2_rva_da4064;
        raw->mRefCount = 0;
        raw->mFlags = 0;
        raw->mId = 0;
        for (uint32_t &v : raw->mValue) v = 0;
        raw->mPtr20 = raw->mPtr24 = raw->mPtr28 = nullptr;
    }
    return raw;
}

uint32_t Word(const void *pointer) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer)); }

// ---- PreFrameworkInit / SetupUserDataDirectories

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

// The options (cTSAppCommonImpl keys) these two read and write.
constexpr uint32_t kOptionResources = 0xec432753;          // "Res/" of the data root
constexpr uint32_t kOptionUserData = 0x2bcfce83;           // the user-data root
constexpr uint32_t kOptionAllUsersData = 0xebcfce8e;       // the all-users data root
constexpr uint32_t kOptionUserDataFolders = 0xcce45144;    // what the user-data folders are relative to
constexpr uint32_t kOptionNeighborhoods = 0x4c4324c5;
constexpr uint32_t kOptionConfig = 0xade2e3ba;
constexpr uint32_t kOptionLogs = 0x4d4896ba;
constexpr uint32_t kOptionLocale = 0x6cd89f8d;
constexpr uint32_t kOptionPackageDirectories = 0xcc9f6e91; // the directories holding eCAS.package / uitext.package
constexpr int32_t kProductMask = 0x20000;                  // this executable's product (0xb39360)

// Game string constants.
constexpr uint32_t kProductNameText = 0xe98108, kEAGamesText = 0xe9900c, kUserDataModeText = 0xe99018;
constexpr uint32_t kInstallText = 0xe99028, kDevelopText = 0xe99030, kUserDataText = 0xe99038;
constexpr uint32_t kActiveUserDataDirText = 0xe98b80, kTemplateDevText = 0xda6394, kHoodDevText = 0xda63a0;
constexpr uint32_t kNeighborhoodTemplateText = 0xe990e0, kNeighborhoodsText = 0xe99044, kLotCatalogText = 0xe990bc;
constexpr uint32_t kActiveAllUsersDataDirText = 0xe990f8, kAllUsersDataText = 0xe99110, kEmptyText = 0xd9b9d0;
constexpr uint32_t kCasPackageName = 0xe98f90, kUiTextPackageName = 0xda62f4, kPackageExtension = 0xda62b0;
constexpr uint32_t kNoExceptionUploadText = 0xe98b44, kLocaleText = 0xe98f98, kImeText = 0xe98fa0;

constexpr int kFolderDocuments = 8, kFolderAllUsers = 9; // 0x1c50d6's folder kinds
constexpr uint32_t kFileSize = 0x9c;                     // cRZFile
constexpr uint32_t kImeServiceA = 0x2ac4516b, kImeServiceB = 0x2ac4516d;

// The user-data folders, relative to kOptionUserDataFolders, in the original's order.
struct UserDataFolder {
    uint32_t key;
    const char *text;
};
constexpr UserDataFolder kUserDataFolders[] = {
    {0x8ce453ed, "Neighborhoods/"}, {0x2d4852ce, "Collections/"}, {0x518033cd, "PetBreeds/"}, {0x31883954, "PetCoats/"},
    {0xad62b375, "Storytelling/"}, {0x6d93abe7, "Thumbnails/"},   {0x4dbdda90, "SC4Terrains/"}, {0x8dd48787, "Cameras/"},
    {0xcde2e39b, "Config/"},       {0x0df2a7ca, "LotCatalog/"},   {0x31a54f14, "LockedBins/"}, {0x71d33748, "Paintings/"},
};

// 0x1c5031 (ours, lane LY): DirectoryExists, else CreateDirectories.
void EnsureDirectory(Text &path) { folders::EnsureDirectory(&path.s); }

// "<special folder>/EA Games/<product>/": 0x1c50d6 (ours, lane LY: the shell folder of the kind, created when missing)
// with '/' separators.
void UserDataRoot(int folder, Text &path, const Text &product)
{
    folders::GetSpecialFolderPath(folder, &path.s, 1);
    cRZFileName::NormaliseSeparators(&path.s, '/');
    path.s.mString.append(GameAddress<const char>(kEAGamesText), 8);
    path.s.mString.push_back('/');
    path.s.mString.append(product.s.mString);
    path.s.mString.push_back('/');
}

} // namespace

// ---- 0xc26147: the bases, the members, then the switches of the process command line

cTSAppFW::cTSAppFW(uint32_t serviceId, const char *name, cIGZCmdLine *commandLine)
    : gz::cGZApp(serviceId, name), mPropertyFiles(nullptr), mByte6c(1), mOtherInstance(0), mOtherBodyShop(0), mWord70(0),
      mMutex(nullptr), mObject80(nullptr), mWord88(0), mWord8c(0), mWord90(0), mShutdownMessage(0)
{
    mReporter.Construct();
    mString244.Construct();
    mFlags264 = static_cast<uint8_t>(mFlags264 & 0xf8); // bits 0..2 cleared, the rest as it was
    appbase::DebuggerPresentCached();                    // the answer is not used

    cRZString value;
    value.Construct();

    // "ExceptionHandling": the reporter is enabled only for "on" or "enabled" (absent, "off", "disabled" and anything
    // else leave it off).
    bool enableReporter = false;
    if (SwitchWithValue(commandLine, "ExceptionHandling", &value)) {
        if (appbase::StringEqualsCString(&value.mString, nullptr, "on")) enableReporter = true;
        else if (appbase::StringEqualsCString(&value.mString, nullptr, "enabled")) enableReporter = true;
        else if (appbase::StringEqualsCString(&value.mString, nullptr, "off")) enableReporter = false;
        else if (appbase::StringEqualsCString(&value.mString, nullptr, "disabled")) enableReporter = false;
    }
    auto *reporter = reinterpret_cast<appbase::cExceptionReporter *>(&mReporter);
    reporter->SetEnabled(enableReporter);
    mShutdownMessage = Import<uint32_t(WINAPI *)(const char *)>(kIatRegisterWindowMessageA)("EA_LAUNCHER_SHUTDOWN_REQUEST_SIMS2EP");

    {
        cRZString reportName, dumpName;
        dumpName.Construct("Sims2Exception"); // built first, passed second
        reportName.Construct("Sims2Exception");
        reporter->SetNames(&reportName, &dumpName);
        // The temporaries die in reverse order: the one built first (passed second) last.
        reportName.Destruct();
        dumpName.Destruct();
    }
    reporter->SetFlag19a(1);

    // "quietCrash" and "noExceptionUpload" become bits 0 and 1 of the flag byte; only bit 0 of the answer is used.
    {
        uint8_t quiet = SwitchPresent(commandLine, "quietCrash");
        mFlags264 = static_cast<uint8_t>((mFlags264 & ~1) | (quiet & 1));
        uint8_t noUpload = SwitchPresent(commandLine, "noExceptionUpload");
        mFlags264 = static_cast<uint8_t>((mFlags264 & ~2) | ((noUpload & 1) << 1));
    }

    mMessageIds.insert(kMsgStateQuery);
    mMessageIds.insert(kMsgStringTables);
    mMessageIds.insert(kMsgFlag4);
    mMessageIds.insert(kMsgReleaseObject60);

    Import<long(WINAPI *)(void *)>(kIatCoInitialize)(nullptr);

    // The single-instance mutexes: the application's own is kept, the Body Shop's only probed.
    mMutex = MakeMutex("Global\\The Sims 2");
    cRZMutex *bodyShop = MakeMutex("Global\\The Sims 2 Body Shop");

    if (SwitchPresent(commandLine, "installUserData")) {
        // Both flags clear (one 16-bit store); the Body Shop mutex is released without having been taken.
        // ORIGINAL-BUG: ReleaseMutex on a mutex this thread does not own (fails harmlessly).
        mOtherInstance = 0;
        mOtherBodyShop = 0;
        VirtualCall<Slot0, kMutexSlotUnlock>(bodyShop);
    } else {
        uint32_t ours = VirtualCall<Slot0, kMutexSlotTryLock>(mMutex);
        mOtherInstance = ours == 0;
        uint32_t theirs = VirtualCall<Slot0, kMutexSlotTryLock>(bodyShop);
        mOtherBodyShop = theirs == 0;
        if (theirs != 0) VirtualCall<Slot0, kMutexSlotUnlock>(bodyShop);
    }
    VirtualCall<Slot0, kMutexSlotDelete>(bodyShop);

    // The switch the developers use to test the crash reporter: a write to address 4.
    if (SwitchPresent(commandLine, "crashTest")) *reinterpret_cast<volatile uint8_t *>(4) = 0x7b;

    value.Destruct();
}

// ---- 0xc266b9: the mutex unlocked and deleted (a null mutex faults, as in the original), COM released, the two
// hook-shaped members torn down, the delegate released; then the list, the id set and the bases (the compiler's)

cTSAppFW::~cTSAppFW()
{
    VirtualCall<Slot0, kMutexSlotUnlock>(mMutex);
    VirtualCall<Slot0, kMutexSlotDelete>(mMutex);
    Import<void(WINAPI *)()>(kIatCoUninitialize)();
    mString244.Destruct();
    mReporter.Destruct();
    if (mObject80) ReleaseObject(mObject80);
}

// ---- 0xc297fa and the folded AddRef / Release

bool cTSAppFW::QueryInterface(uint32_t riid, void **out)
{
    if (riid == kIidMessageTarget) {
        *out = static_cast<cIGZMessageTarget2 *>(this);
        AddRef();
        return true;
    }
    if (riid == kIidAppFW) {
        *out = static_cast<cITSAppFW *>(this);
        AddRef();
        return true;
    }
    if (gz::cGZApp::QueryInterface(riid, out)) return true;
    return cTSAppCommonImpl::QueryInterface(riid, out);
}

uint32_t cTSAppFW::AddRef() { return gz::cGZApp::AddRef(); }
uint32_t cTSAppFW::Release() { return gz::cGZApp::Release(); }

// ---- 0xc27c3e (cIGZSystemService slot 8): the installation checks, then the application's start-up work

namespace {

constexpr uint32_t kIatGetCommandLineW = 0xd9a398, kIatGetDiskFreeSpaceExA = 0xd9a3e0;
constexpr uint32_t kIatRegOpenKeyExA = 0xd9a028, kIatRegQueryValueExA = 0xd9a054, kIatRegCloseKey = 0xd9a02c;
constexpr uint32_t kIatRegCreateKeyExA = 0xd9a030, kIatRegSetValueExA = 0xd9a024;
constexpr uint32_t kRegistryTextSize = 0x104;              // the key and value buffers of 0xc27a16

constexpr uint32_t kOptionControlDirectory = 0x2ce990aa; // where control0.dat / control1.dat are
constexpr uint32_t kOptionUserDataCheck = 0x4bcfce5e;    // the directory that must exist and have entries
constexpr uint32_t kExecutableNameGlobal = 0x10819b8;    // const char *: "Sims2EP9.exe"
constexpr uint32_t kSkinExtensionGlobal = 0x1081a94;     // const char *: ".Sims2Skin"
constexpr uint32_t kAppCaption = 0x10fda54;              // cRZString: the application's caption (LoadStringTableTexts)
constexpr uint32_t kInstallUserDataText = 0xe98884;      // "installUserData"
constexpr uint32_t kStringGroup = 0x7fa7dc7c;            // the group of the start-up error texts (table 0xb0 / 0xcd / 0xb5)
constexpr uint64_t kMinimumFreeBytes = 2000000000;       // below this on the user-data drive: the disk-space question
constexpr uint32_t kMsgUserData = 0xebf16aac, kMsgLoadList = 0x2bea2063, kMsgLoadOne = 0xcbea26d7; // the start message's ids
constexpr uint32_t kSettingsTargetService = 0x9b3798f7, kSettingsTargetIid = 0xfb3798e8;       // 0xc260bc: the +0x80 delegate

constexpr int kGlobalsSlotSkipInstallCheck = 31, kGlobalsSlotPropertyTarget = 21; // GetCachedSingleton()'s slots
constexpr int kTargetSlotSetProperties = 17;
constexpr int kMsgSlotSetId = 5, kMsgSlotSetValue0 = 11, kMsgSlotSetValue1 = 16, kMsgSlotSetText = 30;
constexpr int kServerSlot11 = 11, kServerSlotPost = 4;

using RegOpenKeyExAFn = LONG(WINAPI *)(HKEY, const char *, DWORD, REGSAM, HKEY *);
using RegQueryValueExAFn = LONG(WINAPI *)(HKEY, const char *, DWORD *, DWORD *, BYTE *, DWORD *);
using RegCloseKeyFn = LONG(WINAPI *)(HKEY);
using RegCreateKeyExAFn = LONG(WINAPI *)(HKEY, const char *, DWORD, char *, DWORD, REGSAM, void *, HKEY *, DWORD *);
using RegSetValueExAFn = LONG(WINAPI *)(HKEY, const char *, DWORD, DWORD, const BYTE *, DWORD);

// GetProductPaths (0xcb4690, src/app/ProductInfo.h): the product's registry key ("Software\Electronic Arts\...\") and the
// executable's product directory (under the game's root), each copied out with its length when asked for.
void ProductPaths(const char *executable, char *key, uint32_t *keyLength, char *directory, uint32_t *directoryLength)
{
    product::GetProductPaths(executable, key, keyLength, directory, directoryLength, nullptr, nullptr, nullptr);
}

// A start-up error text (string table `table`, entry `entry`, group kStringGroup) assigned to `message`.
void AssignErrorText(Text &message, uint16_t table, uint16_t entry)
{
    cTSString text;
    text.Construct(table, entry, kStringGroup, cTSString::kStringTableType);
    reinterpret_cast<RZStringSlot *>(&message.s.mString)->Assign(text.mString.mString);
    text.Destruct();
}

// The skin file type: HKEY_CLASSES_ROOT\.Sims2Skin = "The Sims 2 Package Installer", and when that was set, the
// installer's DefaultIcon ("<dir>\CSBin\PackageInstaller.exe,1") and Shell\Open\Command ("<dir>\...exe "%1""). The
// last two keys' creation is not checked: a failed one sets the value on the handle left from before (closed).
void RegisterSkinFileType(const char *installDirectory)
{
    Text program(installDirectory);
    program.s.mString.append("\\CSBin\\PackageInstaller.exe", 0x1b);
    Text name("The Sims 2 Package Installer");
    Text iconKey, icon, commandKey, command;
    iconKey.s.mString = name.s.mString + "\\DefaultIcon";
    icon.s.mString = program.s.mString + ",1";
    commandKey.s.mString = name.s.mString + "\\Shell\\Open\\Command";
    command.s.mString = program.s.mString + " \"%1\"";

    const auto create = Import<RegCreateKeyExAFn>(kIatRegCreateKeyExA);
    const auto close = Import<RegCloseKeyFn>(kIatRegCloseKey);
    HKEY key;
    DWORD disposition;
    auto setDefault = [&](Text &value) {
        return Import<RegSetValueExAFn>(kIatRegSetValueExA)(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE *>(value.s.ToChar()), value.s.Strlen() + 1);
    };
    if (create(HKEY_CLASSES_ROOT, *GameAddress<const char *>(kSkinExtensionGlobal), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, &disposition) != ERROR_SUCCESS)
        return;
    const LONG set = setDefault(name);
    close(key);
    if (set != ERROR_SUCCESS) return;
    create(HKEY_CLASSES_ROOT, iconKey.s.ToChar(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, &disposition);
    setDefault(icon);
    close(key);
    create(HKEY_CLASSES_ROOT, commandKey.s.ToChar(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, &disposition);
    setDefault(command);
    close(key);
}

} // namespace

// The start-up work after the checks passed (the second half of Init 0xc27c3e).
void cTSAppFW::StartUp(char *productKey, uint32_t *keyLength, char *installDirectory, uint32_t *directoryLength, const char *newerExecutable)
{
    mByte6c = 1;
    auto *framework = static_cast<cIGZFrameWork *>(GetServiceObject());
    cIGZCmdLine *commandLine = framework->CommandLine();
    ProductPaths(newerExecutable, productKey, keyLength, installDirectory, directoryLength);
    if (installDirectory[0]) RegisterSkinFileType(installDirectory);
    framework->AddToTick(static_cast<cIGZSystemService *>(this));
    {
        LocalReference delegate{QueryFrameworkService(kSettingsTargetService, kSettingsTargetIid)}; // 0xc260bc
        reinterpret_cast<RZUnknownSlot *>(&mObject80)->Assign(delegate.mObject);
        delegate.Release();
    }

    // The start message: installUserData, or the neighbourhood / lot to load (one, or a ';' list), else the default.
    cRZMessage2Standard *message = NewStandardMessage();
    if (message) AddRefObject(message);
    {
        using SetWord = void(__thiscall *)(void *, uint32_t);
        Text value;
        if (SwitchPresent(commandLine, GameAddress<const char>(kInstallUserDataText))) {
            VirtualCall<SetWord, kMsgSlotSetId>(message, kMsgUserData);
            VirtualCall<SetWord, kMsgSlotSetValue0>(message, 0x8000);
        } else if (SwitchWithValue(commandLine, "load", &value.s)) {
            if (value.s.Find(";", 0, true) != -1) {
                VirtualCall<SetWord, kMsgSlotSetId>(message, kMsgLoadList);
                VirtualCall<SetWord, kMsgSlotSetValue0>(message, 0);
                VirtualCall<SetWord, kMsgSlotSetValue1>(message, 0xffffffff);
            } else {
                VirtualCall<SetWord, kMsgSlotSetId>(message, kMsgLoadOne);
                VirtualCall<SetWord, kMsgSlotSetValue0>(message, 0);
            }
            auto *copy = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
            if (copy) copy->Construct(value.s, 0, 0xffffffff);
            VirtualCall<void(__thiscall *)(void *, void *), kMsgSlotSetText>(message, copy);
        } else {
            VirtualCall<SetWord, kMsgSlotSetId>(message, kMsgUserData);
            VirtualCall<SetWord, kMsgSlotSetValue0>(message, 0);
        }
        VirtualCall<SetWord, kServerSlot11>(GetCachedMessageServer(), 0);
        VirtualCall<void(__thiscall *)(void *, void *, uint32_t), kServerSlotPost>(GetCachedMessageServer(), message, 0);

        // sysMem=<megabytes>: what the crash report gives as the memory (cRZVersion::GetMemory's override).
        if (SwitchWithValue(commandLine, "sysMem", &value.s)) {
            const int megabytes = std::atoi(value.s.ToChar());
            if (megabytes) OriginalFunction<void(__cdecl *)(int, int), 0x1c9e49>()(megabytes, megabytes);
        }

        // A property set for the configuration (+0x60, no null test) and the globals' target.
        void *target = VirtualCall<void *(__thiscall *)(void *), kGlobalsSlotPropertyTarget>(GetCachedSingleton());
        void *memory = ::operator new(sizeof(::cGZPropertySetString));
        ::cGZPropertySetString *properties = nullptr;
        if (memory) {
            std::memset(memory, 0, sizeof(::cGZPropertySetString));
            properties = ::cGZPropertySetStringAdapters::Construct(memory, nullptr); // 0x22b157
        }
        if (properties) AddRefObject(properties);
        mPropertyFiles->LoadGlobalInto(properties);
        VirtualCall<void(__thiscall *)(void *, void *), kTargetSlotSetProperties>(target, properties);
        if (properties) ReleaseObject(properties);
    }
    if (message) ReleaseObject(message);
}

// Before anything else: neither another instance nor the Body Shop running, and no newer product installed (0xcb4cc0),
// both control files present (control<i>.dat in option 0x2ce990aa) and, unless the globals' slot 31 says so, the
// product's registry key and installation directory known (then 0xc2896d), and the user-data directory (option
// 0x4bcfce5e) existing with entries. Each failure picks a string-table text; a newer product is accepted with the
// process's own activeUserDataDir switch, else its DisplayName goes into the text. Then the free space on the user-data
// drive is checked (below 2 GB: a question box, false when declined). Any failure: the fatal message box and false.
// Otherwise the start-up work: the skin file type registered, the application added to the tick, the +0x80 delegate
// taken from the framework, the start message (installUserData / load) posted, the sysMem override, and a property set
// handed to the configuration and to the globals' target.
bool cTSAppFW::Init()
{
    bool result = true;
    bool installBroken = false; // a control file missing, or the product's paths unknown
    bool userDataOk = true;
    char installDirectory[0x104] = {};
    char productKey[0x104] = {};
    uint32_t keyLength = 0x104, directoryLength = 0x104;
    char newerExecutable[0x104];
    bool newerProduct = product::FindNewerProduct(newerExecutable, nullptr);

    // The process's own command line (not the framework's).
    const wchar_t *wide = Import<const wchar_t *(WINAPI *)()>(kIatGetCommandLineW)();
    Text commandText;
    ConvertBytesToString(wide, static_cast<uint32_t>(std::wcslen(wide)), 0x10, &commandText.Face());
    cRZCmdLine processCommandLine;
    processCommandLine.Construct(commandText.s.ToChar());

    if (!mOtherInstance && !mOtherBodyShop && !newerProduct) {
        {
            Text path, directory;
            appinit::CommonImplSlot4(kOptionControlDirectory, Word(&directory.s), 1, 0, 0xffffffff);
            bool controlFiles = true;
            for (int i = 0; i < 2 && controlFiles; ++i) {
                path.s.Sprintf("%scontrol%d.dat", directory.s.ToChar(), i);
                auto *memory = static_cast<cRZFile *>(::operator new(kFileSize));
                cRZFile *file = memory ? memory->Construct() : nullptr;
                if (file) AddRefObject(file);
                VirtualCall<void(__thiscall *)(void *, const cIGZString *), 29>(file, &path.Face()); // SetPath; no null test
                if (!VirtualCall<uint8_t(__thiscall *)(void *), 22>(file)) {                       // Exists
                    installBroken = true;
                    controlFiles = false;
                }
                if (file) ReleaseObject(file);
            }
            if (controlFiles) {
                if (VirtualCall<uint8_t(__thiscall *)(void *), kGlobalsSlotSkipInstallCheck>(GetCachedSingleton())) {
                    appfw::IsUserAdministrator();
                } else {
                    ProductPaths(*GameAddress<const char *>(kExecutableNameGlobal), productKey, &keyLength, installDirectory, &directoryLength);
                    if (!productKey[0] || !installDirectory[0]) installBroken = true;
                    else appfw::IsUserAdministrator();
                }
            }
        }
        Text userData;
        userDataOk = static_cast<uint8_t>(appinit::CommonImplSlot4(kOptionUserDataCheck, Word(&userData.s), 1, 0, 0xffffffff)) != 0;
        if (userDataOk) {
            userDataOk = as2::DirectoryExists(&userData.s);
            if (userDataOk) {
                // Our directory scan (lane LY), through its adapters (the entry points tests stand in for).
                alignas(::DirectoryScan) uint8_t scanMemory[sizeof(::DirectoryScan)];
                ::DirectoryScan *scan = DirectoryScanAdapters::Construct(scanMemory, nullptr, &userData.s, true, false, false);
                userDataOk = scan->mCount != 0;
                DirectoryScanAdapters::Destruct(scan, nullptr);
            }
        }
    }

    Text message;
    if (mOtherInstance) AssignErrorText(message, 0xb0, 2);
    if (mOtherBodyShop) {
        AssignErrorText(message, 0xcd, 1);
        if (message.s.mString.empty()) appfw::BodyShopProductName(&message.Face());
    }
    if (installBroken) AssignErrorText(message, 0xb0, 3);
    if (!userDataOk) AssignErrorText(message, 0xcd, 3);
    bool accepted = false;
    if (newerProduct) {
        Text name(GameAddress<const char>(kActiveUserDataDirText));
        accepted = processCommandLine.IsSwitchPresent(name.Face());
    }
    if (accepted) {
        newerProduct = false;
    } else if (newerProduct) {
        // The newer product's DisplayName (HKEY_CURRENT_USER\<its key>) in the text.
        Text name;
        char key[0x104] = {};
        uint32_t length = 0x104;
        ProductPaths(newerExecutable, key, &length, nullptr, nullptr);
        HKEY handle;
        if (Import<RegOpenKeyExAFn>(kIatRegOpenKeyExA)(HKEY_CURRENT_USER, key, 0, KEY_READ, &handle) == ERROR_SUCCESS) {
            DWORD type, size = 0x104;
            char displayName[0x104];
            Import<RegQueryValueExAFn>(kIatRegQueryValueExA)(handle, "DisplayName", nullptr, &type, reinterpret_cast<BYTE *>(displayName), &size);
            Import<RegCloseKeyFn>(kIatRegCloseKey)(handle);
            Text text(displayName);
            ConvertBytesToString(text.s.ToChar(), text.s.Strlen(), -1, &name.Face());
        }
        cTSString format;
        format.Construct(0xb5, 9, kStringGroup, cTSString::kStringTableType);
        message.s.Sprintf(format.mString.ToChar(), name.s.ToChar());
        format.Destruct();
    }

    // The free space where the user data goes.
    ULARGE_INTEGER available{}, total{}, free{};
    {
        Text userData, system;
        appinit::CommonImplSlot4(kOptionUserData, Word(&userData.s), 1, 0, 0xffffffff);
        RZText::ConvertStringToString(userData.Face(), system.Face(), -1);
        Import<BOOL(WINAPI *)(const char *, ULARGE_INTEGER *, ULARGE_INTEGER *, ULARGE_INTEGER *)>(kIatGetDiskFreeSpaceExA)(
            system.s.ToChar(), &available, &total, &free);
        if (free.QuadPart < kMinimumFreeBytes) {
            cTSString question;
            question.Construct(0xb0, 4, kStringGroup, cTSString::kStringTableType);
            const bool goOn = winmain::AskYesNo(question.mString.ToChar(), GameAddress<cRZString>(kAppCaption)->ToChar());
            question.Destruct();
            if (!goOn) result = false;
        }
        if (result) {
            if (mOtherInstance || mOtherBodyShop || installBroken || !userDataOk || newerProduct) {
                winmain::ShowFatalMessage(message.s.ToChar(), GameAddress<cRZString>(kAppCaption)->ToChar());
                result = false;
            } else {
                StartUp(productKey, &keyLength, installDirectory, &directoryLength, newerExecutable);
            }
        }
    }
    processCommandLine.Destruct();
    return result;
}

// ---- 0xc28fcc (cIGZSystemService slot 10): the first tick after construction only clears the flag; the others read
// the "onTickSleepTime" setting (and sleep that long when it is set) and step four cached services; every tick then
// asks the view service's flags and refreshes it unless bit 3 is set

bool cTSAppFW::OnTick(uint32_t)
{
    if (mByte6c) {
        mByte6c = 0;
    } else {
        // The settings service (0x1cbaf3; used without a null test, as the original).
        LocalReference settings{QueryFrameworkService(kSettingsService, kSettingsIid)};
        UintValue sleepTime{GameAddress<const void>(kAnyUint32VTable), 0};
        bool haveSleep;
        {
            GZServiceString name(GameAddress<const char>(kOnTickSleepTimeText));
            haveSleep = VirtualCall<uint8_t(__thiscall *)(void *, const void *, void *), 9>(settings.mObject, &name, &sleepTime) != 0;
        }
        if (haveSleep) Import<void(WINAPI *)(DWORD)>(kIatSleep)(sleepTime.mValue);
        if (VirtualCall<uint32_t(__thiscall *)(void *), 5>(GetCachedSlot23()) == kTickStateStep)
            VirtualCall<void(__thiscall *)(void *), 10>(OriginalFunction<void *(__cdecl *)(), 0x20df9a>()());
        if (void *holder = GetCachedSingletonB())
            if (void *stepper = VirtualCall<void *(__thiscall *)(void *), 36>(holder))
                VirtualCall<void(__thiscall *)(void *), 10>(stepper);
        VirtualCall<void(__thiscall *)(void *), 17>(OriginalFunction<void *(__cdecl *)(), 0x211da3>()());
        sleepTime.mVTable = GameAddress<const void>(kAnyVTable);
        settings.Release(); // ScopedRef::ReleaseObject 0x1cbd79
    }
    // The view service (0x18ae10; used without a null test).
    LocalReference view{QueryFrameworkService(kViewService, kViewIid)};
    if (!(VirtualCall<uint8_t(__thiscall *)(void *), 31>(view.mObject) & 8)) VirtualCall<void(__thiscall *)(void *), 25>(view.mObject);
    view.Release(); // RZAutoRef::ReleaseObject 0x1466e0
    return true;
}

// ---- 0xc2a1e3 (cIGZSystemService slot 9): a memory snapshot taken (unused), the shutdown message sent, the delegate
// dropped, the application removed from the framework and from its message ids, the configuration at +0x60 saved and
// deleted, and with the EsperSession switch the esper service told and the exception uploader started

bool cTSAppFW::Shutdown()
{
    MemorySnapshot snapshot{GameAddress<const void>(kMemorySnapshotVTable), {}}; // 0x1baa57
    OriginalFunction<bool(__thiscall *)(void *, void *), 0x1bbf19>()(&snapshot, nullptr);

    // A standard message (RZMalloc 0x1c03b7, 0x1c033d), referenced when the allocation worked, then used untested.
    cRZMessage2Standard *message = NewStandardMessage();
    if (message) AddRefObject(message);
    VirtualCall<void(__thiscall *)(void *, uint32_t), 5>(message, kMsgShutdown);
    void *server = GetCachedMessageServer();
    VirtualCall<void(__thiscall *)(void *, void *), 3>(server, message);

    reinterpret_cast<RZUnknownSlot *>(&mObject80)->Assign(nullptr);
    VirtualCall<void(__thiscall *)(void *, void *), 10>(GetServiceObject(), static_cast<cIGZSystemService *>(this));
    reinterpret_cast<KeyMapWalker *>(&mMessageIds)->CallSlot6ForEachKey(Word(static_cast<cIGZMessageTarget2 *>(this)), nullptr);

    // The configuration: saved with no null test (a null +0x60 faults, as in the original), then deleted.
    mPropertyFiles->Save();
    if (cTSAppPropertyFiles *files = mPropertyFiles) {
        files->Destruct();
        ::operator delete(files, cTSAppPropertyFiles::kSize);
    }
    mPropertyFiles = nullptr;

    cIGZCmdLine *commandLine = static_cast<cIGZFrameWork *>(GetServiceObject())->CommandLine();
    if (SwitchPresent(commandLine, GameAddress<const char>(kEsperSessionText))) {
        LocalReference session{QueryFrameworkService(kEsperService, kEsperIid)}; // 0xc260e1
        if (session.mObject) VirtualCall<void(__thiscall *)(void *), 4>(session.mObject);
        LaunchExceptionUploader(0, 1, 1);
        session.Release();
    }
    if (message) ReleaseObject(message);
    snapshot.mVTable = GameAddress<const void>(kMemorySnapshotVTable); // 0x1bab4e
    return true;
}
// ---- 0xc290d8 (cIGZApp slot 6): the CAS and UI text packages of every package directory opened and kept, the
// reporter pointed at the log directory (and the exception uploader started), the resource configuration and the
// locale option, the IME services unless the IME switch turns them off, the configuration object at +0x60 built and
// loaded, and the application registered for its message ids

bool cTSAppFW::PreFrameworkInit()
{
    Text cas(GameAddress<const char>(kCasPackageName)), uiText(GameAddress<const char>(kUiTextPackageName));
    {
        OptionStringList directories; // 0x20e147 / 0x20fadb
        appinit::CommonImplSlot5(kOptionPackageDirectories, Word(&directories), 1, 0, 0xffffffff);
        // The directories last to first (the original's reverse walk).
        for (auto it = directories.rbegin(); it != directories.rend(); ++it) {
            Text path;
            for (const Text *name : {&cas, &uiText}) {
                Text extension(GameAddress<const char>(kPackageExtension));
                path.s.mString = (it->c_str() + name->s.mString) + std::move(extension.s.mString);
                if (!cRZFile::PathExists(path.Face())) continue;
                auto *memory = static_cast<cRZFile *>(::operator new(kFileSize));
                cRZFile *file = memory ? memory->Construct(path.s.ToChar()) : nullptr;
                VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), 3>(file, 1, 2, 1); // Open; no null test
                AddRefObject(file);
                LocalReference held{file};
                AddRefObject(held.mObject); // the list's copy of the reference (0x213c8e)
                mList.emplace_back(held.mObject);
                held.Release();
            }
        }
        cIGZFrameWork *framework = static_cast<cIGZFrameWork *>(GetServiceObject());
        cIGZCmdLine *commandLine = framework->CommandLine();
        {
            Text logs;
            appinit::CommonImplSlot4(kOptionLogs, Word(&logs.s), 1, 0, 0xffffffff);
            mReporter.SetDirectory(&logs.s);
            mReporter.AddListener(static_cast<cIGZMessageTarget2 *>(this));
            const uint32_t noUpload = SwitchPresent(commandLine, GameAddress<const char>(kNoExceptionUploadText));
            LaunchExceptionUploader(static_cast<uint8_t>(noUpload), 0, 0); // the answer's byte, as the original
            cTSAppCommonImpl::ReadResourceConfiguration();

            // The locale option: "Locale/<language name>/" under the resource directory.
            Text locale(GameAddress<const char>(kLocaleText)), language;
            VirtualCall<void(__thiscall *)(void *, cRZString *, uint32_t), 15>(appinit::GetLanguageService(), &language.s, 0);
            locale.s.mString.append("/", 1);
            locale.s.mString.append(language.s.mString);
            locale.s.mString.append("/", 1);
            appinit::CommonImplSlot3(kOptionLocale, kOptionResources, Word(locale.s.Data()), 0, 0xffffffff);
            RegisterRegistryHandlers();

            // The IME services: on unless the IME switch says "off" or "disabled" (any case).
            Text ime;
            bool imeOn = true;
            if (SwitchWithValue(commandLine, GameAddress<const char>(kImeText), &ime.s)) {
                reinterpret_cast<cRZStringHelpers *>(&ime.s)->LowerCase();
                const std::string &v = ime.s.mString;
                if (v != "on" && v != "enabled" && (v == "off" || v == "disabled")) imeOn = false;
            }
            if (imeOn) {
                void *director = GetDirectorFrameworkObject();
                LocalReference first{nullptr};
                if (VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 3>(director, kImeServiceA, kIidLogService, &first.mObject))
                    framework->AddSystemService(static_cast<cIGZSystemService *>(first.mObject));
                LocalReference second{nullptr};
                if (VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 3>(director, kImeServiceB, kIidLogService, &second.mObject))
                    framework->AddSystemService(static_cast<cIGZSystemService *>(second.mObject));
                second.Release();
                first.Release();
            }

            // The configuration object (0xf0 bytes), built and loaded without null tests.
            void *memory = ::operator new(cTSAppPropertyFiles::kSize);
            mPropertyFiles = memory ? static_cast<cTSAppPropertyFiles *>(memory)->Construct(true) : nullptr;
            mPropertyFiles->SetupPaths();
            mPropertyFiles->LoadProperties();
            mPropertyFiles->LoadConfigProperties();
            reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)->CallSlot5ForEachKey(Word(static_cast<cIGZMessageTarget2 *>(this)), nullptr);
        }
    }
    return true;
}

// ---- 0xc27a16: the product's registered name

void cTSAppFW::RegisteredProductName(cIGZString *out)
{
    char key[kRegistryTextSize] = {};
    uint32_t keyLength = kRegistryTextSize;
    char name[kRegistryTextSize] = "The Sims 2 Legacy";
    DWORD nameLength = kRegistryTextSize;
    DWORD type;
    ProductPaths("Sims2.exe", key, &keyLength, nullptr, nullptr);
    HKEY handle;
    if (Import<RegOpenKeyExAFn>(kIatRegOpenKeyExA)(HKEY_CURRENT_USER, key, 0, KEY_READ, &handle) != ERROR_SUCCESS) return;
    // The answer is not checked: a missing value leaves the default text.
    Import<RegQueryValueExAFn>(kIatRegQueryValueExA)(handle, "DisplayName", nullptr, &type, reinterpret_cast<BYTE *>(name),
                                                     &nameLength);
    Import<RegCloseKeyFn>(kIatRegCloseKey)(handle);
    out->FromChar(name);
    // The text converted onto itself from the ANSI code page.
    const uint32_t length = out->Strlen();
    const char *data = out->Data();
    ConvertBytesToString(data, length, -1, out);
}

// ---- 0xc2a7de (cITSAppCommon slot 15): the base's directories, then the user-data layout. Install mode (the default,
// or userDataMode=install): the folders under "UserData/" of the resources, the user-data and all-users roots in
// "<Documents>/EA Games/<product>/" (or the activeUserDataDir / activeAllUsersDataDir switches) created, and the
// templatedev / nhdev overrides. Develop mode (userDataMode=develop, +0x70 = 1): the folders under the user-data root,
// which stays "UserData/" of the resources (falling back to the Documents root when that does not exist), the same for
// "AllUsersData/". Then the log directory is created.

bool cTSAppFW::SetupUserDataDirectories()
{
    if (!cTSAppCommonImpl::SetupUserDataDirectories()) return false;
    Text product(GameAddress<const char>(kProductNameText));
    RegisteredProductName(&product.Face());
    Text documents;
    UserDataRoot(kFolderDocuments, documents, product);
    Text allUsers;
    UserDataRoot(kFolderAllUsers, allUsers, product);
    mWord70 = 0;
    Text mode;
    cIGZCmdLine *commandLine = static_cast<cIGZFrameWork *>(GetServiceObject())->CommandLine();
    if (SwitchWithValue(commandLine, GameAddress<const char>(kUserDataModeText), &mode.s)) {
        if (mode.s.IsEqual(GameAddress<const char>(kInstallText), 0xffffffff, false)) mWord70 = 0;
        else if (mode.s.IsEqual(GameAddress<const char>(kDevelopText), 0xffffffff, false)) mWord70 = 1;
    }

    if (mWord70 == 0) {
        AddOption(kOptionUserDataFolders, kOptionResources, GameAddress<const char>(kUserDataText), 0, -1);
        for (const UserDataFolder &f : kUserDataFolders) AddOption(f.key, kOptionUserDataFolders, f.text, 0, -1);
        Text userData;
        appinit::CommonImplSlot4(kOptionUserDataFolders, Word(&userData.s), 1, 0, 0xffffffff);
        Text active;
        bool useDocuments = true;
        if (SwitchWithValue(commandLine, GameAddress<const char>(kActiveUserDataDirText), &active.s)) {
            inifile::AbsolutePathString(&active.s, &active.s);
            cRZFileName::NormaliseSeparators(&active.s, '/');
            useDocuments = active.s.mString == userData.s.mString;
        }
        if (useDocuments) active.s.mString = documents.s.mString;
        EnsureDirectory(active);
        AddOption(kOptionUserData, 0, active.s.Data(), 0, -1);

        const bool templateDev = SwitchPresent(commandLine, GameAddress<const char>(kTemplateDevText)) != 0;
        const bool hoodDev = SwitchPresent(commandLine, GameAddress<const char>(kHoodDevText)) != 0;
        if (templateDev || hoodDev) {
            AddOption(0xce9b3252, 0, nullptr, 0, -1);
            AddOption(0x8ce453ed, 0, nullptr, 0, -1);
            AddOption(kOptionNeighborhoods, 0, nullptr, 0, -1);
            if (templateDev)
                AddOption(kOptionNeighborhoods, kOptionResources, GameAddress<const char>(kNeighborhoodTemplateText), 1, kProductMask);
            else
                AddOption(kOptionNeighborhoods, kOptionUserDataFolders, GameAddress<const char>(kNeighborhoodsText), 1, kProductMask);
            AddOption(0xdf2a7ca, 0, nullptr, 0, -1);
            AddOption(0x8c5aedd5, 0, nullptr, 0, -1);
            AddOption(0x8c5aedd5, kOptionUserDataFolders, GameAddress<const char>(kLotCatalogText), 0, -1);
        }

        Text activeAllUsers;
        if (SwitchWithValue(commandLine, GameAddress<const char>(kActiveAllUsersDataDirText), &activeAllUsers.s)) {
            inifile::AbsolutePathString(&activeAllUsers.s, &activeAllUsers.s);
            cRZFileName::NormaliseSeparators(&activeAllUsers.s, '/');
        } else {
            activeAllUsers.s.mString = allUsers.s.mString;
        }
        EnsureDirectory(activeAllUsers);
        AddOption(kOptionAllUsersData, 0, activeAllUsers.s.Data(), 0, -1);
    } else {
        AddOption(kOptionUserDataFolders, kOptionUserData, GameAddress<const char>(kEmptyText), 0, -1);
        for (const UserDataFolder &f : kUserDataFolders) AddOption(f.key, kOptionUserDataFolders, f.text, 0, -1);
        AddOption(kOptionUserData, kOptionResources, GameAddress<const char>(kUserDataText), 0, -1);
        Text userData;
        appinit::CommonImplSlot4(kOptionUserData, Word(&userData.s), 1, 0, 0xffffffff);
        if (!as2::DirectoryExists(&userData.s)) {
            AddOption(kOptionUserData, kOptionResources, nullptr, 0, -1);
            AddOption(kOptionUserData, 0, documents.s.Data(), 0, -1);
            EnsureDirectory(documents);
        }
        AddOption(kOptionAllUsersData, kOptionResources, GameAddress<const char>(kAllUsersDataText), 0, -1);
        Text allUsersData;
        appinit::CommonImplSlot4(kOptionAllUsersData, Word(&allUsersData.s), 1, 0, 0xffffffff);
        if (!as2::DirectoryExists(&allUsersData.s)) {
            AddOption(kOptionAllUsersData, kOptionResources, nullptr, 0, -1);
            AddOption(kOptionAllUsersData, 0, allUsers.s.Data(), 0, -1);
            EnsureDirectory(allUsers);
        }
        Text config;
        appinit::CommonImplSlot4(kOptionConfig, Word(&config.s), 1, 0, 0xffffffff);
        EnsureDirectory(config);
    }

    Text logs;
    appinit::CommonImplSlot4(kOptionLogs, Word(&logs.s), 1, 0, 0xffffffff);
    EnsureDirectory(logs);
    return true;
}

// ---- 0xc26bf5: the application director added to the root and started, the language, then the audio director

bool cTSAppFW::AddCOMDirectorsHere()
{
    rz::cRZCOMDllDirector *app = comdir::GetDirector(comdir::kDir_TSAppFW);
    reinterpret_cast<rz::cRZCOMDllDirector *>(GetCOMDirector())->AddDirector(app);
    void *director = comdir::GetDirector(comdir::kDir_TSAppFW);
    auto start = VirtualSlot<bool(__thiscall *)(void *, void *)>(director, 4); // OnStart; the vtable is read first
    start(director, GetDirectorFrameworkObject());
    appinit::InitLanguage(this, nullptr);
    appcom::AddAudioDirector();
    return false; // the original's al is whatever the tail-called AddAudioDirector left (no caller reads it)
}

// ---- 0xc267fb: the application's services, created and registered with the framework (docs/startup-map.md 4.5)

bool cTSAppFW::AddApplicationServicesHere()
{
    auto *framework = static_cast<cIGZFrameWork *>(GetServiceObject());
    auto add = [framework](void *service) { framework->AddSystemService(static_cast<cIGZSystemService *>(service)); };
    RZUnknownSlot held{nullptr}; // the original's one smart pointer, reused for three services

    // The allocator service: referenced through its +0 face for the whole call, registered through its +4 face.
    void *const allocatorMemory = ::operator new(cGZAllocatorService::kSize);
    auto *allocator = allocatorMemory ? cGZAllocatorServiceAdapters::Construct(allocatorMemory, nullptr) : nullptr;
    if (allocator) AddRefObject(allocator);
    add(FaceOf(allocator, 4));

    // The log service: named first (its +0x1c face, slot 20; no null test), then registered through its own
    // QueryInterface into the smart pointer (emptied first).
    void *const logMemory = ::operator new(nGZLog::cLogService::kSize);
    auto *log = logMemory ? nGZLog::cLogServiceAdapters::Construct(logMemory, nullptr) : nullptr;
    VirtualCall<void(__thiscall *)(void *, const char *, const char *, uint32_t), 20>(reinterpret_cast<char *>(log) + 0x1c, "AppDebugger", "NoGroup", 4);
    if (void *old = held.mObject) {
        held.mObject = nullptr;
        ReleaseObject(old);
    }
    QueryObject(log, kIidLogService, &held.mObject);
    add(held.mObject);

    // The property manager, assigned to the smart pointer (which releases the log service's reference).
    void *const propertiesMemory = ::operator new(cGZPropertyManagerString::kSize);
    held.Assign(FaceOf(propertiesMemory ? cGZPropertyManagerStringAdapters::Construct(propertiesMemory, nullptr) : nullptr,
                       cGZPropertyManagerString::kSystemFace));
    add(held.mObject);

    void *const netMemory = ::operator new(cRZNetManager::kSize);
    add(FaceOf(netMemory ? cRZNetManagerAdapters::Construct(netMemory, nullptr) : nullptr, cRZNetManager::kSystemFace));
    void *const sslMemory = ::operator new(cRZNetSSLManager::kSize);
    add(FaceOf(sslMemory ? cRZNetSSLManagerAdapters::Construct(sslMemory, nullptr) : nullptr, cRZNetSSLManager::kSystemFace));

    // The file system: its +0x1c face held for the rest of the call, registered through its QueryInterface when that
    // answers.
    void *const fileSystemMemory = ::operator new(cGZFileSystem::kSize);
    void *fileSystem = FaceOf(fileSystemMemory ? cGZFileSystemAdapters::Construct(fileSystemMemory, nullptr) : nullptr, cGZFileSystem::kFace);
    if (fileSystem) AddRefObject(fileSystem);
    if (void *old = held.mObject) {
        held.mObject = nullptr;
        ReleaseObject(old);
    }
    QueryObject(fileSystem, kIidLogService, &held.mObject);
    if (held.mObject) add(held.mObject);

    bool result = false;
    if (SetupUserDataDirectories()) {
        appservices3::AppendIniCommandLine();
        appdebugger::SetupLogging();
        void *const timerMemory = ::operator new(cGZTimerService::kSize);
        add(timerMemory ? cGZTimerServiceAdapters::Construct(timerMemory, nullptr) : nullptr);
        void *const registryMemory = ::operator new(cGZRegistry::kSize);
        add(FaceOf(registryMemory ? cGZRegistryAdapters::Construct(registryMemory, nullptr) : nullptr, 4));
        void *const clipboardMemory = ::operator new(cGZClipboardManager::kSize);
        add(FaceOf(clipboardMemory ? cGZClipboardManagerAdapters::Construct(clipboardMemory, nullptr) : nullptr, 4));
        void *const callbacksMemory = ::operator new(cRZCallbackSystemService::kSize);
        cRZCallbackSystemService *const callbackService = callbacksMemory ? cRZCallbackSystemServiceAdapters::Construct(callbacksMemory, nullptr) : nullptr;
        void *callbacks = FaceOf(callbackService, 4);
        if (callbacks) AddRefObject(callbacks);
        add(callbacks);
        void *const globalsMemory = ::operator new(cTSGlobals::kSize);
        void *globals = FaceOf(globalsMemory ? cTSGlobalsAdapters::Construct(globalsMemory, nullptr) : nullptr, 4);
        if (globals) AddRefObject(globals);
        add(globals);
        void *const worldMemory = ::operator new(nTSWorld::cWorldDB::kSize);
        void *world = worldMemory ? nTSWorld::cWorldDBAdapters::Construct(worldMemory, nullptr) : nullptr;
        if (world) AddRefObject(world);
        add(world);
        appinit::ReadApplicationOptions(this, nullptr);
        if (world) ReleaseObject(world);
        if (globals) ReleaseObject(globals);
        if (callbacks) ReleaseObject(callbacks);
        result = true;
    }
    if (fileSystem) ReleaseObject(fileSystem);
    if (allocator) ReleaseObject(allocator);
    if (held.mObject) ReleaseObject(held.mObject);
    return result;
}

// ---- 0xc26c30 (cIGZApp slot 11): the debug libraries

namespace {

// The debug builds' libraries, loaded from the library directory when no configuration file names them.
constexpr const char *kDebugLibraries[] = {"GZLogMFCWinD.dll", "GZPerforceD.dll", "EdithEditorD.dll", "TSDebugD.dll",
                                           "TSTestD.dll",      "TSEsperD.dll",    "TSAutomation.dll"};
constexpr uint32_t kOptionLibraryConfiguration = 0x6c253729; // the ini file naming the libraries
constexpr uint32_t kOptionLibraryDirectory = 0x6bcfce93;     // the directory the default libraries are in

constexpr int kCmdSlotArgC = 3, kCmdSlotInsertArgument = 13;

} // namespace

// The editor and debug libraries must have the executable's own file version; the libraries come from the configuration
// file (option 0x6c253729, with configuration "monolithic") when it exists and has a "DLLs" section, else the seven
// debug libraries in the library directory, all optional. The bench switches add "-perfTrackExecution" at the end of
// the command line; metrics / perf / perfTrackExecution / nosound are asked and the answers dropped. Then the list is
// loaded and torn down. Returns the list destructor's al: our adapter's 0 (the original's was what its operator delete
// left in eax; the caller ignores it). The list is ours (src/ts/DynamicLibraryList.h, lane LV; switched by lane LY), built
// and destroyed through its adapters, the entry points tests stand in for; `this` is not used.
bool cTSAppFW::AddDynamicLibrariesHere()
{
    cIGZCmdLine *commandLine = static_cast<cIGZFrameWork *>(GetServiceObject())->CommandLine();
    alignas(DynamicLibraryList) uint8_t listMemory[sizeof(DynamicLibraryList)];
    DynamicLibraryList &libraries = *DynamicLibraryListAdapters::Construct(listMemory, nullptr);
    {
        appbase::cRZVersion version; // the executable's
        version.Construct();
        {
            Text fileVersion;
            version.GetFileVersion(&fileVersion.s);
            for (const char *editor : {"EdithEditorD", "TSDebugD"}) {
                Text name(editor);
                libraries.RequireVersion(&name.s, &fileVersion.s, 1);
            }

            Text configuration;
            appinit::CommonImplSlot4(kOptionLibraryConfiguration, Word(&configuration.s), 1, 0, 0xffffffff);
            Text path, directory;
            appinit::CommonImplSlot4(kOptionLibraryDirectory, Word(&directory.s), 1, 0, 0xffffffff);
            bool configured = false;
            if (cRZFile::PathExists(configuration.Face())) {
                Text name("monolithic"); // 0x140fd0
                configured = libraries.ReadConfiguration(&configuration.Face(), &name.s);
            }
            if (!configured) {
                for (const char *library : kDebugLibraries) {
                    path.s.mString = directory.s.mString + library;
                    libraries.Add(&path.s, 0);
                }
            }

            const bool bench = SwitchPresent(commandLine, "benchLengthMS") || SwitchPresent(commandLine, "benchLengthSimTicks");
            if (bench) {
                Text argument("-perfTrackExecution");
                const int32_t end = VirtualCall<int32_t(__thiscall *)(void *), kCmdSlotArgC>(commandLine);
                VirtualCall<uint8_t(__thiscall *)(void *, const void *, int32_t), kCmdSlotInsertArgument>(commandLine, &argument.s, end);
            } else if (!SwitchPresent(commandLine, "metrics") && !SwitchPresent(commandLine, "perf")) {
                SwitchPresent(commandLine, "perfTrackExecution");
            }
            SwitchPresent(commandLine, "nosound");

            libraries.LoadAll();
            libraries.Report();
        }
        version.Destruct();
    }
    return DynamicLibraryListAdapters::Destruct(&libraries, nullptr) != 0;
}

// ---- 0xc296b4: the launcher's shutdown request becomes a standard message to the message server (and the launcher's
// event is signalled); any other message goes to the delegate at +0x80

bool cTSAppFW::PreTranslateMessage(void *msg)
{
    if (!msg) return false;
    uint32_t message = static_cast<const MSG *>(msg)->message;
    if (message == mShutdownMessage && mShutdownMessage != 0) {
        // A standard message (0x2c bytes from RZMemory::Malloc, built as 0x1c033d does; referenced when the allocation
        // worked, then used untested) with the id 0x4beb45bb (slot 5) and the value 0x40000 (slot 11), posted (slot 4).
        auto *raw = static_cast<cRZMessage2Standard *>(RZMemory::Malloc(cRZMessage2Standard::kSize));
        if (raw) {
            raw->mVTable = &ts2_rva_da4064;
            raw->mRefCount = 0;
            raw->mFlags = 0;
            raw->mId = 0;
            for (uint32_t &v : raw->mValue) v = 0;
            raw->mPtr20 = raw->mPtr24 = raw->mPtr28 = nullptr;
            AddRefObject(raw);
        }
        VirtualCall<void(__thiscall *)(void *, uint32_t), 5>(raw, 0x4beb45bb);
        VirtualCall<void(__thiscall *)(void *, uint32_t), 11>(raw, 0x40000);
        void *server = GetCachedMessageServer();
        VirtualCall<void(__thiscall *)(void *, void *, uint32_t), 4>(server, raw, 0);
        HANDLE event = Import<HANDLE(WINAPI *)(DWORD, BOOL, const char *)>(kIatOpenEventA)(EVENT_MODIFY_STATE, FALSE, "EA_LAUNCHER_SHUTDOWN_REQUEST_SIMS2EP");
        if (event) {
            Import<BOOL(WINAPI *)(HANDLE)>(kIatSetEvent)(event);
            Import<BOOL(WINAPI *)(HANDLE)>(kIatCloseHandle)(event);
        }
        if (raw) ReleaseObject(raw);
        return true;
    }
    if (mObject80) return VirtualCall<bool(__thiscall *)(void *, void *), 3>(mObject80, msg);
    return false;
}

// ---- folded 0x1457d0, 0xc299da, 0xc2a7bb

uint32_t cTSAppFW::GetWord70() { return mWord70; }

void cTSAppFW::SetAppName(const char *text) { mName.mString.assign(text); }

bool cTSAppFW::InitOptions()
{
    AddOption(0x6c253729, 0x6bcfce93, GameAddress<const char>(kAppNameText), 0, -1);
    return FindProductDirectories();
}

// ---- 0xc27605: the application's messages (asked for the standard message interface first)

bool cTSAppFW::DoMessage(void *message)
{
    if (!message) return false;
    void *standard = nullptr;
    bool handled = false;
    if (QueryObject(message, kIidMessageStandard, &standard)) {
        handled = true;
        switch (VirtualCall<uint32_t(__thiscall *)(void *), 4>(message)) { // GetId
        case kMsgStateQuery: {
            void *value = nullptr;
            if (VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 29>(standard, kStateQueryKey, &value))
                UnlockNeighborhoodFiles(static_cast<cIGZString *>(value));
            if (value) ReleaseObject(value);
            break;
        }
        case kMsgReleaseObject60:
            if (mPropertyFiles) mPropertyFiles->Save();
            break;
        case kMsgFlag4:
            mFlags264 |= 4;
            break;
        case kMsgFatal:
            ReportFatal();
            break;
        case kMsgCrashData:
            AddCrashData();
            break;
        case kMsgIgnoredA:
        case kMsgIgnoredB:
            break;
        case kMsgStringTables:
            LoadStringTableTexts();
            break;
        default:
            handled = false;
            break;
        }
    }
    if (standard) ReleaseObject(standard);
    return handled;
}

// The 0xce1505fd case: a cached service's slot 43 with 0xf000, the application string's text from table 0x99 entry 8,
// then six string-table texts into the game's text globals 0x10fda54.. (cRZStrings), each only when it was found; the
// first one goes through 0xc27961 with the language's slot 7 answer first.
void cTSAppFW::LoadStringTableTexts()
{
    constexpr uint32_t kGroupA = 0x7f2128d7, kGroupB = 0x7fa7dc7c, kType = cTSString::kStringTableType;
    void *service = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    VirtualCall<void(__thiscall *)(void *, uint32_t), 43>(service, 0xf000);
    {
        cTSString text;
        text.Construct(0x99, 8, kGroupA, kType);
        reinterpret_cast<RZStringSlot *>(&mString244.mString)->Assign(text.mString.mString);
        text.Destruct();
    }
    cTSString texts[6];
    texts[0].Construct(1, 0x12, kGroupB, kType);
    texts[1].Construct(0x99, 9, kGroupA, kType);
    texts[2].Construct(0x99, 10, kGroupA, kType);
    texts[3].Construct(0x99, 0xb, kGroupA, kType);
    texts[4].Construct(0x99, 0xd, kGroupA, kType);
    texts[5].Construct(0x99, 0xe, kGroupA, kType);
    if (texts[0].mLoaded) {
        cTSString expanded;
        expanded.ConstructEmpty();
        const uint32_t language = VirtualCall<uint32_t(__thiscall *)(void *), 7>(appinit::GetLanguageService());
        appfw::AppendEditionSuffix(texts[0].mString.AsInterface(), expanded.mString.AsInterface(), language);
        GameAddress<RZStringSlot>(0x10fda58)->Assign(expanded.mString.mString);
        expanded.Destruct();
    }
    static constexpr uint32_t kGlobals[6] = {0, 0x10fda78, 0x10fda98, 0x10fdab8, 0x10fdad8, 0x10fdaf8};
    for (int i = 1; i < 6; ++i)
        if (texts[i].mLoaded) GameAddress<RZStringSlot>(kGlobals[i])->Assign(texts[i].mString.mString);
    for (int i = 5; i >= 0; --i) texts[i].Destruct();
}

// ---- lane MB: the helpers (0xc28b7d, 0xc28de0, 0xc28e32, 0xc2a3d6)

namespace {

constexpr uint32_t kOptionAppDirectory = 0x6bcfce93; // where the executable's companions are
constexpr uint32_t kAnyBoolVTable = 0xd9c670;        // cGZAny<bool>
constexpr uint32_t kNoIffUnlockingText = 0xdcd0dc;   // "noStupidIFFUnlocking"
constexpr uint32_t kWildcardText = 0xda33f8;         // "*"
constexpr int kStateSlotLot = 9;                     // the game state's lot (unnamed slot)
constexpr int kLotSlotName = 19;
constexpr int kFamilySlotName = 37;
constexpr int kPackagesSlotUnlock = 27;              // the package service's slot 27 (path)

// A bool value as the settings service fills it (cGZAny<bool>, game vtable).
struct BoolValue {
    const void *mVTable;
    uint8_t mValue;
};

// A game object's name: its slot kSlot gives a cIGZString.
template <int kSlot> const char *NameOf(void *object)
{
    return VirtualCall<cIGZString *(__thiscall *)(void *), kSlot>(object)->ToChar();
}

} // namespace

void cTSAppFW::LaunchExceptionUploader(uint8_t noUpload, uint8_t noLog, uint8_t esper)
{
    Text path;
    appinit::CommonImplSlot4(kOptionAppDirectory, Word(&path.s), 1, 0, 0xffffffff);
    path.s.mString.append("ExceptionUploader.exe");
    if (!cRZFile::PathExists(path.Face())) return;
    RZText::ConvertStringToString(path.Face(), path.Face(), -1);

    Text arguments("-logDir:");
    arguments.s.mString.push_back('"');
    {
        Text logs;
        appinit::CommonImplSlot4(kOptionLogs, Word(&logs.s), 1, 0, 0xffffffff);
        RZText::ConvertStringToString(logs.Face(), logs.Face(), -1);
        arguments.s.mString.append(logs.s.mString);
    }
    arguments.s.mString.push_back('"');

    // ORIGINAL-BUG: the original places "-noLog" / "-esper" at argv[2 + noUpload] with the raw byte, so a noUpload other
    // than 0 or 1 together with either of them writes past its five-entry argv. No caller does that (PreFrameworkInit
    // passes noLog = esper = 0, the others constants), so ours indexes as for 0 / 1 and stays in bounds.
    const char *argv[5] = {path.s.ToChar(), arguments.s.ToChar(), noUpload == 1 ? "-noUpload" : nullptr, nullptr, nullptr};
    int next = noUpload == 1 ? 3 : 2;
    if (noLog == 1) argv[next++] = "-noLog";
    if (esper == 1) argv[next] = "-esper";
    // The program's own path with backslashes (in place: argv[0] still points at it).
    for (char &c : path.s.mString)
        if (c == '/') c = '\\';
    appfw::SpawnAndWait(path.s.mString.c_str(), argv);
}

void cTSAppFW::ReportFatal()
{
    static_cast<cIGZFrameWork *>(GetServiceObject())->CommandLine(); // asked, not used
    if (!(mFlags264 & 1)) {
        const char *caption = GameAddress<cRZString>(kAppCaption)->ToChar();
        winmain::ShowFatalMessage(mString244.AsInterface()->ToChar(), caption);
    }
    if (!(mFlags264 & 2)) LaunchExceptionUploader(0, 1, 0);
}

void cTSAppFW::AddCrashData()
{
    mReporter.AddExtraData("Shutdown", (mFlags264 & 4) ? "1" : "0", 1);
    alignas(appbase::cRZVersion) uint8_t versionMemory[sizeof(appbase::cRZVersion)];
    appbase::cRZVersion *version = reinterpret_cast<appbase::cRZVersion *>(versionMemory)->Construct();
    {
        Text host;
        // cRZVersion's host name (0x1c99d2, ret 8): GetComputerNameA, "localhost" when it fails and no default is given.
        OriginalFunction<uint8_t(__thiscall *)(appbase::cRZVersion *, cRZString *, const cRZString *), 0x1c99d2>()(version, &host.s, nullptr);
        mReporter.AddExtraData("HostName", host.s.mString.c_str(), 1);
        if (const uint32_t usage = appfw::ProcessPagefileUsage()) {
            Text memory;
            memory.Face().Sprintf("%uK", usage >> 10);
            mReporter.AddExtraData("MemoryUsage", memory.Face().ToChar(), 1);
        }
        if (auto *state = As<ifaces::cITSGameStateController>(GetCachedSlot23())) {
            if (ifaces::cITSNeighborhoodInfo *neighborhood = state->GetNeighborhoodInfo()) {
                mReporter.AddExtraData("Neighborhood Name", As<cIGZString>(neighborhood->GetName())->ToChar(), 1);
                mReporter.AddExtraData("Neighborhood Prefix", As<cIGZString>(neighborhood->GetFilePrefix())->ToChar(), 1);
            }
            if (void *lot = VirtualCall<void *(__thiscall *)(void *), kStateSlotLot>(state))
                mReporter.AddExtraData("Lot Name", NameOf<kLotSlotName>(lot), 1);
            if (ifaces::cITSFamily *family = state->GetActiveFamily())
                mReporter.AddExtraData("Family Name", NameOf<kFamilySlotName>(family), 1);
        }
    }
    version->Destruct();
}

void cTSAppFW::UnlockNeighborhoodFiles(cIGZString *neighborhood)
{
    cIGZCmdLine *commandLine = static_cast<cIGZFrameWork *>(GetServiceObject())->CommandLine();
    BoolValue setting{GameAddress<const void>(kAnyBoolVTable), 0};
    bool found;
    {
        GZServiceString name(GameAddress<const char>(kNoIffUnlockingText));
        // The settings service (0x1cbaf3; used without a null test, as the original).
        LocalReference settings{QueryFrameworkService(kSettingsService, kSettingsIid)};
        found = VirtualCall<uint8_t(__thiscall *)(void *, const void *, void *), 9>(settings.mObject, &name, &setting) != 0;
        settings.Release(); // ScopedRef::ReleaseObject 0x1cbd79
    }
    bool noUnlocking = found && setting.mValue;
    // The same globals slot as Init's install-check skip lets the switch say it too.
    if (VirtualCall<uint8_t(__thiscall *)(void *), kGlobalsSlotSkipInstallCheck>(GetCachedSingleton()) &&
        SwitchPresent(commandLine, GameAddress<const char>(kNoIffUnlockingText)))
        noUnlocking = true;
    if (noUnlocking) return;

    void *packages = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    Text directory; // lives to the end, as the original's
    {
        // The neighbourhood's directory (0xc0b314: the neighbourhoods option, the name, a separator).
        OriginalFunction<uint8_t(__cdecl *)(cIGZString *, cIGZString *, uint32_t, uint32_t), 0xc0b314>()(neighborhood, &directory.Face(), 1,
                                                                                                        0xffffffff);
        alignas(::DirectoryScan) uint8_t scanMemory[sizeof(::DirectoryScan)];
        ::DirectoryScan *scan = DirectoryScanAdapters::Construct(scanMemory, nullptr, &directory.s, true, false, false);
        {
            Text pattern(GameAddress<const char>(kWildcardText));
            const uint32_t count = scan->mCount;
            for (uint32_t i = 0; i < count; ++i) {
                dscan::Entry *entry = DirectoryScanAdapters::EntryAt(scan, nullptr, i);
                if (!entry) continue;
                // The wildcard match (0x9cce18, case-insensitive).
                if (!OriginalFunction<uint8_t(__cdecl *)(const cIGZString *, const cIGZString *, uint32_t), 0x9cce18>()(
                        entry->mName.AsInterface(), &pattern.Face(), 0))
                    continue;
                Text path;
                DirectoryScanAdapters::FullPath(entry, nullptr, &path.s);
                VirtualCall<uint8_t(__thiscall *)(void *, cIGZString *), kPackagesSlotUnlock>(packages, &path.Face());
            }
        }
        DirectoryScanAdapters::Destruct(scan, nullptr);
    }
    Text manager;
    appinit::CommonImplSlot4(kOptionNeighborhoods, Word(&manager.s), 1, 0, 0xffffffff);
    manager.s.mString.append("NeighborhoodManager.package");
    VirtualCall<uint8_t(__thiscall *)(void *, cIGZString *), kPackagesSlotUnlock>(packages, &manager.Face());
}

// ---- adapters

cTSAppFW *__fastcall cTSAppFWAdapters::Construct(void *memory, void *, uint32_t serviceId, const char *name, cIGZCmdLine *commandLine)
{
    return ::new (memory) cTSAppFW(serviceId, name, commandLine);
}

void __fastcall cTSAppFWAdapters::Destruct(cTSAppFW *self, void *) { self->cTSAppFW::~cTSAppFW(); }

cTSAppFW *__fastcall cTSAppFWAdapters::DeletingDestructor(cTSAppFW *self, void *, uint32_t flags)
{
    self->cTSAppFW::~cTSAppFW();
    if (flags & 1) ::operator delete(self, sizeof(cTSAppFW));
    return self;
}

bool __fastcall cTSAppFWAdapters::QueryInterface(cTSAppFW *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->cTSAppFW::QueryInterface(riid, out);
}

bool __fastcall cTSAppFWAdapters::Init(cTSAppFW *self, void *)
{
    [[msvc::forceinline_calls]] return self->cTSAppFW::Init();
}

bool __fastcall cTSAppFWAdapters::Shutdown(cTSAppFW *self, void *)
{
    [[msvc::forceinline_calls]] return self->cTSAppFW::Shutdown();
}

bool __fastcall cTSAppFWAdapters::OnTick(cTSAppFW *self, void *, uint32_t counter)
{
    [[msvc::forceinline_calls]] return self->cTSAppFW::OnTick(counter);
}

bool __fastcall cTSAppFWAdapters::PreFrameworkInit(cIGZApp *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cTSAppFW *>(self)->cTSAppFW::PreFrameworkInit();
}

bool __fastcall cTSAppFWAdapters::SetupUserDataDirectories(cTSAppCommonImpl *self, void *)
{
    __assume(self != nullptr);
    [[msvc::forceinline_calls]] return static_cast<cTSAppFW &>(*self).cTSAppFW::SetupUserDataDirectories();
}

bool __fastcall cTSAppFWAdapters::AddDynamicLibrariesHere(cIGZApp *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cTSAppFW *>(self)->cTSAppFW::AddDynamicLibrariesHere();
}

bool __fastcall cTSAppFWAdapters::AddCOMDirectorsHere(cIGZApp *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cTSAppFW *>(self)->cTSAppFW::AddCOMDirectorsHere();
}

bool __fastcall cTSAppFWAdapters::AddApplicationServicesHere(cIGZApp *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cTSAppFW *>(self)->cTSAppFW::AddApplicationServicesHere();
}

bool __fastcall cTSAppFWAdapters::PreTranslateMessage(cITSAppFW *self, void *, void *msg)
{
    [[msvc::forceinline_calls]] return static_cast<cTSAppFW *>(self)->cTSAppFW::PreTranslateMessage(msg);
}

// A slot's `this` is never null: `__assume` and the reference cast drop the face-to-object cast's null test (the
// one-statement form keeps it and loses the byte match; docs/standalone-shape.md).
bool __fastcall cTSAppFWAdapters::InitOptions(cTSAppCommonImpl *self, void *)
{
    __assume(self != nullptr);
    [[msvc::forceinline_calls]] return static_cast<cTSAppFW &>(*self).cTSAppFW::InitOptions();
}

bool __fastcall cTSAppFWAdapters::DoMessage(cIGZMessageTarget2 *self, void *, void *message)
{
    [[msvc::forceinline_calls]] return static_cast<cTSAppFW *>(self)->cTSAppFW::DoMessage(message);
}

} // namespace ts
