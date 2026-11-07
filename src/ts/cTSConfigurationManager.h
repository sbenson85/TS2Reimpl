#pragma once
#include "gz/cGZAny.h" // GZString
#include "gz/cGZPropertySet.h"
#include "gz/cIGZPropertySet.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZUnknownBase.h"
#include "rz/cRZCmdLine.h"
#include "rz/cRZString.h"
#include "rz/cRZTextFile.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// cTSConfigurationManager: the graphics configuration (RTTI cTSConfigurationManager; bases cITSConfigurationManager at +0,
// vtable 0xeb95ac, 19 slots, and cRZUnknown at +4, vtable 0xeb95fc, 6 slots, count at +8). 0x190 bytes. Clsid 0xa1d8285,
// iid 0xa1d8284. The factory 0xcd0360 (registered by the COM director's constructor 0xcb4ec0) is the only creation site; the
// deleting destructor 0xccef70 is reached only through the +4 face's slot 3 (thunk 0xccef62).
//
// At start-up (slot 5) it enumerates the display devices, builds one device record per device (machine and card facts from
// the device and the CPU), runs the configuration script on each (the script sets options and properties in the record's
// two property sets), restores the active device from the saved settings and writes the config log. The settings live in
// the property set the framework hands out (clsid 0xac1887e, iid 0x4bb0d269), held at +0x170.
//
// Standalone shape (docs/standalone-shape.md, lane IO): a real C++ class deriving from its interface and rz::cRZUnknown; the
// compiler emits the vtables and the +4 face's thunks. The factory hook builds ours (the class's switch); the RVA hooks on the
// slot functions are adapters (cTSConfigurationManagerAdapters below) for direct calls and game-built objects.

namespace tsconfig {

// One named setting of an option value: what the script's option block sets (0x1c bytes). A boolean's value is the low
// byte of mValue. The name's buffer comes from the allocator service, not the CRT heap: the script's property commands
// (0xccfca0 and the three after it) build it with 0x1be72c and copy it with 0x315060 (0x177110), and the original's
// destructor frees it through the service. A std::string here hands those blocks to the CRT's free, which corrupts the
// heap in game (the service is the GeneralAllocator).
struct Setting {
    GZString mName;    // +0
    uint32_t mValue;   // +0x18
};
static_assert(sizeof(Setting) == 0x1c);

// One value of an option (0x30 bytes): the settings it applies, by type.
struct Value {
    std::vector<Setting> mBooleans; // +0x00 AnyBoolean
    std::vector<Setting> mUint32s;  // +0x0c AnyUint32
    std::vector<Setting> mSint32s;  // +0x18 AnySint32
    std::vector<Setting> mFloats;   // +0x24 AnyFloat32 (the dword's bits)

    // 0xccefa0 (__thiscall, ret 4): every setting into `set` (its slot 3), booleans first, then uint32, sint32, float, each
    // under a service-string key of its name.
    void ApplyTo(cIGZPropertySetString *set) const;
};
static_assert(sizeof(Value) == 0x30);

// A configuration option (0x2c bytes): a name, the saved and the current value index, the values.
struct Option {
    std::string mName;          // +0x00
    uint32_t mSaved;            // +0x18
    uint32_t mCurrent;          // +0x1c
    std::vector<Value> mValues; // +0x20
};
static_assert(sizeof(Option) == 0x2c);
using OptionVector = std::vector<Option>; // destroyed by 0xcce490 (elements by 0xcccfc0)

// The record's machine strings (vector<cRZString>, 0xc bytes): the game's vector with its destructor (0x1441eb, Tidy).
struct MachineStrings : cRZStringVector {
    MachineStrings() : cRZStringVector{nullptr, nullptr, nullptr} {}
    MachineStrings(const MachineStrings &) = delete;
    MachineStrings &operator=(const MachineStrings &) = delete;
    ~MachineStrings() { Tidy(); }
};
static_assert(sizeof(MachineStrings) == 0xc);

// One display device's record (0x118 bytes, no vtable): built inline in slot 5 (0xcd4270), destroyed by 0xcce860.
struct Device {
    static constexpr uint32_t kTag = 0x2bf06724;
    static constexpr uint32_t kSize = 0x118;

    Device(); // the inline construction in 0xcd4270 (+0x2c..+0x3f and +0x114 are left as the block came)
    Device(const Device &) = delete;
    Device &operator=(const Device &) = delete;
    ~Device() = default; // 0xcce860: the members, last first

