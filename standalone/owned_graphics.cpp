// The graphics layer's image statics for render_object (milestone O, lane O-c; docs/subsystems/scenegraph.md,
// "Milestone O"). Same pattern as owned_statics.cpp (docs/standalone-shape.md, "Owned statics"): each symbol here is the
// image's object in hooked builds (tools/origsyms.py) and this file's definition in the image-free program. Kept apart
// from owned_statics.cpp so gmdc_decode does not link the window, device and D3DX pieces.

#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <d3d9.h>

#include "game/import_slots.h"
#include "gz/cEventFaces.h"
#include "gz/cGZBuffer.h"
#include "gz/cRZCursor.h"
#include "gz/cEventSystemService.h"
#include "rz/MiscHelpersA.h"
#include "gz/GraphicServices.h"
#include "gz/cDeviceResourceManagerDX.h"
#include "gz/cStateBlockDX.h"
#include "gz/cVertexFormat.h"
#include "gz/cShaderProgramsDX.h"
#include "gz/cSurfaceDX.h"
#include "gz/MultiMon.h"
#include "gz/cGZGraphicSystem.h"
#include "gz/GraphicModeSearch.h"
#include "gz/cGraphicSystemDX.h"
#include "gz/cCanvasW32.h"
#include "gz/cGZCanvas.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceEnumDX.h"
#include "gz/cDeviceStateDXDetail.h"
#include "rz/cRZString.h"

// D3DX9 and DxErr9 (DirectX 9.0 SDK Update, Summer 2004: the library the game links statically, db/library-matches.jsonl):
// our code calls them through OriginalFunction at the game's copy; the image-free program links the SDK's libraries
// (CMakeLists.txt, render_object) and sends each entry to the library's function. 0x1546d and 0x1c0e1 are byte-identical
// wrappers; their callees (D3DXOptimizeVerticesEx / D3DXOptimizeFacesEx) tell them apart.
#pragma comment(linker, "/alternatename:??$OriginalEntry@$0BFEGN@@@YAXXZ=_D3DXOptimizeVertices@20")
#pragma comment(linker, "/alternatename:??$OriginalEntry@$0BMAOB@@@YAXXZ=_D3DXOptimizeFaces@20")
#pragma comment(linker, "/alternatename:??$OriginalEntry@$0BMAIH@@@YAXXZ=_D3DXCreateMeshFVF@24")
#pragma comment(linker, "/alternatename:??$OriginalEntry@$0BNFHM@@@YAXXZ=_D3DXLoadSurfaceFromMemory@40")
#pragma comment(linker, "/alternatename:??$OriginalEntry@$0BNHDO@@@YAXXZ=_D3DXSaveSurfaceToFileA@20")
#pragma comment(linker, "/alternatename:??$OriginalEntry@$0BNIIE@@@YAXXZ=_D3DXComputeNormalMap@24")
#pragma comment(linker, "/alternatename:??$OriginalEntry@$0BOCFG@@@YAXXZ=_D3DXLoadSurfaceFromFileInMemory@36")
#pragma comment(linker, "/alternatename:??$OriginalEntry@$0BODBA@@@YAXXZ=_D3DXLoadSurfaceFromSurface@32")
#pragma comment(linker, "/alternatename:??$OriginalEntry@$0BOFKM@@@YAXXZ=_D3DXFilterTexture@16")
#pragma comment(linker, "/alternatename:??$OriginalEntry@$0BOIJO@@@YAXXZ=_D3DXAssembleShader@28")
#pragma comment(linker, "/alternatename:??$OriginalEntry@$0BOJIB@@@YAXXZ=_D3DXCompileShader@40")
#pragma comment(linker, "/alternatename:??$OriginalEntry@$0BOMKI@@@YAXXZ=_D3DXGetShaderConstantTable@8")
#pragma comment(linker, "/alternatename:??$OriginalEntry@$0CBGIP@@@YAXXZ=_D3DXDisassembleShader@16")
#pragma comment(linker, "/alternatename:??$OriginalEntry@$0CCCCA@@@YAXXZ=_D3DXGatherFragments@28")
#pragma comment(linker, "/alternatename:??$OriginalEntry@$0CIMAM@@@YAXXZ=_D3DXCreateFragmentLinker@12")
#pragma comment(linker, "/alternatename:??$OriginalEntry@$0NFGGD@@@YAXXZ=_DXGetErrorString9A@4")

