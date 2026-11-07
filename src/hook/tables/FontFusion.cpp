// The font engine (lane OJ, src/font/FontFusion.h): Bitstream Font Fusion's 18 entry points, linked into the image as
// library code, replaced by our engine on FreeType (the user's decision for Font Fusion, db/third-party.jsonl). cGZFont
// calls ours directly; these hooks catch any other caller. Font Fusion's internal functions are reached only from these
// entry points.
#include "font/FontFusion.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"FontFusion_DeleteMemhandler", 0x9115c0, 30, 0x4c20b9ae, reinterpret_cast<void *>(&fontfusion::tsi_DeleteMemhandler)},
    {"FontFusion_NewMemhandler", 0x9116c7, 32, 0x36360c88, reinterpret_cast<void *>(&fontfusion::tsi_NewMemhandler)},
    {"FontFusion_DeleteInputStream", 0x91180d, 32, 0x95bdc8f2, reinterpret_cast<void *>(&fontfusion::Delete_InputStream)},
    {"FontFusion_NewInputStream3", 0x9119be, 32, 0x19170672, reinterpret_cast<void *>(&fontfusion::New_InputStream3)},
    {"FontFusion_NewNonRamInputStream", 0x911a54, 32, 0x4055f46d, reinterpret_cast<void *>(&fontfusion::New_NonRamInputStream)},
    {"FontFusion_ExtractPureT1FromPCType1", 0x91359f, 32, 0x970846db, reinterpret_cast<void *>(&fontfusion::ExtractPureT1FromPCType1)},
    {"FontFusion_DeleteSfntClass", 0x9198e1, 32, 0x4a30f304, reinterpret_cast<void *>(&fontfusion::Delete_sfntClass)},
    {"FontFusion_NewSfntClass", 0x9199cb, 32, 0x57aec210, reinterpret_cast<void *>(&fontfusion::New_sfntClass)},
    {"FontFusion_DeleteT2K", 0x91bcf2, 32, 0x3ef6a8cc, reinterpret_cast<void *>(&fontfusion::DeleteT2K)},
    {"FontFusion_GlyphExists", 0x91bdc8, 32, 0xa695c884, reinterpret_cast<void *>(&fontfusion::T2K_GlyphExists)},
    {"FontFusion_GetCharCodeByName", 0x91be55, 32, 0x7041c8e7, reinterpret_cast<void *>(&fontfusion::T2K_GetCharCodeByName)},
    {"FontFusion_NewT2K", 0x91be9a, 32, 0xf8d7397f, reinterpret_cast<void *>(&fontfusion::NewT2K)},
    {"FontFusion_GetGlyphIndex", 0x91c04e, 32, 0xe2b424d2, reinterpret_cast<void *>(&fontfusion::T2K_GetGlyphIndex)},
    {"FontFusion_NewTransformation", 0x91c300, 32, 0xbe925544, reinterpret_cast<void *>(&fontfusion::T2K_NewTransformation)},
    {"FontFusion_PurgeMemory", 0x91c8ba, 32, 0x76b83742, reinterpret_cast<void *>(&fontfusion::T2K_PurgeMemory)},
    {"FontFusion_RenderGlyph", 0x91c9a8, 32, 0x80989163, reinterpret_cast<void *>(&fontfusion::T2K_RenderGlyph)},
    {"FontFusion_GetNameString", 0x91da2f, 32, 0x73879922, reinterpret_cast<void *>(&fontfusion::T2K_GetNameString)},
    {"FontFusion_FindFontTypeFromStream", 0x91dc8d, 32, 0x8fafeffe, reinterpret_cast<void *>(&fontfusion::FF_FindFontTypeFromStream)},
};
REGISTER_HOOKS(kHooks);

} // namespace
