#pragma once
#include <cstdint>
#include <vector>

// cTSObject: the game's object class (everything placeable in a lot derives from it). RVAs are
// 1.18.0.187. The functions below are slots of the primary vtable (0xdd2f0c, 359 slots), named by
// slot number; `this` is the primary object (its vtable pointer is at +0).
//
// Fields read or written by the functions reimplemented here (offsets from the primary object):
//   +0x040  pointer to a subobject. Its slot 18 (vf221) returns another object (or null); that object's
//           slot 20 is called with a word.
//   +0x070  byte, cleared by vf245.
//   +0x1c4  pointer to a subobject. Its slot 74 is called by vf245.
//   +0x1cc  signed word, passed to vf245's callee.
//   +0x1d8  dword, written by vf37 with the argument sign-extended from 16 bits.
//   +0x086  byte, set by vf198.
//   +0x096  the eight temps (words); +0x0a6 the object data words (114).
//   +0x190  dword, the argument of vf198.
//   +0x220  float, written by vf314.
//   +0x238  pointer to the object's definition; slot 3 reads a field (0x14 the reference table id, 0x2c compared
//           by ResyncReferenceSource).
//   +0x240  pointer to an object; its slot 4 gives the object vf198 asks for the stack reserve (slot 3), slot 17 a
//           reference table by id and slot 32 another object of the same kind.
//   +0x248, +0x254, +0x260  three vectors of reference-counted pointers (ReloadReferences).
//   +0x230  dword flags. vf31 replaces its low five bits; vf245 sets bit 8.
//   +0x291  byte, set to 1 by vf221.
//   +0x293  byte, cleared by vf221.
//
// The predicates below call a virtual slot and then set only al (`test ax,ax` or `test al,mask` followed
// by `setne al`), so the rest of eax keeps the callee's result. They return that full eax value.

class cIGZString;

class cTSObject {
public:
    // Slot 31, 0x53d780 (ret 4). Sets the low five bits of the flags at +0x230 to the low five bits of the
    // argument, keeping the other bits. The original leaves eax = old flags ^ argument on both paths.
    uint32_t vf31(uint32_t a);

    // Slot 37, 0x5456b0 (ret 4). Stores the argument's low word, sign-extended, at +0x1d8. Returns that value.
    uint32_t vf37(uint32_t a);

    // Slot 119, 0x541690 (ret). Calls its own slot 152 with no arguments; al = (al & 0x14) != 0.
    uint32_t vf119();

    // Slot 124, 0x533bd0 (ret). Calls its own slot 13 with argument 0x6a; al = (ax != 0).
    uint32_t vf124();

    // Slot 125, 0x531250 (ret). Calls its own slot 91 (returns an object), then that object's slot 8 with
    // argument 0x1b; al = (ax != 0) of the slot 8 result.
    uint32_t vf125();

    // Slot 127, 0x53dc00 (ret). Calls its own slot 153 with no arguments; al = (al & 0x5f) != 0.
    uint32_t vf127();

    // Slot 177, 0x546810 (ret 4). Calls its own slot 170 with (argument, 1). Then calls slot 104; if its al
    // is zero, calls slot 208. The original leaves eax holding the last callee's result. Void here: callers
    // of this slot do not read it.
    void vf177(uint32_t a);

    // Slot 178, 0x546890 (ret 8). Calls its own slot 171 with (argument, float, 1), then as vf177 with slots
    // 104 and 208. eax is left as in vf177 (not compared).
    void vf178(uint32_t a, float f);

    // Slot 221, 0x543050 (ret). Clears the byte at +0x293 and sets the byte at +0x291 to 1. Calls slot 18 of the
    // subobject at +0x40; if it returns an object, calls its own slot 47, then calls slot 20 of that object with
    // the returned word (zero-extended). eax is left as the last callee's result (not compared).
    void vf221();

    // Slot 245, 0x52f060 (ret 4). Clears the byte at +0x70, sets bit 8 of the flags at +0x230, then calls
    // slot 74 of the subobject at +0x1c4 with (sign-extended word at +0x1cc, 4, 1). Returns the callee's eax.
    uint32_t vf245(uint32_t a);

    // Slot 314, 0x545b80 (ret 4). Stores the float argument at +0x220. eax is left unchanged (not compared).
    void vf314(float f);