    // 0xccf4f0 (__thiscall, ret 4): the chosen value of every option (the saved one when `useSaved`, else the current one)
    // applied to +0xd0, then every option's current index as an AnyUint32 under the option prefix + its name, then the
    // device number (0xcd47d0).
    void ApplyOptions(bool useSaved);
    // 0xcd47d0 (__thiscall, ret 4): this device's number (+0xf4) as an AnySint32 activeDeviceNum in `set`.
    void SetDeviceNumber(cIGZPropertySetString *set) const;
    // 0xcd5490 (ret 4): the applied settings (+0xd0) copied into `set`.
    void CopyAppliedSettings(cIGZPropertySetString *set);
    // 0xcd4f00 (ret 4): the script variables (+0xac) copied into `set`.
    void CopyScriptVariables(cIGZPropertySetString *set);
    // 0xccf290: before the detection's results are read: the vendor and device ids parsed (hex) from the PCI text (machine
    // string 13, offsets 8 and 0x16, four digits each) when it is longer than 0x1a, +0x2c / +0x30 to -1, the options
    // cleared, the card flag and the +0x40 / +0x58 / +0x70 strings emptied, machine string 11 into +0x88, +0x114 = 1.
    void ResetForDetection();
    // 0xcd4f20: the description (+0xf8): the format at 0xeb983c with the device number, machine string 12 (';' made
    // ' '), the vendor and device ids, the truncated number 1 and machine string 11.
    void BuildDescription();
    // 0xcd3cf0 (ret 4): every AnyUint32 of `saved` whose key starts with the option prefix (case-insensitive) sets the
    // current index of the first option named by the rest of the key.
    void RestoreSavedOptions(cIGZPropertySetString *saved);
    // 0xcd1550: the description string (+0xf8).
    std::string *Description() { return &mDescription; }

    uint32_t mTag;                          // +0x00 kTag
    std::vector<std::string> mText;         // +0x04
    uint32_t mFlags;                        // +0x10 bit 0 CPU feature, bits 1-2 from the device, bit 3 the NVCPL check
    MachineStrings mMachine;                // +0x14 resized to 0x11 by the detection
    std::vector<float> mNumbers;            // +0x20 resized to 0x10 (CPU speed, memory, device numbers)
    int32_t mUnknown2c;                     // +0x2c -1 after the detection
    int32_t mUnknown30;                     // +0x30 -1 after the detection
    uint32_t mVendorId;                     // +0x34
    uint32_t mDeviceId;                     // +0x38
    uint8_t mCardFound;                     // +0x3c
    std::string mText40;                    // +0x40
    std::string mCardVendor;                // +0x58
    std::string mCardName;                  // +0x70
    std::string mText88;                    // +0x88
    OptionVector mOptions;                  // +0xa0
    cGZPropertySetString mVariables;        // +0xac script variables and results
    cGZPropertySetString mApplied;          // +0xd0 the applied settings
    uint32_t mIndex;                        // +0xf4 device number
    std::string mDescription;               // +0xf8
    void *mDevice;                          // +0x110 the device (its slot 25 releases it)
    uint32_t mErrorCode;                    // +0x114 0: configured
};
static_assert(sizeof(Device) == 0x118);
static_assert(offsetof(Device, mFlags) == 0x10);
static_assert(offsetof(Device, mMachine) == 0x14);
static_assert(offsetof(Device, mNumbers) == 0x20);
static_assert(offsetof(Device, mVendorId) == 0x34);
static_assert(offsetof(Device, mCardFound) == 0x3c);
static_assert(offsetof(Device, mText40) == 0x40);
static_assert(offsetof(Device, mText88) == 0x88);
static_assert(offsetof(Device, mOptions) == 0xa0);
static_assert(offsetof(Device, mVariables) == 0xac);
static_assert(offsetof(Device, mApplied) == 0xd0);
static_assert(offsetof(Device, mIndex) == 0xf4);
static_assert(offsetof(Device, mDescription) == 0xf8);
static_assert(offsetof(Device, mDevice) == 0x110);
static_assert(offsetof(Device, mErrorCode) == 0x114);

// 0xccf930 (__cdecl): every property of `from` (its slots 6 / 7) set into `to` (its slot 3) under the same key.
void CopyProperties(cIGZPropertySetString *from, cIGZPropertySetString *to);

// A counted interface pointer member (released through slot 2 when the owner dies).
template <class T> struct CountedMember {
    T *mObject = nullptr;
    CountedMember() = default;
    CountedMember(const CountedMember &) = delete;
    CountedMember &operator=(const CountedMember &) = delete;
    ~CountedMember()
    {
        if (mObject) mObject->Release();
    }
};

// A cRZString member (the hook-shaped class built and destroyed by hand): 0x20 bytes.
struct StringMember {
    union {
        cRZString mString;
    };
    StringMember() { mString.Construct(); } // 0x144f5b
    StringMember(const StringMember &) = delete;
    StringMember &operator=(const StringMember &) = delete;
    ~StringMember() { mString.Destruct(); } // 0x141193
};
static_assert(sizeof(StringMember) == 0x20);

// A cRZString local a callee constructs (cRZString::SplitOff's out): raw memory until then, destroyed by hand.
struct RawStringMember {
    union {
        cRZString mString;
    };
    RawStringMember() {}
    RawStringMember(const RawStringMember &) = delete;
    RawStringMember &operator=(const RawStringMember &) = delete;
    ~RawStringMember() { mString.Destruct(); }
};

// The config log's cRZTextFile member (0xf4 bytes).
struct TextFileMember {
    union {
        cRZTextFile mFile;
    };
    TextFileMember(); // 0x1c3f3e
    TextFileMember(const TextFileMember &) = delete;
    TextFileMember &operator=(const TextFileMember &) = delete;
    ~TextFileMember() { mFile.Destruct(); } // 0x1bab55
};
static_assert(sizeof(TextFileMember) == 0xf4);

} // namespace tsconfig

