#pragma once
// nGZSceneGraph::cShaderInit (lane RZ, scenegraph M2b part 4): the description a shader is built from. 0xa4 bytes, vtable
// 0xe760a4 (27 slots), made by the factory 0xaa82a0 (plain operator new, constructor 0xaa7b90). A shader file's parsers fill
// it (one ShaderDescriptor per pass, set through the slots below); cShader::Build (slot 8 of the shader) reads the descriptor
// vector and the range pairs.
//
//   +0     vtable
//   +4     reference count
//   +8     the data (ShaderInitData, 0x9c bytes; its own constructor is 0xaa7bc0, called with this+8):
//   +8     a dword (slot 4 stores it). Slot 21 returns the address of the whole data: it is what cShader::Build is given
//          (cShader.h's ShaderInitView: the descriptor vector at its +0x28, the range pairs from its +0x3c).
//   +0xc   counted objects (slot 10 appends, slot 24 asks them)
//   +0x18  a name pool (0x18 bytes: a character buffer and a vector of offsets; its functions are another class's)
//   +0x30  the descriptors: a vector of 0xc8-byte ShaderDescriptor, grown on demand by Element (0xaa8460)
//   +0x3c  13 pairs of dwords, all 0xffffffff after construction (slot 20 sets one)
//
// Every slot and helper of the class is ours: the constructor, destructor (0xaa7d70 with the descriptor vector's destroy
// 0xaa7cf0), the deleting destructor, the factory and the table kOwnVTable. The slots that share a body with other classes
// (1, 2, 4, 21) are members of ours that do the same, tested against the image's bodies.
// The descriptor vector's helpers (DescriptorVec, ShaderDescriptor, ShaderTextureSlot) and the name pool
// (src/sg/cShaderNamePool.cpp) are ours too, and so are the object vectors: the toolchain's std::vector over the game's
// {first, last, end} (the image's shared instantiations 0x20c700 assign and 0x255dd0 grow are no longer reached).
#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

// Anything the descriptors keep counted pointers to (slots 0..2 of the usual interface).
class cIShaderObject {
public:
    virtual bool QueryInterface(uint32_t iid, void **out) = 0;
    virtual uint32_t AddRef() = 0;
    virtual uint32_t Release() = 0;
};

struct ObjectVec {
    cIShaderObject **mBegin;
    cIShaderObject **mEnd;
    cIShaderObject **mCap;
};

// An entry of a name pool: a polymorphic object the pool keeps inside its character buffer. Slot 0 destroys it in place
// (called with 0), slot 1 builds a copy of it at the given address; the word at +6 is its size in bytes.
class cIShaderName {
public:
    virtual void Destruct(uint32_t flags) = 0;
    virtual void CopyTo(void *dest) const = 0;

    uint16_t m4;
    uint16_t mSize; // +6
};

// The name pool at +0x18 (and +0x2c of a descriptor); zero is its empty state. w[0..2] a character buffer the entries are
// built in (begin, end, capacity: a std::vector<char>), w[3..5] a vector of dword offsets into it, one per entry. Its
// functions are src/sg/cShaderNamePool.cpp.
struct NamePool {
    uint32_t w[6];

    // How many entries (the image's body is the folded shared_vf_751740, not hooked).
    uint32_t Count() const { return static_cast<uint32_t>(static_cast<int32_t>(w[4] - w[3]) >> 2); }
    // 0xc631d0 (ret 4): entry i plus the base, unchecked (src/sg/cShaderInit.cpp).
    uint32_t At(uint32_t i) const;

    NamePool *Construct();                        // 0xc62f50: empty
    NamePool *CopyConstruct(const NamePool *src); // 0xc62ee0 (ret 4): empty, then Assign
    void Clear();                                 // 0xc630e0: destroys every entry, keeps the storage
    void Destroy();                               // 0xc62f90: Clear, then frees the offsets and the buffer
    void Assign(const NamePool *src);             // 0xc63030 (ret 4): Clear, reserve, Add a copy of each of src's entries
    void Add(const cIShaderName *name);           // 0xc63120 (ret 4): appends a copy of the entry
};

// One texture entry of a descriptor: a texture-manager handle (0xffffffff when empty) and a dword.
struct ShaderTextureSlot {
    uint32_t mHandle;
    uint32_t mValue;