    // Slot 198, 0x5342d0 (ret 4; meaning tentative: (re)initialisation; cTSPerson's 0x51da50 extends it). Runs
    // 0x531320 (not replaced yet) with 0, stores the argument at +0x190, sets object data words 3 (-1), 0x43 (slot 66's
    // word), 0x37 (slot 67's), 0x17 (2) and 7 (slot 352's), prepares the tree sim's stack (+4) with the reserve the
    // +0x240 object's slot 4 object gives for 2 (its slot 3) and the temps (+0x96) as default arguments, calls slot 197
    // with two (0, -1) word pairs, 1 and 0, then the tree sim's slot 41, and sets the byte at +0x86.
    void vf198(uint32_t argument);

    // Slot 244, 0x52df60 (ret 0x10; meaning tentative: send an event). With `runOwnTree` the tree owner (slot 89)
    // names the event's tree (its slot 31: event, 0, 0; a word, 0 for none), which the tree sim (+4) calls (slot 15:
    // owner, 0, tree, the object id word at +0x1cc, 4, argument, 0, 0). With `propagate`, each contained object
    // (slots 66 count and 68 by index) that has an id (its slot 4) is looked up in the context's object list (+0x40
    // slot 26, then slot 29 by id) and gets the event with both flags set.
    void vf244(uint32_t event, bool runOwnTree, bool propagate, uint32_t argument);

    // Slot 249, 0x530e80 (ret 8; meaning tentative: resume a frame). Without a helper from the +0x48 object (slot 6) the
    // tree sim's ResumeFrame does it. Otherwise, when flag bit 7 of +0x230 is set and bit 6 clear, the helper's slot 3
    // (this, frame, node) does; else a paused frame (its slot 22) is unpaused (slot 23) and the tree sim's slot 13 runs
    // (frame, 0), or a node with its flag (slot 18) set runs it with the flag cleared for the call (slot 19). Returns
    // the result of whichever ran, or 0.
    uint32_t vf249(void *frame, void *node);

    // 0x531320 (629 bytes, ret 4; names tentative). Rebuilds the three reference vectors (+0x248, +0x254, +0x260):
    // ResyncReferenceSource first; with `keepStates` the non-null entries of the first vector report a word (slot 4)
    // that is handed back after the rebuild (slot 8, by position); the vectors are cleared and, when the +0x238
    // definition has a table id (its slot 3 for 0x14), the +0x240 object's slot 17 gives the table (interface
    // 0xfba4da51), whose slot 8 refills them. Then object data words 0x43 and 0x37 are refreshed from slots 66 and 67
    // and slot 329 runs.
    void ReloadReferences(bool keepStates);

    // 0x53ea00 (487 bytes; name tentative). Only for an object whose slot 98 says 2, with all three reference vectors
    // empty, a table id and a +0x240 object: when the definition behind the +0x240 object's slot 32 object (its slot 4)
    // differs from ours in field 0x2c, and both sides have the table and accept the id (slot 9), our table takes the
    // other's list (slot 10) unless the two lists have the same length.
    void ResyncReferenceSource();

    // Slot 346, 0x5aacd0 (ret). Calls its own slot 93 with argument 0xf; al = (ax != 0).
    uint32_t vf346();

    // Slot 121, 0x533c20 (ret). al = (signed word at +0x122 > 0). Only al is set; the rest of eax is the
    // caller's, so the result's upper bits are undefined here.
    uint32_t vf121();

    // Slot 183, 0x5406f0 (ret 0x14). Calls its own slot 13 with 9, then slot 182 with (the result's low word,
    // sign-extended, then the five arguments). Returns slot 182's eax.
    uint32_t vf183(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);

    // Slot 211, 0x5343d0 (ret). Calls its own slot 238 with (0x22, 0, 0, 0, 0). Returns slot 238's eax.
    uint32_t vf211();

    // Slot 212, 0x534160 (ret). Calls its own slot 238 with (0x21, 0, 0, 0, 0). Returns slot 238's eax.
    uint32_t vf212();

    // Slot 213, 0x53d6e0 (ret 4). Stores the low byte of the argument at +0x2e8. Void.
    void vf213(uint32_t a);

    // Slot 214, 0x52df50 (ret). Returns the byte at +0x2e8 in al. Only al is defined.
    uint32_t vf214();

