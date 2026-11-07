#pragma once
// nGZSceneGraph::cShader (lane RT, scenegraph M2b part 1): the pass list of a shader. 0x98 bytes, vtable 0xe758dc
// (14 slots), made by the factory 0xaa4fa0 (class operator new 0xaa4d90, constructor 0xaa4a40).
//
// Parts 1 and 2 took the slot bodies over one by one at their RVAs; part 3 gives the class its own tables (below).
// The virtual functions are the slots (declared pure; only those a body of ours calls are named).
//
//   +0    vtable
//   +4    reference count
//   +8    enabled byte (slots 4 and 5)
//   +0xc  the passes: a vector of 0xa4-byte ShaderPass (begin, end, capacity)
//   +0x18 the range table: 13 pairs of uint32, all 0xffffffff after construction (compared by slot 7, hashed into +0x94)
//   +0x80 the info block (slot 9 returns its address): first pass, a dword, the pass count, four flag bytes, a byte
//   +0x94 the hash (slot 6)
//
// Done here (hooked at their RVAs): slot 0 (0xaa6950), slot 3 (0xaa6b30), slot 7 (0xaa6470), slot 11 (0xaa69e0), the
// pass helpers 0xaa6b60 (Reset), 0xaa6510 (Equals), 0xaa6e50 (EndState) and the vector comparison 0xaa3d20.
// Part 2 (lane RV), also hooked: slot 10 (0xaa6c80) with ShaderPass::Validate (0xaa6e80), slot 12 Dump (0xaa65e0), the
// deleting destructor 13 (0xaa4e60) with the destructor body (0xaa4d70), the pass vector destroy (0xaa4ce0), the pass
// destructor (0xaa4ea0), the class operator new/delete (0xaa4d90, 0xaa4dc0), the constructor (0xaa4a40), the factory (0xaa4fa0).
// Part 3 (lane RX), also hooked: slot 8 Build (0xaa5030), the pass
// copy constructor 0xaa4ae0, the part-vector copy constructor 0xaa4980 and the hash mixes 0x98bf40 / 0x98c220. The class now
// has its own tables (kOwnVTable, and kOwnBaseVTable which the destructor body stores): the constructor stores ours, and the
// slots whose body is shared with other classes (1, 4, 5, 6, 9, the jump of 2 and the base's 0xaa6920 / 0xaa69a0 / 0xaa4e40)
// are members of ours that do the same, tested against the image's bodies. Later, also hooked: the pass setup 0xaa53e0
// (ShaderPass::Setup) and the part-vector hash 0xaa4fe0, and the eight apply functions it picks from (ShaderPass::Apply).
#include <cstddef>
#include <cstdint>

namespace nGZGraphic4 {
class cIDeviceDX;
class cIDeviceState;
} // namespace nGZGraphic4

namespace nGZSceneGraph {

struct ShaderRenderCtx;
struct ShaderFrame;

// A counted object a pass keeps. Slots 2 (Release), 3 (Reset) and 13 (EndState) are the ones used here; the part mapper
// (the pass's mP8) answers slot 8, the controllers slot 11.
class cIShaderPart {
public:
    virtual void Slot0() = 0;
    virtual void AddRef() = 0;                 // 1
    virtual void Release() = 0;                // 2
    virtual void Reset() = 0;                  // 3
    virtual void Slot4() = 0;
    virtual void Slot5() = 0;
    virtual void Slot6() = 0;
    virtual void Slot7() = 0;
    virtual ShaderFrame *MapFrame(ShaderFrame *frame, ShaderRenderCtx *ctx) = 0; // 8: the frame the pass draws instead
    virtual const uint32_t *Slot9(void *out, const void *key) = 0; // 9: the key this part maps `key` to (out is scratch)
    virtual void Slot10() = 0;
    virtual void Apply(ShaderRenderCtx *ctx, ShaderFrame *frame) = 0; // 11: put the controller's values on the device
    virtual void Slot12(void *device, const void *key) = 0;       // 12: set the part up on the device for a key
    virtual void EndState(void *ctx) = 0;      // 13 (+0x34)
};

// The three-pointer vector the passes keep their parts in.
struct ShaderPartVec {
    cIShaderPart **mBegin;
    cIShaderPart **mEnd;
    cIShaderPart **mCap;

