#include "sg/cGeometryDataContainer.h"

#include "game/vtable.h"
#include "gz/cSGResource.h"
#include "rz/RZFrameworkHelpers.h"

#include <cstring>
#include <initializer_list>

namespace nGZSceneGraph {

namespace {

// Reads or writes a field at `off` bytes from the subobject.
template <typename T> T &Field(const void *self, uint32_t off)
{
    return *reinterpret_cast<T *>(reinterpret_cast<uint8_t *>(const_cast<void *>(self)) + off);
}

// The record at `index` of a vector whose begin pointer is at `beginOff` and whose element size is `stride`.
uint8_t *Record(const void *self, uint32_t beginOff, uint32_t index, uint32_t stride)
{
    return reinterpret_cast<uint8_t *>(Field<uint32_t>(self, beginOff)) + index * stride;
}

// (end - begin) of a vector, as the original's 32-bit signed value (the subtraction wraps).
int32_t Span(const void *self, uint32_t beginOff, uint32_t endOff)
{
    return static_cast<int32_t>(Field<uint32_t>(self, endOff) - Field<uint32_t>(self, beginOff));
}

// The c_str() of the MSVC std::string at `s`: the inline buffer when the capacity (dword at +0x14) is at most 15,
// otherwise the heap pointer stored in the string's first dword. Same code the game inlines.
const char *CStr(uint8_t *s)
{
    if (*reinterpret_cast<uint32_t *>(s + 0x14) > 15) return *reinterpret_cast<const char *const *>(s);
    return reinterpret_cast<const char *>(s);
}

} // namespace

int32_t cIGeometryDataContainerSub::CountRecordsA()
{
    // The original divides by 48 with a magic multiply (0x2aaaaaab, shift 3, no dividend add).
    return Span(this, 0x04, 0x08) / 48;
}

int32_t cIGeometryDataContainerSub::CountRecordsB()
{
    return Span(this, 0x10, 0x14) / 60;
}

int32_t cIGeometryDataContainerSub::CountRecordsD()
{
    return Span(this, 0x34, 0x38) / 48;
}

int32_t cIGeometryDataContainerSub::CountElements28()
{
    return Span(this, 0x28, 0x2c) / 28;
}

int32_t cIGeometryDataContainerSub::CountRecordsC()
{
    return Span(this, 0x1c, 0x20) / 76;
}

uint8_t cIGeometryDataContainerSub::RecordHasWords(uint32_t index, uint32_t which)
{
    uint8_t *rec = Record(this, 0x10, index, 60);
    if (which > 3) return 0;
    if (which == 3) return Field<uint32_t>(rec, 0x08) > 0 ? 1 : 0;
    uint32_t off = which == 0 ? 0x18 : which == 1 ? 0x24 : 0x30;
    if (Field<uint32_t>(rec, off) == Field<uint32_t>(rec, off + 4)) return 0;
    return (Span(rec, off, off + 4) >> 1) != 0 ? 1 : 0;
}

const char *cIGeometryDataContainerSub::RecordStringAt18(uint32_t index)
{
    return CStr(Record(this, 0x34, index, 48) + 0x18);
}

const char *cIGeometryDataContainerSub::RecordStringAt00(uint32_t index)
{
    return CStr(Record(this, 0x34, index, 48));
}

void *cIGeometryDataContainerSub::CopyHeaderB(void *out, uint32_t index)
{
    uint8_t *rec = Record(this, 0x10, index, 60);
    std::memcpy(out, rec, 8);
    std::memcpy(static_cast<uint8_t *>(out) + 8, rec + 8, 4);
    return out;
}

void *cIGeometryDataContainerSub::CopyRecordC(void *out, uint32_t index)
{
    uint8_t *rec = Record(this, 0x1c, index, 76);
    uint8_t *o = static_cast<uint8_t *>(out);
    std::memcpy(o, rec, 16);
    std::memcpy(o + 0x10, rec + 0x10, 8);
    std::memcpy(o + 0x18, rec + 0x18, 4);
    // Written last, after the copies above, so it replaces the copied bytes at out+0x14.
    const char *s = CStr(rec + 0x1c);
    std::memcpy(o + 0x14, &s, 4);
    return out;
}

void *cIGeometryDataContainerSub::CopyRecordA24(void *out, uint32_t index)
{
    std::memcpy(out, Record(this, 0x04, index, 48), 24);
    return out;
}

void cIGeometryDataContainerSub::SetRecordADword(uint32_t index, uint32_t value)
{
    Field<uint32_t>(Record(this, 0x04, index, 48), 0x14) = value;
}

void cIGeometryDataContainerSub::SetRecordBWord(uint32_t index, uint32_t element, uint16_t value)
{
    uint8_t *words = reinterpret_cast<uint8_t *>(Field<uint32_t>(Record(this, 0x10, index, 60), 0x0c));
    reinterpret_cast<uint16_t *>(words)[element] = value;
}

void cIGeometryDataContainerSub::SetRecordBAndTrim(uint32_t index, uint32_t value)
{
    uint8_t *rec = Record(this, 0x10, index, 60);
    Field<uint32_t>(rec, 0x08) = value;
    // Each uint16 vector's end is set to its begin when they differ (an equal pair is left alone).
    for (uint32_t off : {0x18u, 0x24u, 0x30u}) {
        uint32_t begin = Field<uint32_t>(rec, off);
        if (begin != Field<uint32_t>(rec, off + 4)) Field<uint32_t>(rec, off + 4) = begin;
    }
}

void *cIGeometryDataContainerSub::CopyElement28(void *out, uint32_t index)
{
    std::memcpy(out, Record(this, 0x28, index, 28), 28);
    return out;
}

namespace {

// Sets the end of the word vector whose begin pointer is at `off` of record `index` back to its begin.
__forceinline void TrimEnd(const void *self, uint32_t index, uint32_t off)
{
    uint8_t *rec = Record(self, 0x10, index, 60);
    uint32_t begin = Field<uint32_t>(rec, off);
    if (begin != Field<uint32_t>(rec, off + 4)) Field<uint32_t>(rec, off + 4) = begin;
}

} // namespace

void cIGeometryDataContainerSub::TrimWordVector(uint32_t index, uint32_t which)
{
    // Only 0, 1 and 2 store anything; the original's default path (which > 2) returns without a write.
    // Each case carries its own copy of the code, with its offset as a constant.
    switch (which) {
    case 0: TrimEnd(this, index, 0x18); break;
    case 1: TrimEnd(this, index, 0x24); break;
    case 2: TrimEnd(this, index, 0x30); break;
    default: break;
    }
}

void cIGeometryDataContainerSub::SetRecordCDword(uint32_t index, uint32_t a, uint32_t b, uint32_t value)
{
    // The original calls slot 21 of [this] first; the record is read after the call, as the original does.
    VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), 21>(this, index, a, b);
    Field<uint32_t>(Record(this, 0x1c, index, 76), 0x10) = value;
}

