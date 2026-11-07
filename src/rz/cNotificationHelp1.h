#pragma once
// nRZSTD::cNotificationHelp1 (lane MC, foundations F5): a set of message ids a class registers with the message
// server in one call (game vtable 0xde21d8, 2 slots; base cNotificationHelp 0xde21cc, built by 0x6225c9). 25 classes
// build one inline as a member, so the objects carry the game's vtable: the two slot bodies are hooked at their RVAs.
// 0xc bytes: the vtable, then a std::set<uint32_t> (MSVC: the head node at +4, the size at +8; read in place).
#include <cstdint>
#include <set>

class cIGZUnknown;

namespace nRZSTD {

class cNotificationHelp1 {
public:
    static constexpr uint32_t kVTable = 0xde21d8; // the game's
    static constexpr uint32_t kSize = 0xc;

    // 0xb6d960 (slot 0, ret 8): every id in the set added for `target` (its cIGZMessageTarget face) on `server` (its
    // cIGZMessageServer face; null: the framework's message server, service 0x64063387), in the set's order. False,
    // nothing added, when either face is missing.
    bool AddNotifications(cIGZUnknown *server, cIGZUnknown *target);
    // 0xb6e070 (slot 1, ret 8): the same, removing.
    bool RemoveNotifications(cIGZUnknown *server, cIGZUnknown *target);

    const void *mVTable;
    std::set<uint32_t> mIds; // +4
};
static_assert(sizeof(cNotificationHelp1) == cNotificationHelp1::kSize, "cNotificationHelp1 size");

} // namespace nRZSTD
