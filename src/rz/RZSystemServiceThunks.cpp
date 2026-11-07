#include "rz/RZSystemServiceThunks.h"

#include "rz/cRZRefCount.h"

namespace svc {

namespace {

cRZUnknownMT *UnknownOf(void *service)
{
    return reinterpret_cast<cRZUnknownMT *>(static_cast<char *>(service) + 4);
}

void *Offset(void *p, int32_t bytes)
{
    return static_cast<char *>(p) + bytes;
}

} // namespace

uint32_t __fastcall SystemServiceAddRef(void *service, void *)
{
    return UnknownOf(service)->AddRef();
}

uint32_t __fastcall SystemServiceRelease(void *service, void *)
{
    return UnknownOf(service)->Release();
}

uint32_t __fastcall AddRefServiceAt4(void *self, void *)
{
    return SystemServiceAddRef(Offset(self, 4), nullptr);
}

uint32_t __fastcall ReleaseServiceAt4(void *self, void *)
{
    return SystemServiceRelease(Offset(self, 4), nullptr);
}

uint32_t __fastcall AddRefServiceAt0(void *self, void *)
{
    return SystemServiceAddRef(self, nullptr);
}

uint32_t __fastcall ReleaseServiceAt0(void *self, void *)
{
    return SystemServiceRelease(self, nullptr);
}

uint32_t __fastcall AddRefServiceAtMinus4(void *self, void *)
{
    return SystemServiceAddRef(Offset(self, -4), nullptr);
}

uint32_t __fastcall ReleaseServiceAtMinus4(void *self, void *)
{
    return SystemServiceRelease(Offset(self, -4), nullptr);
}

uint32_t __fastcall AddRefServiceAtMinus1C(void *self, void *)
{
    return SystemServiceAddRef(Offset(self, -0x1c), nullptr);
}

uint32_t __fastcall ReleaseServiceAtMinus1C(void *self, void *)
{
    return SystemServiceRelease(Offset(self, -0x1c), nullptr);
}

} // namespace svc
