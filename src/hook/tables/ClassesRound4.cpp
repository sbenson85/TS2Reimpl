#include "hook/hooks.h"
#include "misc/ClassesRound4.h"

namespace {

const HookSpec kHooks[] = {
    {"nTSSG_cSnowEvenTriangleFirst_vf0", 0x413390, 32, 0xc7141933, MemberAddress(&nTSSG::cSnowEvenTriangleFirst::AddSnowIndices)},
    {"nTSSG_cSnowEvenTriangleSecond_vf0", 0x4133f0, 32, 0x77294b63, MemberAddress(&nTSSG::cSnowEvenTriangleSecond::AddSnowIndices)},
    {"nTSSG_cSnowOddTriangleFirst_vf0", 0x413450, 32, 0x9f0ab01b, MemberAddress(&nTSSG::cSnowOddTriangleFirst::AddSnowIndices)},
    {"nTSSG_cSnowOddTriangleSecond_vf0", 0x4134b0, 32, 0xf2ba9bc4, MemberAddress(&nTSSG::cSnowOddTriangleSecond::AddSnowIndices)},
    {"nTSSG_cBaseEvenTriangleSecond_vf1", 0x4135e0, 32, 0x66aa44c1, MemberAddress(&nTSSG::cBaseEvenTriangleSecond::AddIndices)},
    {"nTSSG_cBaseOddTriangleSecond_vf1", 0x413740, 32, 0xfc62baa3, MemberAddress(&nTSSG::cBaseOddTriangleSecond::AddIndices)},
    {"nTSSG_cBaseEvenTriangleFirst_vf2", 0x413800, 32, 0x88de61d8, MemberAddress(&nTSSG::cBaseEvenTriangleFirst::AddLayeredIndices)},
    {"nTSSG_cBaseEvenTriangleSecond_vf2", 0x4138c0, 32, 0x89490521, MemberAddress(&nTSSG::cBaseEvenTriangleSecond::AddLayeredIndices)},
    {"nTSSG_cBaseOddTriangleFirst_vf2", 0x413990, 32, 0x6548b231, MemberAddress(&nTSSG::cBaseOddTriangleFirst::AddLayeredIndices)},
    {"nTSSG_cBaseOddTriangleSecond_vf2", 0x413a50, 32, 0x02d1911f, MemberAddress(&nTSSG::cBaseOddTriangleSecond::AddLayeredIndices)},
    {"cBaseEvenTriangleSecond_vf0", 0x7a2c06, 32, 0x8c2c99a3, MemberAddress(&cBaseEvenTriangleSecond::AddIndices)},
    {"cBaseOddTriangleSecond_vf0", 0x7a2cb8, 32, 0x990581e5, MemberAddress(&cBaseOddTriangleSecond::AddIndices)},
    // shared_vf bodies referenced only by the triangle polygons' vtables (both copies of the family; the
    // "shared_vf within one family" rule, docs/verification.md); CRCs from tools/hookcrc.py.
    {"shared_vf_413540", 0x413540, 32, 0xaa4a450d, MemberAddress(&nTSSG::cBaseEvenTriangleFirst::AddIndices)},
    {"shared_vf_4136a0", 0x4136a0, 32, 0x5ad7ffbb, MemberAddress(&nTSSG::cBaseOddTriangleFirst::AddIndices)},
    {"cIGZDBPFIndex_vf0", 0x9a7ca0, 32, 0x58410460, MemberAddress(&cIGZDBPFIndex::DeletingDestructor)},
    {"FUN_a075f31a", 0x75f31a, 7, 0x457ce2fa, MemberAddress(&cTSUIService::Destruct)},
    {"cTSUIService_vf6", 0x75f321, 31, 0x789a4441, MemberAddress(&cTSUIService::DeletingDestructor)},
    {"FUN_a0988ba0", 0x988ba0, 32, 0x5db7c706, MemberAddress(&cHTMLLink::Destruct)},
    {"cHTMLLink_vf0", 0x988df3, 32, 0x63023b69, MemberAddress(&cHTMLLink::DeletingDestructor)},
    {"cGZClipboardManagerClipboardData_QueryInterface", 0xc6dd75, 32, 0x1f22c8c7, MemberAddress(&cGZClipboardManagerClipboardData::QueryInterface)},
    {"cGZClipboardManagerClipboardData_vf4", 0xc6daf4, 16, 0x01d79961, MemberAddress(&cGZClipboardManagerClipboardData::QueryData)},
    {"FUN_a0c6d3dd", 0xc6d3dd, 19, 0xa2bb604c, MemberAddress(&cGZClipboardManagerClipboardData::Destruct)},
    {"cGZClipboardManagerClipboardData_vf6", 0xc6d456, 31, 0x646e4770, MemberAddress(&cGZClipboardManagerClipboardData::DeletingDestructor)},
    {"FUN_a0b39370", 0xb39370, 12, 0xdd305926, reinterpret_cast<void *>(&BitMask)},
    {"AVcTSUninstalledNeighborhoodTemplatesFilter_A0xa7ce8a4d_vf0", 0xc2f165, 32, 0x2b19fb01, MemberAddress(&cTSUninstalledNeighborhoodTemplatesFilter::Accept)},
    {"cGZBufferFontRenderer_QueryInterface", 0x90e210, 32, 0x6ee6949f, MemberAddress(&cGZBufferFontRenderer::QueryInterface)},
    {"cGZBufferFontRenderer_Release", 0x90e41f, 26, 0x97208a0a, MemberAddress(&cGZBufferFontRenderer::Release)},
    {"cGZBufferFontRenderer_vf5", 0x90bba9, 17, 0xe2419997, MemberAddress(&cGZBufferFontRenderer::BeginDraw)},
    {"cGZBufferFontRenderer_vf6", 0x90c118, 17, 0x6d230ec0, MemberAddress(&cGZBufferFontRenderer::EndDraw)},
    {"cGZBufferFontRenderer_vf7", 0x90c0af, 32, 0xa89caea1, MemberAddress(&cGZBufferFontRenderer::DrawGlyphs)},
    {"FUN_a074537f", 0x74537f, 32, 0xebe16ab6, MemberAddress(&cTSWinDragAndDropMessageFilter::Dispatch)},
    {"cTSWinDragAndDropMessageFilter_vf3", 0x7452a6, 32, 0x35b69906, MemberAddress(&cTSWinDragAndDropMessageFilter::DoMessage)},
    {"FUN_a074526d", 0x74526d, 26, 0xa5431025, MemberAddress(&cTSWinDragAndDropMessageFilter::Destruct)},
    {"cTSWinDragAndDropMessageFilter_vf4", 0x745287, 31, 0x41b536a6, MemberAddress(&cTSWinDragAndDropMessageFilter::DeletingDestructor)},
    {"cTSWinDragAndDropMessageFilter_vf5", 0x7453d9, 32, 0xea52ea86, MemberAddress(&cTSWinDragAndDropMessageFilter::SetHandler)},
    {"cTSWinDragAndDropMessageFilter_QueryInterface", 0x7453bb, 30, 0x73f3bc64, MemberAddress(&cTSWinDragAndDropMessageFilter::QueryInterface)},
    {"cTSWinDragAndDropMessageFilter_Release", 0x745404, 23, 0xa1fdf88b, MemberAddress(&cTSWinDragAndDropMessageFilter::Release)},
};
REGISTER_HOOKS(kHooks);

} // namespace
