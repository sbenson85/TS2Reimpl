#pragma once
// cTSAudioEvent (lane IY): the audio system's event, a typed property set that is also a framework message. The sound
// manager and the audio layers build one per request (new 0x210087, then the constructor 0x20ff24 with the event id; the
// framework's class 0xcbc79579 through the default constructor 0x20ff8d), fill it through the typed setters and post it.
// RVAs 1.18.0.187.
//
// 0x34 bytes, three faces (RTTI: cITSAudioEvent <- cIGZSndEventStd <- cIGZUnknown at +0, cIGZMessage2Standard <- cIGZMessage2
// at +4, cGZPropertySetT<cKeyUint32Traits> at +8):
//   +0x00 vtable 0xda8200 (30 slots)   +0x04 vtable 0xda827c (37 slots)   +0x08 the property set (vtable 0xda8314, 15 slots)
//   +0x20 reference count               +0x24 user word (slots 28 / 29; never initialised)
//   +0x28 message type (0xdb53383b)     +0x2c event id (slots 4 / 5)      +0x30 counted object (message slots 31..33)
// Its blocks come from its own pool (cRZFastAllocator<cTSAudioEvent> at 0x10cc3e8, under the lock at 0x10cc3f4).
//
// Standalone shape (docs/standalone-shape.md): the two constructors and the class operator new / delete are hooked (delete
// also for the new-expressions' unwind funclets); the vtables are referenced by the constructors and the destructor
// 0x21002f alone (tools/xrefs.py), so every slot, the destructor and the deleting destructor 0x2101c5 are ours in effect
// through those callers.
#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "gz/cIGZAny.h"
#include "gz/cIGZMessage2Standard.h"

#include <cstddef>
#include <cstdint>

class cIGZString;

// The event interface (cITSAudioEvent, iid 0x0bc7957d; with cIGZSndEventStd, iid 0x5b523ba4, at the same address). The typed
// accessors build a value of their type and go through slots 24 / 25 / 27; the Has* slots are one body (slot 27 on the key).
class cITSAudioEvent : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x0bc7957d;
    static constexpr uint32_t kSndEventIid = 0x5b523ba4;
    virtual bool IsEvent() = 0;                                            // slot 3: true
    virtual uint32_t GetEventId() = 0;                                     // slot 4
    virtual uint32_t SetEventId(uint32_t id) = 0;                          // slot 5: returns id
    virtual cIGZMessage2Standard *AsMessage() = 0;                         // slot 6: the +4 face (null for null)
    virtual bool HasFloat(uint32_t key) = 0;                               // slot 7
    virtual float GetFloat(uint32_t key) = 0;                              // slot 8: 0 when absent
    virtual void SetFloat(uint32_t key, float value) = 0;                  // slot 9
    virtual bool HasUint32(uint32_t key) = 0;                              // slot 10
    virtual uint32_t GetUint32(uint32_t key) = 0;                          // slot 11
    virtual void SetUint32(uint32_t key, uint32_t value) = 0;              // slot 12
    virtual bool HasSint32(uint32_t key) = 0;                              // slot 13
    virtual int32_t GetSint32(uint32_t key) = 0;                           // slot 14
    virtual void SetSint32(uint32_t key, int32_t value) = 0;               // slot 15
    virtual bool HasVector3(uint32_t key) = 0;                             // slot 16
    virtual bool GetVector3(uint32_t key, float *out) = 0;                 // slot 17: out written only on success
    virtual void SetVector3(uint32_t key, const float *value) = 0;         // slot 18
    virtual bool HasText(uint32_t key) = 0;                                // slot 19
    virtual bool GetText(uint32_t key, cIGZString *out) = 0;               // slot 20: out's Copy (slot 17) on success
    virtual void SetText(uint32_t key, const cIGZString *text) = 0;        // slot 21
    virtual bool GetFirst(uint32_t *key, void **value) = 0;                // slot 22
    virtual bool GetNext(uint32_t *key, void **value) = 0;                 // slot 23
    virtual bool SetProperty(const uint32_t *key, void *value) = 0;        // slot 24
    virtual bool GetPropertyValue(const uint32_t *key, void *out) = 0;     // slot 25
    virtual void *GetProperty(const uint32_t *key) = 0;                    // slot 26
    virtual bool HasProperty(const uint32_t *key) = 0;                     // slot 27
    virtual uint32_t GetUserWord() = 0;                                    // slot 28
    virtual uint32_t SetUserWord(uint32_t value) = 0;                      // slot 29: returns value
};

