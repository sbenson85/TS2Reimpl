#include "ts/cTSConfigurationManager.h"
#include "app/ExceptionReporter.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "misc/HexParse.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cRZIniFile.h"

#include <cstdio>
#include <cstring>
#include <new>

namespace {

// The script variable names (game globals holding pointers to the image's text) and the option key prefix (a std::string
// global the image's initialiser builds).
constexpr uint32_t kActiveDeviceNumName = 0x10899e0;
constexpr uint32_t kActiveDeviceListName = 0x10899e4;
constexpr uint32_t kSoftwareRasterizerName = 0x10899e8;
constexpr uint32_t kParserErrorCodeName = 0x10899ec; // the script's error code in a record's variables
constexpr uint32_t kOptionPrefix = 0x10899f0;

// Formats and delimiters in the image's data.
constexpr uint32_t kDescriptionFormat = 0xeb983c; // the device description
constexpr uint32_t kDecimalFormat = 0xd9b648;     // one decimal number
constexpr uint32_t kListDelimiter = 0xdab158;     // between the saved device's fields

// The record's machine strings the configuration reads (vector<cRZString> at +0x14).
constexpr uint32_t kDriverString = 11; // copied to +0x88, last field of the description
constexpr uint32_t kNameString = 12;   // the card's name (compared when restoring the active device)
constexpr uint32_t kPciString = 13;    // the PCI id text the vendor and device ids are read from

// ORIGINAL-BUG: the saved device's number field is read with the string conversion of the format at 0xda34f0 into a
// 4-byte integer (the argument's stack slot): the first whitespace-delimited token's bytes and its terminator land on the
// integer's bytes, so "0" gives 0x30 (never a valid index: the card name decides). A token of 4 or more characters runs
// past the integer in the original (a stack overwrite); here only the bytes that fit are written.
void ScanTokenInto(const char *text, uint32_t *out)
{
    while (*text == ' ' || (*text >= '\t' && *text <= '\r')) ++text;
    if (!*text) return; // the conversion fails: the integer keeps its value
    uint8_t *bytes = reinterpret_cast<uint8_t *>(out);
    size_t i = 0;
    for (; i < 4 && text[i] && !(text[i] == ' ' || (text[i] >= '\t' && text[i] <= '\r')); ++i) bytes[i] = uint8_t(text[i]);
    if (i < 4) bytes[i] = 0;
}

const char *NameAt(uint32_t rva) { return *GameAddress<const char *const>(rva); }
const char *OptionPrefix() { return GameAddress<const std::string>(kOptionPrefix)->c_str(); }

// The framework's slot 5: the system service of a class, as the interface asked for, into `out`.
constexpr uint32_t kSettingsClassId = 0xac1887e;
// The device enumerator DetectAndConfigure asks for (slot 23 count, 24 device at, devices' slot 25 releases).
constexpr uint32_t kDeviceEnumClassId = 0x85522803;
constexpr uint32_t kDeviceEnumIid = 0xa55227bb;

// The option prefix + `name` in `key` (the original builds the prefix as a temporary service string and assigns it).
void OptionKey(GZServiceString &key, const char *name)
{
    {
        GZServiceString prefix(OptionPrefix());
        key = prefix;
    }
    key.append(name);
}

} // namespace

