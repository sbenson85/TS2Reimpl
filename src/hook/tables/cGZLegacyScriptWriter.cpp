// Hooks for the legacy control's script writer (src/gz/cGZLegacyScriptWriter.h), lane FI: the formatting helpers are
// hooked at their RVAs (the per-class writers not yet ours call them).
#include "gz/cGZLegacyScriptWriter.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"LegacyScript_AppendTextAttribute", 0x86f25b, 32, 0x59bffad4, reinterpret_cast<void *>(&AppendTextAttribute)},
    {"LegacyScript_AppendFlagAttributes", 0x86e546, 32, 0x590529e5, reinterpret_cast<void *>(&AppendFlagAttributes)},
    {"LegacyScript_AppendBoolAttribute", 0x86ec88, 32, 0x3c57762a, reinterpret_cast<void *>(&AppendBoolAttribute)},
    {"LegacyScript_AppendChangedBoolAttribute", 0x86ece8, 32, 0xd1c4f1f2, reinterpret_cast<void *>(&AppendChangedBoolAttribute)},
    {"LegacyScript_AppendColorAttribute", 0x86ed50, 32, 0x2302e4c0, reinterpret_cast<void *>(&AppendColorAttribute)},
    {"LegacyScript_AppendColor2Attribute", 0x86edad, 32, 0x9d1e2001, reinterpret_cast<void *>(&AppendColor2Attribute)},
    {"LegacyScript_AppendColor4Attribute", 0x86ee19, 32, 0x34c920d9, reinterpret_cast<void *>(&AppendColor4Attribute)},
    {"LegacyScript_AppendHexAttribute", 0x86eea3, 32, 0xa77d7dcc, reinterpret_cast<void *>(&AppendHexAttribute)},
    {"LegacyScript_AppendKeyAttribute", 0x86eef4, 32, 0x18cb117b, reinterpret_cast<void *>(&AppendKeyAttribute)},
    {"LegacyScript_AppendIntAttribute", 0x86ef48, 32, 0xbc95b680, reinterpret_cast<void *>(&AppendIntAttribute)},
    {"LegacyScript_AppendImageItemAttribute", 0x86ef99, 32, 0x56ecf7e0, reinterpret_cast<void *>(&AppendImageItemAttribute)},
    {"LegacyScript_AppendNumberedPairAttribute", 0x86f075, 32, 0xe35840fc, reinterpret_cast<void *>(&AppendNumberedPairAttribute)},
    {"LegacyScript_AppendPairAttribute", 0x86f0cc, 32, 0x823f24b6, reinterpret_cast<void *>(&AppendPairAttribute)},
    {"LegacyScript_AppendRectAttribute", 0x86f120, 32, 0x6876c8b2, reinterpret_cast<void *>(&AppendRectAttribute)},
    {"LegacyScript_AppendQuotedAttribute", 0x86f17a, 32, 0xacd0b091, reinterpret_cast<void *>(&AppendQuotedAttribute)},
    {"LegacyScript_AppendKey3Attribute", 0x86f1dc, 32, 0x971cd14a, reinterpret_cast<void *>(&AppendKey3Attribute)},
    {"LegacyScript_AppendWordAttribute", 0x86f235, 32, 0x606fa26c, reinterpret_cast<void *>(&AppendWordAttribute)},
    {"LegacyScript_EscapeScriptText", 0x86ebb8, 32, 0x8f7e70b4, reinterpret_cast<void *>(&EscapeScriptText)},
    {"LegacyScript_FormatFontId", 0x86e5f8, 32, 0x80fd4c4e, reinterpret_cast<void *>(&FormatFontId)},
    {"LegacyScript_FormatFontProperty", 0x86e655, 32, 0x36f3b7b1, reinterpret_cast<void *>(&FormatFontProperty)},
    {"LegacyScript_GetImagePairProperty", 0x86e6f8, 32, 0x22365e76, reinterpret_cast<void *>(&GetImagePairProperty)},
    {"LegacyScript_GetKeyProperty", 0x87b080, 32, 0x27207c28, reinterpret_cast<void *>(&GetKeyProperty)},
    {"LegacyScript_GetKeyListEntry", 0x87b0d4, 32, 0x998f02eb, reinterpret_cast<void *>(&GetKeyListEntry)},
    {"LegacyScript_WriteLegacyScript", 0x87a9a9, 32, 0xf6e011a8, reinterpret_cast<void *>(&WriteLegacyScript)},
    {"LegacyScript_WriteLegacyClassHeader", 0x87ae44, 32, 0x34c08049, reinterpret_cast<void *>(&WriteLegacyClassHeader)},
    {"LegacyScript_WriteBitmapAttributes", 0x86fc7d, 32, 0xec699f64, reinterpret_cast<void *>(&WriteBitmapAttributes)},
    {"LegacyScript_WriteTextTickerAttributes", 0x8791f3, 32, 0xb5cf8fdb,
     reinterpret_cast<void *>(&WriteTextTickerAttributes)},
    {"LegacyScript_WriteFlatRectAttributes", 0x872b33, 32, 0xd7b7514f, reinterpret_cast<void *>(&WriteFlatRectAttributes)},
    {"LegacyScript_WriteOutlineAttributes", 0x876ea4, 32, 0xbac79586, reinterpret_cast<void *>(&WriteOutlineAttributes)},
    {"LegacyScript_WriteFoldersAttributes", 0x872f45, 32, 0xeff5f995, reinterpret_cast<void *>(&WriteFoldersAttributes)},
    {"LegacyScript_WriteScrollbarAttributes", 0x87751e, 32, 0x9adc8526,
     reinterpret_cast<void *>(&WriteScrollbarAttributes)},
    {"LegacyScript_WriteScrollbar2Attributes", 0x877189, 32, 0x4d82a3b7,
     reinterpret_cast<void *>(&WriteScrollbar2Attributes)},
    {"LegacyScript_WriteTextAttributes", 0x87822e, 32, 0x0694b5e4, reinterpret_cast<void *>(&WriteTextAttributes)},
    {"LegacyScript_WriteSliderAttributes", 0x8777a6, 32, 0xcb7d71f3, reinterpret_cast<void *>(&WriteSliderAttributes)},
    {"LegacyScript_WriteSpinnerAttributes", 0x877979, 32, 0x43423b29, reinterpret_cast<void *>(&WriteSpinnerAttributes)},
    {"LegacyScript_WriteComboAttributes", 0x870b57, 32, 0x9263627a, reinterpret_cast<void *>(&WriteComboAttributes)},
    {"LegacyScript_WriteLineInputAttributes", 0x87551d, 32, 0x04f81826, reinterpret_cast<void *>(&WriteLineInputAttributes)},
    {"LegacyScript_WriteListBoxAttributes", 0x875ccb, 32, 0x6d9fdddc, reinterpret_cast<void *>(&WriteListBoxAttributes)},
    {"LegacyScript_WriteGenAttributes", 0x8730b3, 32, 0xb34fa84b, reinterpret_cast<void *>(&WriteGenAttributes)},
    {"LegacyScript_WriteTextEditAttributes", 0x878762, 32, 0x5562906a, reinterpret_cast<void *>(&WriteTextEditAttributes)},
    {"LegacyScript_WriteBtnAttributes", 0x86ff03, 32, 0xbeb78c08, reinterpret_cast<void *>(&WriteBtnAttributes)},
    {"LegacyScript_WriteOptGrpAttributes", 0x8764bf, 32, 0x176c15ee, reinterpret_cast<void *>(&WriteOptGrpAttributes)},
    {"LegacyScript_WriteTreeViewAttributes", 0x8792f2, 32, 0x6f607675, reinterpret_cast<void *>(&WriteTreeViewAttributes)},
    {"LegacyScript_WriteWindowAttributes", 0x86f2b8, 32, 0xbe144b01, reinterpret_cast<void *>(&WriteWindowAttributes)},
    {"LegacyScript_WriteFileBrowserAttributes", 0x87117a, 32, 0x1f19091f, reinterpret_cast<void *>(&WriteFileBrowserAttributes)},
    {"LegacyScript_WriteGridAttributes", 0x873913, 32, 0x1991dc2e, reinterpret_cast<void *>(&WriteGridAttributes)},
};
REGISTER_HOOKS(kHooks);

} // namespace