// The import slots our graphics code calls through (game/import_slots.h), each holding the function this program's
// own import table resolves.
TS2_OWNED_STATIC(void *, d9a0a8) = reinterpret_cast<void *>(::DeleteObject);
TS2_OWNED_STATIC(void *, d9a0ac) = reinterpret_cast<void *>(::CreateSolidBrush);
TS2_OWNED_STATIC(void *, d9a0b0) = reinterpret_cast<void *>(::GetDCOrgEx);
TS2_OWNED_STATIC(void *, d9a0b4) = reinterpret_cast<void *>(::GetClipBox);
TS2_OWNED_STATIC(void *, d9a0b8) = reinterpret_cast<void *>(::DeleteDC);
TS2_OWNED_STATIC(void *, d9a0bc) = reinterpret_cast<void *>(::CreateDCA);
TS2_OWNED_STATIC(void *, d9a0c0) = reinterpret_cast<void *>(::GetDeviceCaps);
TS2_OWNED_STATIC(void *, d9a114) = reinterpret_cast<void *>(::GetModuleHandleA);
TS2_OWNED_STATIC(void *, d9a124) = reinterpret_cast<void *>(::GetProcAddress);
TS2_OWNED_STATIC(void *, d9a138) = reinterpret_cast<void *>(::GetVersionExA);
TS2_OWNED_STATIC(void *, d9a13c) = reinterpret_cast<void *>(::lstrcpyA);
TS2_OWNED_STATIC(void *, d9a140) = reinterpret_cast<void *>(::SetLastError);
TS2_OWNED_STATIC(void *, d9a144) = reinterpret_cast<void *>(::Sleep);
TS2_OWNED_STATIC(void *, d9a148) = reinterpret_cast<void *>(::EnumResourceNamesA);
TS2_OWNED_STATIC(void *, d9a14c) = reinterpret_cast<void *>(::SetThreadExecutionState);
TS2_OWNED_STATIC(void *, d9a150) = reinterpret_cast<void *>(::GetLocaleInfoA);
TS2_OWNED_STATIC(void *, d9a10c) = reinterpret_cast<void *>(::SetEvent);
TS2_OWNED_STATIC(void *, d9a17c) = reinterpret_cast<void *>(::CloseHandle);
TS2_OWNED_STATIC(void *, d9a184) = reinterpret_cast<void *>(::WaitForSingleObject);
TS2_OWNED_STATIC(void *, d9a1e4) = reinterpret_cast<void *>(::VirtualAlloc);
TS2_OWNED_STATIC(void *, d9a1e8) = reinterpret_cast<void *>(::VirtualFree);
TS2_OWNED_STATIC(void *, d9a244) = reinterpret_cast<void *>(::CreateEventA);
TS2_OWNED_STATIC(void *, d9a3d8) = reinterpret_cast<void *>(::ResetEvent);
TS2_OWNED_STATIC(void *, d9a3dc) = reinterpret_cast<void *>(::PulseEvent);
TS2_OWNED_STATIC(void *, d9a49c) = reinterpret_cast<void *>(::ScreenToClient);
TS2_OWNED_STATIC(void *, d9a4a0) = reinterpret_cast<void *>(::IsIconic);
TS2_OWNED_STATIC(void *, d9a4a4) = reinterpret_cast<void *>(::GetSystemMetrics);
TS2_OWNED_STATIC(void *, d9a4a8) = reinterpret_cast<void *>(::GetWindowRect);
TS2_OWNED_STATIC(void *, d9a4d0) = reinterpret_cast<void *>(::GetCursorPos);
TS2_OWNED_STATIC(void *, d9a4e8) = reinterpret_cast<void *>(::IntersectRect);
TS2_OWNED_STATIC(void *, d9a4ec) = reinterpret_cast<void *>(::OffsetRect);
TS2_OWNED_STATIC(void *, d9a50c) = reinterpret_cast<void *>(::GetWindowPlacement);
TS2_OWNED_STATIC(void *, d9a510) = reinterpret_cast<void *>(::SetWindowPos);
TS2_OWNED_STATIC(void *, d9a514) = reinterpret_cast<void *>(::ReleaseDC);
TS2_OWNED_STATIC(void *, d9a518) = reinterpret_cast<void *>(::GetDC);
TS2_OWNED_STATIC(void *, d9a51c) = reinterpret_cast<void *>(::SystemParametersInfoA);
TS2_OWNED_STATIC(void *, d9a520) = reinterpret_cast<void *>(::GetKeyboardLayout);
TS2_OWNED_STATIC(void *, d9a524) = reinterpret_cast<void *>(::DefWindowProcA);
TS2_OWNED_STATIC(void *, d9a528) = reinterpret_cast<void *>(::UnregisterClassA);
TS2_OWNED_STATIC(void *, d9a52c) = reinterpret_cast<void *>(::RegisterClassExA);
TS2_OWNED_STATIC(void *, d9a530) = reinterpret_cast<void *>(::CreateWindowExA);
TS2_OWNED_STATIC(void *, d9a534) = reinterpret_cast<void *>(::DestroyWindow);
TS2_OWNED_STATIC(void *, d9a538) = reinterpret_cast<void *>(::ShowWindow);
TS2_OWNED_STATIC(void *, d9a53c) = reinterpret_cast<void *>(::MoveWindow);
TS2_OWNED_STATIC(void *, d9a540) = reinterpret_cast<void *>(::IsZoomed);
TS2_OWNED_STATIC(void *, d9a544) = reinterpret_cast<void *>(::GetActiveWindow);
TS2_OWNED_STATIC(void *, d9a548) = reinterpret_cast<void *>(::GetAsyncKeyState);
TS2_OWNED_STATIC(void *, d9a54c) = reinterpret_cast<void *>(::SetCapture);
TS2_OWNED_STATIC(void *, d9a550) = reinterpret_cast<void *>(::ReleaseCapture);
TS2_OWNED_STATIC(void *, d9a554) = reinterpret_cast<void *>(::SetTimer);
TS2_OWNED_STATIC(void *, d9a558) = reinterpret_cast<void *>(::KillTimer);
TS2_OWNED_STATIC(void *, d9a55c) = reinterpret_cast<void *>(::UpdateWindow);
TS2_OWNED_STATIC(void *, d9a560) = reinterpret_cast<void *>(::SetActiveWindow);
// d9a568 (MessageBoxA): standalone/owned_statics.cpp (cRZStd's message box; lane FA).
TS2_OWNED_STATIC(void *, d9a570) = reinterpret_cast<void *>(::PostMessageA);
TS2_OWNED_STATIC(void *, d9a588) = reinterpret_cast<void *>(::BeginPaint);
TS2_OWNED_STATIC(void *, d9a58c) = reinterpret_cast<void *>(::EndPaint);
TS2_OWNED_STATIC(void *, d9a590) = reinterpret_cast<void *>(::InvalidateRect);
TS2_OWNED_STATIC(void *, d9a594) = reinterpret_cast<void *>(::ValidateRect);
TS2_OWNED_STATIC(void *, d9a598) = reinterpret_cast<void *>(::SetWindowTextA);
TS2_OWNED_STATIC(void *, d9a59c) = reinterpret_cast<void *>(::GetClientRect);
TS2_OWNED_STATIC(void *, d9a5a0) = reinterpret_cast<void *>(::AdjustWindowRectEx);
TS2_OWNED_STATIC(void *, d9a5c8) = reinterpret_cast<void *>(::GetAncestor);
TS2_OWNED_STATIC(void *, d9a5cc) = reinterpret_cast<void *>(::LoadIconA);
TS2_OWNED_STATIC(void *, d9a5d0) = reinterpret_cast<void *>(::GetDesktopWindow);
TS2_OWNED_STATIC(void *, d9a5d4) = reinterpret_cast<void *>(::SetWindowLongA);
TS2_OWNED_STATIC(void *, d9a5d8) = reinterpret_cast<void *>(::GetWindowLongA);
TS2_OWNED_STATIC(void *, d9a5dc) = reinterpret_cast<void *>(::FillRect);
TS2_OWNED_STATIC(void *, d9a5e0) = reinterpret_cast<void *>(::ClientToScreen);
TS2_OWNED_STATIC(void *, d9a6c8) = reinterpret_cast<void *>(::Direct3DCreate9);
TS2_OWNED_STATIC(void *, d9a6d8) = reinterpret_cast<void *>(::StringFromGUID2);

