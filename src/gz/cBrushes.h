#pragma once
// nGZGraphic4::cColorBrush, cOutlineColorBrush and cPatternBrush (lane WK, rendering row RB; docs/subsystems/rendering.md):
// the materials cIDraw2D draws shapes with (SetMaterial; cIDraw2DMaterial in gz/cDraw2D.h: slot 3 the state block, slot 4
// the shape). Each takes the points of a shape (x, y floats), asks the 2D renderer for room in the current batch
// (LockBatch: the positions, colours and texture coordinates of 20-byte vertices and the 16-bit indices) and writes
// vertices and a triangle fan {0, i + 1, i + 2}, then DrawBatch.
//   - cColorBrush: a table of colours; the vertex colour is the table interpolated at the point's value (a wrapped
//     index and the next one, by the fraction); an empty table gives the default colour (game global 0xfeb9f4).
//   - cOutlineColorBrush: a fill colour and an outline colour; a filled shape (kind 0) is drawn twice, the fan in the
//     fill colour and then a quad along each edge in the outline colour; a line (kind 1) is the fan in the outline
//     colour.
//   - cPatternBrush: a texture (made from a bitmap kept in the brush by GetStateBlock) the shape is mapped onto: the
//     texture coordinates are the point through a 2 x 2 matrix (the angle and the texture's size) plus an offset.
//
// Standalone shape (docs/standalone-shape.md): all three are made only by their COM creators (0x1538b0 / 0x1538f0 /
// 0x153930, the director's classes 0xdc012582 / 0x9c1d3c77 / 0x5c0ac4af), which are hooked: every object is ours, so
// every slot, constructor and destructor is in effect through them.
//
// Layouts:
//   cColorBrush (0x14): +0 vtable, +4 reference count, +8 the colour table (a vector of 32-bit colours: begin, end,
//     capacity).
//   cOutlineColorBrush (0x10): +0 vtable, +4 reference count, +8 the outline colour, +0xc the fill colour.
//   cPatternBrush (0x50): +0 vtable, +4 reference count, +8 the bitmap (a std::vector of bytes, std::allocator: growth is
//     the library's 0x152c50, operator new 0x824632 and sized delete 0x824624, not the game's heap), +0x14 its pixel
//     format (not counted), +0x18 / +0x1c its width and height, +0x20 its pitch, +0x24 the angle, +0x28 / +0x2c the
//     offset, +0x30 the texture (cITexture, counted), +0x34 the state block (counted), +0x38 the matrix (a, b, c, d) and
//     +0x48 / +0x4c the offset in texture units.
#include "gz/cDraw2D.h"
#include "gz/cTextureDX.h"

#include <cstdint>
#include <vector>

namespace nGZGraphic4 {

class cIPixelFormat;

constexpr uint32_t kIidColorBrush = 0x9c012573;
constexpr uint32_t kIidBrush = 0xdc004962; // cIBrush: all three brushes answer it
constexpr uint32_t kIidOutlineColorBrush = 0xfc1d3c6b;
constexpr uint32_t kIidPatternBrush = 0x3c0ac4a4;
constexpr uint32_t kClassColorBrush = 0xdc012582; // the COM director's classes (creators 0x1538b0, 0x1538f0, 0x153930)
constexpr uint32_t kClassOutlineColorBrush = 0x9c1d3c77;
constexpr uint32_t kClassPatternBrush = 0x5c0ac4af;
constexpr uint32_t kDefaultColourCell = 0xfeb9f4; // the game global holding the default colour

// nGZGraphic4::cIColorBrush (iid 0x9c012573): a brush by a table of colours.
class cIColorBrush : public cIDraw2DMaterial {
public:
    virtual void Resize(uint32_t count) = 0;                           // 5: the table's size (new entries the default colour)
    virtual void SetColor(uint32_t index, const uint32_t *colour) = 0; // 6 eax-ignored: eax is a scratch value left by the store, no result
    virtual uint32_t Size() = 0;                                       // 7
    virtual void GetColor(uint32_t *out, uint32_t index) = 0;          // 8 eax-ignored: eax is a scratch value left by the store, no result
    virtual ~cIColorBrush() = default;                                 // 9: the deleting destructor
};

// nGZGraphic4::cIOutlineColorBrush (iid 0xfc1d3c6b).
class cIOutlineColorBrush : public cIDraw2DMaterial {
public:
    virtual void SetOutlineColor(const uint32_t *colour) = 0; // 5
    virtual void SetFillColor(const uint32_t *colour) = 0;    // 6
    virtual void GetOutlineColor(uint32_t *out) = 0;          // 7 eax-ignored: eax is a scratch value left by the store, no result
    virtual void GetFillColor(uint32_t *out) = 0;             // 8 eax-ignored: eax is a scratch value left by the store, no result
    virtual ~cIOutlineColorBrush() = default;                 // 9
};

class cColorBrush final : public cIColorBrush {
public:
    // 0x1538b0 (cdecl): a new brush with an empty table (the COM director's creator for class 0xdc012582).
    static cColorBrush *__cdecl Create();

