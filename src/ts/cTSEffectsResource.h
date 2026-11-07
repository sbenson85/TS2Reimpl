#pragma once
#include "gz/cGZPersistResource.h"
#include "gz/cIGZUnknown.h"
#include "ts/cTSEffectsCollection.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// cTSEffectsResource: an effects package resource (lane IT): a cTSEffectsCollection that reads and writes itself (the
// binary effects format: the description tables, the effects, the name index, the aliases and triggers) and merges itself
// into the manager's collection when the manager parses resources. RTTI faces: cITSEffectsResource (+0, vtable 0xe8e350, 9
// slots), cTSEffectsCollection (+4, vtable 0xe8e378, 13 slots) and cGZPersistResource (+0x78, vtable 0xe8e3b0, 13 slots).
// 0xb8 bytes. All functions /O2. RVAs 1.18.0.187.
//
// Creation: cTSEffectsResourceFactory's Create slot (0xbd2ed0) and the COM director's factory 0xbd2f90 (registered by the
// cTSEffectsCOMDirector constructor) are the only paths; both hooked, so every resource carries our vtables. The reference
// count is the persist face's (AddRef / Release forward there, so the last Release deletes through its slot 12).
//
// Standalone shape (docs/standalone-shape.md): a real C++ class; the compiler emits the vtables and the adjustor thunks
// (0xbd30db, 0xbd2ea8, 0xbd4018, 0xbd2e3b on the collection face; 0xbd30e3, 0xbd2eb0, 0xbd4020, 0xbd2e43 on the persist
// face). The adapters below serve the RVAs.

// The resource interface (id 0xca51189b; the global name as its RTTI).
class cITSEffectsResource : public cIGZUnknown {
public:
    virtual void SetName(const char *name) = 0;                     // slot 3 (ret 4)
    virtual const char *GetName() = 0;                              // slot 4
    virtual void MergeInto(cTSEffectsCollection *target) = 0;       // slot 5 (ret 4)
    virtual bool Write(void *stream) = 0;                           // slot 6 (ret 4): a cIGZOStream
    virtual bool Read(void *stream) = 0;                            // slot 7 (ret 4): a cIGZIStream
    virtual uint32_t GetClassId() = 0;                              // slot 8
};

class cTSEffectsResource : public cITSEffectsResource, public cTSEffectsCollection, public cGZPersistResource {
public:
    static constexpr uint32_t kInterfaceId = 0xca51189b;
    static constexpr uint32_t kResourceType = 0xea5118b0; // the persist key's type the factory makes
    static constexpr uint32_t kClassId = 0x2a5118ad;
    static constexpr size_t kSize = 0xb8;

    cTSEffectsResource();            // 0xbd2ba0
    ~cTSEffectsResource() override;  // 0xbd2da0; the scalar deleting destructor 0xbd2e50 (slot 12 of the other faces)
    cTSEffectsResource(const cTSEffectsResource &) = delete;
    cTSEffectsResource &operator=(const cTSEffectsResource &) = delete;
    static cTSEffectsResource *Create(); // 0xbd2f90 (cdecl): new, null on failure

    // cIGZUnknown, every face
    bool QueryInterface(uint32_t riid, void **out) override; // 0xbd3090
    uint32_t AddRef() override;                              // 0xbd2ea0: the persist face's count
    uint32_t Release() override;                             // 0xbd4010: the persist face's Release

    // cITSEffectsResource
    void SetName(const char *name) override;                 // slot 3, 0xbd4190
    const char *GetName() override;                          // slot 4, 0xbd3070
    void MergeInto(cTSEffectsCollection *target) override;   // slot 5, 0xbd2ec0 (the collection's 0xbd7f30)
    bool Write(void *stream) override;                       // slot 6, 0xbd41c0
    bool Read(void *stream) override;                        // slot 7, 0xbd30f0
    uint32_t GetClassId() override;                          // slot 8, 0xbd3050

    std::string mName;                  // +0x94
    std::vector<std::string> mStrings;  // +0xac nothing here reads or writes it (the destructor frees it)
};
static_assert(sizeof(cTSEffectsResource) == cTSEffectsResource::kSize, "cTSEffectsResource layout");

// The trigger serializers (cdecl, used by Write / Read): 0xbd8dc0 writes the message and the effect name, 0xbd8a70 reads
// them (the version is not used).
void WriteEffectTrigger(void *stream, const EffectMessageTrigger *trigger);
void ReadEffectTrigger(void *stream, uint32_t version, EffectMessageTrigger *trigger);

// The RVA hooks' entry points (src/hook/tables/cTSEffectsResource.cpp): this in ecx = the face the RVA serves, our members
// called without virtual dispatch.
struct cTSEffectsResourceAdapters {
    using Self = cTSEffectsResource;
    static Self *__fastcall Construct(Self *memory, void *);                          // 0xbd2ba0
    static void __fastcall Destruct(Self *self, void *);                              // 0xbd2da0
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);   // 0xbd2e50
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out); // 0xbd3090
    static uint32_t __fastcall AddRef(Self *self, void *);                            // 0xbd2ea0
    static uint32_t __fastcall Release(Self *self, void *);                           // 0xbd4010
    static void __fastcall SetName(Self *self, void *, const char *name);             // 0xbd4190
    static const char *__fastcall GetName(Self *self, void *);                        // 0xbd3070
    static void __fastcall MergeInto(Self *self, void *, cTSEffectsCollection *target); // 0xbd2ec0
    static bool __fastcall Write(Self *self, void *, void *stream);                   // 0xbd41c0
    static bool __fastcall Read(Self *self, void *, void *stream);                    // 0xbd30f0
    static uint32_t __fastcall GetClassId(Self *self, void *);                        // 0xbd3050
};
