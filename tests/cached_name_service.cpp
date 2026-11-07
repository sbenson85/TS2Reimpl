// Differential test for 0x20de26: the cached service getter behind the full-name tokens (cache global 0x10cc2dc, service ids
// 0x441e5070 / 0x54b7d5: the language service again, under a second cache). A CachedServiceGetter instantiation
// (src/rz/ServiceGetters.h); the world is a fake service object (the framework global 0x10c9378) whose slot 5 answers the query
// and fake answers whose Release is logged.
#include "difftest.h"
#include "rz/ServiceGetters.h"

#include <cstdint>
#include <vector>

namespace {

constexpr uint32_t kCache = 0x10cc2dc;
constexpr uint32_t kServiceGlobal = 0x10c9378;

struct Fake {
    void **vtable;
    uint32_t id;
};

std::vector<uint32_t> gLog;
Fake *gAnswer;

uint32_t __fastcall Query(Fake *self, void *, uint32_t first, uint32_t second, void **out)
{
    gLog.insert(gLog.end(), {1, self->id, first, second, out ? 1u : 0u});
    *out = gAnswer;
    return 1;
}

uint32_t __fastcall Release(Fake *self, void *)
{
    gLog.insert(gLog.end(), {2, self->id});
    return 0;
}

} // namespace

DIFF_TEST(CachedNameService_20de26, 0x20de26)
{
    void *serviceVtable[8] = {};
    void *answerVtable[8] = {};
    serviceVtable[5] = reinterpret_cast<void *>(&Query);
    answerVtable[2] = reinterpret_cast<void *>(&Release);
    Fake service{serviceVtable, 100}, cached{answerVtable, 101}, answers[2] = {{answerVtable, 102}, {answerVtable, 103}};
    using Getter = void *(__cdecl *)();
    const Getter original = ctx.Original<Getter>(0x20de26);
    const Getter ours = &CachedServiceGetter<kCache, 0x441e5070, 0x54b7d5>;
    void *const savedCache = *ctx.image.At<void *>(kCache);
    void *const savedService = *ctx.image.At<void *>(kServiceGlobal);
    for (Fake *cache : {static_cast<Fake *>(nullptr), &cached}) {
        for (Fake *serviceObject : {static_cast<Fake *>(nullptr), &service}) {
            for (Fake *answer : {static_cast<Fake *>(nullptr), &answers[0], &answers[1]}) {
                gAnswer = answer;
                *ctx.image.At<void *>(kServiceGlobal) = serviceObject;
                *ctx.image.At<void *>(kCache) = cache;
                gLog.clear();
                void *a = original();
                void *cacheA = *ctx.image.At<void *>(kCache);
                const std::vector<uint32_t> logA = gLog;
                *ctx.image.At<void *>(kCache) = cache;
                gLog.clear();
                void *b = ours();
                void *cacheB = *ctx.image.At<void *>(kCache);
                ++ctx.cases;
                if (a != b || cacheA != cacheB || logA != gLog) {
                    ctx.Fail("cache %p service %p answer %p: original %p/%p (%zu words), ours %p/%p (%zu words)", cache, serviceObject, answer, a, cacheA,
                             logA.size(), b, cacheB, gLog.size());
                }
            }
        }
    }
    *ctx.image.At<void *>(kCache) = savedCache;
    *ctx.image.At<void *>(kServiceGlobal) = savedService;
}
