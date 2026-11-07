#pragma once
#include "simantics/PrimExpression.h"
#include "simantics/PrimHandlers.h"

#include <cstddef>
#include <cstdint>
#include <string>

// SimAntics primitive handlers, lane S (1.18.0.187): the functions cTSObject::vf332 (0x534650) calls for opcodes
// 0x22, 0x2a, 0x31, 0x32, 0x77, 0x7b and 0x7c, each `handler(this = the object, frame, param)` (__thiscall, ret 8),
// and the small helper 0x7c uses. Opcode -> handler map: docs/subsystems.md ("Established by the host trace").
// They return the branch code the interpreter takes and report bad operands by throwing a 16-bit code
// (`throw (short)code`). Everything is reached through vtable slots (the numbers are vtable offsets / 4); the code
// names the role each slot plays in the handler where it is known.

// The object as these handlers see it. Same prefix as PrimMidObject (PrimMid.h).
struct PrimSObject {
    void **mVTable;         // +0x00 primary vtable (cTSObject, 0xdd2f0c)
    void **mSecondary;      // +0x04 vtable of the secondary subobject (this + 4 is that subobject); slot 7 reads an operand
    uint8_t mPad[0x38];     // +0x08..+0x3f
    void *mContext;         // +0x40 the context object (slots 4, 26 and 3 give the tables the handlers use)
    uint8_t mPad2[4];       // +0x44
    void *mOwner;           // +0x48

    // 0x5a8b30 (opcode 0x7c, WantSatisfy). Reads a person id (operand 1: scope byte +4, id word +5), turns it into an
    // entity id with ResolveEntityId (flag: param byte +0x11 bit 0; error 0x1c when 0), finds the want-handler the
    // context's slot-4 registry knows for the dword at param +7 (slot 23; error 7), builds a 10-byte request
    // {kind (the handler's slot 16), value dword, extra word} from operands 2 (scope +0xb, id +0xc) and 3 (scope
    // +0xe, id +0xf) according to the kind (0..6; error 0x1c or 0x17 when an entity or object is unknown) and hands
    // it to the registry's slot 34 with the entity id, the dword, the request and an extra id. Returns 1 when that
    // says yes, else 0.
    uint32_t Prim7C(void *frame, const PrimParamView *param);

    // 0x59eb90 (opcode 0x31). Looks up the person object by the frame's slot 12 (error 0x15), asks it for interface
    // 0x99c7f5a, from that takes (through the sub-interface at +4) a chain of three referenced objects (errors 0x44e,
    // 0x44f when the second and third are missing) and, from the last one's slot 3 (a mode): 0 resets the first
    // object (slot 32) and tells the context's slot-26 table (slot 71) the person's slot-47 value; 0x11 only
    // resets it. Returns 1.
    uint32_t Prim31(void *frame, const PrimParamView *param);

    // 0x59de90 (opcode 0x77, Message). Builds a six-word message {operand 1 (scope +0x13, id +5), the frame's slot 12,
    // param byte +0xc, operand 2 (scope +0xd, id +0xe), operand 3 (scope +0x10, id +0x11), this slot 19(0)} and delivers it
    // through this slot 317 (object, message): to the one object operand 4 names when param byte +8 bit 1 is set (error
    // 0x17 when unknown), else to every object of the kind param byte +7 selects (modes 6 and 7: the entries of the
    // list at this +0x1c4; others the vector the context's slot-63 object's slot 45 gives) that the matching rules
    // accept. Returns 1 when something was delivered to the direct target or a matching object, else 0.
    uint32_t Prim77(void *frame, const PrimParamView *param);