    // Slot 219, 0x542fa0 (ret). al = (signed word at +0x84 > 0). Only al is defined.
    uint32_t vf219();

    // Slot 222, 0x5456c0 (ret 4). Stores the low byte of the argument at +0x293. Void.
    void vf222(uint32_t a);

    // Slot 224, 0x5455d0 (ret 4). Calls its own slot 14 with (9, argument). Returns slot 14's eax.
    uint32_t vf224(uint32_t a);

    // Slot 315, 0x545f60 (ret 0xc). Calls its own slot 324 with (a1, a2, a3 as a float). Returns a1: the
    // original reads its first argument back from the stack after the call, and the callee leaves it there.
    uint32_t vf315(uint32_t a1, uint32_t a2, float a3);

    // Slot 316, 0x545cd0 (ret 4). Tail-jumps to its own slot 325 with the argument: returns slot 325's eax.
    uint32_t vf316(uint32_t a);

    // Slot 328, 0x5ac9e0 (ret). Calls its own slot 153; al = (al & 0x50) != 0 of the result.
    uint32_t vf328();

    // ---- src/ts/cTSObjectSlots.cpp ----

    // Slot 276, 0x53d980 (ret 4). Stores the argument at +0x194. Void.
    void vf276(uint32_t a);

    // Slot 205, 0x53d1a0 (ret; meaning tentative: restart the object's main tree). Clears object data word 0x3e (+0x122),
    // sets word 0x1d (+0xe0) to -1, clears bits 5 and 8 of word 8 (+0xb6), calls slot 197 with two (0, -1) word pairs, 1 and 0,
    // then starts a tree on the tree sim (+4, 0x577e10): the tree owner (slot 89) as owner, the low word of slot 93's answer
    // for 1 as the tree id, the word at +0x1cc (read before the calls) as the value.
    void vf205();

    // Slot 223, 0x546620 (ret; meaning tentative: refresh once, guarded by the flag at +0x292). Does nothing when slot 219 says
    // al != 0 or the byte at +0x292 is set. Otherwise sets +0x292 and +0x2a6, runs slots 331 and 330 when slot 71 says 0,
    // clears +0x291, and tells the +0x40 object's slot 18 object (slot 21 with this object's id, slot 47), then clears +0x292.
    void vf223();

    // Slot 182, 0x540560 (ret 0x18; meaning tentative: offer an event to the object's helpers). Asks the cIEdithObject
    // subobject (+0x74, slot 10) for a list of objects and queries each for interface 0xaae90adb until one answers; that
    // interface's slot 6 gets the six arguments and its al is the result. 0 when none answers. Only al is defined.
    uint8_t vf182(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6);

    // Slot 259, 0x52ec10 (ret; meaning tentative: one step of a need that decays). Runs slot 258, then picks an object `limits`
    // (held with a reference): with slot 104 true (al) the one the interface 0x5a6216f6 of this (slot 0) leads to through the
    // +0x40 object (slot 24, then slot 22 with the dword at +0x240, then slot 4), or nothing and an early end when any step
    // fails; otherwise the definition at +0x238. The service object (0x20ddfb) gives a second object (slot 23). When `limits`
    // field 0x25 (slot 3) is zero and that object's slot 21 says al == 0, word 0x29 (+0xf8) loses `limits`' step (slot 5 for 0x23,
    // or 0x24 when field 0x23 is zero), and is set to slot 5 of 0x26 when the result is below field 0x26. Then word 0x53 (+0x14c)
    // is decremented (except on the early end).
    void vf259();

    // Slot 284, 0x53a290 (ret 0x18; meaning tentative: collect the tree ids of the object and of what it contains). Returns
    // 0 at once for an object of kind 2 (slot 98). Unless `skipOwnTree` (a byte): with a name, the tree owner (slot 89) turns it into
    // a tree id (slot 31 with a game string made from the name, 0, 0) and the tree sim (+4, slot 15) calls that tree
    // (owner, id, tree, id, 3, d, 0, 0), returning 0 when it fails; then the object id (+0x1cc) is appended to the list. Unless
    // `skipChildren` (a byte), each contained object (slots 66 and 68) that has an id is looked up in the +0x40 context's object
    // list (slot 26, then slot 29) and its slot 284 is called with (list, 0, name, d, e, 0); a failure ends with 0 when the byte
    // `e` is set. Otherwise 1. Only al is defined.
    uint8_t vf284(std::vector<uint16_t> *ids, uint32_t skipOwnTree, const char *name, uint32_t d, uint32_t e, uint32_t skipChildren);

