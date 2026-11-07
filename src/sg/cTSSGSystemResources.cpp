// nTSSG::cTSSGSystem: resources by name (lane IR). See src/sg/cTSSGSystem.h.
#include "sg/cTSSGSystem.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/StringKeyParser.h"
#include "sg/SGLocalString.h"

#include <cstring>

namespace {

template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

void *VirtualBase(void *object, uint32_t vbptr, int entry)
{
    auto *at = static_cast<uint8_t *>(object) + vbptr;
    const int32_t *table = *reinterpret_cast<int32_t *const *>(at);
    return at + table[entry];
}

// Name suffixes and the special model name (game text).
constexpr uint32_t kAnimSuffix = 0xdae018;  // "_anim"
constexpr uint32_t kModelSuffix = 0xdadfe8; // "_cres"
constexpr uint32_t kShapeSuffix = 0xdae030; // "_shpe"
constexpr uint32_t kNoModel = 0xdae7b0;     // "nomodel_cres"
constexpr uint32_t kSuffixLength = 5;
// Key presets for ParseKey and the resource types the resource manager is asked for.
constexpr uint32_t kAnimKey = 0xfb00791e;
constexpr uint32_t kModelKey = 0xe519c933;
constexpr uint32_t kModelType = 0x4512dd13;
constexpr uint32_t kShapeKey = 0xfc6eb1f7;
constexpr uint32_t kShapeType = 0x3c6eb1f0;
// The object's own slots.
constexpr int kSelfFindAnim = 25;
constexpr int kSelfFindPersist = 98;
// The parser target's string vector (32-byte cRZStrings).
constexpr uint32_t kNamesBegin = 0x1ac;
constexpr uint32_t kNamesEnd = 0x1b0;
constexpr uint32_t kNameSize = 0x20;

// The cached resource manager (0x20deb5, not replaced yet).
void *ResourceManager() { return OriginalFunction<void *(__cdecl *)(), 0x20deb5>()(); }

// An interface reference released (slot 2) when it goes or is dropped.
struct Held {
    void *object = nullptr;
    Held() = default;
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
    ~Held() { Drop(); }
    void Drop()
    {
        if (void *old = object) {
            object = nullptr;
            Call<2>(old);
        }
    }
};

// Appends the five-character suffix unless the text already ends with it (case-sensitively).
void EnsureSuffix(nTSSG::SGLocalString &text, uint32_t suffixRva)
{
    const char *suffix = GameAddress<const char>(suffixRva);
    if (!text.Helpers()->EndsWith(suffix, kSuffixLength, 1)) text.value.mString.append(suffix);
}

// ParseKey of the text with the preset; `virtualText`: the text read through the string's own slot 5, as some callers do.
StringKeyParser::Result Parse(nTSSG::SGLocalString &text, uint32_t preset, bool virtualText)
{
    StringKeyParser::Result key{};
    const char *chars = virtualText ? Call<5, const char *>(&text.value) : text.Text();
    StringKeyParser::ParseKey(&key, chars, preset, nullptr);
    return key;
}

// Releases a resource held through its virtual base (table at +4, entry 1).
void ReleaseResource(void *object) { Call<2>(VirtualBase(object, 4, 1)); }

} // namespace

