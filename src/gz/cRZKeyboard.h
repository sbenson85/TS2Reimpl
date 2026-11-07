#pragma once
#include <cstddef>
#include <cstdint>
#include <list>

// cRZKeyboard (lane KQ): the keyboard state the window manager keeps (cGZWinMgrBase +0x6c, src/gz/cGZWinMgrW32.h). It
// records which virtual keys are down (one byte each) and the order they went down, from the key events the manager
// dispatches (KeyDown / KeyUp), and answers the live state from Win32 (GetAsyncKeyState, GetKeyState) through the game's
// import slots (game/import.h).
//
// Game vtable 0xe61c38 (9 slots, no cIGZUnknown: cIGZKeyboard is a plain interface). 0x10c bytes. The only constructor
// 0x9cff2f (vtable, the list, Reset) is called only from the manager's base constructor 0x972aab, and nothing else
// references the vtable, so with the manager ours every keyboard is ours (db/status/cRZKeyboard.jsonl). No destructor
// function: the owner's destructor tidies the list.
class cIGZKeyboard {
public:
    virtual void Reset() = 0;                                                    // slot 0, 0x9cff68
    virtual bool IsKeyDownWithModifiers(uint32_t key, uint32_t modifiers) = 0;  // slot 1, 0x9cffd7
    virtual bool IsKeyDown(uint32_t key) = 0;                                    // slot 2, 0x9cffcc (the raw byte)
    virtual bool IsKeyDownNow(uint32_t key) = 0;                                 // slot 3, 0x9cfff9 (Win32's state)
    virtual bool IsModifierKey(uint32_t key) = 0;                                // slot 4, 0x9cff4b
    virtual uint32_t GetModifiers() = 0;                                         // slot 5, 0x9cff8d
    virtual bool IsKeyToggled(uint32_t key) = 0;                                 // slot 6, 0x9d002e
    virtual void KeyDown(uint32_t key) = 0;                                      // slot 7, 0x9d0048
    virtual void KeyUp(uint32_t key) = 0;                                        // slot 8, 0x9d006a
};

class cRZKeyboard : public cIGZKeyboard {
public:
    static constexpr uint32_t kSize = 0x10c;
    static constexpr uint32_t kKeyCount = 0x100;
    static constexpr uint32_t kShift = 0x10, kControl = 0x11, kMenu = 0x12;            // VK_SHIFT, VK_CONTROL, VK_MENU
    static constexpr uint32_t kModShift = 1, kModControl = 2, kModMenu = 4;            // GetModifiers' bits

    cRZKeyboard(); // 0x9cff2f

    void Reset() override;
    bool IsKeyDownWithModifiers(uint32_t key, uint32_t modifiers) override;
    bool IsKeyDown(uint32_t key) override;
    bool IsKeyDownNow(uint32_t key) override;
    bool IsModifierKey(uint32_t key) override;
    uint32_t GetModifiers() override;
    bool IsKeyToggled(uint32_t key) override;
    void KeyDown(uint32_t key) override;
    void KeyUp(uint32_t key) override;

    // 0x9d0006 (cdecl): whether `key` is down now (GetAsyncKeyState's top bit); VK_LBUTTON asks for the right button when
    // the mouse buttons are swapped. Shared by ~10 other callers in the image, which keep the original.
    static bool __cdecl AsyncKeyDown(uint32_t key);

    std::list<uint32_t> mDownOrder; // +0x04 the keys down, oldest first (KeyDown appends, KeyUp removes)
    bool mDown[kKeyCount];          // +0x0c one byte per virtual key
};

static_assert(sizeof(cRZKeyboard) == cRZKeyboard::kSize, "cRZKeyboard size");
static_assert(offsetof(cRZKeyboard, mDownOrder) == 4 && offsetof(cRZKeyboard, mDown) == 0xc, "cRZKeyboard layout");