namespace tsconfig {

// ---- the device record and its parts ----

Device::Device()
    : mTag(kTag), mText(), mFlags(0), mNumbers(), mText40(), mCardVendor(), mCardName(), mText88(), mOptions(), mVariables(),
      mApplied(), mDescription()
{
}

void Value::ApplyTo(cIGZPropertySetString *set) const
{
    for (const Setting &s : mBooleans) {
        AnyBoolean any{GameAddress<const void>(gzany::kBooleanVTable), uint8_t(s.mValue), {}};
        GZServiceString key(s.mName.c_str());
        set->SetProperty(&key, &any);
    }
    for (const Setting &s : mUint32s) {
        AnyUint32 any{GameAddress<const void>(gzany::kUint32VTable), s.mValue};
        GZServiceString key(s.mName.c_str());
        set->SetProperty(&key, &any);
    }
    for (const Setting &s : mSint32s) {
        AnySint32 any{GameAddress<const void>(gzany::kSint32VTable), int32_t(s.mValue)};
        GZServiceString key(s.mName.c_str());
        set->SetProperty(&key, &any);
    }
    for (const Setting &s : mFloats) {
        AnyFloat32 any{GameAddress<const void>(gzany::kFloat32VTable), 0.0f};
        std::memcpy(&any.mValue, &s.mValue, sizeof(float)); // the float's bits as stored
        GZServiceString key(s.mName.c_str());
        set->SetProperty(&key, &any);
    }
}

void Device::ApplyOptions(bool useSaved)
{
    for (Option &o : mOptions) o.mValues[useSaved ? o.mSaved : o.mCurrent].ApplyTo(&mApplied);
    GZServiceString key;
    for (Option &o : mOptions) {
        OptionKey(key, o.mName.c_str());
        AnyUint32 any{GameAddress<const void>(gzany::kUint32VTable), o.mCurrent};
        mApplied.SetProperty(&key, &any);
    }
    SetDeviceNumber(&mApplied);
}

void Device::SetDeviceNumber(cIGZPropertySetString *set) const
{
    AnySint32 any{GameAddress<const void>(gzany::kSint32VTable), int32_t(mIndex)};
    GZServiceString key(NameAt(kActiveDeviceNumName));
    set->SetProperty(&key, &any);
}

void Device::ResetForDetection()
{
    mVendorId = 0;
    mDeviceId = 0;
    const std::string &pci = mMachine.mFirst[kPciString].mString;
    if (pci.size() > 0x1a) {
        {
            const std::string vendor(pci, 8, 4);
            mVendorId = hexparse::ParseHex32(vendor.c_str());
        }
        const std::string device(pci, 0x16, 4);
        mDeviceId = hexparse::ParseHex32(device.c_str());
    }
    mUnknown2c = -1;
    mUnknown30 = -1;
    mOptions.clear();
    mCardFound = 0;
    mText40.clear();
    mCardVendor.clear();
    mCardName.clear();
    mText88 = mMachine.mFirst[kDriverString].mString;
    mErrorCode = 1;
}

void Device::BuildDescription()
{
    StringMember line;
    line.mString.mString.reserve(0x5dc);
    std::string name(mMachine.mFirst[kNameString].mString.c_str());
    for (size_t i = 0; i < name.size(); ++i)
        if (name[i] == ';') name[i] = ' ';
    line.mString.Sprintf(GameAddress<const char>(kDescriptionFormat), mIndex, name.c_str(), mVendorId, mDeviceId,
                         int32_t(mNumbers[1]), mMachine.mFirst[kDriverString].mString.c_str());
    mDescription.assign(line.mString.ToChar());
}

void Device::RestoreSavedOptions(cIGZPropertySetString *saved)
{
    const std::string &prefix = *GameAddress<const std::string>(kOptionPrefix);
    GZServiceString key;
    GZServiceString optionName;
    void *value;
    for (bool more = saved->GetFirst(&key, &value); more; more = saved->GetNext(&key, &value)) {
        if (VirtualCall<uint32_t(__thiscall *)(void *), 4>(value) != gzany::kTypeUint32 || key.size() < prefix.size()) continue;
        if (_strnicmp(key.c_str(), prefix.c_str(), prefix.size()) != 0) continue;
        const uint32_t index = static_cast<const AnyUint32 *>(value)->mValue;
        {
            const GZServiceString rest(key.c_str() + prefix.size(), key.size() - prefix.size());
            const GZServiceString text(rest.c_str());
            optionName = text;
        }
        for (Option &o : mOptions) {
            if (o.mName == optionName.c_str()) {
                o.mCurrent = index;
                break;
            }
        }
    }
}

void Device::CopyAppliedSettings(cIGZPropertySetString *set) { CopyProperties(&mApplied, set); }

void Device::CopyScriptVariables(cIGZPropertySetString *set) { CopyProperties(&mVariables, set); }

void CopyProperties(cIGZPropertySetString *from, cIGZPropertySetString *to)
{
    GZServiceString key;
    void *value;
    for (bool more = from->GetFirst(&key, &value); more; more = from->GetNext(&key, &value)) to->SetProperty(&key, value);
}

TextFileMember::TextFileMember() { appbase::ConstructTextFile(&mFile, nullptr); }

} // namespace tsconfig

