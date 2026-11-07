#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace ifaces {
class cIGZIStream;
class cIGZOStream;
} // namespace ifaces

namespace nTSWorld {
// nTSWorld::cWorldTerrain::tVertPaintInfo: a terrain vertex's paint, three layer bytes (0xff: unused) and a flag. The
// default (Resize's fill, 0x801020 / 0x800c20) is {ff, ff, ff, 1}. Lane VP.
struct tVertPaintInfo {
    uint8_t mPaint[3] = {0xff, 0xff, 0xff};
    uint8_t mFlag = 1;
};
static_assert(sizeof(tVertPaintInfo) == 4);

// nTSWorld::c2DArray<T> (cIGZSerializable only; 0x1c bytes; refcounted, built with new 0x1c and assigned with
// AddRef/Release). Record: u32 clsid 0x6b943b43, u32 version 1, GZStr "c2DArray", s32 width, s32 height,
// then the elements in storage order (f32 or u8). The count written and read is the vector's size, not
// width * height. Shared with nTSWorld::cWorldTerrain (lot terrain) and embedded by value in the terrain-type
// table nodes (+0x6c). Lane DF: persistence and Resize; lane DD found the layout.
template <class T> struct c2DArray {
    static constexpr uint32_t kClassId = 0x6b943b43;
    static constexpr uint32_t kVersion = 1;
    static constexpr uint32_t kFloatVTable = 0xdcc260;
    static constexpr uint32_t kByteVTable = 0xdcc124;
    static constexpr uint32_t kPaintVTable = 0xe0464c;
    static constexpr uint32_t kVTable = std::is_same_v<T, float> ? kFloatVTable
        : std::is_same_v<T, tVertPaintInfo>                     ? kPaintVTable
                                                                : kByteVTable;

    const void *mVTable;    // +0x00 float: 0xdcc260, u8: 0xdcc124, tVertPaintInfo: 0xe0464c
    uint32_t mRefCount;     // +0x04
    uint32_t mWidth;        // +0x08 (vertices: terrain width + 1)
    uint32_t mHeight;       // +0x0c
    std::vector<T> mData;   // +0x10 row-major, mData[mWidth * y + x]

    // Slots: 0 QI 0x178b20 (shared), 1 AddRef 0x148fb0 (verified), 2 Release 0x4b1d00 (shared by all three
    // instantiations: delete at 0), 5 GetGZCLSID 0x4af570 (shared, 0x6b943b43).
    bool Write(ifaces::cIGZOStream *stream);  // slot 3: float 0x4b2e90, u8 0x4b2d80, paint 0x804d40 (ret 4, EH: local cRZString name)
    bool Read(ifaces::cIGZIStream *stream);   // slot 4: float 0x4b0870, u8 0x4b0770, paint 0x803340 (ret 4, EH)
    uint32_t GetGZCLSID() const;              // slot 5: 0x4af570, the one body of all three copies (lane VP)
    c2DArray *DeletingDestructor(uint32_t flags); // slot 6: float 0x4ad220, u8 0x4ad200, paint 0x801730 (ret 4)
    void Destruct();                          // the destructor body: paint 0x801440 (u8 0x4acfe0, not hooked yet)
    void Resize(uint32_t width, uint32_t height); // float 0x4b1db0, u8 0x4b1d20, paint 0x8040b0 (ret 8)
};
static_assert(sizeof(c2DArray<float>) == 0x1c, "new 0x1c at 0x4b0b37");
static_assert(sizeof(c2DArray<uint8_t>) == 0x1c);
static_assert(offsetof(c2DArray<float>, mData) == 0x10);
static_assert(sizeof(c2DArray<tVertPaintInfo>) == 0x1c);

// The paint array's record (Write 0x804d40 / Read 0x803340): u32 clsid, u32 version 1, GZStr "c2DArray", s32 width,
// s32 height, then per element a u8 count n (the leading paint bytes before the first 0xff, at most 3) and n u8 bytes.
// Read takes each byte as a u16 when the terrain record being read (0x10df504, set by cWorldTerrain::Read 0x8034d0) is
// older than version 4.
template <> bool c2DArray<tVertPaintInfo>::Write(ifaces::cIGZOStream *stream);
template <> bool c2DArray<tVertPaintInfo>::Read(ifaces::cIGZIStream *stream);
} // namespace nTSWorld
