#include "app/AppInit.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cRZCmdLine.h"
#include "rz/cRZString.h"

namespace appinit {

namespace {

struct LanguageName {
    const char *name;
    int32_t id;
};

// In the order the original compares them (the names are distinct, so the order is not observable).
constexpr LanguageName kLanguages[] = {
    {"english", 1},      {"french", 3},     {"brazilian", 0x23}, {"czech", 0x1a},     {"russian", 0xd},    {"hungarian", 0x1c},
    {"german", 4},       {"italian", 5},    {"spanish", 6},      {"dutch", 7},        {"danish", 8},       {"swedish", 9},
    {"norwegian", 0xa},  {"finnish", 0xb},  {"portuguese", 0xe}, {"japanese", 0xf},   {"polish", 0x10},    {"tchinese", 0x12},
    {"schinese", 0x11},  {"thai", 0x13},    {"koreana", 0x14},
};

// The import at 0xd9a700 (SteamInternal_ContextInit, __cdecl, one argument: the address of a static
// context at 0x1081a88). Its result points to the interface pointer.
constexpr uint32_t kSteamContextInitSlotRva = 0xd9a700;
constexpr uint32_t kSteamAppsContextRva = 0x1081a88;
constexpr int kSteamAppsGetCurrentGameLanguage = 4;
constexpr int kFrameworkSlotCommandLine = 21;
constexpr int kCmdLineSlotSwitchWithValue = 8;

} // namespace

int32_t LanguageIdFromName(const std::string &name)
{
    for (const LanguageName &l : kLanguages)
        if (name == l.name) return l.id;
    return -1;
}

void __fastcall InitLanguage(void *, void *)
{
    int32_t language = 1;

    void *framework = GetServiceObject();
    using GetCmdLine = cIGZCmdLine *(__thiscall *)(void *);
    cIGZCmdLine *source = VirtualCall<GetCmdLine, kFrameworkSlotCommandLine>(framework);
    cRZCmdLine commandLine;
    commandLine.Construct(source);

    cRZString value;
    value.Construct();
    cRZString key;
    key.Construct("l");
    using IsSwitch = uint32_t(__thiscall *)(void *, const void *, void *, uint32_t);
    bool present = static_cast<uint8_t>(VirtualCall<IsSwitch, kCmdLineSlotSwitchWithValue>(&commandLine, &key, &value, 1)) != 0;
    key.Destruct();

    bool known = false;
    if (present) {
        void *service = GetLanguageService();
        using Lookup = uint32_t(__thiscall *)(void *, const void *, int32_t *);
        known = static_cast<uint8_t>(VirtualCall<Lookup, kLanguageSlotLookup>(service, &value, &language)) != 0;
    }
    if (!known) {
        language = -1;
        using ContextInit = void **(__cdecl *)(void *);
        void **context = (*GameAddress<ContextInit>(kSteamContextInitSlotRva))(GameAddress<void>(kSteamAppsContextRva));
        void *apps = *context;
        using GetLanguage = const char *(__thiscall *)(void *);
        std::string name(VirtualCall<GetLanguage, kSteamAppsGetCurrentGameLanguage>(apps));
        language = LanguageIdFromName(name);
    }

    void *service = GetLanguageService();
    using SetLanguage = uint32_t(__thiscall *)(void *, int32_t);
    VirtualCall<SetLanguage, kLanguageSlotSetLanguage>(service, language);

    value.Destruct();
    commandLine.Destruct();
}

void *GetCachedCommonImpl()
{
    void **cache = GameAddress<void *>(kCommonImplCacheRva);
    if (*cache == nullptr) {
        void *framework = GetServiceObject();
        using GetApp = void *(__thiscall *)(void *);
        void *app = VirtualCall<GetApp, kFrameworkSlotApp>(framework);
        using Query = uint32_t(__thiscall *)(void *, uint32_t, void **);
        VirtualCall<Query, 0>(app, kCommonImplInterfaceId, cache);
        void *found = *cache;
        using Release = uint32_t(__thiscall *)(void *);
        VirtualCall<Release, 2>(found);
    }
    return *cache;
}

namespace {
template <int Slot> uint32_t ForwardToCommonImpl(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    void *object = GetCachedCommonImpl();
    using Fn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    return VirtualCall<Fn, Slot>(object, a, b, c, d, e);
}
} // namespace

uint32_t __cdecl CommonImplSlot3(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { return ForwardToCommonImpl<3>(a, b, c, d, e); }
uint32_t __cdecl CommonImplSlot4(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { return ForwardToCommonImpl<4>(a, b, c, d, e); }
uint32_t __cdecl CommonImplSlot5(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { return ForwardToCommonImpl<5>(a, b, c, d, e); }

// ---- option flags ----

uint32_t __cdecl GetOptionMask()
{
    uint32_t *mask = GameAddress<uint32_t>(kOptionMaskRva);
    if (*mask == 0) {
        cRZString text;
        text.Construct();
        uint32_t bit = 1;
        for (int i = 0; i < 32; ++i) {
            if (static_cast<uint8_t>(CommonImplSlot4(kOptionQueryId, reinterpret_cast<uint32_t>(&text), 1, 0, bit)) != 0) *mask |= bit;
            bit = (bit << 1) | (bit >> 31);
        }
        *mask |= 0x1000;
        text.Destruct();
    }
    return *mask;
}

uint32_t __cdecl HighestOptionBit()
{
    uint32_t value = GetOptionMask() >> 1;
    uint32_t count = 0;
    while (value != 0) {
        ++count;
        value >>= 1;
    }
    return count;
}

uint32_t __cdecl HasOptionBits(uint32_t bits)
{
    const uint32_t common = GetOptionMask() & bits;
    return (common & ~0xffu) | static_cast<uint32_t>(common == bits);
}

void __fastcall ReadApplicationOptions(void *, void *)
{
    void *framework = GetServiceObject();
    using GetCmdLine = cIGZCmdLine *(__thiscall *)(void *);
    cIGZCmdLine *commandLine = VirtualCall<GetCmdLine, 21>(framework);
    cRZString commandText;
    commandText.Construct();
    cIGZCmdLine *line = commandLine;
    using GetText = uint32_t(__thiscall *)(void *, void *);
    VirtualCall<GetText, 5>(line, &commandText);

    cRZString optionA, optionB, optionC;
    optionA.Construct();
    CommonImplSlot4(0x2bcfce83, reinterpret_cast<uint32_t>(&optionA), 1, 0, 0xffffffffu);
    optionB.Construct();
    CommonImplSlot4(0xebcfce8e, reinterpret_cast<uint32_t>(&optionB), 1, 0, 0xffffffffu);
    optionC.Construct();
    CommonImplSlot4(0x6bcfce93, reinterpret_cast<uint32_t>(&optionC), 1, 0, 0xffffffffu);

    cRZString flagText;
    flagText.Construct();
    uint32_t top = HighestOptionBit();
    for (uint32_t bit = 0; bit <= top; ++bit) CommonImplSlot4(kOptionQueryId, reinterpret_cast<uint32_t>(&flagText), 1, 0, 1u << bit);

    flagText.Destruct();
    optionC.Destruct();
    optionB.Destruct();
    optionA.Destruct();
    commandText.Destruct();
}

// ---- constructors ----


cGZPropertySetBase *cGZPropertySetBase::Construct()
{
    mVTable = GameAddress(kVTable);
    for (uint32_t &f : mFields) f = 0;
    return this;
}

cGZPropertySetString *cGZPropertySetString::Construct()
{
    cGZPropertySetBase::Construct();
    mVTable = GameAddress(kVTable);
    mTail[0] = mTail[1] = mTail[2] = 0;
    return this;
}

} // namespace appinit
