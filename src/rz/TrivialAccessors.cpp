#include "rz/TrivialAccessors.h"

namespace acc {

namespace {

template <typename T> T &At(void *self, uint32_t offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(self) + offset);
}

template <typename T> const T &At(const void *self, uint32_t offset)
{
    return *reinterpret_cast<const T *>(static_cast<const char *>(self) + offset);
}

} // namespace

void __fastcall DoNothing(void *, void *)
{
}

uint32_t __fastcall GetDword08(const void *self, void *)
{
    return At<uint32_t>(self, 0x8);
}

uint32_t __fastcall GetDword0C(const void *self, void *)
{
    return At<uint32_t>(self, 0xc);
}

void __fastcall SetDword0C(void *self, void *, uint32_t value)
{
    At<uint32_t>(self, 0xc) = value;
}

uint32_t __fastcall GetDword10(const void *self, void *)
{
    return At<uint32_t>(self, 0x10);
}

void __fastcall SetDword18(void *self, void *, uint32_t value)
{
    At<uint32_t>(self, 0x18) = value;
}

void __fastcall SetDword30(void *self, void *, uint32_t value)
{
    At<uint32_t>(self, 0x30) = value;
}

uint8_t __fastcall GetByte18(const void *self, void *)
{
    return At<uint8_t>(self, 0x18);
}

void __fastcall SetByte18(void *self, void *, uint32_t value)
{
    At<uint8_t>(self, 0x18) = static_cast<uint8_t>(value);
}

uint8_t __fastcall GetByte09(const void *self, void *)
{
    return At<uint8_t>(self, 0x9);
}

void __fastcall SetByte09(void *self, void *, uint32_t value)
{
    At<uint8_t>(self, 0x9) = static_cast<uint8_t>(value);
}

bool __fastcall ReturnTrue(const void *, void *)
{
    return true;
}

bool __fastcall ReturnTrueIgnoringArgument(const void *, void *, uint32_t)
{
    return true;
}

} // namespace acc