    // 0xaa4980 (thiscall, ret 4, returns this): the copy constructor. The new vector holds exactly as many elements as
    // `from` (no spare room; none at all, and no allocation, when `from` is empty), each copied and counted (slot 1). The
    // block is 32-byte aligned with the raw pointer before it when it is 0x1000 bytes or more, as the vector's destroy
    // (RefPtrVector::Destroy) expects.
    ShaderPartVec *CopyConstruct(const ShaderPartVec *from);
};
static_assert(sizeof(ShaderPartVec) == 12, "vector layout");

// 0xaa3d20 (cdecl): true when the vectors differ (byte sizes differ in a bit above 1, or any element differs).
bool ShaderPartVecDiffer(const ShaderPartVec *a, const ShaderPartVec *b);

// The two integer hashes the shader's hash is built with. 0x98bf40 (cdecl): Thomas Wang's 32-bit mix. 0x98c220 (cdecl): a
// 64-bit mix of the pair (first = low dword, second = high), giving the low dword of the result.
uint32_t ShaderHashMix(uint32_t x);
uint32_t ShaderHashCombine(uint32_t low, uint32_t high);

// 0xaa4fe0 (ret 4; ecx unused): the hash of a part vector, 0 when empty; per element p, in order:
// h = ShaderHashCombine(ShaderHashMix((p >> 2) * 0x9e3779b1), ShaderHashMix(h)). src/sg/cShaderPassSetup.cpp.
uint32_t __stdcall ShaderPartVecHash(const ShaderPartVec *vec);

struct ShaderKey;
struct ShaderInitView;

// One pass (0xa4 bytes, no vtable).
struct ShaderPass {
    // The apply function: this = the pass; (ctx, frame, out, flag), pops 0x10.
    using ApplyFn = void(__thiscall *)(ShaderPass *self, ShaderRenderCtx *ctx, ShaderFrame *frame, uint32_t *out, uint32_t flag);

    ApplyFn mApply;           // +0
    uint8_t mB4, mB5, mB6, mB7;
    cIShaderPart *mP8;        // +8
    cIShaderPart *mPc;        // +0xc
    cIShaderPart *mP10;       // +0x10
    ShaderPartVec mV14;       // +0x14
    cIShaderPart *mP20;       // +0x20
    ShaderPartVec mV24;       // +0x24
    ShaderPartVec mV30;       // +0x30
    uint8_t mB3c, mB3d, mB3e, mPad3f;
    uint32_t m40, m44;
    cIShaderPart *mP48, *mP4c, *mP50, *mP54;
    ShaderPartVec mV58;       // +0x58
    uint32_t mHandles[16];    // +0x64: texture handles, 0xffffffff when empty