// Report texts (the reports name a source file and line, then a message). The image-free program's own wording: the
// same arguments (%s, %d) as the image's messages, not their text.
TS2_OWNED_STATIC(const char, d9e540)[] = "src/gz/cDeviceDX.cpp"; // own text
TS2_OWNED_STATIC(const char, d9e880)[] = "src/gz/cImageManipDX.cpp"; // own text
TS2_OWNED_STATIC(const char, d9f140)[] = "src/gz/cShaderProgramsDX.cpp"; // own text
TS2_OWNED_STATIC(const char, d9f810)[] = "src/gz/cDeviceEnumDX.cpp"; // own text
TS2_OWNED_STATIC(const char, d9fa60)[] = "src/gz/cDeviceResourceManagerDX.cpp"; // own text
TS2_OWNED_STATIC(const char, d9fba8)[] = "src/gz/cDeviceStateDX.cpp"; // own text
TS2_OWNED_STATIC(const char, d9cf08)[] = "src/gz/cGraphicSystemDX.cpp"; // own text
// The texture support checks (src/gz/TextureSupportDX.cpp).
TS2_OWNED_STATIC(const char, da0e78)[] = "src/gz/TextureSupportDX.cpp (textures)"; // own text
TS2_OWNED_STATIC(const char, da10d8)[] = "src/gz/TextureSupportDX.cpp (cube textures)"; // own text
// cDeviceStateDX's validators (src/gz/cDeviceStateDXValidate.cpp).
TS2_OWNED_STATIC(const char, d9fad8)[] = "Device state validation"; // own text
TS2_OWNED_STATIC(const char, d9ff68)[] = "src/gz/cDeviceStateDXValidate.cpp (render targets)"; // own text
TS2_OWNED_STATIC(const char, da00f0)[] = "src/gz/cDeviceStateDXValidate.cpp (rasterisation)"; // own text
TS2_OWNED_STATIC(const char, da0290)[] = "src/gz/cDeviceStateDXValidate.cpp (fixed-function vertices)"; // own text
TS2_OWNED_STATIC(const char, da0410)[] = "src/gz/cDeviceStateDXValidate.cpp (alpha)"; // own text
TS2_OWNED_STATIC(const char, da0518)[] = "src/gz/cDeviceStateDXValidate.cpp (depth)"; // own text
TS2_OWNED_STATIC(const char, da0600)[] = "src/gz/cDeviceStateDXValidate.cpp (stencil)"; // own text
TS2_OWNED_STATIC(const char, da0798)[] = "src/gz/cDeviceStateDXValidate.cpp (texture blending)"; // own text
TS2_OWNED_STATIC(const char, da0940)[] = "src/gz/cDeviceStateDXValidate.cpp (texture sampling)"; // own text
TS2_OWNED_STATIC(const char, d9fc68)[] = "ProcessVertices needs software vertex processing, which the device lacks"; // own text
TS2_OWNED_STATIC(const char, d9fcb4)[] = "The device is not in software vertex processing mode"; // own text
TS2_OWNED_STATIC(const char, d9fce0)[] = "No temporary vertex buffer"; // own text
TS2_OWNED_STATIC(const char, d9fd04)[] = "ProcessVertices: %s\n"; // own text
TS2_OWNED_STATIC(const char, d9fd28)[] = "No cube texture support\n"; // own text
TS2_OWNED_STATIC(const char, d9fd4c)[] = "No volume texture support\n"; // own text
TS2_OWNED_STATIC(const char, d9fd70)[] = "More textures than the device allows\n"; // own text
TS2_OWNED_STATIC(const char, d9fd84)[] = "Stream %d: vertex buffer without software vertex processing\n"; // own text
TS2_OWNED_STATIC(const char, d9fdb8)[] = "Vertex streams: count, stride or offset beyond the device\n"; // own text
TS2_OWNED_STATIC(const char, d9fe0c)[] = "Index buffer without software vertex processing\n"; // own text
TS2_OWNED_STATIC(const char, d9fe34)[] = "ValidateDevice: %s\n"; // own text
TS2_OWNED_STATIC(const char, d9fe58)[] = "No temporary index buffer"; // own text
TS2_OWNED_STATIC(const char, d9fe7c)[] = "Trial draw: %s\n"; // own text
TS2_OWNED_STATIC(const char, d9fea0)[] = "hardware";
TS2_OWNED_STATIC(const char, d9feac)[] = "software";
TS2_OWNED_STATIC(const char, d9feb8)[] = "No device caps for %s vertex processing"; // own text
TS2_OWNED_STATIC(const char, d9ffcc)[] = "Render target 0 is null\n"; // own text
TS2_OWNED_STATIC(const char, d9fff4)[] = "A render target is not set up\n"; // own text
TS2_OWNED_STATIC(const char, da001c)[] = "More render targets than the device allows\n"; // own text
TS2_OWNED_STATIC(const char, da0040)[] = "The depth/stencil target is not set up\n"; // own text
TS2_OWNED_STATIC(const char, da0074)[] = "Scissor test: unsupported\n"; // own text
TS2_OWNED_STATIC(const char, da0150)[] = "Cull mode: unsupported\n"; // own text
TS2_OWNED_STATIC(const char, da0174)[] = "Shade caps incomplete\n"; // own text
TS2_OWNED_STATIC(const char, da01a4)[] = "Colour write enable: unsupported\n"; // own text
TS2_OWNED_STATIC(const char, da01f0)[] = "Fog: unsupported\n"; // own text
TS2_OWNED_STATIC(const char, da02fc)[] = "Fixed-function lighting: unsupported\n"; // own text
TS2_OWNED_STATIC(const char, da0340)[] = "Texture coordinate source: unsupported\n"; // own text
TS2_OWNED_STATIC(const char, da0370)[] = "Texture transform type: unsupported\n"; // own text
TS2_OWNED_STATIC(const char, da03a0)[] = "Texture transform type: unsupported with the other states\n"; // own text
TS2_OWNED_STATIC(const char, da0474)[] = "Alpha blending: unsupported\n"; // own text
TS2_OWNED_STATIC(const char, da04a0)[] = "Alpha test: unsupported\n"; // own text
TS2_OWNED_STATIC(const char, da04c4)[] = "Alpha test function: unsupported\n"; // own text
TS2_OWNED_STATIC(const char, da0580)[] = "Depth test function: unsupported\n"; // own text
TS2_OWNED_STATIC(const char, da0664)[] = "Stencil operation: unsupported\n"; // own text
TS2_OWNED_STATIC(const char, da07fc)[] = "Texture blending: unsupported\n"; // own text
TS2_OWNED_STATIC(const char, da0824)[] = "More blend stages than the device allows\n"; // own text
TS2_OWNED_STATIC(const char, da0844)[] = "The last blend stage writes the temporary register\n"; // own text
TS2_OWNED_STATIC(const char, da0868)[] = "A blend stage is enabled after a disabled one\n"; // own text
TS2_OWNED_STATIC(const char, da08a0)[] = "Texture blending: unsupported with the other states\n"; // own text
TS2_OWNED_STATIC(const char, da08d8)[] = "Specular add: unsupported\n"; // own text
TS2_OWNED_STATIC(const char, da09a8)[] = "Texture addressing: unsupported\n"; // own text
TS2_OWNED_STATIC(const char, da09d4)[] = "Texture addressing: unsupported with the other states\n"; // own text
TS2_OWNED_STATIC(const char, da0a1c)[] = "Texture filter: unsupported\n"; // own text
TS2_OWNED_STATIC(const char, da0a44)[] = "Texture filter: unsupported with the other states\n"; // own text
TS2_OWNED_STATIC(const char, da0a84)[] = "Mip filter: unsupported\n"; // own text
TS2_OWNED_STATIC(const char, da0ab0)[] = "Mip filter: unsupported with the other states\n"; // own text
TS2_OWNED_STATIC(const char, da0af0)[] = "Texture coordinate interpolation: unsupported with the other states\n"; // own text
// IID_IDirect3DTexture9 and IID_IDirect3DCubeTexture9 (d3d9.h).
TS2_OWNED_STATIC(const IID, d9c8b8) = {0x85c31227, 0x3de5, 0x4f00, {0x9b, 0x3a, 0xf1, 0x1a, 0xc3, 0x8c, 0x18, 0xb5}};
TS2_OWNED_STATIC(const IID, d9c8c8) = {0xfff32f81, 0xd953, 0x473a, {0x92, 0x23, 0x93, 0xd6, 0x52, 0xab, 0xa9, 0x3f}};

