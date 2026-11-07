#pragma once
#include <cstdint>

// The graphics COM directors' class id functions (RVAs 1.18.0.187): one per class a director registers, each returning
// the class id (`mov eax, id; ret`, no arguments). The director getters (0x145dd0 cDraw2DDLLCOMDirector, 0x154290
// cDllCOMDirector, 0x8b7f30 cGZDrawCOMDirector) call them for the ids they pass to AddCls; ours (src/rz/COMDirectors.cpp,
// hooked) take the ids from src/rz/COMDirectorData.cpp's table instead, so no code of ours calls these. Named after the
// class whose creator follows the id in the table (the creator's RVA in the comment); the three Draw2D classes whose
// creators are unnamed carry their id.

namespace gfxclassid {
// cDraw2DDLLCOMDirector (0x145dd0)
uint32_t cGZGraphicSystemId();     // 0x147760: 0x20732180 (creator 0x1475e0)
uint32_t cGZCanvasId();            // 0x1496b0: 0x445fabbb (creator 0x1495f0)
uint32_t cDraw2DSystemServiceId(); // 0x14f320: 0x4c9282f8 (creator 0x14f2b0)
uint32_t Class_dc012582Id();       // 0x153a10: 0xdc012582 (creator 0x1538b0)
uint32_t Class_9c1d3c77Id();       // 0x153a20: 0x9c1d3c77 (creator 0x1538f0)
uint32_t Class_5c0ac4afId();       // 0x153a30: 0x5c0ac4af (creator 0x153930)
// cDllCOMDirector (0x154290)
uint32_t cCanvasW32Id();                     // 0x155e10: 0xbb637286 (creator 0x155c70)
uint32_t cGraphicSystemDXId();               // 0x159a20: 0x855227cd (creator 0x159940)
uint32_t cDisplayMonitorW32Id();             // 0x154610: 0x7b64ce01 (creator 0x1545c0)
uint32_t cPixelFormatIteratorId();           // 0x15eb00: 0xbb782354 (creator 0x15ea00)
uint32_t cImageSurfaceDXId();                // 0x161e70: 0x3b75b71b (creator 0x161d40)
uint32_t cDepthStencilSurfaceDXId();         // 0x161e60: 0x7b75b73a (creator 0x161ce0)
uint32_t cRenderTargetDXId();                // 0x161e90: 0x5b75b757 (creator 0x161e00)
uint32_t cLockableRenderTargetDXId();        // 0x161e80: 0x4dee64d7 (creator 0x161da0)
uint32_t cTextureDXId();                     // 0x1636b0: 0xbb892198 (creator 0x163600)
uint32_t cCubeTextureDXId();                 // 0x164950: 0xe944343d (creator 0x1648a0)
uint32_t cStaticVertexComponentArrayDXId();  // 0x166080: 0x3bffec52 (creator 0x165fa0)
uint32_t cDynamicVertexComponentArrayDXId(); // 0x166060: 0xbbfffce5 (creator 0x165ea0)
uint32_t cStaticVertexIndexArrayDXId();      // 0x166090: 0x9bfff0ea (creator 0x166000)
uint32_t cDynamicVertexIndexArrayDXId();     // 0x166070: 0x9c043807 (creator 0x165f20)
// cGZDrawCOMDirector (0x8b7f30)
uint32_t cGZDrawContextId();         // 0x8bf910: 0x0ae6320e (creator 0x8bf7d0)
uint32_t cGZDrawSystemId();          // 0x8c09f0: 0xab2ea3aa (creator 0x8c0920)
uint32_t cGZDrawContextAsBufferId(); // 0x8c2340: 0xab300b20 (creator 0x8c22a0)
uint32_t cGZDrawFontRendererId();    // 0x8c33b0: 0x8b3255f9 (creator 0x8c32a0)
uint32_t cGZBufferId();              // 0x8c7610: 0xc470d325 (creator 0x8c75d0)
uint32_t cGZTransientBufferId();     // 0x8c7690: 0x6cf55d22 (creator 0x8c7620)
} // namespace gfxclassid
