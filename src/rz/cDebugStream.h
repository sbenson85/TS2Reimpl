#pragma once
#include "game/owned.h"
#include "rz/RZFileStreams.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>

// cDebugStream (RTTI `.?AVcDebugStream@?A0x4e576443@@`, an anonymous-namespace class of the sim code; lane FB): a
// cRZOFStream (0x18 bytes, vtable 0xda2c14) that also remembers what it was last asked to write. Its vtable 0xdcf704
// has cRZOFStream's slots except the numeric and text setters (4-9, 12, 14, 15) and the deleting destructor (21).
// Each of those first appends the text of the value to the string at +0x18 (slot 19 of the string, Append), then
// writes it through cRZOFStream's own setter, and returns that setter's result. A text that equals the newline
// ("\n", the cell 0x1014980) instead empties the std::string held by the +0x18 string (it is the end of a line: the
// text collected so far is dropped). The string at +0x38 is only constructed and destroyed here.
//
// The class is created inline in cTSPerson::vf752_24 (0x5144a0): cRZOFStream::Construct(path, 0, 0, 0, 0), the
// vtable store and two empty strings. Construct is that sequence. RVAs are 1.18.0.187.
class cDebugStream : public cRZOFStream {
public:
    static constexpr uint32_t kVTable = 0xdcf704;
    static constexpr uint32_t kSize = 0x58;

    // The inline sequence at 0x5144a0 .. 0x5145d2 (binary 0, order 0, auto-flush 0, append 0), then the vtable.
    cDebugStream *Construct(const cIGZString &path);
    // 0x511760: both strings destroyed (+0x38 then +0x18), then cRZOFStream::Destruct.
    void Destruct();
    // Slot 21, 0x512070 (ret 4): Destruct, then (flags & 1) sized delete of 0x58. Returns this.
    cDebugStream *DeletingDestructor(uint32_t flags);

    // Each returns the base setter's al.
    uint8_t SetSint8(int8_t value);     // slot 4, 0x520d70: "%d" of the sign-extended value
    uint8_t SetUint8(uint8_t value);    // slot 5, 0x5211f0: "%u"
    uint8_t SetSint16(int16_t value);   // slot 6, 0x520bd0
    uint8_t SetUint16(uint16_t value);  // slot 7, 0x521050
    uint8_t SetSint32(int32_t value);   // slot 8, 0x520ca0
    uint8_t SetUint32(uint32_t value);  // slot 9, 0x521120
    uint8_t SetFloat32(float value);    // slot 12, 0x520720: "%f" of the value as a double
    uint8_t SetRZCharStr(const char *text); // slot 14, 0x520ab0
    uint8_t SetGZStr(cIGZString *text);     // slot 15, 0x520800

    cRZString mLine;   // +0x18: the last value's text
    cRZString mUnused; // +0x38: constructed and destroyed, never written here

};
static_assert(sizeof(cDebugStream) == cDebugStream::kSize, "cDebugStream is 0x58 bytes");
static_assert(offsetof(cDebugStream, mLine) == 0x18, "cDebugStream: line text at +0x18");
static_assert(offsetof(cDebugStream, mUnused) == 0x38, "cDebugStream: second string at +0x38");

TS2_OWNED_STATIC(const OwnedVTable, dcf704);
TS2_OWNED_STATIC(const char *, 1014980); // "\n" (the cell holds d9e644; read by the sim code too)
extern "C" const void *ts2_own_vt_dcf704[24];
