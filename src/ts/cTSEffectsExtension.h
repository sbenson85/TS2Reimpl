#pragma once

#include "rz/cRZString.h"
#include "sg/cExtension.h"
#include "ts/cITSEffectsExtension.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace nGZSceneGraph {

struct TSEffectFindKey;
struct TSEffectFindResult;

struct TSEffectsRegistry {
    uint32_t unknown0;
    void *sentinel;
    uint32_t count;
    void *buckets;
    void *bucketEnd;
    void *bucketCapacityEnd;
    uint32_t bucketMask;
    uint32_t bucketCount;
};
static_assert(sizeof(TSEffectsRegistry) == 0x20);

struct TSEffectRecord {
    TSEffectRecord *next, *previous;
    void *firstTextVTable; // +0x08, inline cRZString face
    std::string firstText;
    uint32_t unknown24;
    void *nameProviderVTable; // +0x28, inline interface vptr; slot 5 returns the record's UTF-16 name
    std::string secondText;
    uint32_t unknown44;
    uint32_t hashValue;
    void *instance;
    uint32_t kind;
    uint8_t flag;
    uint8_t padding[3];
};
struct TSEffectFindKey {
    uint32_t unknown0;
    std::string firstText;
    uint32_t unknown1c[2];
    std::string secondText;
    uint32_t unknown3c;
    uint32_t discriminator;
};
struct TSEffectFindResult { TSEffectRecord *next; TSEffectRecord *found; };
static_assert(sizeof(TSEffectRecord) == 0x58 && offsetof(TSEffectRecord, firstText) == 0x0c &&
              offsetof(TSEffectRecord, secondText) == 0x2c && offsetof(TSEffectRecord, hashValue) == 0x48 &&
              offsetof(TSEffectRecord, instance) == 0x4c && offsetof(TSEffectRecord, kind) == 0x50 &&
              offsetof(TSEffectRecord, flag) == 0x54);
static_assert(sizeof(TSEffectFindKey) == 0x44 && offsetof(TSEffectFindKey, firstText) == 4 &&
              offsetof(TSEffectFindKey, secondText) == 0x24 && offsetof(TSEffectFindKey, discriminator) == 0x40);
static_assert(sizeof(TSEffectFindResult) == 8);

// A record's payload as the registry's element type (0x50 bytes): TSEffectsRegistry is a std::unordered_map of these, and
// TSEffectRecord is its list node. A record holds a reference on its instance.
struct TSEffectRecordKey {
    explicit TSEffectRecordKey(const TSEffectFindKey &key);
    TSEffectRecordKey(const std::string &firstText, const std::string &secondText, uint32_t value);
    TSEffectRecordKey(const TSEffectRecordKey &other);
    cRZString first;
    cRZString second;
    uint32_t discriminator;
};
struct TSEffectRecordValue {
    TSEffectRecordValue(void *object, uint32_t kindValue, uint8_t flagValue);
    TSEffectRecordValue(const TSEffectRecordValue &other);
    ~TSEffectRecordValue();
    void *instance;
    uint32_t kind;
    uint8_t flag;
};
using TSEffectInsertValue = std::pair<const TSEffectRecordKey, TSEffectRecordValue>;
static_assert(sizeof(TSEffectInsertValue) == 0x50 && offsetof(TSEffectInsertValue, second) == 0x44);

#pragma warning(push)
#pragma warning(disable : 4250)
class cTSEffectsExtension final : public cExtension, public cITSEffectsExtension {
public:
    static constexpr uint32_t kSize = 0x70;
    static constexpr uint32_t kVTable = 0xeb8d4c;
    static constexpr uint32_t kReferentVTable = 0xeb8d54;
    static constexpr uint32_t kEffectsVTable = 0xeb8d74;
    static constexpr uint32_t kEffectsFaceVTable = 0xeb8dd4;
    static constexpr uint32_t kEffectsIid = cITSEffectsExtension::kIid;

    cTSEffectsExtension();
    ~cTSEffectsExtension() override;
    cTSEffectsExtension *Construct(uint32_t mostDerived);
    void Destruct();
    void InsertRecord(const TSEffectFindKey &key, void *instance, uint32_t kind, uint8_t flag);
    void DestroyRegistry();
    cTSEffectsExtension *DeletingDestructor(uint32_t flags);
    TSEffectFindResult *FindRecord(TSEffectFindResult *result, const TSEffectFindKey *key, uint32_t hash);
    TSEffectRecord **RemoveRecord(TSEffectRecord **result, TSEffectRecord *record);

