#pragma once
#include "gz/cIGZSerializable.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ifaces {
class cIGZIStream;
class cIGZOStream;
} // namespace ifaces

// cTSFloatConstants: the named tuning constants of one table (the game's "float constants": a list of name -> float with a
// description, loaded from and saved to a resource of the neighbourhood's package; cTSNeighborhoodConstants and the
// autonomy constants client read their numbers from one). Class id 0x46434e53 (GetGZCLSID), interface id 0x7b6faf7e. RTTI
// bases cITSFloatConstants, cIGZSerializable, cIGZUnknown, all at +0 (one vtable, 20 slots). 0x18 bytes: +4 the reference
// count, +8 the dirty byte, +0xc the entry vector (begin, end, capacity; 0x48-byte entries). RVAs 1.18.0.187.
//
// Standalone shape (docs/standalone-shape.md): a real C++ class. The compiler emits the vtable, the constructor installs
// it; every object the game builds (the class factory Create 0x4df2d0 the COM director registers, and the constructor
// 0x4df0c0 the three stack users call: cTSNeighborhoodConstants::Load, cTSAutonomyConstantsClient slot 3 and 0x6161a5)
// carries it. The hooks that remain are adapters for the entry points original code still calls directly
// (cTSFloatConstantsAdapters below); every other slot is reached through our vtable.

// The interface (slots 6 to 18; the original's slot order, global as its RTTI). Names are ours.
class cITSFloatConstants : public cIGZSerializable {
public:
    virtual const char *GetName(int32_t index) = 0;                              // slot 6, 0x4df400 (ret 4): null out of range
    virtual float GetFloat(const char *name, float defaultValue, bool add) = 0;  // slot 7, 0x4df320 (ret 0xc)
    virtual void SetFloat(const char *name, float value) = 0;                    // slot 8, 0x4df8c0 (ret 8)
    virtual const char *GetDescription(const char *name) = 0;                    // slot 9, 0x4df380 (ret 4)
    virtual void SetDescription(const char *name, const char *text) = 0;         // slot 10, 0x4dfa30 (ret 8)
    virtual bool Has(const char *name) = 0;                                      // slot 11, 0x4df460 (ret 4)
    virtual void Remove(const char *name) = 0;                                   // slot 12, 0x4df760 (ret 4)
    virtual void Clear() = 0;                                                    // slot 13, 0x4df840
    virtual void Load(void *segment, uint32_t instance) = 0;                     // slot 14, 0x4df4a0 (ret 8)
    virtual void Save(void *fileRef, uint32_t instance) = 0;                     // slot 15, 0x4df880 (ret 8)
    virtual bool IsDirty() = 0;                                                  // slot 16, folded 0x1480a0
    virtual void SetDirty(bool dirty) = 0;                                       // slot 17, folded 0x1489f0 (ret 4)
    virtual uint32_t DropReference() = 0;                                        // slot 18, 0x4df870: the count less one, never frees
    virtual ~cITSFloatConstants() = default;                                     // slot 19, the scalar deleting destructor 0x4df2a0
};

// One entry (0x48 bytes): the value, the name the lookups compare case-insensitively, and a description. It has its own
// two-slot vtable (Read, Write: the stream form of one entry). The strings are the game's cRZString (vtable and layout as
// the original's), so the streams fill them through their own slots; they are built and destroyed by hand (union members).
class cTSFloatConstantPair {
public:
    cTSFloatConstantPair();                                       // an empty entry: 0, two empty strings
    cTSFloatConstantPair(const cTSFloatConstantPair &other);
    cTSFloatConstantPair &operator=(const cTSFloatConstantPair &other);
    ~cTSFloatConstantPair();

    // The text and value of another entry, assigned in place (the shifts of Remove copy the number and both strings).
    void AssignContents(const cTSFloatConstantPair &other);

    virtual bool Read(ifaces::cIGZIStream *stream);   // slot 0, 0x4df500 (ret 4): false when the stream reports an error
    virtual bool Write(ifaces::cIGZOStream *stream);  // slot 1, 0x4dfa90 (ret 4)

