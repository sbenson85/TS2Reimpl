#include "rz/QueryInterfaces.h"

#include "game/vtable.h"
#include "rz/RZFrameworkHelpers.h"

namespace {

// AddRef through the object's own vtable (slot 1), as every original here does.
void AddRef(void *object)
{
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(object);
}

// The original's `neg/sbb/and` mask: object + offset when the object is non-null, otherwise zero.
void *OffsetOrNull(void *object, uint32_t offset)
{
    return object != nullptr ? static_cast<uint8_t *>(object) + offset : nullptr;
}

// Stores an interface pointer (this + offset) through ppv and AddRefs this. Callers have already
// checked ppv for null where the original does.
bool StoreInterface(void *self, uint32_t offset, void **ppv)
{
    *ppv = OffsetOrNull(self, offset);
    AddRef(self);
    return true;
}

// Stores this through ppv and AddRefs it.
bool StoreThis(void *self, void **ppv)
{
    *ppv = self;
    AddRef(self);
    return true;
}

// cRZWinProc4's QueryInterface, reached from the cTSWinProc* family as a tail call (the original
// passes this and the same arguments).
bool WinProc4Fallthrough(void *self, uint32_t riid, void **ppv)
{
    return qi::WinProc4QueryInterface(self, nullptr, riid, ppv);
}

} // namespace

bool __fastcall qi::WinProc4QueryInterface(void *self, void * /*edx*/, uint32_t riid, void **ppv)
{
    // No null check (callers must pass ppv): riid 1 stores through ppv without a null check, so a null ppv faults. Only the
    // second id checks for null.
    if (riid == 1) return StoreThis(self, ppv);
    if (riid == 0x22e85d8e) {
        if (ppv == nullptr) return false;
        return StoreThis(self, ppv);
    }
    return false;
}

bool __fastcall qi::SystemServiceQueryInterface(void *self, void * /*edx*/, uint32_t riid, void **ppv)
{
    // No null check (callers must pass ppv): riid 0x287fb697 stores through ppv without a null check, so a null ppv faults.
    if (riid == 0x287fb697) return StoreThis(self, ppv);
    // Other ids go to the cRZUnknown-style subobject at this + 4 (tail jump in the original).
    auto *unknown = reinterpret_cast<RZUnknownQueryInterface *>(static_cast<uint8_t *>(self) + 4);
    return unknown->QueryInterfaceUnknown(riid, ppv);
}

bool __fastcall qi::TSWinProcGridQueryInterface(void *self, void * /*edx*/, uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid == 0x2c69749d) return StoreInterface(self, 8, ppv);
    if (riid == 0xe98b2f57) return StoreInterface(self, 0xc, ppv);
    return WinProc4Fallthrough(self, riid, ppv);
}

bool __fastcall qi::GZWinQueryInterface(void *self, void * /*edx*/, uint32_t riid, void **ppv)
{
    // No null check (callers must pass ppv): the matching ids store through ppv without a null check, so a null ppv faults.
    if (riid == 1 || riid == 0x22ba0121 || riid == 0xe98b2f57) return StoreThis(self, ppv);
    return false;
}

bool __fastcall qi::TSWinProcPopupQueryInterface(void *self, void * /*edx*/, uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid == 0xacc9d81c) return StoreInterface(self, 8, ppv);
    return WinProc4Fallthrough(self, riid, ppv);
}

bool __fastcall qi::TSWinProcBaseObjectDialogQueryInterface(void *self, void * /*edx*/, uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid == 0x797ea13) return StoreInterface(self, 0xc, ppv);
    return WinProc4Fallthrough(self, riid, ppv);
}

bool __fastcall qi::TSWinProcUCPSubPanelQueryInterface(void *self, void * /*edx*/, uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid == 0xcbb39432) return StoreInterface(self, 8, ppv);
    return WinProc4Fallthrough(self, riid, ppv);
}

bool __fastcall qi::TSWinProcOptionsSubPanelQueryInterface(void *self, void * /*edx*/, uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid == 0x6c9a93a2) return StoreInterface(self, 8, ppv);
    return WinProc4Fallthrough(self, riid, ppv);
}

bool __fastcall qi::TSWinProcGridThumbnailQueryInterface(void *self, void * /*edx*/, uint32_t riid, void **ppv)
{
    // A null ppv does not stop the fall-through: the original sends it to cRZWinProc4 as well. For
    // riid 1 that faults there (see WinProc4QueryInterface).
    if (ppv != nullptr && riid == 0x11670ab9) return StoreThis(self, ppv);
    return WinProc4Fallthrough(self, riid, ppv);
}

bool __fastcall qi::TSUIServiceQueryInterface(void *self, void * /*edx*/, uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid != 0x0bd09a11) return false;
    return StoreThis(self, ppv);
}

bool __fastcall qi::TSSkinBaseQueryInterface(void *self, void * /*edx*/, uint32_t riid, void **ppv)
{
    uint32_t offset;
    switch (riid) {
    case 1:
    case 0x456b8f1d:
    case 0x8c1571d6:
        offset = 0;
        break;
    case 0x51f85085:
        offset = 8;
        break;
    case 0xb1096a8c:
        offset = 4;
        break;
    default:
        return false;
    }
    // No null check (callers must pass ppv): the matching ids store through ppv without a null check, so a null ppv faults.
    return StoreInterface(self, offset, ppv);
}

bool __fastcall qi::TSWinProcSimEditorDialogQueryInterface(void *self, void * /*edx*/, uint32_t riid, void **ppv)
{
    return TSWinProcBaseObjectDialogQueryInterface(static_cast<uint8_t *>(self) + 0x320, nullptr, riid, ppv);
}