namespace nGZNumerics {

// AnyVector3 (vtable 0xda8354; type id 0x2bcb870c): three floats. Built only by cTSAudioEvent's slots 17 / 18 and its own
// Clone, so its slots are ours in effect through those callers.
struct AnyVector3 : public cIGZAny { // a struct, as the original (RTTI .?AUAnyVector3@nGZNumerics@@)
    static constexpr uint32_t kType = 0x2bcb870c;
    AnyVector3() = default; // the floats left as they are (the original's local in slot 17)
    explicit AnyVector3(const float *value) : mValue{value[0], value[1], value[2]} {}
    AnyVector3(const AnyVector3 &) = default;
    ~AnyVector3() override = default;                      // slot 0, 0x2101a5 (delete 0x10)
    bool Assign(const cIGZAny &source) override;           // slot 1, 0x210285: same type only
    int32_t Compare(const cIGZAny &other) const override;  // slot 2, 0x210232
    cIGZAny *Clone() const override;                       // slot 3, 0x21020c
    uint32_t GetType() const override;                     // slot 4, 0x210b21
    const char *GetTypeName() const override;              // slot 5, 0x210b27: "AnyVector3"

    float mValue[3]; // +4
};
static_assert(sizeof(AnyVector3) == 0x10);

} // namespace nGZNumerics

class cTSAudioEvent : public cITSAudioEvent, public cIGZMessage2Standard, public cGZPropertySetUint32 {
public:
    static constexpr uint32_t kSize = 0x34;
    static constexpr uint32_t kMessageType = 0xdb53383b;
    static constexpr uint32_t kPool = 0x10cc3e8; // cRZFastAllocator<cTSAudioEvent>
    static constexpr uint32_t kPoolLock = 0x10cc3f4;
    // The message face's property keys.
    static constexpr uint32_t kData1Key = 0xebd5d033;
    static constexpr uint32_t kData2Key = 0x4bd5d036;
    static constexpr uint32_t kData3Key = 0x8bd5d039;
    static constexpr uint32_t kData4Key = 0x8bd5d03c;
    static constexpr uint32_t kStringKey = 0xebcb7385;
    static constexpr uint32_t kExtraKey = 0x8bcb738c;

    explicit cTSAudioEvent(uint32_t eventId); // 0x20ff24
    cTSAudioEvent();                          // 0x20ff8d: event id 0
    ~cTSAudioEvent() override;                // 0x21002f; the deleting destructor 0x2101c5 (the +8 face's slot 13, thunk 0x21019d)

    static void *operator new(size_t size);             // 0x210087: 0x34 bytes from the pool under its lock
    static void operator delete(void *block, size_t size); // 0x2100f1

    // cIGZUnknown (all three faces; thunks 0x21078c / 0x2101fc / 0x2107bc at +4, 0x210794 / 0x210204 / 0x2107c4 at +8)
    bool QueryInterface(uint32_t riid, void **out) override; // 0x21071b
    uint32_t AddRef() override;                              // 0x2101f2
    uint32_t Release() override;                             // 0x21079c: the +8 face's slot 13 (flag 1) at zero

    // cITSAudioEvent
    bool IsEvent() override;                               // slot 3, shared_vf_144910
    uint32_t GetEventId() override;                        // slot 4, shared_vf_147770
    uint32_t SetEventId(uint32_t id) override;             // slot 5, 0x2108e0
    cIGZMessage2Standard *AsMessage() override;            // slot 6, shared_vf_1c04e0
    bool HasFloat(uint32_t key) override;                  // slots 7 / 10 / 13 / 16 / 19, 0x210544
    float GetFloat(uint32_t key) override;                 // slot 8, 0x21048e
    void SetFloat(uint32_t key, float value) override;     // slot 9, 0x21095d
    bool HasUint32(uint32_t key) override;
    uint32_t GetUint32(uint32_t key) override;             // slot 11, 0x210677
    void SetUint32(uint32_t key, uint32_t value) override; // slot 12, 0x210a99
    bool HasSint32(uint32_t key) override;
    int32_t GetSint32(uint32_t key) override;              // slot 14, 0x210572
    void SetSint32(uint32_t key, int32_t value) override;  // slot 15, 0x2109ba
    bool HasVector3(uint32_t key) override;
    bool GetVector3(uint32_t key, float *out) override;    // slot 17, 0x2106b0
    void SetVector3(uint32_t key, const float *value) override; // slot 18, 0x210ae1
    bool HasText(uint32_t key) override;
    bool GetText(uint32_t key, cIGZString *out) override;  // slot 20, 0x210615
    void SetText(uint32_t key, const cIGZString *text) override; // slot 21, 0x2109f2
    // Slots 22..27 forward to the set (non-virtually); the set face's slots 3 / 6 / 7 / 8 / 9 / 10 are their thunks
    // (0x210ad9, 0x2102f8, 0x210713, 0x210b3d, 0x210b45, 0x210703).
    bool GetFirst(uint32_t *key, void **value) override;            // slot 22, 0x2102f0
    bool GetNext(uint32_t *key, void **value) override;             // slot 23, 0x21070b
    bool SetProperty(const uint32_t *key, void *value) override;    // slot 24, 0x210ad1
    bool GetPropertyValue(const uint32_t *key, void *out) override; // slot 25, 0x210b35
    void *GetProperty(const uint32_t *key) override;                // slot 26, 0x210b2d
    bool HasProperty(const uint32_t *key) override;                 // slot 27, 0x2106fb
    uint32_t GetUserWord() override;                       // slot 28, shared_vf_15e970
    uint32_t SetUserWord(uint32_t value) override;         // slot 29, 0x2109b0