// cITSConfigurationManager (iid 0xa1d8284), slots 3..18.
class cITSConfigurationManager : public cIGZUnknown {
public:
    virtual uint8_t Init() = 0;                                                         // slot 3
    virtual uint8_t Shutdown() = 0;                                                     // slot 4
    virtual void DetectAndConfigure() = 0;                                              // slot 5
    virtual uint8_t IsDeviceConfigured(int32_t index) = 0;                              // slot 6
    virtual uint8_t GetUploadTarget(cIGZString *a, cIGZString *b, cIGZString *c) = 0;   // slot 7 (ret 0xc)
    virtual void SelectDevice(int32_t index) = 0;                                       // slot 8
    virtual void RestoreDevice(int32_t index) = 0;                                      // slot 9
    virtual int32_t GetActiveDevice() = 0;                                              // slot 10 (folded 0x3b3fe0: +0x184)
    virtual void SetActiveDevice(void *device) = 0;                                     // slot 11
    virtual void SetCurrentDevice(int32_t index) = 0;                                   // slot 12
    virtual void SetOptionValue(const char *name, uint32_t index) = 0;                  // slot 13
    virtual void GetOptionValue(const char *name, uint32_t *out) = 0;                   // slot 14
    virtual uint8_t GetOptionSetting(const char *option, const char *property, void *out) = 0; // slot 15
    virtual void GetSavedOptionValue(const char *name, uint32_t *out) = 0;              // slot 16
    virtual void SetListener(cIGZUnknown *listener) = 0;                                // slot 17
    virtual void NotifyListener(const char *name, uint32_t value) = 0;                  // slot 18
};

class cTSConfigurationManager : public cITSConfigurationManager, public rz::cRZUnknown {
public:
    static constexpr uint32_t kInterfaceId = 0xa1d8284;
    static constexpr uint32_t kClassId = 0xa1d8285;
    static constexpr uint32_t kSize = 0x190;

    cTSConfigurationManager();           // 0xccdf90
    ~cTSConfigurationManager() override; // 0xccea70 (the deleting destructor 0xccef70 is the compiler's)

    // 0xcd0360: a new manager (null when the allocation fails).
    static cTSConfigurationManager *Create();

    bool QueryInterface(uint32_t id, void **out) override; // 0xcd3cc0 (ret 8)
    uint32_t AddRef() override;                            // folded 0x1dab20
    uint32_t Release() override;                           // folded 0x1e3970