    bool QueryInterface(uint32_t iid, void **out) override;
    cIExtension *Clone() override;
    bool IsPersistent() override;
    const char *GetTypeName() override;
    bool IsKindOf(uint32_t iid) override;
    uint8_t vf0() override;
    uint8_t Dispatch(uint32_t operation, const char *name, const char *nodeName, uint32_t kind, uint32_t discriminator, uint32_t flag) override;
    void *CreateEffect(uint32_t kind, const char *name, const char *nodeName, uint32_t discriminator, uint8_t flag) override;
    bool vf3(const uint16_t *, std::vector<uint16_t> *) override;
    void *FindEffectNode(const char *, void *);
    bool RemoveByInstance(void *) override;
    uint32_t vf5(const char *, const char *, uint32_t) override;
    void *vf6(const char *, const char *, uint32_t) override;
    void vf7(uint8_t, int32_t) override;
    void vf8(uint32_t) override;
    void vf9(uint8_t) override;
    void vf10(uint32_t) override;
    void vf11(uint32_t) override;
    void vf12(uint32_t, uint32_t, uint32_t) override;
    void vf13(uint32_t, uint32_t) override;
    void vf14() override;
    void vf15() override;
    uint8_t vf16() override;
    uint8_t vf17() override;
    void ReadRecords(void *stream) override;
    void vf19(void *) override;
    void SetByte2b(uint8_t) override;
    void SetWord2c(uint32_t) override;
    void BuildStatusText() override;

    // 0xcc5810's text, kept for tests: what BuildStatusText builds and drops.
    std::string StatusText();

    TSEffectsRegistry mRegistry; // +0x20
    uint32_t mUnknown40;         // +0x40
    uint32_t mUnknown44;         // +0x44
    cRZString mText;             // +0x48

private:
    void ApplyStateBytes(void *instance);                      // the state bytes (+0x42, +0x41) to the instance's slots 7 and 8
    void ConfigureInstance(void *instance, void *node);        // slot 14 (2, node), then ApplyStateBytes
};
#pragma warning(pop)
static_assert(sizeof(cTSEffectsExtension) == cTSEffectsExtension::kSize, "cTSEffectsExtension size");
static_assert(offsetof(cTSEffectsExtension, mRegistry) == 0x20, "registry offset");
static_assert(offsetof(cTSEffectsExtension, mText) == 0x48, "text offset");


struct cTSEffectsExtensionAdapters {
    static cTSEffectsExtension *__fastcall Construct(cTSEffectsExtension *, void *, uint32_t);
    static void __fastcall Destruct(cTSEffectsExtension *, void *);
    static cTSEffectsExtension *__fastcall DeletingDestructor(cTSEffectsExtension *, void *, uint32_t);
    static bool __fastcall QueryInterface(void *, void *, uint32_t, void **);
    static uint32_t __fastcall ForwardNoArg(void *, void *);
    static void *__cdecl FindEffectNode(const char *name, void *node);
    static TSEffectFindResult *__fastcall FindRecord(void *registry, void *, TSEffectFindResult *result,
                                                     const TSEffectFindKey *key, uint32_t hash);
    static TSEffectRecord **__fastcall RemoveRecord(void *registry, void *, TSEffectRecord **result, TSEffectRecord *record);
    static bool __fastcall RemoveByInstance(void *face, void *, void *instance);
    static uint8_t __fastcall HasActiveEffect(void *face, void *);
    static void __fastcall SetStateByte1(void *face, void *, uint32_t value);
    static void __fastcall SetStateByte2(void *face, void *, uint32_t value);
    static void __fastcall BroadcastThree(void *face, void *, uint32_t a, uint32_t b, uint32_t c);
    static void __fastcall BroadcastTwo(void *face, void *, uint32_t a, uint32_t b);
    static void __fastcall FilterActive(void *face, void *, uint8_t flag, int32_t kind);
    static void __fastcall RemoveActive(void *face, void *, uint32_t kind);
    static void __fastcall Unbind(void *face, void *);
    static bool __fastcall CollectNamed(void *face, void *, const uint16_t *name, std::vector<uint16_t> *out);
    static uint8_t __fastcall GetDisplayName(void *face, void *);
    static void __fastcall BindEffects(void *face, void *);
    static void *__fastcall FindInstance(void *face, void *, const char *first, const char *second,
                                         uint32_t discriminator);
    static uint32_t __fastcall RemoveByKey(void *face, void *, const char *first, const char *second,
                                           uint32_t discriminator);
    static void __fastcall ProcessUnused(void *face, void *, uint8_t flag);
    static uint8_t __fastcall Dispatch(void *face, void *, uint32_t operation, const char *name, const char *nodeName,
                                       uint32_t kind, uint32_t discriminator, uint32_t flag);
    static void *__fastcall CreateEffect(void *face, void *, uint32_t kind, const char *name, const char *nodeName,
                                         uint32_t discriminator, uint8_t flag);
    static void __fastcall WriteRecords(void *face, void *, void *writer);
    static void __fastcall ReadRecords(void *face, void *, void *stream);
    static void __fastcall BuildStatusText(void *face, void *);
};

} // namespace nGZSceneGraph
