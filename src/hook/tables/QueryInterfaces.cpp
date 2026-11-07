#include "hook/hooks.h"
#include "rz/QueryInterfaces.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZWinProc4_QueryInterface", 0xb6e020, 32, 0x15d46294, reinterpret_cast<void *>(&qi::WinProc4QueryInterface)},
    {"cTSWinProcGrid_QueryInterface", 0x747105, 32, 0x0afdec12, reinterpret_cast<void *>(&qi::TSWinProcGridQueryInterface)},
    {"cGZWin_QueryInterface", 0x9325d8, 32, 0x75869f3c, reinterpret_cast<void *>(&qi::GZWinQueryInterface)},
    {"cTSWinProcPopup_QueryInterface", 0x749010, 32, 0xcbea235d, reinterpret_cast<void *>(&qi::TSWinProcPopupQueryInterface)},
    {"cTSWinProcBaseObjectDialog_QueryInterface", 0x7c39bb, 32, 0xfd2a92fa,
     reinterpret_cast<void *>(&qi::TSWinProcBaseObjectDialogQueryInterface)},
    {"cTSWinProcUCPSubPanel_QueryInterface", 0x672a95, 32, 0x057ffcb8, reinterpret_cast<void *>(&qi::TSWinProcUCPSubPanelQueryInterface)},
    {"cTSWinProcOptionsSubPanel_QueryInterface", 0x6487b9, 32, 0xe1075ae3,
     reinterpret_cast<void *>(&qi::TSWinProcOptionsSubPanelQueryInterface)},
    {"cTSWinProcGridThumbnail_QueryInterface", 0x75dc5b, 32, 0x49b17d1a,
     reinterpret_cast<void *>(&qi::TSWinProcGridThumbnailQueryInterface)},
    {"cTSUIService_QueryInterface", 0x75f340, 32, 0xc5deb291, reinterpret_cast<void *>(&qi::TSUIServiceQueryInterface)},
    {"nTSCASCore_cTSSkinBase_QueryInterface", 0xb7bbe0, 32, 0x93fe16a7, reinterpret_cast<void *>(&qi::TSSkinBaseQueryInterface)},
    {"cTSWinProcSimEditorDialog_QueryInterface", 0x7c741f, 11, 0x4f91de57,
     reinterpret_cast<void *>(&qi::TSWinProcSimEditorDialogQueryInterface)},
};
REGISTER_HOOKS(kHooks);

} // namespace