    // 0xaa7c80 (thiscall, ret 4, returns this): the handle copied and counted (cTextureHandle::CopyFrom), the value copied.
    ShaderTextureSlot *CopyConstruct(const ShaderTextureSlot *from);
};

// One pass's description (0xc8 bytes).
struct ShaderDescriptor {
    cIShaderObject *mObject00;   // +0x00 (slot 5)
    ObjectVec mPartsA;           // +0x04 (slot 8 appends, slot 22 asks)
    ObjectVec mPartsB;           // +0x10 (slot 9 appends, slot 23 asks)
    cIShaderObject *mObject1c;   // +0x1c (slot 6)
    cIShaderObject *mObject20;   // +0x20 (slot 7)
    cIShaderObject *mObject24;   // +0x24 (slot 14)
    cIShaderObject *mObject28;   // +0x28 (slot 15)
    NamePool mNames;             // +0x2c (slot 11)
    ShaderTextureSlot mTextures[16]; // +0x44 (slot 13 sets, slot 25 looks up)
    uint8_t mByteC4;             // +0xc4 (slot 16)
    uint8_t mByteC5;             // +0xc5 (slot 17)
    uint8_t mByteC6;             // +0xc6 (slot 18)
    uint8_t mByteC7;             // +0xc7 (slot 19)

    // The copy constructor (inlined in 0xaa77c0 and 0xaa7930): the objects counted, the part vectors and the name pool
    // copied, the texture entries copied one by one, the bytes copied.
    ShaderDescriptor *CopyConstruct(const ShaderDescriptor *from);
    // 0xaa7db0 (thiscall, ret 4, returns this): the copy assignment, member by member (the object vectors by
    // std::vector assignment, cShaderInit.cpp).
    ShaderDescriptor *Assign(const ShaderDescriptor *from);
};
static_assert(sizeof(ShaderDescriptor) == 0xc8, "descriptor size");
static_assert(offsetof(ShaderDescriptor, mPartsA) == 4 && offsetof(ShaderDescriptor, mPartsB) == 0x10, "descriptor");
static_assert(offsetof(ShaderDescriptor, mObject1c) == 0x1c && offsetof(ShaderDescriptor, mObject28) == 0x28, "descriptor");
static_assert(offsetof(ShaderDescriptor, mNames) == 0x2c && offsetof(ShaderDescriptor, mTextures) == 0x44, "descriptor");
static_assert(offsetof(ShaderDescriptor, mByteC4) == 0xc4, "descriptor");

struct DescriptorVec {
    ShaderDescriptor *mBegin;
    ShaderDescriptor *mEnd;
    ShaderDescriptor *mCap;

    // 0xaa7cf0 (thiscall): nothing when empty; else the elements are destroyed (DestroyRange), the block given back
    // (sized delete; a block of 0x1000 bytes or more carries its real pointer in the dword before it) and the pointers zeroed.
    void Destroy();

    // 0xaa7500 (cdecl; the vector is unused): destroys [first, last) in place, each descriptor's members in reverse order
    // (the texture handles from the last, the name pool, the objects 0x28..0x1c, the two part vectors, the object at 0).
    static void __cdecl DestroyRange(ShaderDescriptor *first, ShaderDescriptor *last, DescriptorVec *vec);
    // 0xaa7a90 (cdecl): default-constructs count descriptors from first (objects and part vectors zero, an empty name pool,
    // texture handles -1 with their values left as they were, the four bytes zero); returns the end.
    static ShaderDescriptor *__cdecl FillDefault(ShaderDescriptor *first, uint32_t count, DescriptorVec *vec);
    // 0xaa77c0 (cdecl): copy-constructs [first, last) at dest; returns the end.
    static ShaderDescriptor *__cdecl CopyRange(const ShaderDescriptor *first, const ShaderDescriptor *last, ShaderDescriptor *dest,
                                               DescriptorVec *vec);
    // 0xaa7930 (cdecl): copy-constructs count descriptors from first at dest; returns the end.
    static ShaderDescriptor *__cdecl CopyCount(const ShaderDescriptor *first, uint32_t count, ShaderDescriptor *dest, DescriptorVec *vec);
    // 0xaa72f0 (cdecl): copy-assigns count descriptors from first onto dest; returns the end.
    static ShaderDescriptor *__cdecl AssignCount(const ShaderDescriptor *first, uint32_t count, ShaderDescriptor *dest);
    // 0xaa75e0 (thiscall, ret 8; the second argument is unused): grows the storage to hold count (by half again, at least
    // count), the new tail default-filled, the old descriptors copied over then destroyed and their block freed.
    void Reallocate(uint32_t count, const void *unused);
    // 0xaa7130 (thiscall, ret 8): the vector's assign from count descriptors at first.
    void Assign(const ShaderDescriptor *first, uint32_t count);
};

