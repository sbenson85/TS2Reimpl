// Differential test for cTSNeighborhood::RemoveListedObjects (slot 27, 0x476f90), lane CL. Fixture: tests/ts_services_fixture.h.
//
// All fakes: the app singleton, the object table (whose list is a real circular list of nodes that DeleteObject unlinks), the
// definition table, the objects and their infos, one definition record and the report. Compared: the event log (every call and its
// arguments, the report text by content, capacity and size), the list that is left and the fakes' words.
#include "ts_services_fixture.h"

#include "rz/cRZString.h"
#include "ts/cTSNeighborhood.h"
#include "ts_neighborhood_shape.h"

#include <random>
#include <string>

namespace {

// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{1, "d"}, {2, "-ddddd"}, {4, "d------d"}, {5, "d------d"}, {6, "d------d"}, {7, "d------d"}, {8, "d------d"}, {9, "d------d"}};
using namespace svcfx;
using Nb = cTSNeighborhood;

struct Node {
    Node *next, *previous;
    Obj *object;
};
struct List {
    Node *head;
    uint32_t size;
};
constexpr unsigned kObjectsFirst = 4, kObjectCount = 6, kInfosFirst = 10;
Node gHead;
Node gNodes[kObjectCount];
List gList;
char gName[64];

uint32_t Hash(const char *text)
{
    uint32_t h = 2166136261u;
    for (; *text; ++text) h = (h ^ uint8_t(*text)) * 16777619u;
    return h;
}

uint32_t ObjectId(Obj *o, uint32_t *)
{
    o->value[7] = o->value[7] * 1664525u + 1013904223u;
    return (o->value[7] & 0xffff0000u) | o->value[0];
}
uint32_t Select(Obj *self, uint32_t *a)
{
    for (unsigned i = 0; i < kObjectCount; ++i)
        if (a[0] == P(&F(kInfosFirst + i))) return (self->value[0] >> i) & 1;
    return 0;
}
uint32_t SelectName(Obj *, uint32_t *a)
{
    Ev(Hash(reinterpret_cast<const char *>(a[0])));
    return 0;
}
uint32_t Delete(Obj *, uint32_t *a)
{
    for (Node *n = gHead.next; n != &gHead; n = n->next) {
        if (n->object->value[0] == (a[0] & 0xffff)) {
            n->previous->next = n->next;
            n->next->previous = n->previous;
            --gList.size;
            break;
        }
    }
    return 0;
}
uint32_t Count(Obj *self, uint32_t *a)
{
    // value[1..5]: how many instances each of the five required definitions has
    static const uint32_t guids[] = {0x81e6bef9, 0x865a6812, 0xef121974, 0xa4258067, 0xaed879c5};
    for (unsigned i = 0; i < 5; ++i)
        if (a[0] == guids[i]) return self->value[1 + i];
    return 0;
}
uint32_t Find(Obj *self, uint32_t *a)
{
    static const uint32_t guids[] = {0x81e6bef9, 0x865a6812, 0xef121974, 0xa4258067, 0xaed879c5};
    for (unsigned i = 0; i < 5; ++i)
        if (a[0] == guids[i] && ((self->value[0] >> i) & 1)) return P(&F(3));
    return 0;
}
uint32_t ReportText(Obj *, uint32_t *a)
{
    const auto *text = reinterpret_cast<const cRZString *>(a[0]);
    Ev(Hash(text->Data()));
    Ev(uint32_t(text->mString.size()));
    Ev(uint32_t(text->mString.capacity()));
    Ev(text->mRefCount);
    return 0;
}
} // namespace

DIFF_TEST(NeighborhoodLotsC_RemoveListedObjects, 0x476f90)
{
    using Fn = void(__thiscall *)(Nb *, void *);
    unsigned ok = 0;
    for (uint32_t seed = 0; seed < 400; ++seed) {
        for (int route = 0; route < 2; ++route) {
            nbshape::gRoute = nbshape::Route(route);
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                ResetAll();
                std::mt19937 rng(seed * 7919u + 17);
                Globals g;
                alignas(8) uint8_t bytes[sizeof(Nb)] = {};
                Nb *nb = reinterpret_cast<Nb *>(bytes);
                nbshape::Build(ctx, side == 0, bytes);
                // 0 app, 1 definition table, 2 object table, 3 definition record, 4.. objects, 10.. infos, 11 report (index 16)
                g.Set(kAppCell, &F(0));
                F(0).Set(24, 0, P(&F(1)));
                F(0).Set(26, 0, P(&F(2)));
                F(1).value[0] = rng() & 0x3f;                 // the selected infos (bit per object) / the definitions with a record
                F(1).Set(16, "p", 0, SelectName);
                F(1).Set(43, "p", 0, Select);
                F(1).Set(14, "d", 0, Find);
                F(2).Set(31, 0, P(&gList));
                F(2).Set(17, "d", 0, Delete);
                F(2).Set(42, "d", 0, Count);
                for (int i = 0; i < 5; ++i) F(2).value[1 + i] = (rng() % 3 == 0) ? 1 + rng() % 3 : 0;
                std::strcpy(gName, (seed % 4 == 0) ? "" : (seed % 4 == 1) ? "A fairly long definition name that is not short" : "Chair");
                F(3).Set(8, 0, P(gName));
                F(16).Set(17, "p", 0, ReportText);
                gHead.next = gHead.previous = &gHead;
                gList.head = &gHead;
                gList.size = 0;
                Node *tail = &gHead;
                const unsigned count = rng() % (kObjectCount + 1);
                for (unsigned i = 0; i < count; ++i) {
                    Obj &object = F(kObjectsFirst + i);
                    object.value[0] = 100 + i * 7;
                    object.Set(61, 0, P(&F(kInfosFirst + i)));
                    object.Set(47, 0, 0, ObjectId);
                    gNodes[i].object = &object;
                    gNodes[i].previous = tail;
                    gNodes[i].next = &gHead;
                    tail->next = &gNodes[i];
                    gHead.previous = &gNodes[i];
                    tail = &gNodes[i];
                    ++gList.size;
                }
                nbshape::Call<Fn>(ctx, side == 0, nb, 0, 0x476f90, NB_ADAPTER(RemoveListedObjects), &F(16));
                for (Node *n = gHead.next; n != &gHead; n = n->next) Ev(n->object->value[0]);
                Blob &b = blobs[side];
                b.insert(b.end(), gEv.begin(), gEv.end());
                CaptureFakes(b, kValueKinds);
                nbshape::Destroy(ctx, side == 0, nb);
            }
            if (Same(ctx, blobs[0], blobs[1], "RemoveListedObjects", seed)) ++ok;
        }
        nbshape::gRoute = nbshape::kVTableRoute;
    }
    std::printf("  RemoveListedObjects: %u worlds ok\n", ok);
}