    void Reset();                             // 0xaa6b60
    bool Equals(const ShaderPass *other) const; // 0xaa6510
    void EndState(void *ctx);                 // 0xaa6e50: slot 13 of every part in mV30
    // 0xaa6e80 (ret 0x14): set the device up for this pass with the table's handles and ask it whether it can run the
    // pass; false (logged when `report`) when it cannot. `table` has 0x22 dwords.
    bool Validate(void *device, uint32_t index, const uint32_t *table, const ShaderKey *key, bool report);
    // 0xaa53e0 (ret 0x14; src/sg/cShaderPassSetup.cpp): set the pass up from descriptor `index` of the init for `key` on
    // the device (Build's context): its flags and programs, the part mapper (the descriptor's object +0, registered with
    // the material manager) and the key it maps to, the apply function, the vertex declarations (plain, hardware and the
    // preprocess program's output), the controllers (mV14 for the preprocess program, mV24/mV30 the dynamic ones, mV58
    // all), the textures, the state block recorded from all of it, and the pass hash m44. False (logged when `report`)
    // at the first thing the device cannot do; what was set up so far stays.
    bool Setup(nGZGraphic4::cIDeviceDX *device, uint32_t index, const ShaderInitView *init, const ShaderKey *key, bool report);
    // The eight apply functions Setup picks from (thiscall, ret 0x10; src/sg/cShaderPassApply.cpp): draw the frame's
    // geometry with this pass. kMapped: the part mapper (mP8, slot 8) gives the frame drawn instead, first of all.
    // kPreprocess (mPc present): the preprocess program goes on the device (slot 14), the mV14 controllers apply, and the
    // geometry's Prepare (slot 6) answers *out; otherwise *out comes in from the previous pass. Then, unless *out is -1: the
    // state block (mP20) when `flag`'s low byte or mB4 is set, a clipping command (enabled when bit 1 of the frame's +0x16,
    // mB3e or mB6 is), the mV24 controllers, and the geometry's Render (slot 8, its answer to *out) when kHardware (mB5),
    // else its Begin (slot 7) from *out.
    //   0xaa44d0 <0,0,0>   0xaa3d70 <1,0,0>   0xaa3e90 <0,1,0>   0xaa3f70 <1,1,0>
    //   0xaa40a0 <0,0,1>   0xaa4180 <1,0,1>   0xaa42b0 <0,1,1>   0xaa43a0 <1,1,1>   (<kPreprocess, kHardware, kMapped>)
    template <bool kPreprocess, bool kHardware, bool kMapped>
    void Apply(ShaderRenderCtx *ctx, ShaderFrame *frame, uint32_t *out, uint32_t flag);
    // Ours of the eight, by index (kPreprocess | kHardware << 1 | kMapped << 2): what Setup stores in mApply.
    static ApplyFn ApplyFor(uint32_t index);
    // 0xaa4ea0 (ret 4): release everything the pass holds; with bit 0 of `flags` also free the pass. Returns this.
    ShaderPass *Destruct(uint32_t flags);
    // 0xaa4ae0 (thiscall, ret 4, returns this): the copy constructor, on raw storage. Every field is copied; each part
    // pointer in it is counted (slot 1; null ones skipped), each part vector is copied by ShaderPartVec::CopyConstruct, and
    // each of the 16 texture handles by the handle copy 0xae8240 (which counts the handle in the texture manager).
    ShaderPass *CopyConstruct(const ShaderPass *from);
};

// A shader key: three dwords (slot 10 compares the third and reads the 4-bit fields of all three).
struct ShaderKey {
    uint32_t w[3];
};

// What Build reads of the cShaderInit (27 slots; its class is a later lane's): the descriptor vector at
// +0x28/+0x2c (0xc8-byte elements, one per pass) and 24 dwords at +0x3c that become the range table's pairs 1..12.
struct ShaderInitView {
    static constexpr uint32_t kDescriptorBytes = 0xc8;
    uint8_t mPad0[0x28];
    const uint8_t *mDescBegin;   // +0x28
    const uint8_t *mDescEnd;     // +0x2c
    uint8_t mPad30[0xc];
    uint32_t mRanges[24];        // +0x3c
};
static_assert(offsetof(ShaderInitView, mDescBegin) == 0x28 && offsetof(ShaderInitView, mRanges) == 0x3c, "init view");
static_assert(sizeof(ShaderPass) == 0xa4, "pass layout");
static_assert(offsetof(ShaderPass, mV14) == 0x14 && offsetof(ShaderPass, mV24) == 0x24 && offsetof(ShaderPass, mV30) == 0x30, "vectors");
static_assert(offsetof(ShaderPass, mB3c) == 0x3c && offsetof(ShaderPass, m40) == 0x40 && offsetof(ShaderPass, mP48) == 0x48, "pass");
static_assert(offsetof(ShaderPass, mV58) == 0x58 && offsetof(ShaderPass, mHandles) == 0x64, "pass tail");

// The parts of the renderer's state that slot 11 reads and writes.
struct ShaderRenderCtx {
    uint8_t mPad0[0x28c];
    class cShader *mShader;   // +0x28c: the shader last applied
    uint32_t m290;
    uint32_t m294;
    uint8_t mSame;            // +0x298: set when the previous pass setup can be reused
    uint8_t mPad299[7];
    uint32_t mShaderChanges;  // +0x2a0
    uint32_t mPassCount;      // +0x2a4
    uint32_t m2a8;
    nGZGraphic4::cIDeviceState *mState; // +0x2ac: the device's state cache (the apply functions' slots 11, 12 and 14)
};
static_assert(offsetof(ShaderRenderCtx, mShader) == 0x28c && offsetof(ShaderRenderCtx, m290) == 0x290, "ctx");
static_assert(offsetof(ShaderRenderCtx, mSame) == 0x298 && offsetof(ShaderRenderCtx, mShaderChanges) == 0x2a0, "ctx");
static_assert(offsetof(ShaderRenderCtx, mPassCount) == 0x2a4 && offsetof(ShaderRenderCtx, mState) == 0x2ac, "ctx");

// What slot 11 and the apply functions read of the frame (a display list item).
struct ShaderFrame {
    uint8_t mPad0[0xc];
    uint32_t m0c;             // the geometry (cIGeometryRender, src/sg/cIGeometryRender.h); slot 11 compares it as a value
    uint8_t mPad10[4];
    uint16_t mSubset;         // +0x14: the index the geometry's slots 6..8 get
    uint8_t m16;              // bit 3 set: no reuse; bit 1: clipping on
    uint8_t mPad17[0x2c - 0x17];
    const uint32_t *m2c;      // +0x2c
    uint8_t mPad30[8];
    uint32_t m38;             // bit 1 set: no reuse
};
static_assert(offsetof(ShaderFrame, m0c) == 0xc && offsetof(ShaderFrame, mSubset) == 0x14 && offsetof(ShaderFrame, m16) == 0x16, "frame");
static_assert(offsetof(ShaderFrame, m2c) == 0x2c && offsetof(ShaderFrame, m38) == 0x38, "frame");

// The info block slot 9 returns (the object's +0x80..0x93).
struct ShaderInfo {
    ShaderPass *mFirstPass;   // +0x80
    uint32_t m84;
    uint32_t mCount;          // +0x88
    uint8_t mFlags[4];        // +0x8c
    uint8_t m90;              // +0x90
    uint8_t mPad91[3];
};
static_assert(sizeof(ShaderInfo) == 0x14, "info layout");

class cShader {
public:
    static constexpr uint32_t kIidA = 0xcd2e1086;  // stored without a count
    static constexpr uint32_t kIidB = 0xcd2e1781;  // stored without a count
    static constexpr uint32_t kIidCounted = 0xfc17e651;