namespace {

// The cSGResource secondary subobject that a subobject thunk passes to its body: `sub ecx,[ecx-4]; sub ecx,0x50`.
nGZSceneGraph::cSGResourceSubobject *ThunkBody(void *self)
{
    uintptr_t p = reinterpret_cast<uintptr_t>(self);
    p -= *reinterpret_cast<const uint32_t *>(p - 4);
    p -= 0x50;
    return reinterpret_cast<cSGResourceSubobject *>(p);
}

} // namespace

int32_t __fastcall VbThunkCompareIdentity(void *self, void *, void *other)
{
    return ThunkBody(self)->CompareIdentity(other);
}

void *__fastcall VbThunkGetVirtualBase(void *self, void *, void *arg)
{
    return ThunkBody(self)->GetVirtualBase(arg);
}

void *__fastcall VbThunkCopyKeyTo(void *self, void *, void *out)
{
    return ThunkBody(self)->CopyKeyTo(out);
}

void __fastcall VbThunkSetFlag(void *self, void *, uint8_t value)
{
    ThunkBody(self)->SetFlag(value);
}

uint8_t __fastcall VbThunkSetResource(void *self, void *, void *resource)
{
    return ThunkBody(self)->SetResource(resource) ? 1 : 0;
}

} // namespace nGZSceneGraph
