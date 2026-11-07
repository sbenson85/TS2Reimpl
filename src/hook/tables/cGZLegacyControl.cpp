// Hooks for cGZLegacyControl (src/gz/cGZLegacyControl.size.cpp), lane MC: the standalone shape. The UIScript
// director's creator 0x85b2b3 (class 0xfa450242) is the only way to the constructor 0x85d62d (a tail jump), and the
// vtables 0xe482a4 / 0xe482d4 / 0xe482f4 are stored only by that constructor and the destructor 0x85d82b: with this
// hook every control carries our vtables, and the slots are in effect through callers. FindUint32Attribute 0x86401e is
// hooked at its RVA (the window classes' handlers call it), and so are lane MD's readers and ApplyCommonProperties (F6's
// original handlers and other original code call them; the tooltip list's vtable 0xe486ac is stored only by 0x86549d).
// LoadUIScript 0xb41b50 and its helpers (lane MD) are hooked at their RVAs: about 150 UI classes call them.
// Lane NH: the grid's matrix constructor 0x85d3bc (it has a second original caller, 0x940ae3), its setters
// 0x8640a4 / 0x86422c and the file browser's key helper 0x87b143 are hooked at their RVAs (Grid and FileBrowser's handlers,
// ours, call ours directly; the original callers, if any, reach the hooks).
#include "gz/LoadUIScript.h"
#include "gz/cGZLegacyControl.h"
#include "gz/cGZLegacyGridMatrix.h"
#include "gz/cGZLegacyHandlers.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZLegacyControl_Create", 0x85b2b3, 22, 0x23203d9e, reinterpret_cast<void *>(static_cast<cIGZScriptableControl *(__cdecl *)()>(&cGZLegacyControl::Create))},
    {"FindUint32Attribute", 0x86401e, 32, 0xa08eda81, reinterpret_cast<void *>(&FindUint32Attribute)},
    {"FindTextAttribute", 0x863fae, 32, 0x5c404350, reinterpret_cast<void *>(&FindTextAttribute)},
    {"FindPairAttribute", 0x863e87, 32, 0xe203f762, reinterpret_cast<void *>(&FindPairAttribute)},
    {"FindColorAttribute", 0x863e06, 32, 0x5b4ed377, reinterpret_cast<void *>(&FindColorAttribute)},
    {"HasAttribute", 0x864077, 32, 0xbbb35609, reinterpret_cast<void *>(&HasAttribute)},
    {"LoadLocalizedText", 0x86dceb, 32, 0xcecbc19b, reinterpret_cast<void *>(&LoadLocalizedText)},
    {"SetResourceKeyProperty", 0x87b35c, 32, 0xe2451c51, reinterpret_cast<void *>(&SetResourceKeyProperty)},
    {"ApplyCommonProperties", 0x86549d, 32, 0x5c69e8ed, reinterpret_cast<void *>(&ApplyCommonProperties)},
    {"EvaluateWindowCondition", 0xb40d20, 32, 0x29bdcdd8, reinterpret_cast<void *>(&EvaluateWindowCondition)},
    {"ApplyScriptConditions", 0xb42250, 32, 0x39502f18, reinterpret_cast<void *>(&ApplyScriptConditions)},
    {"UpdateScriptScreenSize", 0xb429b0, 32, 0xff039456, reinterpret_cast<void *>(&UpdateScriptScreenSize)},
    {"LoadUIScript", 0xb41b50, 32, 0x4bbcc115, reinterpret_cast<void *>(&LoadUIScript)},
    {"FindRectAttribute", 0x863ee9, 32, 0xaf3313f1, reinterpret_cast<void *>(&FindRectAttribute)},
    {"LoadImageResource", 0x86dc44, 32, 0xc9864d73, reinterpret_cast<void *>(&LoadImageResource)},
    {"SetImagePairProperty", 0x86df76, 32, 0x66850af0, reinterpret_cast<void *>(&SetImagePairProperty)},
    {"ApplyBitmapProperties", 0x864376, 32, 0x57811b65, reinterpret_cast<void *>(&ApplyBitmapProperties)},
    {"FUN_a08674f6", 0x8674f6, 32, 0x4080e1ad, &ApplyFlatRectProperties},
    {"FUN_a086b2be", 0x86b2be, 32, 0x1489b979, &ApplyTextProperties},
    {"FUN_a086df41", 0x86df41, 32, 0x30c1b624, &SetUint32WindowProperty},
    {"FUN_a086df0c", 0x86df0c, 32, 0x95fa0402, &SetTextWindowProperty},
    {"cGZLegacyControl_FindSint32Attribute", 0x863f55, 32, 0xa08eda81, reinterpret_cast<void *>(&FindSint32Attribute)},
    {"FUN_a086aa7f", 0x86aa7f, 32, 0x7557716c, &ApplySliderProperties},
    {"FUN_a086a820", 0x86a820, 32, 0x4920f2e2, &ApplyScrollbarProperties},
    {"FUN_a086dbb5", 0x86dbb5, 32, 0x70bb6262, reinterpret_cast<void *>(&LoadImageByKey)},
    {"FUN_a086a2a5", 0x86a2a5, 32, 0xdef9e770, &ApplyOutlineProperties},
    {"FUN_a086a4a8", 0x86a4a8, 32, 0xb5153e3b, &ApplyScrollbar2Properties},
    {"FUN_a08676ea", 0x8676ea, 32, 0x9c27729d, &ApplyFoldersProperties},
    {"FUN_a0865043", 0x865043, 32, 0xdace435d, &ApplyComboProperties},
    {"FUN_a0868e93", 0x868e93, 32, 0xf5cc0ae5, &ApplyListBoxProperties},
    {"FUN_a08688a3", 0x8688a3, 32, 0x5ff84de5, &ApplyLineInputProperties},
    {"cGZLegacyControl_ApplySpinnerProperties", 0x86ac57, 32, 0x89ec4018, &ApplySpinnerProperties},
    {"FUN_a086be58", 0x86be58, 32, 0xb50988b8, &ApplyTextTickerProperties},
    {"FUN_a08645be", 0x8645be, 32, 0x6c379adc, &ApplyButtonProperties},
    {"FUN_a086b5b0", 0x86b5b0, 32, 0x5e7c8a46, &ApplyTextEditProperties},
    {"FUN_a0863aa8", 0x863aa8, 32, 0x95b8c745, &ApplyFlagTable},
    {"cGZLegacyControl_ApplyOptionGroupProperties", 0x86942b, 32, 0xcb29b469, &ApplyOptionGroupProperties},
    {"cGZLegacyControl_ApplyGenProperties", 0x86d5ee, 32, 0xcfbe4119, &ApplyGenProperties},
    {"cGZLegacyControl_ApplyFileBrowserProperties", 0x866410, 32, 0xa27b81d5, &ApplyFileBrowserProperties},
    {"cGZLegacyControl_ApplyGridProperties", 0x867920, 32, 0xb5ad3013, &ApplyGridProperties},
    {"cGZLegacyControl_ApplyTreeViewProperties", 0x86c19e, 32, 0x997ad0d4, &ApplyTreeViewProperties},
    {"cGZLegacyControl_ParseGridCell", 0x863ba3, 32, 0xb7f7f0a1, reinterpret_cast<void *>(&ParseGridCell)},
    {"cGZLegacyControl_ConstructGridMatrix", 0x85d3bc, 32, 0x69f5393a, reinterpret_cast<void *>(&legacygrid::ConstructGridMatrix)},
    {"cGZLegacyControl_GridMatrixSetCell", 0x8640a4, 32, 0x81211d24, MemberAddress(&legacygrid::CellMatrix::Set)},
    {"cGZLegacyControl_GridMatrixSetEntry", 0x86422c, 32, 0x9502eaf7, MemberAddress(&legacygrid::LineMatrix::Set)},
    {"cGZLegacyControl_CellMatrixDestroy", 0x85d901, 32, 0x63199c11, reinterpret_cast<void *>(&legacygrid::CellMatrix::Destroy)},
    {"cGZLegacyControl_LineMatrixDestroy", 0x85d928, 32, 0x87393780, reinterpret_cast<void *>(&legacygrid::LineMatrix::Destroy)},
    {"cGZLegacyControl_GridCellConstruct", 0x85d5f0, 32, 0xc41d7370, reinterpret_cast<void *>(&legacygrid::GridCell::Construct)},
    {"cGZLegacyControl_GridCellCopy", 0x85d59d, 32, 0xb5731826, reinterpret_cast<void *>(&legacygrid::GridCell::Copy)},
    {"cGZLegacyControl_GridCellAssign", 0x85d8b2, 32, 0xf1a10695, reinterpret_cast<void *>(&legacygrid::GridCell::Assign)},
    {"cGZLegacyControl_AppendFileBrowserKey", 0x87b143, 32, 0xcab811fb, reinterpret_cast<void *>(&AppendFileBrowserKey)},
};
REGISTER_HOOKS(kHooks);

} // namespace