    virtual bool QueryInterface(uint32_t iid, void **out) = 0; // 0
    virtual uint32_t AddRef() = 0;                              // 1
    virtual uint32_t Release() = 0;                             // 2
    virtual void ResetPasses() = 0;                             // 3
    virtual void SetEnabled(bool on) = 0;                       // 4
    virtual bool GetEnabled() = 0;                              // 5
    virtual uint32_t GetHash() = 0;                             // 6
    virtual bool IsEqual(cShader *other) = 0;                   // 7
    virtual bool Build(void *ctx, const ShaderInitView *init, const uint32_t *table, const ShaderKey *key, bool report) = 0; // 8 (ret 0x14)
    virtual ShaderInfo *GetInfo() = 0;                          // 9
    virtual bool Slot10(void *, void *, void *, void *) = 0;    // 10 (ret 0x10)
    virtual void Apply(ShaderRenderCtx *ctx, ShaderFrame *frame) = 0; // 11
    virtual void Dump(void *, uint32_t) = 0;                    // 12
    virtual void Delete(uint32_t flags) = 0;                    // 13

    uint32_t mRefCount;       // +4
    uint8_t mEnabled;         // +8
    uint8_t mPad9[3];
    struct PassVec {
        ShaderPass *mBegin;
        ShaderPass *mEnd;
        ShaderPass *mCap;
        void Destroy();       // 0xaa4ce0: destruct the passes, free the storage, zero the pointers
        // 0xaa47f0 (cdecl; the vector, third, unused): construct `count` default passes on raw storage from `first`
        // (everything zero but mB4 = 1, the texture handles 0xffffffff; the pad byte +0x3f untouched). Returns the end.
        static ShaderPass *FillDefault(ShaderPass *first, uint32_t count, PassVec *vector);
        // 0xaa45e0 (thiscall, ret 8; the second argument, the allocator, unused): resize to `size` passes (more than the
        // capacity) in a new block: capacity grown by half, or to `size` when that is more; the new passes default
        // (FillDefault), the old ones copied over (ShaderPass::CopyConstruct) and then destructed, the old block freed.
        void Reallocate(uint32_t size, void *allocator);
    } mPasses;                // +0xc
    uint32_t mRanges[26];     // +0x18
    ShaderInfo mInfo;         // +0x80
    uint32_t mHash;           // +0x94

