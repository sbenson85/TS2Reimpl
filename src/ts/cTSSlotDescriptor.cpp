#include "ts/cTSSlotDescriptor.h"
#include "game/vtable.h"
#include <xmmintrin.h>

namespace {
constexpr float kLegacyScale = 1.0f / 16.0f;
constexpr uint32_t kLegacyFlag200 = 0x200;
constexpr uint32_t kLegacyFlag400 = 0x400;

// cIGZIStream / cIGZOStream typed transfers, with their original slot numbering.
void ReadValue(void *stream, int slot, void *out)
{
    using Get = bool(__thiscall *)(void *, void *);
    VirtualSlot<Get>(stream, slot)(stream, out);
}
void WriteInteger(void *stream, int slot, uint32_t value)
{
    using Put = bool(__thiscall *)(void *, uint32_t);
    VirtualSlot<Put>(stream, slot)(stream, value);
}
void WriteFloat(void *stream, float value)
{
    using Put = bool(__thiscall *)(void *, float);
    VirtualCall<Put, 12>(stream, value);
}
bool HasError(void *stream)
{
    using Error = uint8_t(__thiscall *)(void *);
    return VirtualCall<Error, 18>(stream) != 0;
}
uint32_t ReadVersion(void *stream)
{
    using GetObject = void *(__thiscall *)(void *);
    using GetVersion = const uint32_t *(__thiscall *)(void *);
    void *owner = VirtualCall<GetObject, 20>(stream);
    return *VirtualCall<GetVersion, 78>(owner);
}
}

bool cTSSlotDescriptor::Read(void *stream)
{
    const uint32_t version = ReadVersion(stream);
    // ORIGINAL-BUG: failed integer getters still convert the reused stream-argument
    // stack slot. Initially it holds the stream address, then the last integer read.
    int32_t legacyInteger = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stream));
    if (version < 3) {
        ReadValue(stream, 12, &mField0C);
        ReadValue(stream, 12, &mField10);
        mField0C *= kLegacyScale;
        mField10 *= kLegacyScale;
        if (version == 0) {
            // CVTTSS2SI yields INT_MIN for NaN/out-of-range, rather than C++ UB.
            legacyInteger = static_cast<uint16_t>(_mm_cvtt_ss2si(_mm_set_ss(mField14)));
            ReadValue(stream, 6, &legacyInteger);
            mField14 = static_cast<float>(static_cast<int16_t>(legacyInteger));
        } else {
            ReadValue(stream, 12, &mField14);
        }
        ReadValue(stream, 6, &mField08);
        if (version == 2) ReadValue(stream, 6, &legacyInteger); // obsolete word
        mField2C = mField30 = mField34 = mFlags = 0;
    } else {
        ReadValue(stream, 6, &mField08);
        ReadValue(stream, 12, &mField0C);
        ReadValue(stream, 12, &mField10);
        if (version < 0x47) {
            mField0C *= kLegacyScale;
            mField10 *= kLegacyScale;
        }
        ReadValue(stream, 12, &mField14);
        ReadValue(stream, 8, &mField2C);
        ReadValue(stream, 8, &mField30);
        ReadValue(stream, 8, &mField34);
        ReadValue(stream, 8, &mFlags);
        if (version >= 4) ReadValue(stream, 8, &mField3C);
        if (version >= 5) {
            if (version < 0x43) {
                const float scale = version < 10 ? 1.0f : kLegacyScale;
                ReadValue(stream, 8, &legacyInteger);
                mField40 = static_cast<float>(legacyInteger) * scale;
                ReadValue(stream, 8, &legacyInteger);
                mField44 = static_cast<float>(legacyInteger) * scale;
                ReadValue(stream, 8, &legacyInteger);
                mField48 = static_cast<float>(legacyInteger) * scale;
            } else {
                ReadValue(stream, 12, &mField40);
                ReadValue(stream, 12, &mField44);
                ReadValue(stream, 12, &mField48);
            }
            ReadValue(stream, 8, &mField20);
            ReadValue(stream, 8, &mField24);
        }
        if (version >= 7) ReadValue(stream, 12, &mField4C);
        if (version >= 8) ReadValue(stream, 8, &mField28);
        if (version >= 9) ReadValue(stream, 8, &mField50);
        if (version >= 10) {
            if (version < 0x47) {
                ReadValue(stream, 8, &legacyInteger);
                mField54 = static_cast<float>(legacyInteger) * kLegacyScale;
            } else {
                ReadValue(stream, 12, &mField54);
            }
        }
    }
    if (version < 9) {
        if (mFlags & kLegacyFlag200) {
            mField50 = -3;
            mFlags &= ~kLegacyFlag200;
        } else if (mFlags & kLegacyFlag400) {
            mField50 = -1;
            mFlags &= ~kLegacyFlag400;
        }
    }
    // Only formats below 10 rescale this field, even though other migrations last longer.
    if (version < 10) mField4C *= kLegacyScale;
    if (version >= 0x40) ReadValue(stream, 8, &mField18);
    else mField18 = 0;
    if (version >= 0x41) ReadValue(stream, 8, &mField1C);
    else mField1C = -1;
    return !HasError(stream);
}

bool cTSSlotDescriptor::Write(void *stream)
{
    WriteInteger(stream, 6, mField08);
    WriteFloat(stream, mField0C);
    WriteFloat(stream, mField10);
    WriteFloat(stream, mField14);
    WriteInteger(stream, 8, mField2C);
    WriteInteger(stream, 8, mField30);
    WriteInteger(stream, 8, mField34);
    WriteInteger(stream, 8, mFlags);
    WriteInteger(stream, 8, mField3C);
    WriteFloat(stream, mField40);
    WriteFloat(stream, mField44);
    WriteFloat(stream, mField48);
    WriteInteger(stream, 8, mField20);
    WriteInteger(stream, 8, mField24);
    WriteFloat(stream, mField4C);
    WriteInteger(stream, 8, mField28);
    WriteInteger(stream, 8, mField50);
    WriteFloat(stream, mField54);
    WriteInteger(stream, 8, mField18);
    WriteInteger(stream, 8, mField1C);
    return !HasError(stream);
}