    float mValue;                                     // +4
    union { cRZString mName; };                       // +8
    union { cRZString mDescription; };                // +0x28
};
static_assert(sizeof(cTSFloatConstantPair) == 0x48, "float constant entry layout");
static_assert(offsetof(cTSFloatConstantPair, mValue) == 4, "entry value");
static_assert(offsetof(cTSFloatConstantPair, mName) == 8, "entry name");
static_assert(offsetof(cTSFloatConstantPair, mDescription) == 0x28, "entry description");

class cTSFloatConstants final : public cITSFloatConstants {
public:
    static constexpr uint32_t kClassId = 0x46434e53;         // GetGZCLSID
    static constexpr uint32_t kInterfaceId = 0x7b6faf7e;     // QueryInterface answers this and cIGZUnknown (1)
    static constexpr uint32_t kStreamStringsId = 0x3b2271c0; // the older streams' string reader, asked of a stream
    static constexpr uint32_t kWriteVersion = 0x55;          // Write's metadata version and second word
    static constexpr uint32_t kNewStringVersion = 0x3f;      // from this version an entry's strings are cIGZString records

    cTSFloatConstants();                                 // 0x4df0c0 (46 bytes): hook entry cTSFloatConstantsAdapters::Construct
    ~cTSFloatConstants() override;                       // slot 19 (scalar deleting, 0x4df2a0) and the body 0x4df260
    cTSFloatConstants(const cTSFloatConstants &) = delete;
    cTSFloatConstants &operator=(const cTSFloatConstants &) = delete;
    static cTSFloatConstants *Create();                  // 0x4df2d0 (cdecl): new, null when out of memory

    // cIGZUnknown
    bool QueryInterface(uint32_t riid, void **out) override; // slot 0, 0x4df4d0 (ret 8)
    uint32_t AddRef() override;                              // slot 1, folded 0x148fb0
    uint32_t Release() override;                             // slot 2, folded 0x44cc60: frees the object at the last reference

    // cIGZSerializable
    bool Write(ifaces::cIGZOStream *stream) override;        // slot 3, 0x4dfae0 (ret 4)
    bool Read(ifaces::cIGZIStream *stream) override;         // slot 4, 0x4df5e0 (ret 4)
    uint32_t GetGZCLSID() override;                          // slot 5, 0x4df3f0

    // cITSFloatConstants
    const char *GetName(int32_t index) override;
    float GetFloat(const char *name, float defaultValue, bool add) override;
    void SetFloat(const char *name, float value) override;
    const char *GetDescription(const char *name) override;
    void SetDescription(const char *name, const char *text) override;
    bool Has(const char *name) override;
    void Remove(const char *name) override;
    void Clear() override;
    void Load(void *segment, uint32_t instance) override;
    void Save(void *fileRef, uint32_t instance) override;
    bool IsDirty() override;
    void SetDirty(bool dirty) override;
    uint32_t DropReference() override;

    uint32_t mRefCount;                    // +4
    uint8_t mDirty;                        // +8 a byte, kept as it is (the slots 16/17 read and store whole bytes)
    uint8_t mPad9[3];
    std::vector<cTSFloatConstantPair> mEntries; // +0xc

private:
    cTSFloatConstantPair *Find(const char *name); // the first entry whose name equals (case-insensitively), or null
};
static_assert(sizeof(cTSFloatConstants) == 0x18, "cTSFloatConstants layout");
static_assert(offsetof(cTSFloatConstants, mRefCount) == 4, "reference count");
static_assert(offsetof(cTSFloatConstants, mDirty) == 8, "dirty byte");
static_assert(offsetof(cTSFloatConstants, mEntries) == 0xc, "entry vector");

// The RVA hooks' entry points (src/hook/tables/cTSFloatConstants.cpp): the entries original code calls directly (the
// constructor and destructor body of the stack objects, GetFloat from 66 sites, Remove, Load, Save). Each has the original's
// calling convention and calls ours without virtual dispatch.
struct cTSFloatConstantsAdapters {
    using Self = cTSFloatConstants;
    static Self *__fastcall Construct(void *memory, void *);
    static void __fastcall Destruct(Self *self, void *);
    static float __fastcall GetFloat(Self *self, void *, const char *name, float defaultValue, bool add);
    static void __fastcall Remove(Self *self, void *, const char *name);
    static void __fastcall Load(Self *self, void *, void *segment, uint32_t instance);
    static void __fastcall Save(Self *self, void *, void *fileRef, uint32_t instance);
};