    // cIGZMessage2Standard (the +4 face)
    bool Create(uint32_t iid, void **out) override;        // slot 3, shared_vf_1474b0: false
    uint32_t GetType() override;                           // slot 4, shared_vf_15e970 (+0x28)
    uint32_t SetType(uint32_t type) override;              // slot 5, 0x2109b0
    bool IsEqual(cIGZMessage2 *other) override;            // slot 6, 0x21015d
    bool IsLess(cIGZMessage2 *other) override;             // slot 7, 0x21017d
    bool HasData1() override;                              // slot 8, 0x2104c7
    uint32_t GetData1() override;                          // slots 9 / 10, 0x210338
    void *GetVoid1() override;
    void SetData1(uint32_t value) override;                // slots 11 / 12, 0x2107cc
    void SetVoid1(void *value) override;
    bool HasData2() override;                              // slot 13, 0x2104e0
    uint32_t GetData2() override;                          // slots 14 / 15, 0x21037a
    void *GetVoid2() override;
    void SetData2(uint32_t value) override;                // slots 16 / 17, 0x21080f
    void SetVoid2(void *value) override;
    bool HasData3() override;                              // slot 18, 0x2104f9
    uint32_t GetData3() override;                          // slots 19 / 20, 0x2103bc
    void *GetVoid3() override;
    void SetData3(uint32_t value) override;                // slots 21 / 22, 0x210852
    void SetVoid3(void *value) override;
    bool HasData4() override;                              // slot 23, 0x210512
    uint32_t GetData4() override;                          // slots 24 / 25, 0x2103fe
    void *GetVoid4() override;
    void SetData4(uint32_t value) override;                // slots 26 / 27, 0x210895
    void SetVoid4(void *value) override;
    bool HasString() override;                             // slot 28, 0x210559
    bool GetString(uint32_t iid, void **out) override;     // slot 29, 0x2105ab
    void SetString(const cIGZString *text) override;       // slot 30, 0x210a3e
    bool HasIGZUnknown() override;                         // slot 31, shared_vf_210551
    cIGZUnknown *GetIGZUnknown() override;                 // slot 32, shared_vf_147770 (+0x30)
    void SetIGZUnknown(cIGZUnknown *object) override;      // slot 33, 0x210999 (RZUnknownSlot::Assign)
    bool HasExtra() override;                              // slot 34, 0x21052b
    bool GetExtra(uint32_t iid, void **out) override;      // slot 35, 0x210440
    void SetExtra(cIGZUnknown *object) override;           // slot 36, 0x2108ea

    uint32_t mRefCount = 0;              // +0x20
    uint32_t mUserWord;                  // +0x24 never initialised
    uint32_t mMessageType = kMessageType; // +0x28
    uint32_t mEventId;                   // +0x2c
    cIGZUnknown *mObject = nullptr;      // +0x30 counted

private:
    bool HasKey(uint32_t key);
    uint32_t GetWord(uint32_t key);
    void SetWord(uint32_t key, uint32_t value);
};
static_assert(sizeof(cTSAudioEvent) == 0x34);

// The hooks' entry points (src/hook/tables/cTSAudioEvent.cpp): the game's new-expressions call these on the block they got.
struct cTSAudioEventAdapters {
    static cTSAudioEvent *__fastcall Construct(void *memory, void *, uint32_t eventId); // 0x20ff24
    static cTSAudioEvent *__fastcall ConstructDefault(void *memory, void *);           // 0x20ff8d
};
