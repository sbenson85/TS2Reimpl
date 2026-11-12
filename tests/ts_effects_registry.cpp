// The effects extension's registry as a container: the insert 0xcc4e30 and the teardown 0xcc5280 / 0xcc4db0 against ours on
// the same sequences. Each side's registry is torn down by the other's code, so the two agree on the layout as well.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"

#include "ts/cTSEffectsExtension.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

using nGZSceneGraph::cTSEffectsExtension;
using nGZSceneGraph::TSEffectFindKey;
using nGZSceneGraph::TSEffectInsertValue;
using nGZSceneGraph::TSEffectRecord;
using nGZSceneGraph::TSEffectRecordKey;
using nGZSceneGraph::TSEffectRecordValue;
using nGZSceneGraph::TSEffectsRegistry;

namespace {

class Counted {
public:
    virtual bool __thiscall QueryInterface(uint32_t, void **) { return false; }
    virtual uint32_t __thiscall AddRef() { return ++count; }
    virtual uint32_t __thiscall Release() { return --count; }
    uint32_t count = 0;
};

struct Side {
    alignas(8) uint8_t memory[cTSEffectsExtension::kSize];
    Counted objects[6];
    cTSEffectsExtension *Extension() { return reinterpret_cast<cTSEffectsExtension *>(memory); }
    TSEffectsRegistry *Registry() { return reinterpret_cast<TSEffectsRegistry *>(memory + 0x20); }
};

// The registry with its pointers as positions in the list.
std::string Picture(Side &side)
{
    TSEffectsRegistry *registry = side.Registry();
    auto *const sentinel = static_cast<TSEffectRecord *>(registry->sentinel);
    std::vector<TSEffectRecord *> order;
    std::string out;
    char line[160];
    TSEffectRecord *before = sentinel;
    for (TSEffectRecord *record = sentinel->next; record != sentinel; record = record->next) {
        order.push_back(record);
        const auto object = static_cast<Counted *>(record->instance);
        std::snprintf(line, sizeof(line), "[%s|%s|%x|%x|%d|%u|%u|%u|%u|%d]\n", record->firstText.c_str(), record->secondText.c_str(),
                      record->hashValue, record->unknown24, record->previous == before, record->unknown44,
                      object ? uint32_t(object - side.objects) : 99u, record->kind, uint32_t(record->flag),
                      record->firstTextVTable == record->nameProviderVTable);
        out += line;
        before = record;
    }
    const auto position = [&](void *word) {
        if (word == sentinel) return -1;
        for (size_t i = 0; i < order.size(); ++i)
            if (order[i] == word) return int(i);
        return -2;
    };
    std::snprintf(line, sizeof(line), "%u %x %u %u %u %d\n", registry->count, registry->unknown0, registry->bucketMask, registry->bucketCount,
                  uint32_t(static_cast<void **>(registry->bucketEnd) - static_cast<void **>(registry->buckets)),
                  int(static_cast<void **>(registry->bucketCapacityEnd) - static_cast<void **>(registry->buckets)));
    out += line;
    const auto **buckets = static_cast<const void **>(registry->buckets);
    for (uint32_t i = 0; i < registry->bucketCount * 2; ++i) {
        std::snprintf(line, sizeof(line), "%d ", position(const_cast<void *>(buckets[i])));
        out += line;
    }
    for (const Counted &object : side.objects) {
        std::snprintf(line, sizeof(line), "(%u)", object.count);
        out += line;
    }
    return out;
}

const char *const kFirst[] = {"", "a", "fifteen letters", "sixteen letters!", "a name well past the short buffer of the string"};
const char *const kSecond[] = {"", "b", "second", "another name past the short string buffer"};
const float kLoads[] = {1.0f, 1.0f, 0.5f, 2.0f};

} // namespace

// Sequences short and long enough to rehash more than once, repeated keys in them, null instances, three load factors.
DIFF_TEST(EffectsRegistry_Insert, 0xcc4e30)
{
    shape::CodePatch gameNew(0x824632u, shape::CodeOf(+[](size_t n) -> void * { return ::operator new(n); }));
    shape::CodePatch gameDelete(0x824624u, shape::CodeOf(+[](void *p, size_t) { ::operator delete(p); }));
    using InsertFn = void *(__thiscall *)(void *, void *, const TSEffectInsertValue *);
    using DestroyFn = void(__thiscall *)(void *);
    const auto insert = ctx.Original<InsertFn>(0xcc4e30);
    const auto destroy = ctx.Original<DestroyFn>(0xcc5280);
    std::mt19937 r(0xcc4e30);
    uint32_t repeats = 0, largest = 0;
    for (uint32_t seed = 0; seed < 300; ++seed) {
        auto *sides = new Side[2]();
        const float load = kLoads[seed % 4];
        for (int s = 0; s < 2; ++s) {
            sides[s].Extension()->Construct(1);
            std::memcpy(&sides[s].Registry()->unknown0, &load, 4);
        }
        const uint32_t length = seed % 8 == 7 ? 300 + r() % 300 : r() % 40;
        for (uint32_t step = 0; step < length; ++step) {
            const std::string first = step % 2 ? kFirst[r() % 5] : std::string(kFirst[r() % 5]) + std::to_string(r() % 400);
            const std::string second = kSecond[r() % 4];
            const uint32_t discriminator = r() % 4 ? r() % 3 : r();
            const uint32_t which = r() % 7, kind = r() % 4, flag = r() % 2;
            const uint32_t before = sides[0].Registry()->count;
            {
                const TSEffectInsertValue value(TSEffectRecordKey(first, second, discriminator),
                                                TSEffectRecordValue(which < 6 ? &sides[0].objects[which] : nullptr, kind, uint8_t(flag)));
                struct { void *node; uint8_t inserted; } result = {};
                void *returned = insert(sides[0].Registry(), &result, &value);
                if (returned != &result) ctx.Fail("the insert's result");
                if (result.inserted != (sides[0].Registry()->count != before)) ctx.Fail("the insert's flag");
            }
            TSEffectFindKey key{};
            key.firstText = first;
            key.secondText = second;
            key.discriminator = discriminator;
            sides[1].Extension()->InsertRecord(key, which < 6 ? &sides[1].objects[which] : nullptr, kind, uint8_t(flag));
            repeats += sides[0].Registry()->count == before;
            if (step % 16 == 0 || step + 1 == length) {
                const std::string a = Picture(sides[0]), b = Picture(sides[1]);
                if (a != b) ctx.Fail("seed %u step %u:\n%.600s\n--- ours\n%.600s", seed, step, a.c_str(), b.c_str());
            }
        }
        largest = sides[0].Registry()->bucketCount > largest ? sides[0].Registry()->bucketCount : largest;
        // Each by its own code, or crossed.
        if (seed % 2) {
            destroy(sides[0].Registry());
            sides[1].Extension()->DestroyRegistry();
        } else {
            sides[0].Extension()->DestroyRegistry();
            destroy(sides[1].Registry());
        }
        for (int s = 0; s < 2; ++s)
            for (const Counted &object : sides[s].objects)
                if (object.count != 0) ctx.Fail("seed %u side %d: %u references left", seed, s, object.count);
        delete[] sides;
        ++ctx.cases;
    }
    if (repeats < 100 || largest < 512) ctx.Fail("%u repeated keys, at most %u buckets", repeats, largest);
}