// cDeviceStateDX's state tables: the engine's state index, Direct3D's state, its default value (src/gz/cDeviceStateDXDetail.h).
TS2_OWNED_STATIC(const nGZGraphic4StateName, feddb8)[0x59] = {
    {0x2d, D3DRS_ZENABLE, 0x0},
    {0x2a, D3DRS_FILLMODE, 0x3},
    {0x2b, D3DRS_SHADEMODE, 0x2},
    {0x2e, D3DRS_ZWRITEENABLE, 0x1},
    {0x30, D3DRS_ALPHATESTENABLE, 0x0},
    {0x0, D3DRS_LASTPIXEL, 0x1},
    {0x38, D3DRS_SRCBLEND, 0x2},
    {0x3a, D3DRS_DESTBLEND, 0x1},
    {0x2c, D3DRS_CULLMODE, 0x3},
    {0x2f, D3DRS_ZFUNC, 0x4},
    {0x31, D3DRS_ALPHAREF, 0x0},
    {0x32, D3DRS_ALPHAFUNC, 0x8},
    {0x33, D3DRS_ALPHABLENDENABLE, 0x0},
    {0x22, D3DRS_FOGENABLE, 0x0},
    {0x1a, D3DRS_SPECULARENABLE, 0x0},
    {0x23, D3DRS_FOGCOLOR, 0x0},
    {0x24, D3DRS_FOGTABLEMODE, 0x0},
    {0x26, D3DRS_FOGSTART, 0x0},
    {0x27, D3DRS_FOGEND, 0x3f800000 /* 1.0f */},
    {0x28, D3DRS_FOGDENSITY, 0x3f800000 /* 1.0f */},
    {0x29, D3DRS_RANGEFOGENABLE, 0x0},
    {0x3d, D3DRS_STENCILENABLE, 0x0},
    {0x42, D3DRS_STENCILFAIL, 0x1},
    {0x44, D3DRS_STENCILZFAIL, 0x1},
    {0x46, D3DRS_STENCILPASS, 0x1},
    {0x48, D3DRS_STENCILFUNC, 0x8},
    {0x40, D3DRS_STENCILREF, 0x0},
    {0x41, D3DRS_STENCILMASK, 0xffffffff},
    {0x3f, D3DRS_STENCILWRITEMASK, 0xffffffff},
    {0x3c, D3DRS_TEXTUREFACTOR, 0xffffffff},
    {0x1, D3DRS_WRAP0, 0x0},
    {0x2, D3DRS_WRAP1, 0x0},
    {0x3, D3DRS_WRAP2, 0x0},
    {0x4, D3DRS_WRAP3, 0x0},
    {0x5, D3DRS_WRAP4, 0x0},
    {0x6, D3DRS_WRAP5, 0x0},
    {0x7, D3DRS_WRAP6, 0x0},
    {0x8, D3DRS_WRAP7, 0x0},
    {0x17, D3DRS_LIGHTING, 0x1},
    {0x25, D3DRS_FOGVERTEXMODE, 0x0},
    {0x1b, D3DRS_COLORVERTEX, 0x1},
    {0x18, D3DRS_LOCALVIEWER, 0x1},
    {0x19, D3DRS_NORMALIZENORMALS, 0x0},
    {0x1d, D3DRS_DIFFUSEMATERIALSOURCE, 0x1},
    {0x1e, D3DRS_SPECULARMATERIALSOURCE, 0x2},
    {0x1c, D3DRS_AMBIENTMATERIALSOURCE, 0x0},
    {0x1f, D3DRS_EMISSIVEMATERIALSOURCE, 0x0},
    {0x11, D3DRS_CLIPPLANEENABLE, 0x0},
    {0x4c, D3DRS_POINTSIZE_MIN, 0x3f800000 /* 1.0f */},
    {0x4e, D3DRS_POINTSPRITEENABLE, 0x0},
    {0x20, D3DRS_MULTISAMPLEANTIALIAS, 0x1},
    {0x21, D3DRS_MULTISAMPLEMASK, 0xffffffff},
    {0x4f, D3DRS_PATCHEDGESTYLE, 0x0},
    {0x4d, D3DRS_POINTSIZE_MAX, 0x3f800000 /* 1.0f */},
    {0x12, D3DRS_COLORWRITEENABLE, 0xf},
    {0x36, D3DRS_BLENDOP, 0x1},
    {0x50, D3DRS_POSITIONDEGREE, 0x3},
    {0x51, D3DRS_NORMALDEGREE, 0x1},
    {0x4b, D3DRS_SLOPESCALEDEPTHBIAS, 0x0},
    {0x52, D3DRS_MINTESSELLATIONLEVEL, 0x3f800000 /* 1.0f */},
    {0x53, D3DRS_MAXTESSELLATIONLEVEL, 0x3f800000 /* 1.0f */},
    {0x55, D3DRS_ADAPTIVETESS_X, 0x0},
    {0x56, D3DRS_ADAPTIVETESS_Y, 0x0},
    {0x57, D3DRS_ADAPTIVETESS_Z, 0x3f800000 /* 1.0f */},
    {0x58, D3DRS_ADAPTIVETESS_W, 0x0},
    {0x54, D3DRS_ENABLEADAPTIVETESSELLATION, 0x0},
    {0x3e, D3DRS_TWOSIDEDSTENCILMODE, 0x0},
    {0x43, D3DRS_CCW_STENCILFAIL, 0x1},
    {0x45, D3DRS_CCW_STENCILZFAIL, 0x1},
    {0x47, D3DRS_CCW_STENCILPASS, 0x1},
    {0x49, D3DRS_CCW_STENCILFUNC, 0x8},
    {0x13, D3DRS_COLORWRITEENABLE1, 0xf},
    {0x14, D3DRS_COLORWRITEENABLE2, 0xf},
    {0x15, D3DRS_COLORWRITEENABLE3, 0xf},
    {0x35, D3DRS_BLENDFACTOR, 0xffffffff},
    {0x16, D3DRS_SRGBWRITEENABLE, 0x0},
    {0x4a, D3DRS_DEPTHBIAS, 0x0},
    {0x9, D3DRS_WRAP8, 0x0},
    {0xa, D3DRS_WRAP9, 0x0},
    {0xb, D3DRS_WRAP10, 0x0},
    {0xc, D3DRS_WRAP11, 0x0},
    {0xd, D3DRS_WRAP12, 0x0},
    {0xe, D3DRS_WRAP13, 0x0},
    {0xf, D3DRS_WRAP14, 0x0},
    {0x10, D3DRS_WRAP15, 0x0},
    {0x34, D3DRS_SEPARATEALPHABLENDENABLE, 0x0},
    {0x39, D3DRS_SRCBLENDALPHA, 0x2},
    {0x3b, D3DRS_DESTBLENDALPHA, 0x1},
    {0x37, D3DRS_BLENDOPALPHA, 0x1},
};
TS2_OWNED_STATIC(const nGZGraphic4StateName, fee1e8)[0x12] = {
    {0x0, D3DTSS_COLOROP, 0x1},
    {0x2, D3DTSS_COLORARG1, 0x2},
    {0x3, D3DTSS_COLORARG2, 0x1},
    {0x4, D3DTSS_ALPHAOP, 0x1},
    {0x6, D3DTSS_ALPHAARG1, 0x2},
    {0x7, D3DTSS_ALPHAARG2, 0x1},
    {0xc, D3DTSS_BUMPENVMAT00, 0x0},
    {0xd, D3DTSS_BUMPENVMAT01, 0x0},
    {0xe, D3DTSS_BUMPENVMAT10, 0x0},
    {0xf, D3DTSS_BUMPENVMAT11, 0x0},
    {0xa, D3DTSS_TEXCOORDINDEX, 0x0},
    {0x10, D3DTSS_BUMPENVLSCALE, 0x0},
    {0x11, D3DTSS_BUMPENVLOFFSET, 0x0},
    {0xb, D3DTSS_TEXTURETRANSFORMFLAGS, 0x0},
    {0x1, D3DTSS_COLORARG0, 0x1},
    {0x5, D3DTSS_ALPHAARG0, 0x1},
    {0x8, D3DTSS_RESULTARG, 0x1},
    {0x9, D3DTSS_CONSTANT, 0xffffffff},
};
TS2_OWNED_STATIC(const nGZGraphic4StateName, fee2c0)[0xd] = {
    {0x0, D3DSAMP_ADDRESSU, 0x1},
    {0x1, D3DSAMP_ADDRESSV, 0x1},
    {0x2, D3DSAMP_ADDRESSW, 0x1},
    {0x3, D3DSAMP_BORDERCOLOR, 0x0},
    {0x4, D3DSAMP_MAGFILTER, 0x1},
    {0x5, D3DSAMP_MINFILTER, 0x1},
    {0x6, D3DSAMP_MIPFILTER, 0x0},
    {0x7, D3DSAMP_MIPMAPLODBIAS, 0x0},
    {0x8, D3DSAMP_MAXMIPLEVEL, 0x0},
    {0x9, D3DSAMP_MAXANISOTROPY, 0x1},
    {0xa, D3DSAMP_SRGBTEXTURE, 0x0},
    {0xb, D3DSAMP_ELEMENTINDEX, 0x0},
    {0xc, D3DSAMP_DMAPOFFSET, 0x100},
};
// The index maps the constructor builds from them.
TS2_OWNED_STATIC(uint16_t, 10ca448)[0x59]{};
TS2_OWNED_STATIC(uint8_t, 10ca4fc)[0x12]{};
TS2_OWNED_STATIC(uint8_t, 10ca510)[0xd]{};