namespace nTSSG {

void cTSSGSystem::PreloadAnimations()
{
    if (!mRenderOptions[0] || !mParserTarget) return;
    void *resource = nullptr;
    for (uint32_t i = 0;; ++i) {
        auto *target = static_cast<uint8_t *>(mParserTarget);
        uint8_t *const begin = *reinterpret_cast<uint8_t **>(target + kNamesBegin);
        uint8_t *const end = *reinterpret_cast<uint8_t **>(target + kNamesEnd);
        if (i >= uint32_t(int32_t(end - begin) >> 5)) break;
        SGLocalString text(Call<5, const char *>(begin + i * kNameSize));
        EnsureSuffix(text, kAnimSuffix);
        text.Helpers()->LowerCase();
        const StringKeyParser::Result parsed = Parse(text, kAnimKey, false);
        if (!parsed.hash24) continue;
        uint32_t key[2] = {parsed.hash24, parsed.crc32};
        if (void *old = resource) {
            resource = nullptr;
            ReleaseResource(old);
        }
        if (Call<kSelfFindPersist, uint8_t>(this, key, text.Text(), &resource)) Call<13>(mAnimSystem.mObject, resource);
    }
    if (resource) ReleaseResource(resource);
}

bool cTSSGSystem::HasModel(const char *name, const uint32_t *group)
{
    SGLocalString text(name);
    EnsureSuffix(text, kModelSuffix);
    if (text.value.IsEqual(GameAddress<const char>(kNoModel), false)) return false;
    void *manager = ResourceManager();
    StringKeyParser::Result key = Parse(text, kModelKey, true);
    if (group) {
        const uint32_t parsed = key.id;
        key.id = *group;
        const bool found = Call<15, uint8_t>(manager, &key) != 0;
        key.id = parsed;
        if (found) return true;
    }
    return Call<15, uint8_t>(manager, &key) != 0;
}

void cTSSGSystem::FindModel(const char *name, const uint32_t *group, const char *fallback, uint32_t iid, void **out,
                            uint32_t)
{
    *out = nullptr;
    SGLocalString text(name);
    if (text.value.mString.empty() && fallback) {
        text.value.mString.assign(fallback);
        fallback = nullptr;
    }
    EnsureSuffix(text, kModelSuffix);
    if (_stricmp(text.Text(), GameAddress<const char>(kNoModel)) == 0) return;

    void *manager = ResourceManager();
    Held resource;
    StringKeyParser::Result key = Parse(text, kModelKey, true);
    bool found = false;
    if (group) {
        const uint32_t parsed = key.id;
        key.id = *group;
        resource.Drop();
        found = Call<3, uint8_t>(manager, &key, kModelType, &resource.object, 0u, 0u) != 0;
        if (!found) key.id = parsed;
    }
    if (!found) {
        resource.Drop();
        found = Call<3, uint8_t>(manager, &key, kModelType, &resource.object, 0u, 0u) != 0;
        if (!found && fallback) {
            text.value.mString.assign(fallback);
            text.value.mString.append(GameAddress<const char>(kModelSuffix));
            StringKeyParser::Result second = Parse(text, kModelKey, true);
            void *again = ResourceManager();
            resource.Drop();
            Call<3>(again, &second, kModelType, &resource.object, 0u, 0u);
        }
    }
    if (resource.object) {
        Held model;
        Call<15>(resource.object, 1u, kModelType, &model.object, 0u);
        Call<9>(Call<25, void *>(mLightingManager.mObject), model.object);
        Call<0>(model.object, iid, out);
    }
}

bool cTSSGSystem::FindShape(const char *name, void **out)
{
    *out = nullptr;
    SGLocalString text(name);
    EnsureSuffix(text, kShapeSuffix);
    Held resource;
    StringKeyParser::Result key = Parse(text, kShapeKey, true);
    void *manager = ResourceManager();
    resource.Drop();
    bool found = false;
    if (Call<3, uint8_t>(manager, &key, kShapeType, &resource.object, 0u, 0u) && resource.object) {
        Held shape;
        Call<15>(resource.object, 1u, kShapeType, &shape.object, 0u);
        found = Call<0, uint8_t>(shape.object, kShapeType, out) != 0;
    }
    return found;
}

bool cTSSGSystem::FindAnimation(const char *name, void **out, uint8_t force)
{
    SGLocalString text;
    if (name) {
        text.value.mString.assign(name);
        EnsureSuffix(text, kAnimSuffix);
        text.Helpers()->LowerCase();
    }
    const StringKeyParser::Result parsed = Parse(text, kAnimKey, false);
    uint32_t key[2] = {parsed.hash24, parsed.crc32};
    return Call<kSelfFindAnim, uint8_t>(this, key, text.Text(), 1u, out, uint32_t(force)) != 0;
}

} // namespace nTSSG