    // 0xcd1ab0 (slot 3): drops the settings set, asks the framework (slot 5) for a new one (clsid 0xac1887e, iid 0x4bb0d269)
    // and keeps it counted; answers whether there was one.
    uint8_t Init() override;
    // 0xcd5540 (slot 4): drops the settings set and every device record; answers 1.
    uint8_t Shutdown() override;
    // 0xcd4270 (slot 5): the detection and configuration. Shutdown and Init (slots 4, 3 through the vtable); the device
    // enumerator from the framework (clsid 0x85522803): per device a new record, DetectProcessor (0xcd0600, original), reset
    // and description, pushed; RestoreActiveDevice from the settings; per record the configuration script (0xcd3550, original,
    // inside catch (...)), the configParserErrorCode it set, and for a configured record its options saved and applied and
    // its variables copied to the device's properties; the active device's saved options again when the settings' software
    // flag matches its own; then WriteConfigLog (0xcd2190, original).
    void DetectAndConfigure() override;
    // 0xcd1be0 (slot 6): whether device `index`'s record says configured (+0x114 == 0).
    uint8_t IsDeviceConfigured(int32_t index) override;
    // 0xcd0d80 (slot 7): the upload target from the command line (original for now).
    uint8_t GetUploadTarget(cIGZString *a, cIGZString *b, cIGZString *c) override;
    // 0xccf880 (slot 8): for a configured device, its current options applied and its script variables copied into the
    // settings; then its applied settings copied into the settings.
    void SelectDevice(int32_t index) override;
    // 0xccf8e0 (slot 9): for a configured device, its saved options applied; then its applied settings into the settings.
    void RestoreDevice(int32_t index) override;
    int32_t GetActiveDevice() override { return mActiveDevice; } // slot 10, folded 0x3b3fe0
    // 0xcd4880 (slot 11): `device` (a display device) becomes the active one: its record's number goes into the settings,
    // and its software-rasterizer flag (read from the device's property set, 1 by default); then the device list text
    // (the count, ';', each record's description and ';') as an AnyString activeDeviceList.
    void SetActiveDevice(void *device) override;
    // 0xcd2a80 (ret 4): the active device (+0x184) found again from the saved settings: activeDeviceNum, then the
    // activeDeviceList text (+0x188 says whether both were there); the saved device's description is cut from the list
    // and matched against the records' descriptions, else its number and its machine string 12 are tried.
    void RestoreActiveDevice(cIGZPropertySetString *saved);
    // 0x7c4ff0 (slot 12): the current device (+0x18c).
    void SetCurrentDevice(int32_t index) override;
    // 0xcd5110 (slot 13): when the current device exists, its option `name`'s current value becomes `index` (when it has
    // that many values): the index is set as an AnyUint32 under the option prefix + name in the settings and in the
    // device's variables, and the value's settings applied to +0x14c and to the settings.
    void SetOptionValue(const char *name, uint32_t index) override;
    // 0xcd1560 (slot 14): the current device's option `name`'s current value index (0 when none, or it has no values).
    void GetOptionValue(const char *name, uint32_t *out) override;
    // 0xcd1740 (slot 15): the current device's option `option`: its current value's settings in a scratch set, and the one
    // named `property` read into `out`. False when there is no such option.
    uint8_t GetOptionSetting(const char *option, const char *property, void *out) override;
    // 0xcd1650 (slot 16): as slot 14 with the saved index.
    void GetSavedOptionValue(const char *name, uint32_t *out) override;
    // 0xcd4ec0 (slot 17): the listener (+0x174), counted.
    void SetListener(cIGZUnknown *listener) override;
    // 0xcd1a10 (slot 18): with a listener, its slot 9 with the name as a service string and the value.
    void NotifyListener(const char *name, uint32_t value) override;

    tsconfig::Device *CurrentDevice() const { return mDevices[mCurrentDevice]; }

    // +0x00 cITSConfigurationManager vtable, +0x04 cRZUnknown vtable, +0x08 its count (mRefCount)
    tsconfig::StringMember mLogPath;                           // +0x0c
    tsconfig::StringMember mLine;                              // +0x2c
    tsconfig::TextFileMember mLog;                             // +0x4c
    tsconfig::OptionVector mOptions;                           // +0x140
    cGZPropertySetString mOverrides;                           // +0x14c
    tsconfig::CountedMember<cIGZPropertySetString> mSettings;  // +0x170
    tsconfig::CountedMember<cIGZUnknown> mListener;            // +0x174
    std::vector<tsconfig::Device *> mDevices;                  // +0x178 (the records are deleted by Shutdown only)
    int32_t mActiveDevice;                                     // +0x184 not set by the constructor
    uint8_t mActiveSoftware;                                   // +0x188 not set by the constructor
    int32_t mCurrentDevice;                                    // +0x18c
};
static_assert(sizeof(cTSConfigurationManager) == 0x190);
static_assert(offsetof(cTSConfigurationManager, mRefCount) == 0x8);
static_assert(offsetof(cTSConfigurationManager, mLogPath) == 0xc);
static_assert(offsetof(cTSConfigurationManager, mLine) == 0x2c);
static_assert(offsetof(cTSConfigurationManager, mLog) == 0x4c);
static_assert(offsetof(cTSConfigurationManager, mOptions) == 0x140);
static_assert(offsetof(cTSConfigurationManager, mOverrides) == 0x14c);
static_assert(offsetof(cTSConfigurationManager, mSettings) == 0x170);
static_assert(offsetof(cTSConfigurationManager, mListener) == 0x174);
static_assert(offsetof(cTSConfigurationManager, mDevices) == 0x178);
static_assert(offsetof(cTSConfigurationManager, mActiveDevice) == 0x184);
static_assert(offsetof(cTSConfigurationManager, mActiveSoftware) == 0x188);
static_assert(offsetof(cTSConfigurationManager, mCurrentDevice) == 0x18c);

namespace tsconfig {
constexpr uint32_t kUnknownFace = 4;
}

struct cTSConfigurationManagerAdapters {
    using Self = cTSConfigurationManager;
    using Device = tsconfig::Device;
    static void __fastcall SetCurrentDevice(Self *self, void *, int32_t index);                      // 0x7c4ff0
};