using tsconfig::Device;
using tsconfig::Option;

// ---- construction and destruction ----

cTSConfigurationManager::cTSConfigurationManager()
    : mLogPath(), mLine(), mLog(), mOptions(), mOverrides(), mSettings(), mListener(), mDevices(), mCurrentDevice(0)
{
}

// Out of line, not defaulted: the compiler's destructor stores both vtables first, as the original's. Then the members, last
// first: the record vector's storage (the records themselves are Shutdown's), the listener, the settings, the overrides set,
// the options, the log file, the two strings; then cRZUnknown's.
cTSConfigurationManager::~cTSConfigurationManager() {}

cTSConfigurationManager *cTSConfigurationManager::Create() { return new (std::nothrow) cTSConfigurationManager; }

bool cTSConfigurationManager::QueryInterface(uint32_t id, void **out)
{
    if (id != kInterfaceId) return rz::cRZUnknown::QueryInterface(id, out);
    *out = static_cast<cITSConfigurationManager *>(this);
    AddRef();
    return true;
}

uint32_t cTSConfigurationManager::AddRef() { return rz::cRZUnknown::AddRef(); }

uint32_t cTSConfigurationManager::Release() { return rz::cRZUnknown::Release(); }

// ---- the slots ----

uint8_t cTSConfigurationManager::Init()
{
    if (cIGZPropertySetString *old = mSettings.mObject) {
        mSettings.mObject = nullptr;
        old->Release();
    }
    cIGZUnknown *held = nullptr;
    if (void *framework = GetServiceObject()) {
        using Query = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
        VirtualCall<Query, 5>(framework, kSettingsClassId, cGZPropertySetString::kIid, reinterpret_cast<void **>(&held));
    }
    uint8_t found = 0;
    if (held) {
        cIGZPropertySetString *old = mSettings.mObject;
        auto *settings = reinterpret_cast<cIGZPropertySetString *>(held);
        if (settings != old) {
            settings->AddRef();
            mSettings.mObject = settings;
            if (old) old->Release();
        }
        found = 1;
    }
    if (held) held->Release();
    return found;
}

uint8_t cTSConfigurationManager::Shutdown()
{
    if (cIGZPropertySetString *old = mSettings.mObject) {
        mSettings.mObject = nullptr;
        old->Release();
    }
    for (Device *device : mDevices) delete device;
    mDevices.clear();
    return 1;
}

