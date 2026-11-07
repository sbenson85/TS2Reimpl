#pragma once
#include <cstdint>

// The AddRef and Release slots of classes built on cRZSystemService, which are tail jumps into the
// cRZUnknownMT part that sits 4 bytes after the cRZSystemService part (RVAs 1.18.0.187). Classes
// that have the service part at +0 (cGZTimerService) or at +4 (cGZRegistry, cGZClipboardManager,
// cRZCallbackSystemService) get one thunk per subobject vtable, each adjusting `this` by that
// subobject's offset first; the adjusting ones are shared by whichever classes have the same layout.
//
// All are __thiscall with no arguments: ecx = the subobject pointer the slot was called with. They
// are written __fastcall with a dummy edx (the same convention for a no-argument callee). None is
// hooked: they are one or two instructions and tail-call functions that are already hooked.
//
// "ServiceAt<N>" in the names is where the cRZSystemService part lies relative to the pointer given.
namespace svc {

// 0x1b9640 (also what 0x145248 and 0x1cc320 come to): ecx is the cRZSystemService part.
uint32_t __fastcall SystemServiceAddRef(void *service, void *edx);
// 0x1b967a (= cRZSystemService::Release; 0x1a85b8 and 0x1cd7a0 come to the same).
uint32_t __fastcall SystemServiceRelease(void *service, void *edx);

// ServiceAt+4: the pointer is a primary subobject at +0 and the service part follows it.
uint32_t __fastcall AddRefServiceAt4(void *self, void *edx);   // 0x145240
uint32_t __fastcall ReleaseServiceAt4(void *self, void *edx);  // 0x1a85b0
// ServiceAt0: the pointer is the service part itself.
uint32_t __fastcall AddRefServiceAt0(void *self, void *edx);   // 0x145248 and 0x1cc320 (tail jump to 0x1b9640)
uint32_t __fastcall ReleaseServiceAt0(void *self, void *edx);  // 0x1a85b8 and 0x1cd7a0
// ServiceAt-4: the pointer is the cRZUnknownMT part, 4 bytes after the service part.
uint32_t __fastcall AddRefServiceAtMinus4(void *self, void *edx);   // 0x145250 (from +8: sub 8, add 4), 0x1cc325 (from +4)
uint32_t __fastcall ReleaseServiceAtMinus4(void *self, void *edx);  // 0x1a85c0, 0x1cd7a5
// ServiceAt-0x1c: the pointer is the cIGZTimerService part of cGZTimerService.
uint32_t __fastcall AddRefServiceAtMinus1C(void *self, void *edx);   // 0x1cc32d
uint32_t __fastcall ReleaseServiceAtMinus1C(void *self, void *edx);  // 0x1cd7ad

} // namespace svc
