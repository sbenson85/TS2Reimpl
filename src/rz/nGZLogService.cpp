// More of nGZLog::cService's cIService slots (lane LY, F4): adding an output back end (slot 11), the group level
// setters by wildcard (slots 9 and 20) and the configuration text (slot 24, with the presets and the quoted text
// helpers). The rest of the service is in nGZLog.size.cpp.
#include "rz/nGZLog.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/RZWildcard.h"

#include <cstring>

namespace nGZLog {
namespace {

cLogService *ServiceOf(void *sub)
{
    return reinterpret_cast<cLogService *>(static_cast<char *>(sub) - cLogService::kServiceSubobjectOffset);
}

// cIGZUnknown.
constexpr int kSlotAddRef = 1;
constexpr int kSlotRelease = 2;
using RefFn = uint32_t(__thiscall *)(void *);
using QueryFn = bool(__thiscall *)(void *, uint32_t, void **);

// An output back end's slots.
constexpr int kOutputSlotGetFormatter = 6;
constexpr int kOutputSlotSetFormatter = 7;
constexpr int kOutputSlotGetFilter = 8;
constexpr int kOutputSlotSetFilter = 9;
constexpr int kOutputSlotSetText = 10;
constexpr int kOutputSlotText = 11;
constexpr int kOutputSlotSetEnabled = 12;
constexpr int kOutputSlotWriteConfig = 19;
using GetFn = void *(__thiscall *)(void *);
using SetFn = void(__thiscall *)(void *, void *);
using TextFn = const char *(__thiscall *)(void *);
using SetTextFn = void(__thiscall *)(void *, const char *);
using SetByteFn = void(__thiscall *)(void *, uint8_t);
using WriteFn = void(__thiscall *)(void *, void *);

// A filter's or formatter's slots.
constexpr int kNamedSlotWrite = 5;
constexpr int kNamedSlotSetName = 6;
constexpr int kNamedSlotName = 7;
constexpr int kNamedSlotClone = 8;
// The filter interface (0xaca92697) and its slot 9: set a group's level.
constexpr uint32_t kFilterInterface = 0xaca92697;
constexpr int kFilterSlotSetGroupLevel = 9;
using SetGroupLevelFn = void(__thiscall *)(void *, const char *, int32_t);

// The stream's slots.
constexpr int kStreamSlotText = 14;
constexpr int kStreamSlotString = 15;
using StreamTextFn = void(__thiscall *)(void *, const char *);
using StreamStringFn = void(__thiscall *)(void *, const cIGZString *);

// Group names that are never recorded (game globals pointing at "NoGroup" and "AnyGroup").
constexpr uint32_t kNoGroupPointer = 0x1085d98;
constexpr uint32_t kAnyGroupPointer = 0x1085d9c;

// The configuration text.
constexpr char kGroupNamesHeader[] = "\n# Group Names --------------\n\n";
constexpr char kGroupNamePrefix[] = "# ";
constexpr char kPresetsHeader[] = "\n# Presets --------------\n\n";
constexpr char kFormattersHeader[] = "\n# Formatters --------------\n\n";
constexpr char kFiltersHeader[] = "\n# Filters --------------\n\n";
constexpr char kReportersHeader[] = "\n# Reporters --------------\n\n";
constexpr char kPresetWord[] = "logPreset";
constexpr char kSeparator[] = " ";
constexpr char kNewline[] = "\n";
constexpr char kQuote = '"';
constexpr char kEscapedNewline[] = "\\n";
constexpr char kEscapedTab[] = "\\t";
constexpr size_t kEscapeLength = 2;
constexpr size_t kSmallCapacity = 15; // a std::string's own buffer

void AddRef(void *object) { VirtualCall<RefFn, kSlotAddRef>(object); }
// The service's locks are called through their vtables (slot 1 lock, slot 2 unlock), as the original does.
constexpr int kLockSlotLock = 1;
constexpr int kLockSlotUnlock = 2;
void Lock(cRZCriticalSection *lock) { VirtualCall<RefFn, kLockSlotLock>(lock); }
void Unlock(cRZCriticalSection *lock) { VirtualCall<RefFn, kLockSlotUnlock>(lock); }
void Release(void *object) { VirtualCall<RefFn, kSlotRelease>(object); }

// The filter or formatter called `name` for a new output: the output's own (held), else the service's of that name
// (held), else a clone of the service's default named `name` and added to `list`.
void *ChooseNamed(void *own, void *(__fastcall *find)(cLogService *, void *, const char *), cLogService *service,
                  const char *name, void *defaultObject, std::vector<RefHolder> &list)
{
    RZUnknownSlot held{own};
    if (own) {
        AddRef(own);
        return own;
    }
    held.Assign(find(service, nullptr, name));
    if (held.mObject) return held.mObject;
    held.Assign(VirtualCall<GetFn, kNamedSlotClone>(defaultObject));
    VirtualCall<SetTextFn, kNamedSlotSetName>(held.mObject, name);
    list.push_back(reinterpret_cast<const RefHolder &>(held.mObject));
    return held.mObject;
}

// Records `group` in the service's name table unless it is one of the two reserved names (only while the
// service's flag at +0xf4 is set).
void RecordGroupName(cLogService *service, const char *group)
{
    if (!service->mCollectGroups) return;
    if (_stricmp(group, *GameAddress<const char *>(kNoGroupPointer)) == 0) return;
    if (_stricmp(group, *GameAddress<const char *>(kAnyGroupPointer)) == 0) return;
    Lock(&service->mLock28);
    service->mNames.insert(LogString(group));
    Unlock(&service->mLock28);
}

void SetGroupLevelThrough(void *filter, const char *group, int32_t level)
{
    VirtualCall<SetGroupLevelFn, kFilterSlotSetGroupLevel>(filter, group, level);
}

} // namespace

uint8_t __fastcall ServiceAddOutput(void *sub, void *, const char *name, void *output)
{
    cLogService *service = ServiceOf(sub);
    Lock(&service->mLock90);
    for (const RefHolder *it = service->mOutputs.data(); it != service->mOutputs.data() + service->mOutputs.size(); ++it) {
        if (_stricmp(VirtualCall<TextFn, kOutputSlotText>(it->mObject), name) == 0) {
            Unlock(&service->mLock90);
            return 0;
        }
    }
    void *filter = ChooseNamed(VirtualCall<GetFn, kOutputSlotGetFilter>(output), &FindFilter, service, name,
                               service->mFilter.mObject, service->mFilters);
    void *formatter = ChooseNamed(VirtualCall<GetFn, kOutputSlotGetFormatter>(output), &FindFormatter, service, name,
                                  service->mFormatter.mObject, service->mFormatters);
    VirtualCall<SetTextFn, kOutputSlotSetText>(output, name);
    VirtualCall<SetFn, kOutputSlotSetFormatter>(output, formatter);
    VirtualCall<SetFn, kOutputSlotSetFilter>(output, filter);
    VirtualCall<SetByteFn, kOutputSlotSetEnabled>(output, uint8_t{1});
    AddRef(output);
    service->mOutputs.push_back(reinterpret_cast<const RefHolder &>(output));
    Release(output);
    if (formatter) Release(formatter);
    if (filter) Release(filter);
    Unlock(&service->mLock90);
    return 1;
}

uint8_t __fastcall ServiceSetFilterGroupLevel(void *sub, void *, const char *pattern, const char *group, int32_t level)
{
    cLogService *service = ServiceOf(sub);
    uint8_t matched = 0;
    RecordGroupName(service, group);
    Lock(&service->mLock90);
    for (const RefHolder *it = service->mFilters.data(); it != service->mFilters.data() + service->mFilters.size(); ++it) {
        void *filter = nullptr;
        VirtualCall<QueryFn, 0>(it->mObject, kFilterInterface, &filter);
        if (!filter) continue;
        if (RZWildcard::MatchCase(VirtualCall<TextFn, kNamedSlotName>(filter), pattern, 0)) {
            SetGroupLevelThrough(filter, group, level);
            matched = 1;
        }
        Release(filter);
    }
    Unlock(&service->mLock90);
    return matched;
}

uint8_t __fastcall ServiceSetOutputGroupLevel(void *sub, void *, const char *pattern, const char *group, int32_t level)
{
    cLogService *service = ServiceOf(sub);
    uint8_t matched = 0;
    RecordGroupName(service, group);
    Lock(&service->mLock90);
    for (const RefHolder *it = service->mOutputs.data(); it != service->mOutputs.data() + service->mOutputs.size(); ++it) {
        void *output = it->mObject;
        if (!RZWildcard::MatchCase(VirtualCall<TextFn, kOutputSlotText>(output), pattern, 0)) continue;
        // The output's filter is asked unchecked (a null filter faults, as in the original).
        void *outputFilter = VirtualCall<GetFn, kOutputSlotGetFilter>(output);
        void *filter = nullptr;
        VirtualCall<QueryFn, 0>(outputFilter, kFilterInterface, &filter);
        if (filter) SetGroupLevelThrough(filter, group, level);
        matched = 1;
        if (filter) Release(filter);
    }
    Unlock(&service->mLock90);
    return matched;
}

cRZString *__fastcall QuoteText(void *, void *, const cRZString *text, cRZString *out)
{
    std::string &s = out->mString;
    s.clear();
    // The image's reserve also gives a heap buffer back when the request fits the string's own small buffer
    // (our library's reserve never shrinks).
    const size_t wanted = text->Strlen() * 2 + 2;
    if (wanted <= kSmallCapacity) s.shrink_to_fit();
    else s.reserve(wanted);
    s.push_back(kQuote);
    for (uint32_t i = 0; i < text->Strlen(); ++i) {
        const char c = text->mString[i];
        if (c == '\n') s.append(kEscapedNewline, kEscapeLength);
        else if (c == '\t') s.append(kEscapedTab, kEscapeLength);
        else s.push_back(c);
    }
    s.push_back(kQuote);
    return out;
}

void __fastcall WritePresets(AliasMap *aliases, void *, void *stream)
{
    LogString scratch;
    for (const auto &entry : *aliases) {
        VirtualCall<StreamTextFn, kStreamSlotText>(stream, kPresetWord);
        VirtualCall<StreamTextFn, kStreamSlotText>(stream, kSeparator);
        VirtualCall<StreamStringFn, kStreamSlotString>(stream, entry.first.AsInterface());
        VirtualCall<StreamTextFn, kStreamSlotText>(stream, kSeparator);
        VirtualCall<StreamStringFn, kStreamSlotString>(stream, QuoteText(aliases, nullptr, &entry.second, &scratch)->AsInterface());
        VirtualCall<StreamTextFn, kStreamSlotText>(stream, kNewline);
    }
}

void __fastcall ServiceWriteConfig(void *sub, void *, void *stream)
{
    cLogService *service = ServiceOf(sub);
    auto text = [stream](const char *s) { VirtualCall<StreamTextFn, kStreamSlotText>(stream, s); };
    text(kGroupNamesHeader);
    Lock(&service->mLock28);
    for (const LogString &name : service->mNames) {
        text(kGroupNamePrefix);
        text(name.ToChar());
        text(kNewline);
    }
    text(kPresetsHeader);
    WritePresets(service->mAliases, nullptr, stream);
    Unlock(&service->mLock28);
    text(kFormattersHeader);
    for (const RefHolder *it = service->mFormatters.data(); it != service->mFormatters.data() + service->mFormatters.size(); ++it)
        VirtualCall<WriteFn, kNamedSlotWrite>(it->mObject, stream);
    text(kFiltersHeader);
    for (const RefHolder *it = service->mFilters.data(); it != service->mFilters.data() + service->mFilters.size(); ++it)
        VirtualCall<WriteFn, kNamedSlotWrite>(it->mObject, stream);
    text(kReportersHeader);
    Lock(&service->mLock90);
    for (const RefHolder *it = service->mOutputs.data(); it != service->mOutputs.data() + service->mOutputs.size(); ++it)
        VirtualCall<WriteFn, kOutputSlotWriteConfig>(it->mObject, stream);
    Unlock(&service->mLock90);
}

} // namespace nGZLog
