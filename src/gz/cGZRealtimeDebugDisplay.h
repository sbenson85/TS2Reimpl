#pragma once
// cGZRealtimeDebugDisplay (lane LA): the framework's realtime debug display service (system service id 0x8414a2c2,
// priority 4,000,000; cIGZRealtimeDebugDisplay iid 0x6054a2fb), and the entries it keeps (gz::cGZRTDEntry,
// gz::cGZRTDValueEntry). 0x34 bytes, built for size (/O1). RVAs 1.18.0.187.
//
// It holds a list of entries of three kinds (the byte at entry +4: 0, 1, 2) and a visible flag, and tells the message
// server about every change: Show / Hide post 0x628ff806 / 0xe28ff811, every add posts 0x428e9072, every successful
// remove sends 0x428e9072 at once (cIGZMessageServer slot 3 instead of 4). The message server (service 0x64063387,
// cIGZMessageServer 0xc1085722) is fetched from the framework on first use (Init or any slot that posts) and held
// counted until Shutdown. Nothing in the class reads the entries back: the display only collects them and notifies.
//
// Faces (RTTI order): +0 cRZSystemService / cIGZSystemService (game vtable 0xe5b0f4, 13 slots), +4 cRZUnknownMT (0xe5b12c,
// 6: slot 3 the deleting destructor's thunk 0x970577), +0x1c cIGZRealtimeDebugDisplay (0xe5b148, 14).
//
// Standalone shape (docs/standalone-shape.md): two creation sites call the constructor 0x9704bb on a 0x34-byte block,
// cTSAppFWCOMDirector::OnStart (0xc2beec, ours) and the COM class factory 0x92f5ed (class id 0x0414a2b1, in the
// cGZWinCOMDirector's class table). OnStart builds ours (through the Construct adapter below) and the factory is hooked
// (Create), and the vtables are referenced only by the constructor and the destructor 0x97054b. The entries' constructors
// (0x970399, 0x9703bd, 0x9703fc) are called only by the display's add slots, their vtables (0xe5b0d4, 0xe5b0e4)
// referenced only by those and the destructors 0x9704f9 / 0x97050e, so with the factory hook every display and every
// entry carries our vtables.
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <vector>

namespace ifaces {
class cIGZMessageServer;
} // namespace ifaces

namespace gz {

// cGZRTDEntry (vtable 0xe5b0d4, 3 slots; 0x2c bytes): a counted entry (the count is the byte at +5) with a kind, a key
// and a name. The display makes kind 0 entries of this class (AddEntry0) and never sets the name.
class cGZRTDEntry {
public:
    static constexpr uint32_t kSize = 0x2c;

    explicit cGZRTDEntry(uint32_t key); // 0x970399 (ret 4): kind and count 0, an empty name
    cGZRTDEntry(const cGZRTDEntry &) = delete;
    cGZRTDEntry &operator=(const cGZRTDEntry &) = delete;

    virtual uint32_t AddRef();     // slot 0, 0x97077e: the count byte + 1, returned widened
    virtual uint32_t Release();    // slot 1, 0x9709dd: above 1 the count - 1; else deleted, 0
    virtual ~cGZRTDEntry();        // slot 2, deleting 0x97057f (destructor 0x9704f9)

    uint8_t mKind;  // +4: 0, 1 or 2 (which add slot made it)
    uint8_t mRefs;  // +5
                    // +6, +7: never written (the constructor stores the word at +4 and the dword at +8)
    uint32_t mKey;  // +8: the kind 0 key (AddEntry0's argument; 0 for value entries)
    union {
        cRZString mName; // +0xc: built empty (0x144f5b), never set by the display
    };
};
static_assert(sizeof(cGZRTDEntry) == cGZRTDEntry::kSize, "cGZRTDEntry size (AddEntry0's new; the deleting destructor's delete)");
static_assert(offsetof(cGZRTDEntry, mKind) == 4 && offsetof(cGZRTDEntry, mRefs) == 5 &&
                  offsetof(cGZRTDEntry, mKey) == 8 && offsetof(cGZRTDEntry, mName) == 0xc,
              "cGZRTDEntry layout");

// cGZRTDValueEntry (vtable 0xe5b0e4, 3 slots: AddRef / Release the base's through jump stubs; 0x54 bytes): an entry with
// a second key and a value, and private copies of a list of texts.
class cGZRTDValueEntry : public cGZRTDEntry {
public:
    static constexpr uint32_t kSize = 0x54;

