#pragma once
#include <cstdint>

// nGZSceneGraph::cGeometryDataContainer (the GMDC mesh resource). Its secondary subobject nGZSceneGraph::
// cIGeometryDataContainer (vtable 0xe73224, at object + 0x2c) is the `this` of the functions here, as the game
// passes it. Relative to `this` the class has no data members: the offsets below are the ones these functions
// touch. Vectors are (begin, end) dword pairs, elements packed at the stated stride.
//   +0x04/+0x08  vector of 48-byte records (A)
//   +0x10/+0x14  vector of 60-byte records (B)
//   +0x1c/+0x20  vector of 76-byte records (C); each holds a std::string at record +0x1c
//   +0x28/+0x2c  vector of 28-byte elements
//   +0x34/+0x38  vector of 48-byte records (D); each holds a std::string at record +0x00
// Record layouts used here: 60-byte record B: qword at +0x00, dword at +0x08, pointer to a uint16 array at +0x0c,
// three uint16 vectors as (begin, end) pairs at +0x18/+0x1c, +0x24/+0x28 and +0x30/+0x34.
namespace nGZSceneGraph {

class cIGeometryDataContainerSub {
public:
    // 0xa70e10 (`ret`, 24 bytes): signed (end - begin) / 48 of vector A, as a 32-bit value in eax.
    int32_t CountRecordsA();

    // 0xa70e30 (`ret`, 28 bytes): signed (end - begin) / 60 of vector B.
    int32_t CountRecordsB();

    // 0xa70e50 (`ret`, 24 bytes): signed (end - begin) / 48 of vector D.
    int32_t CountRecordsD();

    // 0xa70e70 (`ret`, 28 bytes): signed (end - begin) / 28 of the 28-byte element vector.
    int32_t CountElements28();

    // 0xa70e90 (`ret`, 24 bytes): signed (end - begin) / 76 of vector C.
    int32_t CountRecordsC();

    // 0xa70b30 (`ret 8`, 108 bytes): record i of vector B (`index`), query `which`. Case 0, 1 and 2 are true when
    // the uint16 vector at record offset 0x18, 0x24 or 0x30 is not empty, using ((end - begin) >> 1) != 0 (signed
    // shift). Case 3 is true when the dword at record +0x08 is above zero (unsigned). Any other `which` gives 0.
    // Only al is defined; the caller tests al.
    uint8_t RecordHasWords(uint32_t index, uint32_t which);

    // 0xa70d80 (`ret 4`, 29 bytes): the c_str() of the std::string at record +0x18 of vector D, record `index`.
    const char *RecordStringAt18(uint32_t index);

    // 0xa70da0 (`ret 4`, 24 bytes): the c_str() of the std::string at record +0x00 of vector D, record `index`.
    const char *RecordStringAt00(uint32_t index);

    // 0xa70d50 (`ret 8`, 39 bytes): copies the 12-byte header of vector B's record `index` (qword at +0, dword at
    // +8) to `out`, and returns `out`.
    void *CopyHeaderB(void *out, uint32_t index);

    // 0xa71000 (`ret 8`, 69 bytes): copies vector C's record `index` into `out` (bytes 0x00..0x0f, 0x10..0x17 and the
    // dword at 0x18), then stores in out+0x14 the c_str() of the record's std::string at +0x1c, and returns `out`.
    // The c_str() write comes last, so it overwrites the copied bytes at out+0x14.
    void *CopyRecordC(void *out, uint32_t index);

    // 0xa708d0 (`ret 8`, 36 bytes): copies the first 24 bytes of vector A's record `index` to `out`, returns `out`.
    void *CopyRecordA24(void *out, uint32_t index);

    // 0xa72d20 (`ret 8`, 23 bytes): stores `value` at the dword +0x14 of vector A's record `index`.
    void SetRecordADword(uint32_t index, uint32_t value);

    // 0xa72fb0 (`ret 0xc`, 34 bytes): stores the uint16 `value` at element `element` of the uint16 array at +0x0c
    // of vector B's record `index`.
    void SetRecordBWord(uint32_t index, uint32_t element, uint16_t value);

    // 0xa72dd0 (`ret 8`, 60 bytes): stores `value` at the dword +0x08 of vector B's record `index`, then for each of
    // the three uint16 vectors sets end (+0x1c, +0x28, +0x34) to begin (+0x18, +0x24, +0x30) when they differ.
    void SetRecordBAndTrim(uint32_t index, uint32_t value);

    // 0xa70eb0 (`ret 8`, 49 bytes): copies the 28-byte element `index` of the element vector (+0x28) to `out`, returns `out`.
    void *CopyElement28(void *out, uint32_t index);

    // 0xa700b0 (`ret 8`, 114 bytes): `which` 0, 1 or 2 picks the uint16 vector at record +0x18, +0x24 or +0x30 of vector B's
    // record `index`; its end (+0x1c, +0x28 or +0x34) is set to its begin when they differ. Any other `which` stores nothing
    // (the original leaves eax = which - 2 there, which no caller reads; this returns void).
    void TrimWordVector(uint32_t index, uint32_t which);

    // 0xa72e70 (`ret 0x10`, 41 bytes): calls this subobject's own slot 21 (vf44_21, through [this] so the game's vtable
    // applies) with (index, a, b), then stores `value` at the dword +0x10 of vector C's record `index`.
    void SetRecordCDword(uint32_t index, uint32_t a, uint32_t b, uint32_t value);
};

// The subobject thunks of nGZSceneGraph::cGeometryDataContainer's second vtable (0xe73314, object + 0x80). Each thunk is
// `sub ecx, [ecx-4]; sub ecx, 0x50; jmp body`: the vtordisp at this-4 gives the cSGResourceSubobject (object + 0x30 of
// the cSGResource base) that the body takes as `this`. The replacements below compute that pointer and call the
// reimplemented body. They are __fastcall with a dummy edx, so their stack arguments are the thunk's (the body's `ret N`).
int32_t __fastcall VbThunkCompareIdentity(void *self, void *edx, void *other);     // vf128_11, body 0xa3c3d0, ret 4
void *__fastcall VbThunkGetVirtualBase(void *self, void *edx, void *arg);          // vf128_9, body 0xa3c720, ret 4
void *__fastcall VbThunkCopyKeyTo(void *self, void *edx, void *out);               // vf128_22, body 0xa3c770, ret 4
void __fastcall VbThunkSetFlag(void *self, void *edx, uint8_t value);              // vf128_16, body 0xa3c870, ret 4
uint8_t __fastcall VbThunkSetResource(void *self, void *edx, void *resource);      // vf128_15, body 0xa3cdd0, ret 4

} // namespace nGZSceneGraph
