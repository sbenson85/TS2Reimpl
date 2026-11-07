#pragma once
#include "app/AppBase.h"
#include "rz/ServiceGetters.h"
#include "rz/cGZAllocatorService.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>

// Startup helpers of the application object (cTSAppFW's PreFrameworkInit / AddCOMDirectorsHere /
// AddApplicationServicesHere and what they call). RVAs are 1.18.0.187; docs/startup-map.md 4.3-4.5.
namespace appinit {

// ---- the language service (0x9c5a5d) ----
//
// A cached service getter (cache global 0x10f51c8, service ids 0x441e5070 / 0x54b7d5): the same shape as
// the eight getters of rz/ServiceGetters.h, so it is that template. 0x9c5a5d is `__cdecl, no
// arguments`; the object is the language service. Its slots used by startup:
//   slot 8  (ret 4)    set the game language to an id (the ids of the `Locale` config switch)
//   slot 18 (ret 8)    (const cRZString *name, int32_t *id): look a language name up; false when unknown
constexpr uint32_t kLanguageServiceCacheRva = 0x10f51c8;
constexpr uint32_t kLanguageServiceFirstId = 0x441e5070;
constexpr uint32_t kLanguageServiceSecondId = 0x54b7d5;
constexpr int kLanguageSlotSetLanguage = 8;
constexpr int kLanguageSlotLookup = 18;

inline void *GetLanguageService()
{
    return CachedServiceGetter<kLanguageServiceCacheRva, kLanguageServiceFirstId, kLanguageServiceSecondId>();
}

// ---- 0xc29a0b: choose the game language (called by AddCOMDirectorsHere with ecx = the cIGZApp face) ----
//
// __thiscall, no stack arguments; `this` is not used. Copies the process command line (framework
// slot 21 -> cRZCmdLine), looks for the switch "l" with a value (IsSwitchPresent, command line slot
// 8, trimming quotes). If it is present and the language service (slot 18) recognises the value, that
// id is used. Otherwise the language name from Steam (ISteamApps::GetCurrentGameLanguage, reached
// through the import SteamInternal_ContextInit at 0xd9a700, slot 4 of the interface) is mapped to an
// id, -1 when unknown. The id goes to the language service's slot 8.
//
// Names and ids (case sensitive): english 1, french 3, german 4, italian 5, spanish 6, dutch 7,
// danish 8, swedish 9, norwegian 10, finnish 11, russian 13, portuguese 14, japanese 15, polish 16,
// schinese 17, tchinese 18, thai 19, koreana 20, czech 26, hungarian 28, brazilian 35.
void __fastcall InitLanguage(void *face, void *edx);

// The language id for a Steam language name, -1 for a name that is not listed.
int32_t LanguageIdFromName(const std::string &name);

// ---- 0x1d0c00 / 0x1d0c60 / 0x1d0d10: forwarders to the cTSAppCommonImpl interface ----
//
// __cdecl, five arguments. The object is cached in the game global at 0x10cc040: when it is null, the
// framework object's slot 37 is called, then slot 0 (QueryInterface) of its result with id
// 0xebabd35c and the cache global as the output, then slot 2 (Release) of the object found (no null
// checks anywhere). Then the cached object's slot N is called with the same five arguments and its
// result returned: 0x1d0d10 is slot 3, 0x1d0c00 slot 4, 0x1d0c60 slot 5.
constexpr uint32_t kCommonImplCacheRva = 0x10cc040;
constexpr uint32_t kCommonImplInterfaceId = 0xebabd35c;
constexpr int kFrameworkSlotApp = 37;

void *GetCachedCommonImpl();
uint32_t __cdecl CommonImplSlot3(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e); // 0x1d0d10
uint32_t __cdecl CommonImplSlot4(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e); // 0x1d0c00
uint32_t __cdecl CommonImplSlot5(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e); // 0x1d0c60

// ---- 0xb39380 / 0xb39470 / 0xc274e3: the option flags read from cTSAppCommonImpl ----
//
// 0xb39380 (__cdecl, no arguments; EH frame): the flag mask in the game global 0x10faf94. While it is 0,
// bit i (i = 0..31) is set when CommonImplSlot4(0x4bcfce5e, &emptyString, 1, 0, 1 << i) returns true
// (al); afterwards 0x1000 is set whether or not anything was, so a built mask is never 0. Returns the
// global. The string argument is a cRZString local, empty on every call.
constexpr uint32_t kOptionMaskRva = 0x10faf94;
constexpr uint32_t kOptionQueryId = 0x4bcfce5e;
uint32_t __cdecl GetOptionMask();
// 0xb39470 (__cdecl): the index of the highest bit of GetOptionMask() (0 when the mask is 0 or 1).
uint32_t __cdecl HighestOptionBit();
// 0xb39490 (__cdecl, one argument): whether GetOptionMask() has all the bits of `bits`. The result is a dword whose low byte is the
// answer (0 or 1) and whose upper 24 bits are those of (mask & bits): callers read only al.
uint32_t __cdecl HasOptionBits(uint32_t bits);
// 0xc274e3 (ecx unused; reached with the cIGZApp face): asks the framework's command line (slot 21) for
// its text (that object's slot 5, into a local cRZString), then reads the text options 0x2bcfce83,
// 0xebcfce8e and 0x6bcfce93 (CommonImplSlot4 with a local string, 1, 0, -1 each), then for every bit
// 0..HighestOptionBit() the flag 1 << bit (CommonImplSlot4(0x4bcfce5e, &string, 1, 0, 1 << bit)); the
// results of all of them are not used. Called by AddApplicationServicesHere.
void __fastcall ReadApplicationOptions(void *face, void *edx);

// ---- constructors of the services AddApplicationServicesHere creates (docs/startup-map.md 4.5) ----
//
// The std::map/std::set-shaped lists several of the classes below hold have a sentinel node made at
// construction (0x1ba147 for 0x34 bytes, 0x1a8666 for 0x30, inline for 0x1c in cWorldDB): a block whose
// first three dwords point to itself and whose word at +0xc is 0x101; the rest is not written. The two
// helper functions are the library's (head-node allocation of an empty std::map instantiation), so
// they are not reimplemented or hooked; the constructors below make the node the same way.

// cGZPropertySetT<string traits> (0x18 bytes at the start of the property sets): 0x1bec45 sets the
// vtable (0xda3dc0) and zeroes +4..+0x14.
struct cGZPropertySetBase {
    static constexpr uint32_t kVTable = 0xda3dc0;
    cGZPropertySetBase *Construct();                // 0x1bec45
    const void *mVTable;
    uint32_t mFields[5];
};
static_assert(sizeof(cGZPropertySetBase) == 0x18, "cGZPropertySetBase layout");

// cGZPropertySetString (0x24 bytes): the base, then the vtable 0xd9f718 and +0x18..+0x20 zeroed.
struct cGZPropertySetString : cGZPropertySetBase {
    static constexpr uint32_t kVTable = 0xd9f718;
    cGZPropertySetString *Construct();              // 0x22b157
    uint32_t mTail[3];
};
static_assert(sizeof(cGZPropertySetString) == 0x24, "cGZPropertySetString layout");

// cGZPropertyManagerT<string traits> (0xc26045, the property manager service) is a class of ours: src/gz/cGZPropertyManager.h.

// cRZNetSSLManager (0xc644aa, the SSL manager service) is a class of ours: src/rz/cRZNetSSLManager.h.

// nTSWorld::cWorldDB's constructor (0xd0b6c0) moved to src/world/cWorldDB.* (lane JC: the class in the standalone shape).

} // namespace appinit
