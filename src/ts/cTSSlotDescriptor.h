#pragma once
#include <cstddef>
#include <cstdint>

// cITSSlotDescriptor / cIGZSerializable at +0 (vtable RVA 0xdcd6d8).
// The deleting destructor at 0x4daeb0 confirms the 0x58-byte allocation.
// Field meanings beyond the reference count and flag bits are not yet established.
class cTSSlotDescriptor {
public:
    bool Read(void *stream);  // 0x4dc020: load and migrate older slot formats
    bool Write(void *stream); // 0x4dcae0: save the current slot format

    const void *mVTable; // +0x00
    uint32_t mRefCount;  // +0x04
    uint16_t mField08;   // +0x08
    uint8_t mPadding0A[2]; // +0x0a: untouched by serialization
    float mField0C;      // +0x0c
    float mField10;      // +0x10
    float mField14;      // +0x14
    uint32_t mField18;   // +0x18
    int32_t mField1C;    // +0x1c
    uint32_t mField20;   // +0x20
    uint32_t mField24;   // +0x24
    uint32_t mField28;   // +0x28
    uint32_t mField2C;   // +0x2c
    uint32_t mField30;   // +0x30
    uint32_t mField34;   // +0x34
    uint32_t mFlags;     // +0x38: old bits 0x200/0x400 migrate to +0x50
    uint32_t mField3C;   // +0x3c
    float mField40;      // +0x40
    float mField44;      // +0x44
    float mField48;      // +0x48
    float mField4C;      // +0x4c
    int32_t mField50;    // +0x50
    float mField54;      // +0x54
};

static_assert(sizeof(cTSSlotDescriptor) == 0x58);
#define SLOT_FIELD_OFFSET(field, offset) static_assert(offsetof(cTSSlotDescriptor, field) == offset)
SLOT_FIELD_OFFSET(mVTable, 0x00);
SLOT_FIELD_OFFSET(mRefCount, 0x04);
SLOT_FIELD_OFFSET(mField08, 0x08);
SLOT_FIELD_OFFSET(mPadding0A, 0x0a);
SLOT_FIELD_OFFSET(mField0C, 0x0c);
SLOT_FIELD_OFFSET(mField10, 0x10);
SLOT_FIELD_OFFSET(mField14, 0x14);
SLOT_FIELD_OFFSET(mField18, 0x18);
SLOT_FIELD_OFFSET(mField1C, 0x1c);
SLOT_FIELD_OFFSET(mField20, 0x20);
SLOT_FIELD_OFFSET(mField24, 0x24);
SLOT_FIELD_OFFSET(mField28, 0x28);
SLOT_FIELD_OFFSET(mField2C, 0x2c);
SLOT_FIELD_OFFSET(mField30, 0x30);
SLOT_FIELD_OFFSET(mField34, 0x34);
SLOT_FIELD_OFFSET(mFlags, 0x38);
SLOT_FIELD_OFFSET(mField3C, 0x3c);
SLOT_FIELD_OFFSET(mField40, 0x40);
SLOT_FIELD_OFFSET(mField44, 0x44);
SLOT_FIELD_OFFSET(mField48, 0x48);
SLOT_FIELD_OFFSET(mField4C, 0x4c);
SLOT_FIELD_OFFSET(mField50, 0x50);
SLOT_FIELD_OFFSET(mField54, 0x54);
#undef SLOT_FIELD_OFFSET