void cTSConfigurationManager::DetectAndConfigure()
{
    // Shutdown and Init through this object's vtable (slots 4 and 3), as the original.
    cITSConfigurationManager *self = this;
    VirtualCall<uint8_t(__thiscall *)(void *), 4>(self);
    VirtualCall<uint8_t(__thiscall *)(void *), 3>(self);
    tsconfig::CountedMember<cIGZUnknown> enumerator; // released after the log, as the original's local reference
    if (void *framework = GetServiceObject()) {
        using Query = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
        VirtualCall<Query, 5>(framework, kDeviceEnumClassId, kDeviceEnumIid, reinterpret_cast<void **>(&enumerator.mObject));
    }
    void *const devices = enumerator.mObject;
    using Count = int32_t(__thiscall *)(void *);
    using DeviceAt = void *(__thiscall *)(void *, int32_t);
    using ReleaseDevice = void(__thiscall *)(void *);
    // One record per device: detected, reset, described.
    for (int32_t i = 0; i < VirtualCall<Count, 23>(devices); ++i) {
        void *device = VirtualCall<DeviceAt, 24>(devices, i);
        Device *record = new Device;
        record->mDevice = device;
        record->mIndex = uint32_t(i);
        OriginalFunction<void(__thiscall *)(Device *, int32_t, void *), 0xcd0600>()(record, i, device);
        record->ResetForDetection();
        record->BuildDescription();
        mDevices.push_back(record);
        VirtualCall<ReleaseDevice, 25>(device);
    }
    RestoreActiveDevice(mSettings.mObject);
    // Each record's configuration script. A record the script left without an error code keeps its current options as the
    // saved ones, applies them and hands its script variables to the device's properties.
    for (int32_t i = 0; i < VirtualCall<Count, 23>(devices); ++i) {
        void *device = VirtualCall<DeviceAt, 24>(devices, i);
        Device *record = mDevices[i];
        try {
            OriginalFunction<void(__thiscall *)(cTSConfigurationManager *, Device *), 0xcd3550>()(this, record);
        } catch (...) {
            // The original's catch (...) swallows whatever the script throws.
        }
        AnyUint32 code{GameAddress<const void>(gzany::kUint32VTable), 1};
        bool found;
        {
            GZServiceString key(NameAt(kParserErrorCodeName));
            found = record->mVariables.GetPropertyValue(&key, &code);
        }
        if (found) record->mErrorCode = code.mValue;
        if (record->mErrorCode == 0) {
            for (Option &o : record->mOptions) o.mSaved = o.mCurrent;
            record->ApplyOptions(false);
            void *info = VirtualCall<void *(__thiscall *)(void *), 16>(device);
            auto *properties = VirtualCall<cIGZPropertySetString *(__thiscall *)(void *), 22>(info);
            record->CopyScriptVariables(properties);
        }
        VirtualCall<ReleaseDevice, 25>(device);
    }
    // The active device, when configured: its saved options again if the software-rasterizer flag in the settings is the
    // one its script set.
    if (mActiveDevice != -1 && VirtualCall<uint8_t(__thiscall *)(void *, int32_t), 6>(self, mActiveDevice)) {
        Device *record = mDevices[mActiveDevice];
        AnyBoolean wanted{GameAddress<const void>(gzany::kBooleanVTable), 1, {}};
        AnyBoolean detected{GameAddress<const void>(gzany::kBooleanVTable), 1, {}};
        {
            GZServiceString key(NameAt(kSoftwareRasterizerName));
            record->mVariables.GetPropertyValue(&key, &detected);
        }
        bool found;
        {
            GZServiceString key(NameAt(kSoftwareRasterizerName));
            found = mSettings.mObject->GetPropertyValue(&key, &wanted);
        }
        if (found && wanted.mValue == detected.mValue) {
            record->RestoreSavedOptions(mSettings.mObject);
            record->ApplyOptions(false);
        }
    }
    OriginalFunction<void(__thiscall *)(cTSConfigurationManager *), 0xcd2190>()(this);
}

uint8_t cTSConfigurationManager::IsDeviceConfigured(int32_t index) { return mDevices[index]->mErrorCode == 0; }

uint8_t cTSConfigurationManager::GetUploadTarget(cIGZString *a, cIGZString *b, cIGZString *c)
{
    using Fn = uint8_t(__thiscall *)(cTSConfigurationManager *, cIGZString *, cIGZString *, cIGZString *);
    return OriginalFunction<Fn, 0xcd0d80>()(this, a, b, c);
}

void cTSConfigurationManager::SelectDevice(int32_t index)
{
    if (IsDeviceConfigured(index)) {
        mDevices[index]->ApplyOptions(false);
        tsconfig::CopyProperties(&mDevices[index]->mVariables, mSettings.mObject);
    }
    mDevices[index]->CopyAppliedSettings(mSettings.mObject);
}

void cTSConfigurationManager::RestoreDevice(int32_t index)
{
    if (IsDeviceConfigured(index)) mDevices[index]->ApplyOptions(true);
    mDevices[index]->CopyAppliedSettings(mSettings.mObject);
}

