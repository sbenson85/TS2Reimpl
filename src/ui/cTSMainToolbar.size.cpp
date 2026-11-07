// cTSMainToolbar (lane JE): construction, the service, message and serializable faces (src/ui/cTSMainToolbar.h).
#include "rz/RZMessageServer.h"
#include "ui/cTSMainToolbar.h"

#include "app/AppServices2.h" // AcquireTestingService, RawString, AssignServiceString, TidyString
#include "game/game.h"
#include "game/original.h"
#include "gz/LoadUIScript.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cIGZSystemService.h"
#include "misc/UtilitiesC.h" // GetCachedServiceObjectD, KeyMapSlot5Walker
#include "rz/MiscHelpersB.h" // KeyMapWalker
#include "rz/Singleton_20ddfb.h"
#include "ts/cTSString.h"
#include "ui/cTSMainToolbarInternal.h"

#include <cstring>
#include <new>
#include "gz/cGZClipboardManagerClipboard.h"

using namespace toolbar;

// ---- the original callees the slots share ----

namespace toolbar {

void *StateService() { return OriginalFunction<void *(__cdecl *)(), 0x211da3>()(); }
void *Clock() { return OriginalFunction<void *(__cdecl *)(), 0x20df9a>()(); }
void *Notifier() { return static_cast<void *>(GetMessageServer()); }
void *MessageServer() { return GetCachedMessageServer(); }
void *SoundProbe() { return OriginalFunction<void *(__cdecl *)(), 0x9c5b7e>()(); }

// 0x211051 (20 B): the cell 0x10f5194, filled from the service object (0x14323f -> 0x1427b5) when empty.
void *Framework()
{
    void **const cell = GameAddress<void *>(0x10f5194);
    if (!*cell) *cell = GetServiceObject();
    return *cell;
}

void TraceA(uint32_t a, uint32_t b) { OriginalFunction<void(__cdecl *)(uint32_t, uint32_t), 0x61ea5d>()(a, b); }
void TraceB(uint32_t a, uint32_t b) { OriginalFunction<void(__cdecl *)(uint32_t, uint32_t), 0x61eb0d>()(a, b); }

void Trace(uint32_t rva, const void *value)
{
    using Fn = void(__cdecl *)(void *, const void *);
    uint32_t unused[2]; // the callers' 8-byte frame slot; the callees never touch it
    switch (rva) {
    case 0x620af9: OriginalFunction<Fn, 0x620af9>()(unused, value); break;
    case 0x620b15: OriginalFunction<Fn, 0x620b15>()(unused, value); break;
    case 0x620b4d: OriginalFunction<Fn, 0x620b4d>()(unused, value); break;
    case 0x620b69: OriginalFunction<Fn, 0x620b69>()(unused, value); break;
    case 0x620bbd: OriginalFunction<Fn, 0x620bbd>()(unused, value); break;
    default: break;
    }
}

} // namespace toolbar

namespace {

// The message ids the toolbar listens to (Init registers each with the notifier, Shutdown drops them), in the order the
// constructor inserts them.
constexpr uint32_t kMessageIds[] = {
    0x62ba7b44, 0xd1,       0x0dd96f8f, 0xd2,       0xc0,       0xb7,       0x98,       0x86,       0x96,
    0x93,       0xb5,       0x4b46aebf, 0xcbccdece, 0xac646794, 0xd5,       0xcc,       0xde,       0xe3,
    0x4cb22952, 0x6ca29b2d, 0x6ca29b13, 0x4ca29b60, 0x6ca29b91, 0x0ca297f4, 0x6ca297fc, 0x0ccf2ce5, 0xed5059d3,
    0x4d6543b0, 0x6da7796c, 0x8e9dce8e, 0x0e9dd622, 0xcdafda21, 0x0dab45a0, 0xdd,       0xd25c46dd, 0xdf,
};

// The notifier's ids the message target face is registered for (Init: slot 5; Shutdown: slot 6).
constexpr uint32_t kNotifyFirst[] = {0x62ba7b42, 0x62ba7b43};
constexpr uint32_t kNotifyLast[] = {0x8ecc276b, 0xaecc2782, 0xcecc27a8};
constexpr uint32_t kLivePipId = 0x8e00d3c9; // the testing service's "livePIP" entry

// A service-allocated string built from a literal (the library constructor 0x1bec0e: the 16 buffer bytes, the size and the
// capacity zeroed, then assigned) and tidied by 0x1bf753 on every exit.
struct LiteralString {
    as2::RawString mString;
    explicit LiteralString(const char *text)
    {
        std::memset(mString.mBuf, 0, sizeof(mString.mBuf));
        mString.mSize = 0;
        mString.mCapacity = 0;
        as2::AssignServiceString(&mString, nullptr, text, static_cast<uint32_t>(std::strlen(text)));
    }
    LiteralString(const LiteralString &) = delete;
    LiteralString &operator=(const LiteralString &) = delete;
    ~LiteralString() { as2::TidyString(&mString, nullptr); }
};

// The testing service, held for the scope (AcquireTestingService, released by ScopedRef::ReleaseObject).
struct TestingService {
    ScopedRef mRef;
    TestingService() { as2::AcquireTestingService(&mRef, nullptr); }
    TestingService(const TestingService &) = delete;
    TestingService &operator=(const TestingService &) = delete;
    ~TestingService() { mRef.ReleaseObject(); }
};

} // namespace