    // Slot 8, 0xaa5030 (ret 0x14): build the passes from the cShaderInit's descriptors. Copies the init's 24 range dwords over
    // pairs 1..12, asks slot 10 (with the key twice, no context) whether the shader's support flags are consistent (false:
    // logged when `report`, result false). With a context that answers yes to its slot 35, and a table, it resizes the pass
    // vector to the descriptor count (0xaa47f0 / 0xaa45e0 grow it, the extra passes are destructed when it shrinks), has
    // ShaderPass::Setup set each pass up, sets the device's mode 3 and validates each pass (ShaderPass::Validate); a failing pass gives
    // false at once. Then, always: the info block (count, flags, first pass) and the hash of the range table combined with
    // each pass's field +0x44 are rebuilt.
    bool BuildBody(void *ctx, const ShaderInitView *init, const uint32_t *table, const ShaderKey *key, bool report);

    // The slots shared with other classes' bodies (the image's folded copies), members of ours that do the same.
    uint32_t AddRefBody();                             // slot 1 (0x146a50, a jump to 0x148fb0): count + 1, returned
    uint32_t ReleaseBody();                            // slot 2 (0xaa69d0, a jump to the base's 0xaa69a0)
    void SetEnabledBody(uint8_t on);                   // slot 4 (0x1489f0)
    uint8_t GetEnabledBody();                          // slot 5 (0x1480a0)
    uint32_t GetHashBody();                            // slot 6 (0x24bde0)
    ShaderInfo *GetInfoBody();                         // slot 9 (0xaa6b20)
    // The base class (cShaderComponentT<cIShader>, table 0xe758a0 in the image): its query (0xaa6920) and deleting destructor
    // (0xaa4e40), and its do-nothing slot 3 (0x140fe0).
    bool BaseQueryInterfaceBody(uint32_t iid, void **out);
    void BaseDoNothing();
    cShader *BaseDeleteBody(uint32_t flags);

    // Our tables: the image's slot order and count (14 each). Slots whose body is hooked hold the hook's replacement; the
    // rest the members above. The base's 7 pure slots (6..12) hold the toolchain's _purecall.
    static const void *const kOwnVTable[14];
    static const void *const kOwnBaseVTable[14];

    bool QueryInterfaceBody(uint32_t iid, void **out); // slot 0, 0xaa6950
    void ResetPassesBody();                            // slot 3, 0xaa6b30
    bool IsEqualBody(cShader *other);                  // slot 7, 0xaa6470
    void ApplyBody(ShaderRenderCtx *ctx, ShaderFrame *frame); // slot 11, 0xaa69e0
    // Slot 10, 0xaa6c80 (ret 0x10): can the device run this shader for key2 (key1 is the key it was built for)?
    bool ValidateBody(const ShaderKey *key1, const ShaderKey *key2, const uint32_t *table, void *ctx);
    void DestructBody();                               // 0xaa4d70
    cShader *DeleteBody(uint32_t flags);               // slot 13, 0xaa4e60
    static void *PoolNew(size_t size);                 // 0xaa4d90 (cdecl; the size is ignored)
    static void PoolDelete(void *block);
    // Slot 12, 0xaa65e0 (ret 8): write "Layer:", the pass count and, per pass, the disassembly the shader-programs
    // manager has for each of its three program handles, indented by `indent` spaces, to a text stream (another class's
    // object, called through its slots 8 uint, 9 int, 14 C string and 15 string object). Leaves eax undefined.
    void DumpBody(void *stream, uint32_t indent);
    cShader *Construct();                              // 0xaa4a40 (thiscall, returns this; the base constructor 0xaa4960 inlined)
    static cShader *Create();                          // 0xaa4fa0 (cdecl): a new shader from the pool, or null               // 0xaa4dc0 (cdecl; null is ignored)
};
static_assert(offsetof(cShader, mRefCount) == 4 && offsetof(cShader, mEnabled) == 8, "shader");
static_assert(offsetof(cShader, mPasses) == 0xc && offsetof(cShader, mRanges) == 0x18, "shader");
static_assert(offsetof(cShader, mInfo) == 0x80 && offsetof(cShader, mHash) == 0x94, "shader");
static_assert(sizeof(cShader) == 0x98, "shader size");

} // namespace nGZSceneGraph