void cTSConfigurationManager::SetActiveDevice(void *device)
{
    for (Device *record : mDevices) {
        if (record->mDevice != device) continue;
        record->SetDeviceNumber(mSettings.mObject);
        AnyBoolean software{GameAddress<const void>(gzany::kBooleanVTable), 1, {}};
        {
            GZServiceString key(NameAt(kSoftwareRasterizerName));
            void *info = VirtualCall<void *(__thiscall *)(void *), 16>(device);
            auto *properties = VirtualCall<cIGZPropertySetString *(__thiscall *)(void *), 22>(info);
            properties->GetPropertyValue(&key, &software);
        }
        GZServiceString key(NameAt(kSoftwareRasterizerName));
        mSettings.mObject->SetProperty(&key, &software);
        break;
    }
    std::string list;
    tsconfig::StringMember count;
    count.mString.Sprintf(GameAddress<const char>(kDecimalFormat), int32_t(mDevices.size()));
    list.append(count.mString.ToChar());
    list.push_back(';');
    for (Device *record : mDevices) {
        list.append(record->mDescription);
        list.push_back(';');
    }
    AnyString any;
    any.Construct(list.c_str());
    {
        GZServiceString key(NameAt(kActiveDeviceListName));
        mSettings.mObject->SetProperty(&key, &any);
    }
    any.Destruct();
}

void cTSConfigurationManager::RestoreActiveDevice(cIGZPropertySetString *saved)
{
    mActiveDevice = -1;
    AnySint32 number{GameAddress<const void>(gzany::kSint32VTable), 0};
    {
        GZServiceString key(NameAt(kActiveDeviceNumName));
        mActiveSoftware = saved->GetPropertyValue(&key, &number);
    }
    AnyString list{GameAddress<const void>(gzany::kStringVTable), nullptr};
    bool have = false;
    if (mActiveSoftware) {
        GZServiceString key(NameAt(kActiveDeviceListName));
        have = saved->GetPropertyValue(&key, &list);
    }
    mActiveSoftware = have;
    if (have) {
        const std::string text(VirtualCall<const char *(__thiscall *)(void *), 5>(list.mString));
        int32_t count = 0;
        if (std::sscanf(text.c_str(), GameAddress<const char>(kDecimalFormat), &count) == 1 && count != 0) {
            int32_t separators = 0;
            for (size_t i = 0; i < text.size(); ++i)
                if (text[i] == ';') ++separators;
            const int32_t perDevice = (separators - 1) / count;
            const uint32_t first = uint32_t(number.mValue * perDevice + 1);
            const uint32_t last = uint32_t((number.mValue + 1) * perDevice + 1);
            // ORIGINAL-BUG: a separator that is not there leaves the position at the address of `saved` (the argument's
            // stack slot reused): the start then throws out_of_range from the substring, the end clamps to the text's end.
            uint32_t start = uint32_t(uintptr_t(saved));
            for (uint32_t i = 0, seen = 0; i < text.size(); ++i) {
                if (text[i] == ';' && ++seen == first) {
                    start = i;
                    break;
                }
            }
            uint32_t end = uint32_t(uintptr_t(saved));
            for (uint32_t i = 0, seen = 0; i < text.size(); ++i) {
                if (text[i] == ';' && ++seen == last) {
                    end = i;
                    break;
                }
            }
            tsconfig::StringMember name;
            name.mString.mString = std::string(text, start + 1, end - start - 1);
            const uint32_t devices = uint32_t(mDevices.size());
            bool found = false;
            for (uint32_t i = 0; i < devices; ++i) {
                if (mDevices[i]->mDescription == name.mString.ToChar()) {
                    mActiveDevice = int32_t(i);
                    found = true;
                    break;
                }
            }
            if (!found) {
                uint32_t index = devices;
                auto *helpers = reinterpret_cast<cRZIniStringHelpers *>(&name.mString);
                const char *delimiter = GameAddress<const char>(kListDelimiter);
                tsconfig::RawStringMember numberText;
                helpers->SplitOff(&numberText.mString, delimiter);
                ScanTokenInto(numberText.mString.ToChar(), &index);
                tsconfig::RawStringMember cardName;
                helpers->SplitOff(&cardName.mString, delimiter);
                using CompareTo = int32_t(__thiscall *)(void *, const void *, uint32_t);
                auto sameCard = [&](uint32_t i) {
                    return VirtualCall<CompareTo, 15>(&mDevices[i]->mMachine.mFirst[kNameString], &cardName.mString, 1u) == 0;
                };
                if (index < uint32_t(mDevices.size()) && sameCard(index)) {
                    mActiveDevice = int32_t(index);
                } else {
                    bool matched = false;
                    for (uint32_t i = 0; i < uint32_t(mDevices.size()); ++i) {
                        if (sameCard(i)) {
                            mActiveDevice = int32_t(i);
                            matched = true;
                            break;
                        }
                    }
                    if (!matched && index < uint32_t(mDevices.size())) mActiveDevice = int32_t(index);
                }
            }
        }
    }
    if (list.mString) list.mString->Release();
}