// cDeviceDX (src/gz/cDeviceDX.h). The default display mode is zero (windowed on the desktop); nothing writes it.
TS2_OWNED_STATIC(nGZGraphic4::cDisplayMode, 10ca208){};
TS2_OWNED_STATIC(nGZGraphic4::cDisplayMode, 10ca868){}; // the desktop mode request (gz/GraphicModeSearch.h)
TS2_OWNED_STATIC(uint32_t, 10ca21c){};
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10ca22c){};
TS2_OWNED_STATIC(const nGZEvent::cEventAttribute, fecaf8) = {0, ts2_rva_d9b9d0, 0, 0, 0, 0};
TS2_OWNED_STATIC(const char, d9e598)[] = "--------------------------------------------------------------------------------\n"; // own text
TS2_OWNED_STATIC(const char, d9e5f0)[] = "Direct3D device\n"; // own text
TS2_OWNED_STATIC(const char, d9e644)[] = "\n";

// cDeviceEnumDX (src/gz/cDeviceEnumDX.h).
TS2_OWNED_STATIC(const uint32_t, d9f498)[6] = {D3DFMT_A2R10G10B10, D3DFMT_A8R8G8B8, D3DFMT_A1R5G5B5, D3DFMT_X8R8G8B8,
                                              D3DFMT_X1R5G5B5, D3DFMT_R5G6B5};
