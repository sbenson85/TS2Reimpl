#pragma once
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>
#include <list>

// Lane FC. cIGZUnknownList (interface id 0x12e): an ordered list of reference-counted objects. Slots 3 to 17; the
// element accessors hand an element out through its own QueryInterface (the id and out pointer are the caller's), so a
// list of cIGZUnknown answers any interface its elements do.
class cIGZUnknownList : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x12e;

    // Slot 5 / 17: the visitor's slot 3 is called as (element, a, b) for each element, in order.
    struct Visitor {
        virtual void vf0() = 0;
        virtual void vf1() = 0;
        virtual void vf2() = 0;
        virtual void Visit(cIGZUnknown *element, uint32_t a, uint32_t b) = 0; // slot 3
    };

    virtual uint32_t Size() const = 0;                                                 // slot 3
    virtual bool GetItem(uint32_t index, uint32_t iid, void **out) = 0;                // slot 4: false past the end
    virtual bool ForEach(Visitor *visitor, uint32_t a, uint32_t b) = 0;                // slot 5
    virtual bool PrependAll(cIGZUnknownList *other) = 0;                               // slot 6: other's items, in order, at the front
    virtual bool PushFront(cIGZUnknown *item) = 0;                                     // slot 7
    virtual bool AppendAll(cIGZUnknownList *other) = 0;                                // slot 8
    virtual bool PushBack(cIGZUnknown *item) = 0;                                      // slot 9
    virtual bool GetFront(uint32_t iid, void **out) = 0;                               // slot 10: false when empty
    virtual bool GetBack(uint32_t iid, void **out) = 0;                                // slot 11
    virtual bool PopFront(uint32_t iid, void **out) = 0;                               // slot 12: removed only when the query succeeded
    virtual bool PopBack(uint32_t iid, void **out) = 0;                                // slot 13
    virtual bool Clear() = 0;                                                          // slot 14: always true
    virtual bool Assign(cIGZUnknownList *other) = 0;                                   // slot 15: Clear, then PrependAll
    virtual bool Clone(cIGZUnknownList **out) = 0;                                     // slot 16: a new list holding this one's items
    virtual bool ForEachAgain(Visitor *visitor, uint32_t a, uint32_t b) = 0;           // slot 17: the same body as slot 5
};

// cRZUnknownList (0x10 bytes, vtable 0xda3008, 19 slots): the standalone shape. Reference count at +0xc, the
// element list (std::list of counted references: 12-byte nodes) at +4 (head pointer, size).
//
// Built (`new` of 0x10 bytes, then constructor 0x1b905f) by about a dozen sites and by Clone; destroyed through the
// deleting destructor (slot 18, 0x1b910f), or inline by 0x428dc0 (the non-deleting destructor 0x1b909d).
class cRZUnknownList : public cIGZUnknownList {
public:
    static constexpr uint32_t kVTable = 0xda3008;
    static constexpr uint32_t kSize = 0x10;

    // A held reference: AddRef on copy, Release on destruction (the game's cRZAutoRefCount; one pointer).
    struct Ref {
        cIGZUnknown *p;
        explicit Ref(cIGZUnknown *object) : p(object) {}
        // A new reference to the object (AddRef when non-null).
        static Ref Acquire(cIGZUnknown *object)
        {
            if (object) object->AddRef();
            return Ref(object);
        }
        Ref(const Ref &other) : p(other.p)
        {
            if (p) p->AddRef();
        }
        Ref &operator=(const Ref &) = delete;
        ~Ref()
        {
            if (p) p->Release();
        }
    };

    cRZUnknownList();          // 0x1b905f: empty, count 0
    virtual ~cRZUnknownList(); // slot 18 (deleting 0x1b910f; destructor 0x1b909d)
    cRZUnknownList(const cRZUnknownList &) = delete;
    cRZUnknownList &operator=(const cRZUnknownList &) = delete;

    bool QueryInterface(uint32_t riid, void **ppv) override; // 0x1b9477: ids 1 and 0x12e
    uint32_t AddRef() override;                              // 0x15e520 (folded: ++[ecx + 0xc])
    uint32_t Release() override;                             // 0x1b9500
    uint32_t Size() const override;                          // 0x46390 (folded)
    bool GetItem(uint32_t index, uint32_t iid, void **out) override;       // 0x1b91e2
    bool ForEach(Visitor *visitor, uint32_t a, uint32_t b) override;       // 0x1b9166
    bool PrependAll(cIGZUnknownList *other) override;                      // 0x1b9369
    bool PushFront(cIGZUnknown *item) override;                            // 0x1b9424
    bool AppendAll(cIGZUnknownList *other) override;                       // 0x1b92bc
    bool PushBack(cIGZUnknown *item) override;                             // 0x1b9333
    bool GetFront(uint32_t iid, void **out) override;                      // 0x1b922f
    bool GetBack(uint32_t iid, void **out) override;                       // 0x1b9217
    bool PopFront(uint32_t iid, void **out) override;                      // 0x1b9282
    bool PopBack(uint32_t iid, void **out) override;                       // 0x1b9246
    bool Clear() override;                                                 // 0x1b915b
    bool Assign(cIGZUnknownList *other) override;                          // 0x1b9196
    bool Clone(cIGZUnknownList **out) override;                            // 0x1b91ad
    bool ForEachAgain(Visitor *visitor, uint32_t a, uint32_t b) override;  // 0x1b9166 again

    std::list<Ref> mItems; // +0x04 (head, size)
    uint32_t mRefCount;    // +0x0c
};

static_assert(sizeof(cRZUnknownList) == cRZUnknownList::kSize, "cRZUnknownList size");
static_assert(offsetof(cRZUnknownList, mItems) == 0x04, "cRZUnknownList layout");
static_assert(offsetof(cRZUnknownList, mRefCount) == 0x0c, "cRZUnknownList layout");

struct cRZUnknownListAdapters {
    static cRZUnknownList *__fastcall Construct(void *memory, void *edx);   // 0x1b905f
    static void __fastcall Destruct(cRZUnknownList *self, void *edx);       // 0x1b909d (no free)
    static bool __fastcall PushBack(cRZUnknownList *self, void *edx, cIGZUnknown *item); // 0x1b9333 (direct caller 0x428dc0)
};