    // 0x585590 (opcode 0x7b, Cinematic). Looks up the object operand 1 (scope +0xd, id +0xe) names in the context's
    // slot-26 table (error 0x17), requires its slot 266 to report at least one (error 0x6c), loads the text for string
    // table 0x97 / entry (param word +4, or operand 2 when param byte +9 bit 4 is set) + 1 through a string loader
    // (the locale object the context's slot 24 / frame slot 20 / slot 38 chain gives picks the group: bit 5 of
    // param byte +9 a fixed group, bit 6 the locale's slot-70 object, else its slot-18 object) and passes it with the
    // object's slot-265 target and four flags (byte +9 bits 0..3 and 7) to the cached service's slot 27 (0x211da3).
    // A non-negative error code the service reports is thrown. Returns the service's answer (0 without a target).
    uint32_t Prim7B(void *frame, const PrimParamView *param);

    // 0x5a7c60 (opcode 0x22, UIEffect). Kind (param byte +4) 8 passes an operand, this's slot 47 and param word +0xc to
    // the cached UI service's slot-3 object (0x20e031) when there is one. The other kinds load two texts (string
    // table 0x96, entries param word +7 + 1 and word +5 + 1, groups chosen by byte +9 bits 2/3 and 0/1 as in Prim7B),
    // parse them as numbers (strtoul, base 0) and act on the UI service's slot-12 object: kinds 3, 4 and 7 when both
    // numbers are 0, otherwise through the two objects the numbers name in the cached director (0x9c5bd6 slot 3,
    // slot 39 twice; error 0x7a when missing) and a per-kind call (0..10). Errors: 0x7d for an empty text or one
    // number being 0. Returns 1.
    uint32_t Prim22(void *frame, const PrimParamView *param);

    // 0x59d740 (opcode 0x32, MakeActionString). With param byte +0xd set it picks an object (an id from this slot 19,
    // param +9, or an operand; errors 0x7a, 0x17) and gives it to the UI manager (0x4529d0) when there is one, or to the
    // interface-0x5a69d3d5 object this finds. Otherwise it loads a text (table 0x12e, entry param word +0x12 or this
    // slot 19(0), with fallbacks; error 0x5e when empty), lets this's slot 262 fill a small argument block and shows
    // the text through the UI manager (0x4526a0) or the interface object's slot 26. Returns 1.
    uint32_t Prim32(void *frame, const PrimParamView *param);

    // 0x585a60 (opcode 0x2a, CreateObject). Finds the object definition to create (param byte +9: bit 2 the definition
    // of the object the frame names, with the registry's side effect; bit 6 this +0x1c0's; bit 7 the id built from this
    // slot 19(1)/(0); else the dword at param +4), works out where it goes from param byte +8 (kind 0..10: relative to
    // this, to the frame's object, to an operand-selected object or a direction...), checks the spot with the cached
    // placement service (0x20e0b2) and the original placement helpers, makes the object (this slot 63 / its slot 16),
    // places it, and binds it (interface 0x99c7f5a / 0x5a69d3d5 queries, the id into frame slot 13). Returns 1 when
    // the object was created, else 0. Errors: 0x90, 0x8, 0x36, 0xa.
    uint32_t Prim2A(void *frame, const PrimParamView *param);
};

// 0x26be10 (__thiscall, ret 4): copies the text of another cRZString-layout object into this one (the std::string at
// +4: nothing when it is the same string, else assign of the other's data and size). Returns this.
struct SString {
    const void *mVTable;  // +0x00
    std::string mString;  // +0x04
    uint32_t mRefCount;   // +0x1c
    SString *AssignText(const SString *other);
};
static_assert(sizeof(SString) == 0x20, "SString layout");
static_assert(offsetof(PrimSObject, mContext) == 0x40, "PrimSObject layout");
static_assert(offsetof(PrimSObject, mOwner) == 0x48, "PrimSObject layout");

// 0x5a9f00 (__cdecl: a 16-bit id and a flag byte, the id returned in ax). Resolves an id against the cached
// singleton (GetCachedSingleton 0x20ddfb): with the flag, the id when the singleton's slot-10 object's slot 16 accepts
// (id, 1); without, the 16-bit value slot 57 of the 0x5a69d3d5 interface of the object slot-26/29 find for the id
// (error 0x17 when there is none, 0 when the interface is missing). 0 otherwise.
int16_t __cdecl ResolveEntityId(int16_t id, uint8_t flag);
