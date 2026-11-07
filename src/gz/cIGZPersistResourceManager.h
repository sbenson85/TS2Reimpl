#pragma once
#include "gz/cIGZUnknown.h"

#include <cstdint>

// cIGZPersistResourceManager: the resource manager interface (54 slots), implementable (lane FX). Implemented by
// gz::cGZPersistResourceManager (src/gz/cGZPersistResourceManagerShape.h; game vtable 0xe60274) and its only subclass
// cTSPersistSystem (vtable 0xda5d74, which appends slots 54..57). Slot order = declaration order; the signatures are those
// of the hook-shaped bodies (src/gz/cGZPersistResourceManager.h: `vfN` there is slot N here; the names the callers' view
// ifaces::cIGZPersistResourceManager gives are in the comments). Byte answers are uint8_t, so a raw byte goes through
// unnormalised. `ret N` per slot = 4 * the argument count.
class cIGZPersistResourceManager : public cIGZUnknown {
public:
    virtual uint32_t vf3(uint32_t key, uint32_t iid, uint32_t out, uint32_t a4, uint32_t a5) = 0;   // 3 GetResource
    virtual uint8_t vf4(uint32_t key, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6) = 0;
    virtual uint8_t vf5(uint32_t key, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) = 0;     // 5 GetPrivateResource
    virtual uint8_t vf6(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) = 0;
    virtual uint8_t vf7(const void *key, uint32_t iid, void **out, uint32_t a4, uint32_t a5) = 0;
    virtual uint8_t vf8(void *resource) = 0;                                                       // 8 CacheResource
    virtual uint32_t vf9(uint32_t a1, void *resource) = 0;
    virtual uint8_t vf10(uint32_t key) = 0;                                                        // 10 ForgetResource
    virtual uint8_t vf11(uint32_t key) = 0;                                                        // 11 IsResourceCached
    virtual uint8_t vf12(void *const *range, uint32_t a2) = 0;
    virtual uint8_t vf13(const void *key, uint32_t a2) = 0;
    virtual uint8_t vf14(void *resource, const void *key, void *segment) = 0;
    virtual uint8_t vf15(uint32_t key) = 0;                                                        // 15 TestForKey
    virtual uint32_t vf16(uint32_t a1, uint32_t a2) = 0;
    virtual uint32_t vf17(uint32_t list, uint32_t type) = 0;
    virtual uint32_t vf18(uint32_t list, uint32_t filter) = 0;                                     // 18 GetResourceKeyList
    virtual uint32_t vf19(uint32_t list, uint32_t type) = 0;
    virtual uint8_t vf20(uint32_t factoryId, uint32_t type, void *factory) = 0;
    virtual uint8_t vf21(uint32_t factoryId) = 0;
    virtual uint8_t vf22(uint32_t type, void **out) = 0;
    virtual uint32_t vf23(const uint32_t *key, uint32_t b) = 0;
    virtual uint32_t vf24(void *resource, uint32_t a2) = 0;
    virtual uint32_t vf25() = 0;
    virtual uint32_t vf26(uint32_t index) = 0;
    virtual uint32_t vf27(uint32_t segment) = 0;                                                   // 27 AddSegment (-> 28)
    virtual uint8_t vf28(void *segment) = 0;
    virtual uint8_t vf29(void *segment) = 0;
    virtual uint8_t vf30(void *segment) = 0;                                                       // 30 RemoveSegment
    virtual uint8_t vf31(uint32_t segment) = 0;
    virtual uint8_t vf32() = 0;                                                                    // 32 FlushSegments
    virtual uint8_t vf33(uint32_t key, void **out) = 0;
    virtual uint8_t vf34(uint32_t key, void **out) = 0;                                            // 34 FindSegmentWithKey
    virtual uint32_t vf35() = 0;                                                                   // 35 GetSegmentCount
    virtual uint32_t vf36(uint32_t index) = 0;                                                     // 36 GetSegmentAt
    virtual uint8_t vf37(uint32_t predicate, uint32_t a2) = 0;
    virtual uint32_t vf38(void **out, uint32_t *count) = 0;
    virtual uint8_t vf39(uint32_t key, uint32_t record, uint32_t mode) = 0;                        // 39 OpenRecord
    virtual uint8_t vf40(uint32_t a1, void *const *holder) = 0;
    virtual uint8_t vf41(void *cache) = 0;
    virtual uint8_t vf42(void *cache) = 0;                                                         // 42 RemoveCacheStrategy
    virtual uint8_t vf43() = 0;
    virtual void vf44(uint32_t a1) = 0;
    virtual uint8_t vf45() = 0;                                                                    // 45 AdvanceCacheGenerations
    virtual void vf46(void *segment) = 0;
    virtual uint32_t vf47() = 0;                                                                   // 47 GetDefaultSegment
    virtual uint32_t vf48() = 0;
    virtual uint8_t vf49(uint32_t a1, uint32_t a2, uint32_t a3) = 0;
    virtual uint8_t vf50(uint32_t a1, uint32_t a2) = 0;
    virtual uint8_t vf51(const uint32_t *key, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) = 0;
    virtual uint8_t vf52(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6) = 0;
    virtual uint8_t vf53(uint32_t a1, void *a2, void *a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) = 0;
};