// ---- construction ----

// 0x619068 (939 B): the service base (id 0xe92db716, priority 0), the members (bytes the original leaves alone stay
// unwritten), two trace calls, the 36 message ids, then the snapshot the modified test compares against.
cTSMainToolbar::cTSMainToolbar()
    : rz::cRZSystemService(kServiceId, 0), mMode(0), mWord48(0), mPauseCount(0), mWord50(0), mWord54(2), mPanelMode(0),
      mWord5C(0), mWord64(0), mWord68(2), mZoom(4), mPaused(true), mPanel(nullptr), mWindow78(nullptr),
      mTickPending(false), mPauseToggle(false), mFlag9C(true), mFlag9D(true), mFlag9E(true), mFlag9F(false), mFlagA0(false),
      mFlagA8(false), mFlagA9(true), mFlagAA(true), mBusy(false), mFlagAD(false), mValueB0(0.0f), mWordB4(1),
      mFlag124(true), mFlag125(false), mFlag126(false), mFlag127(true), mFlag128(false), mFlag129(false)
{
    TraceA(0, 0);
    const void *const descriptors[2] = {GameAddress<void>(0x101ed88), GameAddress<void>(0x101ede4)};
    const void *const pair = descriptors;
    Trace(0x620af9, &pair);
    for (uint32_t id : kMessageIds) mMessageIds.insert(id);
    mSavedPaused = mPaused;
    mSavedWord50 = mWord50;
    mByte30 = 0;
    mModified = false;
    mFlag32 = false;
    mFlag33 = false;
    mSavedWord54 = mWord54;
}

// 0x61a06b (496 B): the members in reverse order (the help text, the counted windows, the stopwatch, the tick list, the
// message id set), then the base.
cTSMainToolbar::~cTSMainToolbar() noexcept {}

cITSMainToolbar *__cdecl cTSMainToolbar::Create() { return new cTSMainToolbar; }

cTSMainToolbar *__fastcall cTSMainToolbarAdapters::Construct(void *memory, void *) { return ::new (memory) cTSMainToolbar; }

// ---- cIGZUnknown ----

// 0x620275 (61 B): the toolbar and serializable faces, else the base's.
bool cTSMainToolbar::QueryInterface(uint32_t riid, void **out)
{
    if (riid == cITSMainToolbar::kIID)
        *out = static_cast<cITSMainToolbar *>(this);
    else if (riid == kSerialIid)
        *out = static_cast<cIGZSerializable *>(this);
    else
        return rz::cRZSystemService::QueryInterface(riid, out);
    AddRef();
    return true;
}

uint32_t cTSMainToolbar::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cTSMainToolbar::Release() { return rz::cRZSystemService::Release(); }

// ---- cIGZSystemService ----

// 0x61e515 (446 B): the help text (table 1, entry 0xb39350() + 1, group 0x7fa7dc7c), the message face registered with the
// notifier (two ids), the testing service's "livePIP" entry, three more ids, the message ids with the notifier
// (KeyMapSlot5Walker), the framework's slot 9 told about the service; trace calls between.
bool cTSMainToolbar::Init()
{
    {
        cTSString text;
        const uint32_t entry = OriginalFunction<uint32_t(__cdecl *)(), 0xb39350>()() + 1;
        text.Construct(1, static_cast<uint16_t>(entry), 0x7fa7dc7c, cTSString::kStringTableType);
        mHelpText.s.mString = text.mString.mString;
        text.Destruct();
    }
    TraceA(0, 0);
    const uint32_t first = 0x22;
    Trace(0x620b4d, &first);
    cIGZMessageTarget *const target = static_cast<cIGZMessageTarget *>(this);
    for (uint32_t id : kNotifyFirst) RawSlot<5, void>(Notifier(), target, id);
    TestingService testing;
    if (testing.mRef.mObject) {
        LiteralString name("livePIP");
        RawSlot<4, void>(testing.mRef.mObject, static_cast<cIGZMessageTarget2 *>(this), kLivePipId, &name.mString, 0u);
    }
    for (uint32_t id : kNotifyLast) RawSlot<5, void>(Notifier(), target, id);
    TraceB(0, 0);
    const void *const descriptors[2] = {GameAddress<void>(0x101ede8), GameAddress<void>(0x101ee44)};
    const void *const pair = descriptors;
    Trace(0x620b69, &pair);
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)
        ->CallSlot5ForEachKey(Word(static_cast<cIGZMessageTarget2 *>(this)), nullptr);
    As<ifaces::cIGZFrameWork>(Framework())->AddToTick(As<ifaces::cIGZSystemService>(this));
    const uint32_t last = 0x53;
    Trace(0x620bbd, &last);
    return true;
}

