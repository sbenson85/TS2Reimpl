#pragma once
// The original (not yet replaced) functions the GenericSimCall cases call. They are all defined in PrimGenericSimCall.cpp, so
// the status entry of 0x58bc50 can declare them in one place.
#include <cstdint>

namespace gscorig {

// 0x57ee10 (__cdecl, two sign-extended words, result in al): does the attribute word match.
bool CheckAttribute(int32_t a, int32_t b);
// 0x211da3 (__cdecl, no arguments): a cached service getter.
void *CachedObject211da3();
// 0x57d860 (__thiscall, one argument, ret 4): sets/clears object flags; its result is the case's result.
uint32_t ObjectFlags(void *object, uint32_t mask);
// 0x9c5a89 (__cdecl, no arguments): the cached message server.
void *MessageServer();
// 0x7e8c60 (__cdecl, seven stack arguments, result in al): tries to place an object (block and position as the cases build them).
bool PlaceObject(void *object, void *block, void *position);
// 0x57c6d0 / 0x1d20f0: constructor and destructor of the 0x20-byte list-shaped local case 49 builds and drops unused.
void ConstructList(void *bytes);
void DestructList(void *bytes);
// Parses a GUID text into 16 bytes (hexparse::ParseGuidText, 0x4f8eb0).
void ParseGuid(void *out16, const char *text);
// 0x54b2d0 (__cdecl, one argument, result in al).
uint8_t CheckStackObject(void *object);

} // namespace gscorig