    // Slot 358, 0x5ae3a0 (ret; meaning tentative: the other object of the same kind in the group). For an object of kind 0xc (slot
    // 98) with a non-negative slot 57 index: the +0x40 context's manager (slot 26) gives the group for that index (slot 83), and the
    // last member whose id (slot 7) differs from ours (slot 47) and whose kind (slot 98) is 0xb or 0xc is returned. The reference
    // taken on it is dropped before the return, so the pointer is only borrowed. Null when there is none.
    void *vf358();

    // Slot 201, 0x53ccb0 (ret 0x10; meaning tentative: reset the object data and restart the main tree). With a byte `b`, first
    // asks the tree sim's slot 43 and this slot 337 with (b's upper bytes | (word a != 0)) and stops when either says al != 0.
    // Then: slot 187 gives the pairs, a tick, an object, a word and a flag; the 114 object data words (+0xa6) are cleared except
    // 17 kept from before; word 0xb = id (+0x1cc), 0x4b/0x39 = 0xff, words 0x47..0x4a = 0, 0xff, 0xff, 0xff, word 0x3a = the
    // definition's (+0x238) slot-5 field 0x31; with byte `c` the two owner tables (+0x88/+0x8c, +0x90/+0x94: lists of words)
    // are cleared; the temps (+0x96) and 16 bytes at +0x1b0 are cleared. Slot 9 (ids 0xfa1c39f7, 0xfa1c39ed) may give a service
    // whose slot 24 (0xbb263d5) gives a stream: both are released through their virtual base, the stream gets slot 8(0), then
    // slot 282 runs. The tree (slot 93 for 1) of the tree owner (slot 89) is started (tree sim StartTree, value = id), slot 197 gets
    // the pairs and the tick, and words 2, 3, 0x17, 4, 5, 0xd, 0x2a, 0x4e, 7 are set from slot 47 of the object, slot 187's word,
    // 2, the definition's fields 6, 3, 5, 4 (when field 0xc is non-zero), 0xd, and slot 352. Slot 258 runs, the +0x40 object's
    // slot 26 manager gets slot 71 (id, -1), and unless slot 104 says al != 0: slot 238 (0, 0, {d,...}, 0, 0), the definition's
    // fields 0x6a/0x6b may set words 0x2a/0x52, the subobject at +0x74 gets slot 12, slot 236 (0, 1, 1) runs when `a` is non-zero
    // and slot 233. The subobject's slot 13 ends it. Void.
    void vf201(uint32_t a, uint32_t b, uint32_t c, uint32_t d);

    // Slot 202, 0x53aa80 (ret 8; meaning tentative: advance the object to a phase of its life). Runs slot 215, tells the tree sim
    // (slot 46) and this (slot 340) the phase, and calls slot 197 with (slot 33's pairs, the dword at +0x1d8, 1). Phase below 0x20:
    // the word at +0xfa toggles between its low flags (bit 0 set -> clear bit 1; else bit 1 set -> clear bit 0; else set both),
    // bit 7 of its low byte clears bits 0 and 7 again, and word +0xfc gets bits 1 and 3; then, as for phase 0x20, the limit step:
    // when slot 104 is false, or true and this->QueryInterface(0x5a6216f6)'s slot 6 object's slot 11 object is this object, word
    // 0xf8 is raised to the limit (slot 5 of 0x12) when it is below the other limit (slot 5 of 0x26) of the object info's (slot 61)
    // slot 27 object's slot 4 object. Phases up to 0x22 also run the service step: the director framework object (0x1ae18d) slot 3
    // (0xfba4da53, 0xfba4da4d) service's slot 11(1) sets word +0xae. The number word at +0x2a4 is looked up in the object registry
    // (+0x1c4); the object's slot 280 is kept only when it passes (slot 25 of 0x10000 and 0x20000 false, slot 61 non-zero whose
    // slot 28 says 0xec987278), else the word is cleared. Slot 238 (2, 0, eventArgs or two zero dwords, 0, 0), then slots 204,
    // 201 (0, 1, bit 2 of the dword +0x190, 0), 238 (0x2b), ReloadReferences(true) and (slot 188) slot 193(1) run when
    // the +0x240 object's slot 44 and the +0x48 object's slot 16 both say al != 0 or bit 2 of +0x190 is set. With bit 2 of +0x190
    // set, the text (table 0x85, entry slot 34 of the object info + 1, group slot 11 of the tree owner) is offered to the service of
    // slot 9 (slot 78, ToChar and 1) and slot 221 runs when its slot 120 object's slot 71 says 0. Finally slots 290 (slot 293(i))
    // for every i below slot 291, 233, 216 and 296(0). Void.
    void vf202(int32_t phase, const void *eventArgs);