// 0x62232a (294 B): Init's registrations dropped (slot 6 of the notifier, the testing service's slot 5, the walker), the
// panel object deleted (its slot 0 with 1), the tick list emptied.
bool cTSMainToolbar::Shutdown()
{
    cIGZMessageTarget *const target = static_cast<cIGZMessageTarget *>(this);
    for (uint32_t id : kNotifyFirst) RawSlot<6, void>(Notifier(), target, id);
    for (uint32_t id : kNotifyLast) RawSlot<6, void>(Notifier(), target, id);
    TestingService testing;
    if (testing.mRef.mObject) {
        LiteralString name("livePIP");
        RawSlot<5, void>(testing.mRef.mObject, static_cast<cIGZMessageTarget2 *>(this), kLivePipId, &name.mString);
    }
    reinterpret_cast<KeyMapWalker *>(&mMessageIds)->CallSlot6ForEachKey(Word(static_cast<cIGZMessageTarget2 *>(this)), nullptr);
    if (mPanel) {
        RawSlot<0, void>(mPanel, 1u);
        mPanel = nullptr;
    }
    mTickObjects.clear();
    return true;
}

// 0x62013a (163 B): with a tick pending, each listed object the service object D still knows (its slot 22) handed to
// ApplyScriptConditions (0xb42250) and kept, the others dropped from the list; then the base's answer (true).
bool cTSMainToolbar::OnTick(uint32_t counter)
{
    if (mTickPending) {
        auto it = mTickObjects.begin();
        while (it != mTickObjects.end()) {
            const ToolbarRef object = *it; // held across the erase
            if (RawSlot<22, bool>(GetCachedServiceObjectD(), object.Get())) {
                ApplyScriptConditions(static_cast<cIGZUnknown *>(object.Get()));
                ++it;
            } else {
                it = mTickObjects.erase(it);
            }
        }
        mTickPending = false;
    }
    return rz::cRZSystemService::OnTick(counter);
}

// ---- cIGZMessageTarget / cIGZMessageTarget2: still game code (forwarded on their faces) ----

bool cTSMainToolbar::DoMessage(cGZMessage *message)
{
    return OriginalFunction<bool(__thiscall *)(void *, cGZMessage *), 0x61b99c>()(static_cast<cIGZMessageTarget *>(this),
                                                                                   message);
}

bool cTSMainToolbar::DoMessage(void *message)
{
    return OriginalFunction<bool(__thiscall *)(void *, void *), 0x61bc58>()(static_cast<cIGZMessageTarget2 *>(this),
                                                                            message);
}

// ---- cIGZSerializable ----

void cTSMainToolbar::Snapshot()
{
    mModified = false;
    mFlag32 = false;
    mFlag33 = false;
    mSavedPaused = mPaused;
    mSavedWord50 = mWord50;
    mSavedWord54 = mWord54;
}

// 0x62255c (109 B): version 3, the pause byte, the toolbar's Word50 and Word54 (its slots 26 and 28), then the snapshot;
// true when the stream has no error (its slot 18 answers 0).
bool cTSMainToolbar::Write(ifaces::cIGZOStream *stream)
{
    stream->SetSint32(3u);
    stream->SetSint32(static_cast<uint32_t>(*reinterpret_cast<const uint8_t *>(&mPaused)));
    cITSMainToolbar *const self = this;
    stream->SetSint32(static_cast<uint32_t>(self->Word50()));
    stream->SetSint32(static_cast<uint32_t>(self->Word54()));
    Snapshot();
    return stream->GetError() == 0;
}

// 0x6202da (195 B): the version, the pause word (version 0: paused, and the word read again), Word50, Word54, a word
// version 2 has; the snapshot; the notifier's slot 4 told with message 0xcecc27a8; true when the stream has no error.
bool cTSMainToolbar::Read(ifaces::cIGZIStream *stream)
{
    int32_t version;
    int32_t value;
    stream->GetSint32(&version);
    stream->GetSint32(&value);
    if (version == 0) {
        mPaused = true;
        stream->GetSint32(&value);
    } else {
        mPaused = value != 0;
    }
    stream->GetSint32(&mWord50);
    stream->GetSint32(&value);
    mWord54 = static_cast<int32_t>(value);
    if (version == 2) {
        int32_t unused;
        stream->GetSint32(&unused);
    }
    Snapshot();
    const uint32_t message[4] = {0xcecc27a8, 0, 0, 0};
    RawSlot<4, void>(Notifier(), message, 0u);
    return stream->GetError() == 0;
}

uint32_t cTSMainToolbar::GetGZCLSID() { return kClassId; }