    cGZRTDValueEntry(uint32_t key, uint32_t value); // 0x9703bd (ret 8)
    // 0x9703fc (ret 0x10): each of the `count` texts AddRef'd (never released: ORIGINAL-BUG) and copied into a new
    // cRZString (counted once), in order.
    cGZRTDValueEntry(uint32_t key, uint32_t value, cIGZString *const *texts, uint32_t count);
    ~cGZRTDValueEntry() override; // 0x97050e (deleting 0x97059e): the first mTextCount copies released

    uint32_t mValueKey;                 // +0x2c: the kind 1 / 2 key
    uint32_t mValue;                    // +0x30
    cIGZString *const *mTexts;          // +0x34: the caller's array, kept as given (0 from the two-argument constructor)
    uint32_t mTextCount;                // +0x38
    std::vector<uint32_t> mUnused;      // +0x3c: always empty (built empty, freed by the destructor, nothing else)
    std::vector<cRZString *> mCopies;   // +0x48: the copies (each counted)
};
static_assert(sizeof(cGZRTDValueEntry) == cGZRTDValueEntry::kSize, "cGZRTDValueEntry size (the add slots' new; the deleting destructor's delete)");
static_assert(offsetof(cGZRTDValueEntry, mValueKey) == 0x2c && offsetof(cGZRTDValueEntry, mValue) == 0x30 &&
                  offsetof(cGZRTDValueEntry, mTexts) == 0x34 && offsetof(cGZRTDValueEntry, mTextCount) == 0x38 &&
                  offsetof(cGZRTDValueEntry, mUnused) == 0x3c && offsetof(cGZRTDValueEntry, mCopies) == 0x48,
              "cGZRTDValueEntry layout");

} // namespace gz

// cIGZRealtimeDebugDisplay (iid 0x6054a2fb), slots 3-13. The add slots return true; the remove slots whether an entry of
// their kind had the key (the first such entry is removed).
class cIGZRealtimeDebugDisplay : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x6054a2fb;
    virtual uint8_t Show() = 0;                                  // 3 (0x970c12): the post's result byte is left in al
    virtual uint8_t Hide() = 0;                                  // 4 (0x9708a7): likewise
    virtual bool IsVisible() = 0;                                // 5 (folded 0x1458b0)
    virtual bool AddEntry0(uint32_t key) = 0;                    // 6 (0x9706fc, ret 4)
    virtual bool RemoveEntry0(uint32_t key) = 0;                 // 7 (0x970aae, ret 4)
    virtual bool AddTextEntry1(uint32_t key, uint32_t value, cIGZString *const *texts, uint32_t count) = 0; // 8 (0x970669, ret 0x10)
    virtual bool AddEntry1(uint32_t key, uint32_t value) = 0;    // 9 (0x9705dc, ret 8)
    virtual bool RemoveEntry1(uint32_t key) = 0;                 // 10 (0x9709fc, ret 4)
    virtual bool AddTextEntry0(uint32_t key, uint32_t value, cIGZString *const *texts, uint32_t count) = 0; // 11 (0x970818, ret 0x10)
    virtual bool AddEntry2(uint32_t key, uint32_t value) = 0;    // 12 (0x97078b, ret 8)
    virtual bool RemoveEntry2(uint32_t key) = 0;                 // 13 (0x970b60, ret 4)
};

class cGZRealtimeDebugDisplay : public rz::cRZSystemService, public cIGZRealtimeDebugDisplay {
public:
    static constexpr uint32_t kVTable = 0xe5b0f4;
    static constexpr uint32_t kVTable4 = 0xe5b12c;
    static constexpr uint32_t kDisplayVTable = 0xe5b148;
    static constexpr uint32_t kSize = 0x34;
    static constexpr uint32_t kServiceId = 0x8414a2c2;
    static constexpr int32_t kPriority = 4000000;
    // Messages (ids; the three data words are 0).
    static constexpr uint32_t kShownMessage = 0x628ff806;
    static constexpr uint32_t kHiddenMessage = 0xe28ff811;
    static constexpr uint32_t kChangedMessage = 0x428e9072;

