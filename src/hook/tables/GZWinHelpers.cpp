#include "hook/hooks.h"
#include "ui/GZWinHelpers.h"

namespace {

const HookSpec kHooks[] = {
    {"GZWinDrawFrame", 0x92ef70, 32, 0x40f27fed, reinterpret_cast<void *>(&GZWinDrawFrame)},
    {"GZWinDrawBevel", 0x92ee00, 32, 0x4af387b4, reinterpret_cast<void *>(&GZWinDrawBevel)},
    {"GZWinShadeColor", 0x92f310, 32, 0x438175af, reinterpret_cast<void *>(&GZWinShadeColor)},
    {"GZWinHideQuiet", 0x9351b9, 32, 0xd2963881, reinterpret_cast<void *>(&GZWinHideQuiet)},
    {"GZWinShowQuiet", 0x935329, 32, 0x19244401, reinterpret_cast<void *>(&GZWinShowQuiet)},
    {"GZWinEnableOnlyFromList", 0x93509c, 32, 0x1841c312, reinterpret_cast<void *>(&GZWinEnableOnlyFromList)},
    {"GZWinEnableOnlyInRange", 0x9350d2, 32, 0xc89abfa7, reinterpret_cast<void *>(&GZWinEnableOnlyInRange)},
};
REGISTER_HOOKS(kHooks);

} // namespace