TS2_OWNED_STATIC(const uint32_t, d9f4b0)[11][2] = {
    {0, D3DRTYPE_SURFACE}, {0, D3DRTYPE_SURFACE},
    {D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_SURFACE},
    {D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE}, {0, D3DRTYPE_TEXTURE}, {0, D3DRTYPE_VOLUMETEXTURE}, {0, D3DRTYPE_CUBETEXTURE},
    {D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_TEXTURE}, {D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE},
    {D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_CUBETEXTURE},
    {D3DUSAGE_RENDERTARGET, D3DRTYPE_CUBETEXTURE}};
TS2_OWNED_STATIC(const uint32_t, d9f508)[5] = {D3DFMT_A8R8G8B8, D3DFMT_A1R5G5B5, D3DFMT_X8R8G8B8, D3DFMT_X1R5G5B5,
                                              D3DFMT_R5G6B5};
// By source class, then destination class; four tables: neither, stretching, from textures, both.
TS2_OWNED_STATIC(const uint8_t, d9f964)[4][4][4] = {
    {{0, 0, 0, 0}, {0, 1, 1, 0}, {0, 1, 1, 0}, {0, 1, 1, 1}},
    {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 1, 1, 0}, {0, 1, 1, 0}},
    {{0, 1, 1, 0}, {0, 1, 1, 0}, {0, 1, 1, 0}, {0, 1, 1, 1}},
    {{0, 1, 1, 0}, {0, 1, 1, 0}, {0, 1, 1, 0}, {0, 1, 1, 0}},
};
TS2_OWNED_STATIC(nGZGraphic4::cDisplayMode, 10ca3a0){};

