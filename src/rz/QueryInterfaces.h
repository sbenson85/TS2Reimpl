#pragma once
#include <cstdint>

// QueryInterface implementations (slot 0 of their classes; RVAs 1.18.0.187). Each original is
// __thiscall, `ret 8`: ecx = this, then riid and ppv on the stack. These are written as
// __fastcall with a dummy edx, which passes this in ecx, riid and ppv on the stack, and pops them
// on return, the same as __thiscall. That makes them plain functions a hook entry can point at.
//
// Results are bool, which the original returns in al as 0 or 1 (the upper bits of eax are not
// defined). Tests compare al only. Every function AddRefs `this` through its own vtable (slot 1)
// when it stores a pointer, and stores the pointer with the offset given per function.
//
// The classes are named after the original's functions. None of them has a header in src/ yet, so
// the functions take `this` as void *. The cTSWinProc* family falls through to cRZWinProc4 (below)
// for any id it doesn't handle, as the original tail-calls it.

namespace qi {

// 0xb6e020 (cRZWinProc4): riid 1 stores this with no null check; riid 0x22e85d8e checks ppv;
// anything else returns false.
bool __fastcall WinProc4QueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

// 0x1b9650 (cRZSystemService): riid 0x287fb697 stores this; anything else tail-calls
// RZUnknownQueryInterface::QueryInterfaceUnknown on this + 4.
bool __fastcall SystemServiceQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

// 0x747105 (cTSWinProcGrid): null ppv fails; riid 0x2c69749d stores this + 8, 0xe98b2f57 stores
// this + 0xc; anything else goes to cRZWinProc4.
bool __fastcall TSWinProcGridQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

// 0x9325d8 (cGZWin): riid 1, 0x22ba0121 and 0xe98b2f57 store this; anything else returns false.
bool __fastcall GZWinQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

// 0x749010 (cTSWinProcPopup): null ppv fails; riid 0xacc9d81c stores this + 8; else cRZWinProc4.
bool __fastcall TSWinProcPopupQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

// 0x7c39bb (cTSWinProcBaseObjectDialog): null ppv fails; riid 0x797ea13 stores this + 0xc; else
// cRZWinProc4.
bool __fastcall TSWinProcBaseObjectDialogQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

// 0x672a95 (cTSWinProcUCPSubPanel): null ppv fails; riid 0xcbb39432 stores this + 8; else cRZWinProc4.
bool __fastcall TSWinProcUCPSubPanelQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

// 0x6487b9 (cTSWinProcOptionsSubPanel): null ppv fails; riid 0x6c9a93a2 stores this + 8; else
// cRZWinProc4.
bool __fastcall TSWinProcOptionsSubPanelQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

// 0x75dc5b (cTSWinProcGridThumbnail): riid 0x11670ab9 with a non-null ppv stores this; everything
// else (including a null ppv) goes to cRZWinProc4.
bool __fastcall TSWinProcGridThumbnailQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

// 0x75f340 (cTSUIService): null ppv fails; riid 0x0bd09a11 stores this; else false.
bool __fastcall TSUIServiceQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

// 0xb7bbe0 (nTSCASCore::cTSSkinBase): riid 1, 0x456b8f1d, 0x8c1571d6 store this; 0x51f85085 stores
// this + 8; 0xb1096a8c stores this + 4; anything else returns false. No null check on ppv.
bool __fastcall TSSkinBaseQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

// 0x7c741f (cTSWinProcSimEditorDialog): adds 0x320 to this and tail-jumps to the base dialog's
// QueryInterface (cTSWinProcBaseObjectDialogQueryInterface above).
bool __fastcall TSWinProcSimEditorDialogQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

} // namespace qi
