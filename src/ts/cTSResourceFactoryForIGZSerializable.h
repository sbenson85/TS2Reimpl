#pragma once
#include <cstddef>
#include <cstdint>

// cTSResourceFactoryForIGZSerializable: the generic persist factory (framework class 0xaba91d51, created by
// 0x1f6740 through COMDirectorData; cIGZPersistResourceFactory vtable 0xda6e90, 10 slots). A record's key type is the
// framework class id of the resource; the resource reads and writes itself through cIGZSerializable (0xe4fda3d4) on
// the record's cIGZIStream (0x51b11bd) / cIGZOStream (0xc51b11cb). Structure: lanes CU and DA (notes/feedback/DA.md).
// All members __thiscall, /O2. Not owned here (shared_vf, never hooked): slot 0 QueryInterface 0x1ee250 and slot 2
// Release 0x1ee310 (the same bodies sit in five factory vtables), slot 1 AddRef 0x148fb0 (verified), slot 8 0x1f62f0
// (cGZPersistResourceFactory's slot 8: slot 3 with a4 = a5 = 0).
class cTSResourceFactoryForIGZSerializable {
public:
    static constexpr uint32_t kVTable = 0xda6e90;
    static constexpr uint32_t kClassId = 0xaba91d51;
    static constexpr uint32_t kIdResource = 0x456b8f1d;     // asked of the created resource (cIGZPersistResource)
    static constexpr uint32_t kIdSerializable = 0xe4fda3d4; // cIGZSerializable
    static constexpr uint32_t kIdIStream = 0x051b11bd;      // cIGZIStream
    static constexpr uint32_t kIdOStream = 0xc51b11cb;      // cIGZOStream

    static cTSResourceFactoryForIGZSerializable *Create(); // 0x1f6740 (cdecl, not a Ghidra function): new 8, vtable, 0

    bool CreateInstance(void *record, uint32_t riid, void **out, uint32_t a4, uint32_t a5);       // 0x1f6310, slot 3 (ret 0x14)
    bool CreateInstanceByType(uint32_t type, uint32_t riid, void **out, uint32_t a4, uint32_t a5); // 0x1f63f0, slot 4 (ret 0x14)
    bool Read(void *resource, void *record);              // 0x1f6cf0, slot 5 (ret 8)
    bool Write(void *resource, void *record);             // 0x1f6f30, slot 6 (ret 8)
    void *DeletingDestructor(uint32_t flags);             // 0x1f6180, slot 7 (ret 4): delete(this, 8) when bit 0
    bool CreateInstanceByType3(uint32_t type, uint32_t riid, void **out); // 0x1f63d0, slot 9 (ret 0xc): slot 4 (.., 0, 0)

    const void *mVTable; // +0
    uint32_t mRefCount;  // +4 (Release 0x1ee310: below 2 calls slot 7 with 1)
};

static_assert(sizeof(cTSResourceFactoryForIGZSerializable) == 8, "new 8 in 0x1f6740");
static_assert(offsetof(cTSResourceFactoryForIGZSerializable, mRefCount) == 4);