// The behaviour types' records for an unknown event (src/gz/cAbstractBehaviorTypeMT.h, MissingAttributeRecord).
TS2_OWNED_STATIC(const nGZEvent::cEventAttribute, feb410) = {0, ts2_rva_d9b9d0, 0, 0, 0, 0}; // cGZCanvas
TS2_OWNED_STATIC(const nGZEvent::cEventAttribute, febc60) = {0, ts2_rva_d9b9d0, 0, 0, 0, 0}; // cCanvasW32
TS2_OWNED_STATIC(const nGZEvent::cEventAttribute, febd08) = {0, ts2_rva_d9b9d0, 0, 0, 0, 0}; // cGraphicSystemDX
TS2_OWNED_STATIC(const nGZEvent::cEventAttribute, feb1c0) = {0, ts2_rva_d9b9d0, 0, 0, 0, 0}; // cGZGraphicSystem

// cGZCanvas's message pools (element size, block size; no blocks yet) and slot 22's HWND cell.
TS2_OWNED_STATIC(nGZGraphic4::cGZMessagePool, feb660) = {0x10, 0x800, nullptr, nullptr};
TS2_OWNED_STATIC(nGZGraphic4::cGZMessagePool, feb670) = {0x20, 0x1000, nullptr, nullptr};
TS2_OWNED_STATIC(nGZGraphic4::cGZMessagePool, feb680) = {0x1c, 0xe00, nullptr, nullptr};
TS2_OWNED_STATIC(void *, 10c9570){};

// The behaviour types' connection manager caches (EventManagerCell); cGZCanvas and cGZGraphicSystem share one.
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10c9500){};
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10c9708){};
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10c9774){};

