// Differential tests for nRZSTD::cNotificationHelp1 (lane MC, src/rz/cNotificationHelp1.h).
//
// NotificationHelp_Run: AddNotifications 0xb6d960 and RemoveNotifications 0xb6e070 against ours on objects built the
// same on both sides (the game's vtable 0xde21d8, an id set built in place: empty, one id, several). The framework
// (game global 0x10c9378) is a recording fake whose GetSystemService (slot 5) hands out the message server fake or
// nothing; the server argument is null, that fake or another; each server and the target hand out their
// cIGZMessageServer / cIGZMessageTarget face or refuse; the global itself is null now and then. Compared: the
// result, the event log (every call with its arguments: the ids in the order sent), every fake's count.
#include "difftest.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "rz/cNotificationHelp1.h"

#include <new>
#include <random>
#include <set>

namespace notification_help_test {

using svcfx::Blob;
using svcfx::F;
using svcfx::Obj;
using Help = nRZSTD::cNotificationHelp1;

constexpr uint32_t kFrameworkCell = 0x10c9378;
constexpr uint32_t kServerIid = 0xc1085722;
constexpr uint32_t kTargetIid = 0xe98b2f57;
constexpr uint32_t kServiceId = 0x64063387;
enum : unsigned { kFw = 1, kServer, kArgServer, kServerFace, kTarget, kTargetFace, kOther };

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }

// Hands out `face` for `iid` (counted) now and then; refuses otherwise.
uint32_t Hand(uint32_t *a, unsigned face, uint32_t iid)
{
    if (a[0] != iid || Draw(5) == 0) return 0;
    ++F(face).refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &F(face);
    return 1;
}
uint32_t ServerQuery(Obj *, uint32_t *a) { return Hand(a, kServerFace, kServerIid); }
uint32_t TargetQuery(Obj *, uint32_t *a) { return Hand(a, kTargetFace, kTargetIid); }
uint32_t GetService(Obj *, uint32_t *a)
{
    if (a[0] != kServiceId || a[1] != kServerIid || Draw(5) == 0) return 0;
    ++F(kServer).refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(kServer);
    return 1;
}

void BuildFakes()
{
    svcfx::ResetAll();
    F(kFw).Set(5, "ddp", 0, &GetService);
    F(kServer).Set(0, "dp", 0, &ServerQuery);
    F(kArgServer).Set(0, "dp", 0, &ServerQuery);
    F(kTarget).Set(0, "dp", 0, &TargetQuery);
    F(kOther).Set(0, "dp", 0u); // no face for anything
    F(kServerFace).Set(5, "pd", 1);
    F(kServerFace).Set(6, "pd", 1);
}

struct Object {
    explicit Object(uint32_t seed)
    {
        std::mt19937 rng(seed * 17 + 3);
        mem[0] = uint32_t(uintptr_t(GameAddress<void>(Help::kVTable)));
        ids = ::new (&mem[1]) std::set<uint32_t>;
        const unsigned n = rng() % 3 == 0 ? 0 : 1 + rng() % 6;
        for (unsigned i = 0; i < n; ++i) ids->insert(rng() % 4 ? 0x1000 + rng() % 64 : rng());
    }
    ~Object() { ids->~set(); }
    Object(const Object &) = delete;
    Object &operator=(const Object &) = delete;
    uint32_t mem[3];
    std::set<uint32_t> *ids;
};

} // namespace notification_help_test

using namespace notification_help_test;

DIFF_TEST(NotificationHelp_Run, 0xb6d960)
{
    // Also reached: 0xb6e070 (RemoveNotifications).
    svcfx::Globals globals;
    for (uint32_t seed = 1; seed <= 6000; ++seed) {
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes();
            gRng.seed(seed);
            globals.Set(kFrameworkCell, Draw(8) ? static_cast<void *>(&F(kFw)) : nullptr);
            const unsigned serverPick = Draw(4);
            cIGZUnknown *server = serverPick == 0   ? nullptr
                                  : serverPick == 1 ? reinterpret_cast<cIGZUnknown *>(&F(kArgServer))
                                  : serverPick == 2 ? reinterpret_cast<cIGZUnknown *>(&F(kOther))
                                                    : reinterpret_cast<cIGZUnknown *>(&F(kServer));
            const unsigned targetPick = Draw(5);
            cIGZUnknown *target = targetPick == 0   ? nullptr
                                  : targetPick == 1 ? reinterpret_cast<cIGZUnknown *>(&F(kOther))
                                                    : reinterpret_cast<cIGZUnknown *>(&F(kTarget));
            const bool remove = seed % 2 == 0;
            Object object(seed);
            auto *help = reinterpret_cast<Help *>(object.mem);
            bool r;
            if (side) {
                r = remove ? help->RemoveNotifications(server, target) : help->AddNotifications(server, target);
            } else {
                using Fn = bool(__thiscall *)(void *, cIGZUnknown *, cIGZUnknown *);
                r = ctx.Original<Fn>(remove ? 0xb6e070 : 0xb6d960)(object.mem, server, target);
            }
            Blob &b = results[side];
            b.push_back(r);
            b.push_back(uint32_t(object.ids->size()));
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            svcfx::CaptureFakes(b);
        }
        if (!svcfx::Same(ctx, results[0], results[1], "NotificationHelp_Run", seed)) break;
    }
}
