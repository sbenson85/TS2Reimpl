#pragma once
// cTSGameTip (lane KX): one game tip, a persist resource of type 0xb1827a47 (the tips package's records) that the game tip
// service (src/ts/cTSGameTipService.h) creates, loads and saves. A tip names up to three strings of the service's STR#
// table (0x12d in the tips group) by index, and carries a category (the bit the UI's tip filter tests) and a value.
// 0x30 bytes, /O2. RVAs 1.18.0.187.
//
// Faces (RTTI order): +0 cGZPersistResource / cIGZPersistResource (vtable 0xeb7588, 13 slots: the base's, slot 12 this
// class's deleting destructor 0xcb5ae0), +0x1c cIGZSerializable (0xeb75c0, 6 slots: Write 0xcb6e20, Read 0xcb6370,
// GetGZCLSID 0xcb5dc0), +0x20 cITSGameTip (0xeb75dc, 17 slots). The original's slot bodies take `this` as their face and
// reach the fields at face-relative offsets; ours are members. Most cITSGameTip getters and setters are folded with other
// classes' (shared_vf_*: 0xa32a40, 0xa29260, 0x4350e0, 0x2b7070, 0x664b40, 0x4351b0, 0x149c60, 0x145770): implemented
// here, never hooked.
//
// Standalone shape (docs/standalone-shape.md): the two constructors (0xcb58a0, 0xcb5940) are called only by the service's
// CreateTip / CreateDefaultTips and the resource factory's CreateResource, all ours once the service's factory 0xcb5ce0 is
// hooked; the vtables are referenced only by those constructors and the destructor 0xcb5a40, so every tip carries ours
// (db/status/cTSGameTip.jsonl).
#include "gz/cGZPersistResource.h"
#include "gz/cIGZSerializable.h"
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>

class cIGZString;

// cITSGameTip (iid 0xf1813fc3): slots 3-16. The three string ids index the game tip service's string table; the texts
// come from the service (its GetText, slot 6).
class cITSGameTip : public cIGZUnknown {
public:
    virtual uint32_t GetId() = 0;                    // 3 (0xcb6080: the persist key's instance, through slot 7 of +0)
    virtual uint32_t GetCategory() = 0;              // 4 (folded 0xa32a40: zero-extended)
    virtual int32_t GetStringId0() = 0;              // 5 (0xcb60d0: sign-extended)
    virtual int32_t GetStringId1() = 0;              // 6 (folded 0xa29260)
    virtual int32_t GetStringId2() = 0;              // 7 (0xcb6120)
    virtual uint32_t GetValue() = 0;                 // 8 (folded 0x145770)
    virtual bool GetString0(cIGZString *out) = 0;    // 9 (0xcb6090, ret 4)
    virtual bool GetString1(cIGZString *out) = 0;    // 10 (0xcb6130, ret 4)
    virtual bool GetString2(cIGZString *out) = 0;    // 11 (0xcb60e0, ret 4)
    virtual void SetCategory(uint16_t category) = 0; // 12 (folded 0x4350e0, ret 4)
    virtual void SetStringId0(int16_t id) = 0;       // 13 (folded 0x2b7070, ret 4)
    virtual void SetStringId1(int16_t id) = 0;       // 14 (folded 0x664b40, ret 4)
    virtual void SetStringId2(int16_t id) = 0;       // 15 (folded 0x4351b0, ret 4)
    virtual void SetValue(uint32_t value) = 0;       // 16 (folded 0x149c60, ret 4)
};

class cTSGameTip : public cGZPersistResource, public cIGZSerializable, public cITSGameTip {
public:
    static constexpr uint32_t kType = 0xb1827a47;           // the persist key's type; also GetGZCLSID
    static constexpr uint32_t kIid = 0xf1813fc3;            // cITSGameTip
    static constexpr uint32_t kSerializableIid = 0xe4fda3d4; // cIGZSerializable
    static constexpr uint16_t kDefaultCategory = 0x1000;    // what the constructors store
    static constexpr uint32_t kUserTipKind = 0x1000;        // the constructor's kind for CreateTip's tips
    static constexpr uint16_t kUserTipCategory = 0x11;      // their category (0xb39350, a constant getter)
    static constexpr uint16_t kVersion = 2;                 // Write's version; Read takes string id 0 from version 2 on
    static constexpr size_t kSize = 0x30;

    cTSGameTip();                                 // 0xcb5940: the key's type set
    cTSGameTip(uint32_t instance, uint32_t kind); // 0xcb58a0 (ret 8): also the instance; kind 0x1000: category 0x11
    ~cTSGameTip() override;                       // 0xcb5a40; slot 12 the scalar deleting destructor 0xcb5ae0
    cTSGameTip(const cTSGameTip &) = delete;
    cTSGameTip &operator=(const cTSGameTip &) = delete;

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0xcb62d0; thunks 0xcb630d (+0x1c), 0xcb6315 (+0x20)
    uint32_t AddRef() override;                              // the base's (folded 0x5522b0; thunks 0x5522b5, 0xcb5b20)
    uint32_t Release() override;                             // the base's (folded 0x556090; thunks 0x556095, 0xcb63d4)

    // ---- cIGZSerializable (+0x1c) ----
    bool Write(ifaces::cIGZOStream *stream) override; // 0xcb6e20
    bool Read(ifaces::cIGZIStream *stream) override;  // 0xcb6370
    uint32_t GetGZCLSID() override;                   // 0xcb5dc0

    // ---- cITSGameTip (+0x20) ----
    uint32_t GetId() override;
    uint32_t GetCategory() override;
    int32_t GetStringId0() override;
    int32_t GetStringId1() override;
    int32_t GetStringId2() override;
    uint32_t GetValue() override;
    bool GetString0(cIGZString *out) override;
    bool GetString1(cIGZString *out) override;
    bool GetString2(cIGZString *out) override;
    void SetCategory(uint16_t category) override;
    void SetStringId0(int16_t id) override;
    void SetStringId1(int16_t id) override;
    void SetStringId2(int16_t id) override;
    void SetValue(uint32_t value) override;

    cITSGameTip *Tip() { return this; }

    // +0x00 .. +0x1b: cGZPersistResource; +0x1c, +0x20: the two faces' vtables
    int16_t mStringIds[3];  // +0x24 indices into the service's string table (0: none)
    uint16_t mCategory;     // +0x2a
    uint32_t mValue;        // +0x2c

private:
    bool GetString(int16_t id, cIGZString *out); // the body of slots 9-11
};
static_assert(sizeof(cTSGameTip) == cTSGameTip::kSize, "new 0x30");
static_assert(offsetof(cTSGameTip, mStringIds) == 0x24 && offsetof(cTSGameTip, mCategory) == 0x2a &&
                  offsetof(cTSGameTip, mValue) == 0x2c,
              "cTSGameTip layout");