    // Slot 157, 0x544f60 (ret 4; meaning tentative: is this segment one of the object's). Runs slot 153, then compares the six-dword
    // record (x1 y1 level1 x2 y2 level2; the floats compared as sortable integers within -32..31) of each of slot 159's count of
    // records (slot 163 fills record i) with `segment`, in the same or the reversed direction. Returns al = 1 on the first match, else 0.
    uint8_t vf157(const uint32_t *segment);

    // Slot 158, 0x544060 (ret 4): the same with slots 160 (count) and 165 (record).
    uint8_t vf158(const uint32_t *segment);

    // Slot 324, 0x541140 (ret 0xc; meaning tentative: ask the first helper for a value). The cIEdithObject subobject (+0x74, slot 10)
    // lists objects; the first that answers interface 0xaae90adb gets slot 4 with (out, a2, a3); the 12 bytes of out are then up to
    // the helper. When none answers out gets the 12 bytes at game globals 0x10daf34 (8) and 0x10daf3c (4). Returns out.
    void *vf324(void *out, uint32_t a2, float a3);

    // ---- src/ts/cTSObjectSlots2.cpp ----

    // Slot 325, 0x540fc0 (ret 4; meaning tentative: ask the first helper for a value). As vf324: the first helper listed by the
    // cIEdithObject subobject (+0x74, slot 10) that answers interface 0xaae90adb gets slot 5 with the argument and its eax is the
    // result; with none, the notch offset of facing 0 (FacingToNotchOffset, 2).
    uint32_t vf325(uint32_t a);

    // Slot 195, 0x543ae0 (ret 4; meaning tentative: fill a target with the entries attributes 4 and 0x2a select). See the body.
    // Returns al (null target: 0; nothing selected: 1). Only al is defined.
    uint8_t vf195(void **target);

    // Slot 261, 0x5290e0 (ret 4; meaning tentative: describe the tree sim's current error into a string). Void; see the body.
    void vf261(cIGZString *out);

    // Slot 330, 0x53fbb0 (ret; meaning tentative: put the object's footprint rectangles into the placer). Void; see the body.
    void vf330();

    // Slot 65, 0x5426a0 (ret 8; meaning tentative: the object's footprint rectangles at a position and facing). `position` is two
    // floats (x, y), `facingNotch` points at the notch (only its low three bits count). The object's base rectangle list (+0x1e4,
    // begin and end pointers to 16-byte x1 y1 x2 y2 rectangles) is built on first use by 0x7eaf50 (flag byte +0x293). The cache
    // vector at +0x200 (begin, end, capacity, then the min x, min y, max x, max y of what it holds) is cleared and refilled with the
    // list's rectangles turned by the notch (pairs of notches share a rotation: 0/1 none, 2/3, 4/5, 6/7 quarter turns), moved by the
    // position and snapped inwards to the 1/8 grid with a 0.01 margin (a rectangle narrower than 1/16 after snapping is widened to
    // 0.105 around its left/bottom edge). Returns the address of the cache vector.
    void *vf65(const float *position, const uint32_t *facingNotch);
};

// 0x545f90 (cdecl, ret; tentative: the event check callback vf330 gives the placer for objects in a group). Answers al = 1 (and
// sets *outFlag = 0, *outWeight = 1 + attribute 10 / 100) when the interface 0x9b25d761 of `owner` accepts event 5 for the id.
uint8_t __cdecl PlacementEventCheck(int32_t *outFlag, float *outWeight, void *owner, const uint16_t *objectId);
