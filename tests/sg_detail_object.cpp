// Differential tests for nGZSceneGraph::GetDetailObject (0xaead80, lane TI).
//
// The service and the objects it hands out are fakes whose slots record their calls. Both sides run from the same
// game globals (the framework's service object 0x10c9378 and the cell 0x10f9028), reset before each run; the result,
// the cell and the recorded calls are compared.
//   service:        slot 5 (a, b, out) stores the held object through the output pointer
//   held object:    slot 2 (Release), slot 9 (no arguments: the detail object)
// A missing service or a missing answer crashes the original, so only present ones are run.
#include "difftest.h"
#include "sg/DetailObject.h"

#include <cstring>
#include <vector>

namespace {

struct Event {
    int slot;
    const void *self;
    uint32_t a, b;
    uintptr_t ret;
};

std::vector<Event> gEvents;
void *gHeldResult = nullptr;    // what the service stores through its output pointer
void *gDetailAnswer = nullptr;  // what the held object's slot 9 answers

constexpr uint32_t kService = 0x10c9378;
constexpr uint32_t kCell = 0x10f9028;

void *gVtService[8];
void *gVtHeld[16];
void *gVtThrowing[16];

uint32_t __fastcall FakeRelease(void *self, void *)
{
    gEvents.push_back({2, self, 0, 0, 0});
    return 0;
}

uint32_t __fastcall FakeServiceQuery(void *self, void *, uint32_t a, uint32_t b, void **out)
{
    gEvents.push_back({5, self, a, b, reinterpret_cast<uintptr_t>(gHeldResult)});
    *out = gHeldResult;
    return 0;
}

void *__fastcall FakeSlot9(void *self, void *)
{
    gEvents.push_back({9, self, 0, 0, reinterpret_cast<uintptr_t>(gDetailAnswer)});
    return gDetailAnswer;
}

void *__fastcall ThrowingSlot9(void *self, void *)
{
    gEvents.push_back({9, self, 0, 0, 0});
    throw 7;
}

void *gService[1] = {gVtService};
void *gHeld[1] = {gVtHeld};
void *gThrowing[1] = {gVtThrowing};
void *gDetailA[1] = {gVtHeld};
void *gDetailB[1] = {gVtHeld};

void SetupFakes()
{
    std::memset(gVtService, 0, sizeof(gVtService));
    std::memset(gVtHeld, 0, sizeof(gVtHeld));
    std::memset(gVtThrowing, 0, sizeof(gVtThrowing));
    gVtService[5] = reinterpret_cast<void *>(&FakeServiceQuery);
    gVtHeld[2] = reinterpret_cast<void *>(&FakeRelease);
    gVtHeld[9] = reinterpret_cast<void *>(&FakeSlot9);
    gVtThrowing[2] = reinterpret_cast<void *>(&FakeRelease);
    gVtThrowing[9] = reinterpret_cast<void *>(&ThrowingSlot9);
}

bool SameEvents(const std::vector<Event> &a, const std::vector<Event> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].slot != b[i].slot || a[i].self != b[i].self || a[i].a != b[i].a || a[i].b != b[i].b || a[i].ret != b[i].ret)
            return false;
    return true;
}

} // namespace

DIFF_TEST(SG_DetailObject_Get, 0xaead80)
{
    SetupFakes();
    auto orig = ctx.Original<void *(__cdecl *)()>(0xaead80);
    for (void *cacheIn : {static_cast<void *>(nullptr), static_cast<void *>(gDetailA)})
        for (void *answer : {static_cast<void *>(gDetailA), static_cast<void *>(gDetailB), static_cast<void *>(nullptr)}) {
            *ctx.image.At<void *>(kService) = gService;
            *ctx.image.At<void *>(kCell) = cacheIn;
            gEvents.clear();
            gHeldResult = gHeld;
            gDetailAnswer = answer;
            void *a = orig();
            const std::vector<Event> eventsA = gEvents;
            void *const cellA = *ctx.image.At<void *>(kCell);

            *ctx.image.At<void *>(kService) = gService;
            *ctx.image.At<void *>(kCell) = cacheIn;
            gEvents.clear();
            gHeldResult = gHeld;
            gDetailAnswer = answer;
            void *b = nGZSceneGraph::GetDetailObject();
            void *const cellB = *ctx.image.At<void *>(kCell);
            ++ctx.cases;
            if (a != b || cellA != cellB || !SameEvents(eventsA, gEvents))
                ctx.Fail("GetDetailObject cache=%p answer=%p: original %p cell %p (%zu events), ours %p cell %p (%zu events)", cacheIn,
                         answer, a, cellA, eventsA.size(), b, cellB, gEvents.size());
        }
    *ctx.image.At<void *>(kCell) = nullptr;
    *ctx.image.At<void *>(kService) = nullptr;
}

// Slot 9 throws: the frame releases the held reference during unwinding, the exception leaves the call, the cell stays empty.
DIFF_TEST(SG_DetailObject_Throws, 0xaead80)
{
    SetupFakes();
    auto orig = ctx.Original<void *(__cdecl *)()>(0xaead80);
    bool thrown[2] = {false, false};
    std::vector<Event> events[2];
    void *cell[2];
    for (int side = 0; side < 2; ++side) {
        *ctx.image.At<void *>(kService) = gService;
        *ctx.image.At<void *>(kCell) = nullptr;
        gEvents.clear();
        gHeldResult = gThrowing;
        try {
            if (side == 0) orig();
            else nGZSceneGraph::GetDetailObject();
        } catch (int) {
            thrown[side] = true;
        }
        events[side] = gEvents;
        cell[side] = *ctx.image.At<void *>(kCell);
    }
    ++ctx.cases;
    if (!thrown[0] || !thrown[1] || cell[0] != cell[1] || !SameEvents(events[0], events[1]))
        ctx.Fail("GetDetailObject throwing slot 9: original thrown=%d (%zu events), ours thrown=%d (%zu events)", thrown[0],
                 events[0].size(), thrown[1], events[1].size());
    *ctx.image.At<void *>(kCell) = nullptr;
    *ctx.image.At<void *>(kService) = nullptr;
}
