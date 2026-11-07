#pragma once
// nGZSceneGraph::DrawMissingTextureName (0xae8310; lane VV, milestone O): the picture a texture gets when its image is
// missing: a white level with a red diagonal cross and the texture's name written over it again and again in small type
// (black and blue in turn, each line shifted two letters on). Called by the missing-texture update strategy
// (src/sg/cTextureUpdateStrategy.cpp) and by the image data's reader when an image fails to load (src/sg/cImageData.cpp).
#include "gz/cIGZString.h"

namespace nGZGraphic4 {
class cILockableSurface;
}

namespace nGZSceneGraph {

// 0xae8310 (cdecl): draws into the whole of `surface` (locked through nRZGraphic4::cSurfaceLockAsGZBuffer for the time
// of the call): every locked row filled with 0xff bytes; red (the surface's pixel format's ARGB face packs 0xffff0000)
// along both diagonals of the largest square from the top corners, in 2-byte pixels for 16-bit formats and 4-byte ones
// otherwise; then, when `name` is not empty, the font system's "Lucida Console" at size 14 writes `name` from each
// line's left edge to the right edge, the first copy starting `offset` letters in (offset growing by 2 a line, modulo
// the length), the copies black and blue in turn, the lines a font line spacing apart down the locked rows.
void __cdecl DrawMissingTextureName(nGZGraphic4::cILockableSurface *surface, cIGZString *name);

} // namespace nGZSceneGraph