    bool QueryInterface(uint32_t riid, void **out) override;               // 0x153ec0: ids 1, 0x9c012573, 0xdc004962
    uint32_t AddRef() override;                                            // 0x148fb0 (shared)
    uint32_t Release() override;                                           // 0x153f50 (shared): at zero the deleting destructor
    uint32_t GetStateBlock(cIDraw2D *draw2d, cIDeviceDX *device) override; // 0x6c6a0 (shared): 0
    void Draw(cIDraw2D *draw2d, uint32_t kind, uint32_t count, const float *points, const float *values) override; // 0x153090
    void Resize(uint32_t count) override;                                  // 0x153fe0
    void SetColor(uint32_t index, const uint32_t *colour) override;        // 0x153fb0 eax-ignored: eax is a scratch value left by the store, no result
    uint32_t Size() override;                                              // 0x153ad0 (shared)
    void GetColor(uint32_t *out, uint32_t index) override;                 // 0x153070 eax-ignored: eax is a scratch value left by the store, no result
    ~cColorBrush() override = default;                                     // 0x152f00 (deleting): the table freed

private:
    uint32_t mRefCount = 0;
    std::vector<uint32_t> mColours;
};
static_assert(sizeof(cColorBrush) == 0x14, "cColorBrush layout");

class cOutlineColorBrush final : public cIOutlineColorBrush {
public:
    // 0x1538f0 (cdecl): a new brush, both colours the default colour (class 0x9c1d3c77).
    static cOutlineColorBrush *__cdecl Create();

    bool QueryInterface(uint32_t riid, void **out) override;               // 0x153ef0: ids 1, 0xdc004962, 0xfc1d3c6b
    uint32_t AddRef() override;                                            // 0x148fb0 (shared)
    uint32_t Release() override;                                           // 0x153f50 (shared)
    uint32_t GetStateBlock(cIDraw2D *draw2d, cIDeviceDX *device) override; // 0x6c6a0 (shared): 0
    void Draw(cIDraw2D *draw2d, uint32_t kind, uint32_t count, const float *points, const float *values) override; // 0x1533b0
    void SetOutlineColor(const uint32_t *colour) override;                 // 0x153fa0
    void SetFillColor(const uint32_t *colour) override;                    // 0x153fd0
    void GetOutlineColor(uint32_t *out) override;                          // 0x153060 eax-ignored: eax is a scratch value left by the store, no result
    void GetFillColor(uint32_t *out) override;                             // 0x1538a0 eax-ignored: eax is a scratch value left by the store, no result
    ~cOutlineColorBrush() override = default;                              // 0x152f70 (deleting)

private:
    uint32_t mRefCount = 0;
    uint32_t mOutline = 0;
    uint32_t mFill = 0;
};
static_assert(sizeof(cOutlineColorBrush) == 0x10, "cOutlineColorBrush layout");

// nGZGraphic4::cIPatternBrush (iid 0x3c0ac4a4): a brush by a bitmap.
class cIPatternBrush : public cIDraw2DMaterial {
public:
    // 5, the bitmap copied in (height * pitch bytes), the texture made from it later by GetStateBlock.
    virtual void SetImage(const void *bits, cIPixelFormat *format, uint32_t width, uint32_t height, uint32_t pitch) = 0; // 0x153a40 eax-ignored: memcpy's result left over; nothing in the image asks for this interface
    virtual void ResetCache() = 0;                    // 6
    virtual void SetAngle(float angle) = 0;           // 7
    virtual void SetOffset(float x, float y) = 0;     // 8
    virtual float GetAngle() = 0;                     // 9
    virtual float *GetOffset(float *out) = 0;         // 10: x, y into `out`, which is returned
    virtual ~cIPatternBrush() = default;              // 11: the deleting destructor
};

class cPatternBrush final : public cIPatternBrush {
public:
    // 0x153930 (cdecl): a new brush with no image, the identity matrix (class 0x5c0ac4af).
    static cPatternBrush *__cdecl Create();

    bool QueryInterface(uint32_t riid, void **out) override;               // 0x153f20: ids 1, 0x3c0ac4a4, 0xdc004962
    uint32_t AddRef() override;                                            // 0x148fb0 (shared)
    uint32_t Release() override;                                           // 0x153f70 (shared): at zero the deleting destructor
    // 0x153ae0: the texture made from the bitmap if there is none, then the state block drawing with it (0 when the
    // texture could not be made).
    uint32_t GetStateBlock(cIDraw2D *draw2d, cIDeviceDX *device) override;
    // 0x153770: the fan with colour 0 and texture coordinates the matrix and offset applied to the point.
    void Draw(cIDraw2D *draw2d, uint32_t kind, uint32_t count, const float *points, const float *values) override;
    void SetImage(const void *bits, cIPixelFormat *format, uint32_t width, uint32_t height, uint32_t pitch) override; // 0x153a40 eax-ignored: memcpy's result left over; nothing in the image asks for this interface
    void ResetCache() override;                 // 0x154210: the bitmap emptied (its block kept), texture and state block let go
    void SetAngle(float angle) override;        // 0x154150: the matrix from the angle and the texture's size
    void SetOffset(float x, float y) override;  // 0x154050: the offset in texture units
    float GetAngle() override;                  // 0x153f90 (shared)
    float *GetOffset(float *out) override;      // 0x147810 (shared)
    ~cPatternBrush() override;                  // 0x152fa0 (deleting): state block, texture, then the bitmap

private:
    bool MakeTexture(cIDeviceDX *device);

    uint32_t mRefCount = 0;
    std::vector<uint8_t> mBitmap;
    cIPixelFormat *mFormat = nullptr;
    uint32_t mWidth = 0, mHeight = 0, mPitch = 0;
    float mAngle = 0.0f;
    float mOffsetX = 0.0f, mOffsetY = 0.0f;
    cITexture *mTexture = nullptr;
    cIDeviceResource *mStateBlock = nullptr;
    float mMatrix[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    float mTextureOffsetX = 0.0f, mTextureOffsetY = 0.0f;
};
static_assert(sizeof(cPatternBrush) == 0x50, "cPatternBrush layout");

} // namespace nGZGraphic4