// The data after the reference count; the constructor 0xaa7bc0 is called with the address of this.
struct ShaderInitData {
    uint32_t mAttribute;     // +8
    ObjectVec mObjects;      // +0xc
    NamePool mNames;         // +0x18
    DescriptorVec mDescs;    // +0x30
    uint32_t mRanges[26];    // +0x3c: 13 pairs

    // 0xaa7bc0 (thiscall, returns this): zero, an empty name pool, no descriptors, every range dword 0xffffffff.
    ShaderInitData *Construct();
};
static_assert(sizeof(ShaderInitData) == 0x9c, "data size");

class cShaderInit {
public:
    static constexpr uint32_t kIidCounted = 0x5c17ff18;

    virtual bool QueryInterface(uint32_t iid, void **out) = 0;                 // 0
    virtual uint32_t AddRef() = 0;                                              // 1
    virtual uint32_t Release() = 0;                                             // 2
    virtual uint32_t Clone(uint32_t iid, void **out) = 0;                       // 3 (ret 8)
    virtual uint32_t SetAttribute(uint32_t value) = 0;                          // 4
    virtual void SetObject00(int32_t pass, cIShaderObject *object) = 0;        // 5
    virtual void SetObject1c(int32_t pass, cIShaderObject *object) = 0;        // 6
    virtual void SetObject20(int32_t pass, cIShaderObject *object) = 0;        // 7
    virtual void AddPartA(int32_t pass, cIShaderObject *object) = 0;           // 8
    virtual void AddPartB(int32_t pass, cIShaderObject *object) = 0;           // 9
    virtual void AddObject(cIShaderObject *object) = 0;                         // 10
    virtual void AddPassName(int32_t pass, const void *name) = 0;              // 11
    virtual void AddName(const void *name) = 0;                                 // 12
    virtual void SetTexture(int32_t pass, uint32_t index, uint32_t source, uint32_t value) = 0; // 13 (ret 0x10)
    virtual void SetObject24(int32_t pass, cIShaderObject *object) = 0;        // 14
    virtual void SetObject28(int32_t pass, cIShaderObject *object) = 0;        // 15
    virtual void SetByteC4(int32_t pass, uint8_t value) = 0;                    // 16
    virtual void SetByteC5(int32_t pass, uint8_t value) = 0;                    // 17
    virtual void SetByteC6(int32_t pass, uint8_t value) = 0;                    // 18
    virtual void SetByteC7(int32_t pass, uint8_t value) = 0;                    // 19
    virtual void SetRange(uint32_t pair, uint32_t first, uint32_t second) = 0; // 20 (ret 0xc)
    virtual ShaderInitData *GetData() = 0;                                      // 21 (what cShader::Build is given)
    virtual bool PartAIs(int32_t pass, uint32_t iid, void **out) = 0;          // 22 (ret 0xc)
    virtual bool PartBIs(int32_t pass, uint32_t iid, void **out) = 0;          // 23 (ret 0xc)
    virtual bool ObjectIs(uint32_t iid, void **out) = 0;                        // 24 (ret 8)
    virtual void *TextureOf(int32_t pass, uint32_t index) = 0;                  // 25 (ret 8)
    virtual cShaderInit *Delete(uint32_t flags) = 0;                            // 26

    uint32_t mRefCount;       // +4
    ShaderInitData mData;     // +8

    // 0xaa8460 (thiscall, ret 4): the descriptor of a pass, the vector grown with default descriptors when the pass is past
    // its end. `pass` must not be negative.
    ShaderDescriptor *Element(uint32_t pass);