void cTSConfigurationManager::SetCurrentDevice(int32_t index) { mCurrentDevice = index; }

void cTSConfigurationManager::SetOptionValue(const char *name, uint32_t index)
{
    if (uint32_t(mCurrentDevice) >= mDevices.size()) return;
    Device *device = mDevices[mCurrentDevice];
    GZServiceString key;
    for (Option &o : device->mOptions) {
        if (o.mName != name || index >= o.mValues.size()) continue;
        o.mCurrent = index;
        OptionKey(key, o.mName.c_str());
        AnyUint32 setting{GameAddress<const void>(gzany::kUint32VTable), index};
        mSettings.mObject->SetProperty(&key, &setting);
        AnyUint32 variable{GameAddress<const void>(gzany::kUint32VTable), index};
        mDevices[mCurrentDevice]->mVariables.SetProperty(&key, &variable);
        o.mValues[index].ApplyTo(&mOverrides);
        o.mValues[index].ApplyTo(mSettings.mObject);
    }
}

void cTSConfigurationManager::GetOptionValue(const char *name, uint32_t *out)
{
    *out = 0;
    for (const Option &o : CurrentDevice()->mOptions) {
        if (o.mName == name && !o.mValues.empty()) {
            *out = o.mCurrent;
            return;
        }
    }
}

uint8_t cTSConfigurationManager::GetOptionSetting(const char *option, const char *property, void *out)
{
    for (const Option &o : CurrentDevice()->mOptions) {
        if (o.mName != option) continue;
        // ORIGINAL-BUG: the current index is not checked against the values (an option without values reads past them).
        cGZPropertySetString scratch;
        o.mValues[o.mCurrent].ApplyTo(&scratch);
        GZServiceString key(property);
        return scratch.cGZPropertySetString::GetPropertyValue(&key, out);
    }
    return 0;
}

void cTSConfigurationManager::GetSavedOptionValue(const char *name, uint32_t *out)
{
    *out = 0;
    for (const Option &o : CurrentDevice()->mOptions) {
        if (o.mName == name && !o.mValues.empty()) {
            *out = o.mSaved;
            return;
        }
    }
}

void cTSConfigurationManager::SetListener(cIGZUnknown *listener)
{
    cIGZUnknown *old = mListener.mObject;
    if (listener == old) return;
    if (listener) listener->AddRef();
    mListener.mObject = listener;
    if (old) old->Release();
}

void cTSConfigurationManager::NotifyListener(const char *name, uint32_t value)
{
    if (!mListener.mObject) return;
    GZServiceString key(name);
    VirtualCall<void(__thiscall *)(void *, GZServiceString *, uint32_t), 9>(mListener.mObject, &key, value);
}

// ---- the RVA hook's adapter ----
// 0x7c4ff0 is also called directly by original code (0x6f891b), so it keeps its hook. Every other function of the class
// is in effect through its callers: the factory hook gives every manager our vtables, and the original functions are
// reached only from the game's vtables and from each other (db/status/cTSConfigurationManager.jsonl).

using Adapters = cTSConfigurationManagerAdapters;

void __fastcall Adapters::SetCurrentDevice(Self *self, void *, int32_t index)
{
    [[msvc::forceinline_calls]] return self->Self::SetCurrentDevice(index);
}
