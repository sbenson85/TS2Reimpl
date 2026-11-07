#pragma once
#include <cstdint>
#include <string>

#include "game/owned.h"

// g_Framework (0x10c9378) and its second reference (0x10c937c) as owned statics: the image's words in the hooked game,
// owned_statics.cpp's otherwise (the image-free start-up stores the framework there).
TS2_OWNED_STATIC(void *, 10c9378);
TS2_OWNED_STATIC(void *, 10c937c);
// RZFormattedErrorCtor's 2 KB formatting buffer (lane RU; zeroed .bss in the image, owned_statics.cpp's otherwise).
TS2_OWNED_STATIC(char, 10cb840)[0x800];

// Small framework helpers that many game functions call (RVAs 1.18.0.187). Each is the only
// unverified callee of dozens to hundreds of functions, so they are reimplemented first. Names
// describe what the code does; the original's own names are unknown.

// 0x142080 (__thiscall, ret 4): assignment of an object pointer held in a slot. AddRefs the new
// object (vtable slot 1), stores it, then Releases the old one (slot 2). Returns the slot.
struct RZUnknownSlot {
    void *mObject;
    RZUnknownSlot *Assign(void *object);
};

// 0x1427b5 (__cdecl, no arguments): returns the game global at 0xa10c9378 (purpose unknown). Also
// reached through the 5-byte thunk at 0x14323f, so it is not hooked.
uint32_t RZGetGlobal10C9378();

// 0x1b945c (__thiscall, ret 8): QueryInterface of cRZUnknown-style objects that implement only
// cIGZUnknown (riid 1). A match stores this in *ppv, AddRefs it (slot 1) and returns true;
// anything else returns false and leaves *ppv alone. Shared by about 111 classes (shared_vf_1b945c),
// so it is verified but not hooked.
struct RZUnknownQueryInterface {
    bool QueryInterfaceUnknown(uint32_t riid, void **ppv);
};

// std::string operator= (the library's 0x14205a) over an object that holds the string at +0. Returns this.
struct RZStringSlot {
    std::string mString;
    RZStringSlot *Assign(const std::string &other);
};

// 0x1d099e (__cdecl, varargs): constructs a nGZCommandParser::cError-style object in place (vtable
// 0xda5060, std::string message at +4). The message is formatted with _vsnprintf_l into the game's
// 2 KB buffer at 0xa10cb840 and copied into the string. Returns dst.
void *RZFormattedErrorCtor(void *dst, const char *format, ...);

// 0x1be23b (__thiscall, ret 4): walks a circular intrusive list (node: prev at +0, next at +4).
// For each node after the head, in list order, calls this's vtable slot 3 with the node (its next
// pointer is read before the call). Then calls slot 3 with the head. Does nothing for a null head.
struct RZListNodeVisitor {
    void VisitNodesThenHead(void *head);
};

// 0x1b9052 (__thiscall): cRZUnknown-layout constructor. Sets the vtable (0xda2fd0) and a zero
// count at +4, returns this. Byte-identical copies (0x1b9035, 0x1c8c84, 0xb2a7f6, 0xb2ed40) differ
// only in the vtable address, so they need their own entries if they should be replaced too.
struct RZUnknownLayout {
    const void *mVTable;
    uint32_t mRefCount;
    RZUnknownLayout *Construct();
};

// 0xb6d930 (__thiscall): stores the cRZWinProc4 vtable (0xe89534) at +0. Likely the vtable step
// of that class's constructor.
struct RZWinProc4Vtable {
    const void *mVTable;
    uint32_t mRefCount; // +4: AddRef (slot 1, 0x148fb0) and Release (slot 2, 0xb6e050) count here
    void SetVTable();
    // 0xb6d920 (__thiscall, no arguments): cRZWinProc4's constructor. Stores the vtable, then clears
    // the reference count; returns this.
    RZWinProc4Vtable *Construct();
};

// 0x9e8850 (__thiscall): returns the dword 12 bytes before this (a field of an enclosing object,
// read through a secondary base). Not hookable (4 bytes).
struct RZEnclosedFields {
    uint32_t GetDwordMinus12() const;
    // 0x9d9570 (__thiscall): returns the byte 8 bytes before this. Not hookable (4 bytes).
    uint8_t GetByteMinus8() const;
};
