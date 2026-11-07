// cRZKeyboard (lane KQ): the window manager's keyboard state, in the standalone shape (src/gz/cRZKeyboard.h). Built for
// size in the game.
#include "gz/cRZKeyboard.h"

#include "game/import.h"

#include <cstring>

namespace {

// The game's import slots (game/import.h): user32.
using GetSystemMetricsFn = int(__stdcall *)(int);
using GetAsyncKeyStateFn = int16_t(__stdcall *)(int);
using GetKeyStateFn = int16_t(__stdcall *)(int);
constexpr uint32_t kIatGetSystemMetrics = 0xd9a4a4;
constexpr uint32_t kIatGetAsyncKeyState = 0xd9a548;
constexpr uint32_t kIatGetKeyState = 0xd9a5a4;

constexpr int kSwapButton = 0x17; // SM_SWAPBUTTON
constexpr uint32_t kLeftButton = 1, kRightButton = 2; // VK_LBUTTON, VK_RBUTTON

} // namespace

// 0x9cff2f: the vtable, an empty list, then Reset (a direct call).
cRZKeyboard::cRZKeyboard()
{
    cRZKeyboard::Reset();
}

// 0x9cff68: every key up, the order list emptied.
void cRZKeyboard::Reset()
{
    std::memset(mDown, 0, sizeof(mDown));
    mDownOrder.erase(mDownOrder.begin(), mDownOrder.end());
}

// 0x9cffd7: the key is down and the modifiers are exactly `modifiers` (both asked through the vtable).
bool cRZKeyboard::IsKeyDownWithModifiers(uint32_t key, uint32_t modifiers)
{
    bool down = IsKeyDown(key);
    if (down) down = GetModifiers() == modifiers;
    return down;
}

// 0x9cffcc: the recorded state (the key is not range-checked).
bool cRZKeyboard::IsKeyDown(uint32_t key)
{
    return mDown[key];
}

// 0x9d0006 (cdecl).
bool __cdecl cRZKeyboard::AsyncKeyDown(uint32_t key)
{
    if (key == kLeftButton && ImportFunction<GetSystemMetricsFn, kIatGetSystemMetrics>()(kSwapButton)) key = kRightButton;
    const uint32_t state = static_cast<uint16_t>(ImportFunction<GetAsyncKeyStateFn, kIatGetAsyncKeyState>()(int(key)));
    return (state >> 15 & 1) != 0;
}

// 0x9cfff9: Win32's state now (0x9d0006).
bool cRZKeyboard::IsKeyDownNow(uint32_t key)
{
    return AsyncKeyDown(key);
}

// 0x9cff4b: shift, control or alt.
bool cRZKeyboard::IsModifierKey(uint32_t key)
{
    return key == kShift || key == kControl || key == kMenu;
}

// 0x9cff8d: the modifier keys down (recorded state, asked through the vtable): shift 1, control 2, alt 4.
uint32_t cRZKeyboard::GetModifiers()
{
    uint32_t modifiers = IsKeyDown(kShift) ? kModShift : 0;
    if (IsKeyDown(kControl)) modifiers |= kModControl;
    if (IsKeyDown(kMenu)) modifiers |= kModMenu;
    return modifiers;
}

// 0x9d002e: the toggle bit of Win32's key state (0x9d003b).
bool cRZKeyboard::IsKeyToggled(uint32_t key)
{
    return (ImportFunction<GetKeyStateFn, kIatGetKeyState>()(int(key)) & 1) != 0;
}

// 0x9d0048: a key going down is recorded once, at the end of the order list.
void cRZKeyboard::KeyDown(uint32_t key)
{
    if (mDown[key]) return;
    mDown[key] = true;
    mDownOrder.push_back(key);
}

// 0x9d006a: a key going up leaves the list (every entry of it).
void cRZKeyboard::KeyUp(uint32_t key)
{
    if (!mDown[key]) return;
    mDown[key] = false;
    mDownOrder.remove(key);
}