// cCanvasW32: Show's saved flag and rectangle, PlaceOnMonitor's title bar offset (measured on first use).
TS2_OWNED_STATIC(bool, 10c96dc){};
TS2_OWNED_STATIC(int32_t, 10c96e0)[4]{};
TS2_OWNED_STATIC(int32_t, 10c96f0){};

// The multi-monitor stubs' pointers and flags (src/gz/MultiMon.h), all unset until the first lookup.
TS2_OWNED_STATIC(FARPROC, 10c96b8){};
TS2_OWNED_STATIC(FARPROC, 10c96bc){};
TS2_OWNED_STATIC(FARPROC, 10c96c0){};
TS2_OWNED_STATIC(FARPROC, 10c96c4){};
TS2_OWNED_STATIC(FARPROC, 10c96c8){};
TS2_OWNED_STATIC(FARPROC, 10c96cc){};
TS2_OWNED_STATIC(FARPROC, 10c96d0){};
TS2_OWNED_STATIC(uint32_t, 10c96d4){};
TS2_OWNED_STATIC(uint32_t, 10c96d8){};

// The graphics classes' fixed-size pools (constructed here, as the game's static initialisers construct them).
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x1c>, 10ca058){}; // cImageSurfaceDX
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x20>, fec63c){};  // cMipSurfaceDX
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x1c>, 10ca07c){}; // cLockableRenderTargetDX
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x18>, 10ca064){}; // cDepthStencilSurfaceDX
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x18>, 10ca070){}; // cRenderTargetDX
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x28>, 10ca2fc){}; // cVertexShaderDX
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x28>, 10ca308){}; // cPixelShaderDX
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x58>, fed220){};  // cVertexDeclarationDX
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x18>, fee35c){};  // cStateBlockDX
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0xc>, fedb58){};   // cDeviceResourceIterator
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x10>, fed8f8){};  // cDisplayModeIterator

// The graphic services' manager caches (GraphicServices.h); all start empty.
TS2_OWNED_STATIC(void *, 10ca87c){};
TS2_OWNED_STATIC(void *, 10ca830){};
TS2_OWNED_STATIC(void *, 10ca990){};
TS2_OWNED_STATIC(void *, 10caa24){};
TS2_OWNED_STATIC(void *, 10caa28){};
TS2_OWNED_STATIC(void *, 10caa3c){};

// MiscHelpersA's texts (QueryServiceValue's fallback, LogD3DError's format and category) and cRZStd's mode byte.
TS2_OWNED_STATIC(const char, da1088)[] = "Direct3D error (no description)"; // own text
TS2_OWNED_STATIC(const char, da10a0)[] = "Direct3D call failed: %s"; // own text
TS2_OWNED_STATIC(const char, da10b8)[] = "D3DError";
TS2_OWNED_STATIC(uint8_t, ff246c) = 1;

// The surfaces' report source files, IID_IDirect3DBaseTexture9 (d3d9.h) and cSurfaceCopy's stretch kinds (cSurfaceDX.h),
// and the vertex declarations' report source file (cVertexFormat.h).
TS2_OWNED_STATIC(const char, d9dab0)[] = "src/gz/cSurfaceDX.cpp"; // own text
TS2_OWNED_STATIC(const char, d9dcd8)[] = "src/gz/cSurfaceDX.cpp (texture level)"; // own text
TS2_OWNED_STATIC(const IID, d9c8a8) = {0x580ca87e, 0x1d3c, 0x4d54, {0x99, 0x1d, 0xb7, 0xd3, 0xe3, 0xc2, 0x98, 0xce}};
TS2_OWNED_STATIC(const uint32_t, d9db08)[6] = {1, 4, 11, 12, 2, 7};
TS2_OWNED_STATIC(const char, d9ea18)[] = "src/gz/cVertexFormat.cpp"; // own text

// TrackMouseLeave's TrackMouseEvent cell and guard (cCanvasW32.h), the event system's connection manager static and
// guard (cEventSystemService.h), the cursor code's graphics system cell (cRZCursor.h) and cGZBuffer's pool.
TS2_OWNED_STATIC(void *, 10c96f4){};
TS2_OWNED_STATIC(int, 10c96f8){};
TS2_OWNED_STATIC(uint32_t, 10cadd8)[0x11]{};
TS2_OWNED_STATIC(int, 10cae1c){};
TS2_OWNED_STATIC(ifaces::cIGZGraphicSystem *, 10f51a4){};
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x54>, 105f82c){};

// The event classes' vtables (cEventFaces.h, OwnedVTable): each symbol is our own face's table. The array references
// each face's table so it is emitted in this program.
#pragma comment(linker, "/alternatename:_ts2_rva_d9ca78=??_7cNullEvent@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_d9cb2c=??_7?$cScalarEvent@K@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_da239c=??_7cConnectionChangedEvent@rzface@@6B@")
extern "C" const void *const ts2_own_event_vtables[] = {nGZEvent::OwnNullEventVTable(), nGZEvent::OwnScalarEventVTable(),
                                                        nGZEvent::OwnConnectionChangedEventVTable()};