    // 0x92f5ed: the COM class factory (class id 0x0414a2b1, registered by the cGZWinCOMDirector constructor 0x92f3a7):
    // a new display as its cIGZRealtimeDebugDisplay face, null when the allocation fails.
    static cIGZRealtimeDebugDisplay *__cdecl Create();

    cGZRealtimeDebugDisplay();                    // 0x9704bb
    ~cGZRealtimeDebugDisplay() noexcept override; // 0x97054b (deleting 0x9705bd; +4 thunk 0x970577)
    cGZRealtimeDebugDisplay(const cGZRealtimeDebugDisplay &) = delete;
    cGZRealtimeDebugDisplay &operator=(const cGZRealtimeDebugDisplay &) = delete;

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x9709a3; thunks 0x9709cd (+4) and 0x9709d5 (+0x1c)
    uint32_t AddRef() override;                              // folded (the base's)
    uint32_t Release() override;                             // folded (the base's)

    // ---- cIGZSystemService ----
    bool Init() override;     // 0x9708f7
    bool Shutdown() override; // 0x970c63

    // ---- cIGZRealtimeDebugDisplay (+0x1c) ----
    uint8_t Show() override;
    uint8_t Hide() override;
    bool IsVisible() override;
    bool AddEntry0(uint32_t key) override;
    bool RemoveEntry0(uint32_t key) override;
    bool AddTextEntry1(uint32_t key, uint32_t value, cIGZString *const *texts, uint32_t count) override;
    bool AddEntry1(uint32_t key, uint32_t value) override;
    bool RemoveEntry1(uint32_t key) override;
    bool AddTextEntry0(uint32_t key, uint32_t value, cIGZString *const *texts, uint32_t count) override;
    bool AddEntry2(uint32_t key, uint32_t value) override;
    bool RemoveEntry2(uint32_t key) override;

    // +0x00 .. +0x1b: cRZSystemService; +0x1c: the cIGZRealtimeDebugDisplay face's vtable
    bool mVisible;                            // +0x20 Show / Hide
    cIGZUnknown *mServer;                     // +0x24 the message server, counted; fetched on first use, dropped by Shutdown
    std::list<gz::cGZRTDEntry *> mEntries;    // +0x28 each counted once, in the order added
    uint32_t mUnused30;                       // +0x30 zeroed by the constructor, read by nothing

private:
    ifaces::cIGZMessageServer *Server(); // the message server, fetched first when not held (inlined in every slot)
    bool Add(gz::cGZRTDEntry *entry);    // the add slots' common tail: counted, listed, 0x428e9072 posted
    bool Remove(gz::cGZRTDEntry *found); // the remove slots' tail: unlisted, released, 0x428e9072 sent
    void Post(uint32_t id);              // a message with `id` posted (not urgent)
};

static_assert(sizeof(cGZRealtimeDebugDisplay) == cGZRealtimeDebugDisplay::kSize, "new 0x34 in cTSAppFWCOMDirector::OnStart");
static_assert(offsetof(cGZRealtimeDebugDisplay, mVisible) == 0x20 && offsetof(cGZRealtimeDebugDisplay, mServer) == 0x24 &&
                  offsetof(cGZRealtimeDebugDisplay, mEntries) == 0x28 && offsetof(cGZRealtimeDebugDisplay, mUnused30) == 0x30,
              "cGZRealtimeDebugDisplay layout");

// The constructor's entry point (0x9704bb's convention, not hooked): cTSAppFWCOMDirector::OnStart builds the display
// through it, and the tests patch it as they patch the original constructor.
struct cGZRealtimeDebugDisplayAdapters {
    static cGZRealtimeDebugDisplay *__fastcall Construct(void *memory, void *edx); // 0x9704bb
};