    // Slot 3, 0xaa8180 (ret 8): a new shader init (counted) with this one's attribute, objects, names, descriptors and
    // ranges, asked for `iid` / `out` (its slot 0), then released; returns what that release returns (1 when the new object
    // lives on because the answer was positive, else 0).
    uint32_t CloneBody(uint32_t iid, void **out);
    // Slot 0, 0xaa8430 (ret 8): the ids 1 and 0x5c17ff18 give the object, counted.
    bool QueryInterfaceBody(uint32_t iid, void **out);
    uint32_t AddRefBody();                                   // slot 1 (0x148fb0, shared)
    uint32_t ReleaseBody();                                  // slot 2 (0x292890, shared): the last reference deletes through slot 26
    uint32_t SetAttributeBody(uint32_t value);               // slot 4 (0x14a260, shared)
    ShaderInitData *GetDataBody();                           // slot 21 (0x142840, shared): this + 8
    // Slots 5, 6, 7, 14 and 15 (0xaa85b0, 0xaa8630, 0xaa85f0, 0xaa8720, 0xaa8570; ret 8): store a counted object in the
    // pass's field (counted first, the old one released after); negative passes are ignored.
    void SetObject00Body(int32_t pass, cIShaderObject *object);
    void SetObject1cBody(int32_t pass, cIShaderObject *object);
    void SetObject20Body(int32_t pass, cIShaderObject *object);
    void SetObject24Body(int32_t pass, cIShaderObject *object);
    void SetObject28Body(int32_t pass, cIShaderObject *object);
    // Slots 8 and 9 (0xaa80b0, 0xaa7f90; ret 8) and 10 (0xaa8030, ret 4): append a counted object to a pass's vector (or the
    // init's); the object is held across the call.
    void AddPartABody(int32_t pass, cIShaderObject *object);
    void AddPartBBody(int32_t pass, cIShaderObject *object);
    void AddObjectBody(cIShaderObject *object);
    // Slots 11 and 12 (0xaa8160 ret 8, 0xaa8150 ret 4): add a name to the pass's pool or the init's (the pool is the image's).
    void AddPassNameBody(int32_t pass, const void *name);
    void AddNameBody(const void *name);
    // Slot 13, 0xaa8690 (ret 0x10): the pass's texture entry `index` (not checked) takes a handle made from `source` and the
    // dword `value`.
    void SetTextureBody(int32_t pass, uint32_t index, uint32_t source, uint32_t value);
    // Slots 16..19 (0xaa8700, 0xaa8550, 0xaa8670, 0xaa8530; ret 8): the pass's bytes +0xc4..+0xc7.
    void SetByteC4Body(int32_t pass, uint8_t value);
    void SetByteC5Body(int32_t pass, uint8_t value);
    void SetByteC6Body(int32_t pass, uint8_t value);
    void SetByteC7Body(int32_t pass, uint8_t value);
    // Slot 20, 0xaa8510 (ret 0xc): range pair `pair` (not checked).
    void SetRangeBody(uint32_t pair, uint32_t first, uint32_t second);
    // Slots 22 and 23 (0xaa83c0, 0xaa8310; ret 0xc) and 24 (0xaa8380, ret 8): true when one of the pass's parts (or the init's
    // objects), asked in order for iid / out, answers yes; false for a pass out of range.
    bool PartAIsBody(int32_t pass, uint32_t iid, void **out);
    bool PartBIsBody(int32_t pass, uint32_t iid, void **out);
    bool ObjectIsBody(uint32_t iid, void **out);
    // Slot 25, 0xaa8760 (ret 8): the texture manager's object for the pass's entry `index` (0 for a pass out of range).
    void *TextureOfBody(int32_t pass, uint32_t index);
    // Slot 26, 0xaa7f60: the destructor, then with bit 0 of the flags the memory (sized delete of 0xa4).
    cShaderInit *DeleteBody(uint32_t flags);
    void DestructBody();                                     // 0xaa7d70
    cShaderInit *Construct();                                // 0xaa7b90 (thiscall, returns this)
    static cShaderInit *Create();                            // 0xaa82a0 (cdecl): a new one, or null

    static const void *const kOwnVTable[27];
};
static_assert(offsetof(cShaderInit, mRefCount) == 4 && offsetof(cShaderInit, mData) == 8, "init");
static_assert(sizeof(cShaderInit) == 0xa4, "init size");
static_assert(offsetof(ShaderInitData, mDescs) == 0x28 && offsetof(ShaderInitData, mRanges) == 0x34, "data");

} // namespace nGZSceneGraph
