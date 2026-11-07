// Differential test for nTSSG::cTSSGSystem::PlayCinematic (slot 28, src/sg/cTSSGSystemCinematicPlay.cpp). Hook shape:
// the original runs on the same object image as ours.
//
// The world is made of wide fakes (tests/sg_tssg_wide.h) and svcfx fakes: mRefB0 (its virtual base's slot 11 is the
// clock), mRefB4 and its animated entry, the scene manager (slot 18 the camera object, slot 17 the scene), the camera's
// holder (+0x128, its slot 54 placement and slot 120 model), the cinematic (actions, match flags, animation lists, the
// parts), the actor objects (models, placements), the extras, the channel owner and channels, the animations and the
// parts (names, labels, flags, durations in st0), the controllers, the wall manager and the tile service; the framework,
// settings (cell 0x10cc28c), app (0x10cc288), properties, probe and message are svcfx fakes. The object's own slots 26
// (find animation) and 29 (end) are a test vtable. Stood in over the image (not replaced yet): 0xae1ed0 (animated face),
// 0xae42c0 (channel owner), 0xae7900 (object name), 0xae4ef0 (blend), 0xae1f50 (attach), 0x26cbe0 (record, 30 words),
// 0xaeb0e0 (cleanup), 0x9c5b7e (probe), 0x212e61 (tile service); cRZStopwatch::Start (ours) on both sides. The stopwatch
// reset (ours) runs for real on the global stopwatch 0x10cc848, whose bytes are compared.
//
// Compared after each run: the event log, the result and *error, the object's bytes (normalised), the fakes, the cells,
// the stopwatch and the frees (count and sorted sizes).
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "sg_tssg_wide.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "rz/cRZStopwatch.h"
#include "sg/cTSSGSystem.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <utility>

namespace {

using namespace svcfx;
using nTSSG::cTSSGCinematicActor;
using nTSSG::cTSSGSystem;
using nTSSG::SGVirtualRef;
using sgwide::W;

constexpr uint32_t kObjectSize = 0x3b8;
constexpr uint32_t kObjectTag = 0x5e000000;
constexpr uint32_t kArrayTag = 0x6a000000;
constexpr uint32_t kServiceCell = 0x10c9378;
constexpr uint32_t kSettingsCell = 0x10cc28c;
constexpr uint32_t kWatch = 0x10cc848;
constexpr uint32_t kCells[] = {kAppCell, kServiceCell, kSettingsCell};
constexpr char kCellKinds[] = "ppp"; // the cells' words as the outcome logs them (svcfx::DumpCells)

enum : uint32_t { evStand = 0x81, evSelf = 0x82, evWords = 0x83 };

constexpr unsigned kActors = 4;
constexpr unsigned kExtras = 2;
constexpr unsigned kAnims = 3;
constexpr unsigned kParts = 3;
enum : unsigned {
    wB0 = 0,
    wB0Face,
    wB4,
    wEntry,
    wEntryBase,
    wAnimatedQ,
    wScene,
    wSceneObject,
    wCamera,
    wCameraFace,
    wCameraBase,
    wHolder,
    wHolderModel,
    wCinematic,
    wWall,
    wOwner,
    wChannel,
    wChannels,
    wBlend,
    wAttached,
    wAttachedFace,
    wEvents,
    wEventsFace,
    wController,
    wOldController,
    wAnimName,
    wTile,
    wObject,                       // kActors
    wModel = wObject + kActors,    // kActors (wModel + 1 is a multiple of 4: its address ends in a zero byte)
    wFace = wModel + kActors,      // kActors
    wExtra = wFace + kActors,      // kExtras
    wAnim = wExtra + kExtras,      // kAnims
    wAnimBase = wAnim + kAnims,    // kAnims
    wPart = wAnimBase + kAnims,    // kParts
    wPartBase = wPart + kParts,    // kParts
    wEnd = wPartBase + kParts,
};
static_assert(wEnd <= sgwide::kWide);
static_assert((wModel + 1) % 4 == 0);

enum : unsigned { fFramework = 0, fService, fApp, fProperties, fSettings, fProbe, fMessage };

struct Draw {
    unsigned actors, extras, parts;
    bool present[kActors], model[kActors], face[kActors];
    uint32_t value[kActors], match[kActors], kind[kActors], level[kActors];
    int nameKind[kActors];
    uint32_t held, b4, qi, sceneCamera, cameraFace, animatedWritten;
    int32_t index;
    uint32_t owner, channel, channelKind, channelsQi, channelsWritten;
    uint32_t target[7], current[7], moved[7];
    uint32_t holderLevel, holderValue, tileLevel;
    uint32_t findMask, findWrites, blendMask, attachMask, eventsMask;
    uint32_t animName[kAnims], partName[kParts], nameId;
    float animLength[kAnims], partLength[kParts];
    uint32_t animFlags[kAnims], partFlags[kParts];
    bool settings, properties, wall, probe, message, oldController;
    uint8_t hiding, option8, field163;
    uint32_t now, restore, saved[3];
    int32_t start, error, endError;
};
Draw gD;

std::string gAction[kActors], gList[kActors];
int32_t gError;
unsigned gFindCalls, gBlendCalls, gAttachCalls, gEventsCalls;
const char *const kLabels[] = {"label0", "a_label_past_sixteen_characters", ""}; // test text
const char *const kNames[] = {nullptr, "", "sim", "a_name_past_sixteen_characters"}; // test text

uint32_t Al(uint32_t v) { return 0xabcd0000u | v; }
void LogWords(const void *p, unsigned count)
{
    Ev(evWords);
    if (!p) return Ev(0xdead);
    for (unsigned i = 0; i < count; ++i) Ev(static_cast<const uint32_t *>(p)[i]);
}
void LogText(const char *text)
{
    if (!text) return Ev(0xdead);
    for (const char *c = text; *c; ++c) Ev(uint8_t(*c));
    Ev(0x7e57);
}
int ObjectIndex(const void *object, unsigned first, unsigned count)
{
    for (unsigned k = 0; k < count; ++k)
        if (object == &W(first + k)) return int(k);
    return -1;
}

// ---- wide customs ----
uint32_t Placement(const uint32_t *words, uint32_t *args)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(args[0])), words, 28);
    return args[0];
}
uint32_t HolderPlacement(sgwide::Wide *, uint32_t *args) { return Placement(gD.target, args); }
uint32_t ObjectPlacement(sgwide::Wide *self, uint32_t *args)
{
    // The first read gives the drawn placement, later ones (after slot 63) the moved one.
    const bool movedAlready = self->value[0] != 0;
    self->value[0] = 1;
    return Placement(movedAlready ? gD.moved : gD.current, args);
}
uint32_t LogTarget(sgwide::Wide *, uint32_t *args) { return LogWords(reinterpret_cast<void *>(uintptr_t(args[0])), 7), 0u; }
uint32_t HolderPose(sgwide::Wide *, uint32_t *args)
{
    const uint32_t pose[3] = {0x3f000000u, gD.holderValue, 0xc1200000u};
    std::memcpy(reinterpret_cast<void *>(uintptr_t(args[0])), pose, 12);
    return 0;
}
uint32_t LogPose(sgwide::Wide *, uint32_t *args) { return LogWords(reinterpret_cast<void *>(uintptr_t(args[0])), 3), 0u; }
uint32_t LogLevel(sgwide::Wide *, uint32_t *args) { return LogWords(reinterpret_cast<void *>(uintptr_t(args[0])), 1), 0u; }
uint32_t LogSpot(sgwide::Wide *, uint32_t *args) { return LogWords(reinterpret_cast<void *>(uintptr_t(args[0])), 3), 0u; }
uint32_t EntryQuery(sgwide::Wide *, uint32_t *args) // (iid, out): the answer points at the vbtable pointer (+4)
{
    if (gD.animatedWritten) sgwide::Out(args, 1, P(&W(wAnimatedQ)) + 4);
    return Al(gD.qi);
}
uint32_t ChannelsQuery(sgwide::Wide *, uint32_t *args)
{
    if (gD.channelsQi & 0xff || gD.channelsWritten) sgwide::Out(args, 1, P(&W(wChannels)));
    return Al(gD.channelsQi);
}
uint32_t PerIndex(const std::string *texts, uint32_t *args) { return args[0] < kActors ? P(texts[args[0]].c_str()) : 0u; }
uint32_t Action(sgwide::Wide *, uint32_t *args) { return PerIndex(gAction, args); }
uint32_t List(sgwide::Wide *, uint32_t *args) { return PerIndex(gList, args); }
uint32_t Match(sgwide::Wide *, uint32_t *args) { return args[0] < kActors ? Al(gD.match[args[0]]) : 0x5a5a5a5au; }

// ---- svcfx customs ----
uint32_t ServiceGet(Obj *, uint32_t *args)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(args[2])) = 0;
    return 0;
}
uint32_t FrameworkGet(Obj *, uint32_t *args) // (class, iid, out)
{
    uint32_t object = 0;
    if (args[0] == 0x4d5d50f0) object = gD.eventsMask >> (gEventsCalls++ & 31) & 1 ? P(&W(wEvents)) + 4 : 0u;
    else if (args[0] == 0xcbc79579) object = gD.message ? P(&F(fMessage)) : 0u;
    else if (args[0] == 0x09d0ce69) object = P(&W(wController));
    *reinterpret_cast<uint32_t *>(uintptr_t(args[2])) = object;
    return object != 0;
}

// ---- the object's own slots ----
uint32_t __fastcall SelfFind(void *self, void *, const char *text, void **out, uint32_t force)
{
    Ev(evSelf), Ev(26), Ev(Norm(P(self)));
    LogText(text);
    Ev(Norm(P(out))), Ev(force);
    const unsigned k = gFindCalls++;
    const bool found = gD.findMask >> (k & 31) & 1;
    if (found || gD.findWrites >> (k & 31) & 1) *out = &W(wAnim + k % kAnims);
    return Al(found);
}
void __fastcall SelfEnd(void *self, void *, uint32_t abort)
{
    Ev(evSelf), Ev(29), Ev(Norm(P(self))), Ev(abort);
    if (gD.endError != -1) gError = gD.endError;
}
void *gSelfVTable[100];

// ---- stand-ins ----
void *__cdecl StandAnimatedOf(void *object)
{
    Ev(evStand), Ev(0xae1ed0), Ev(Norm(P(object)));
    if (object == &W(wCamera)) return gD.cameraFace ? &W(wCameraFace) : nullptr;
    const int k = ObjectIndex(object, wObject, kActors);
    return k >= 0 && gD.face[k] ? &W(wFace + unsigned(k)) : nullptr;
}
void *__cdecl StandChannelOwner(void *animated)
{
    Ev(evStand), Ev(0xae42c0), Ev(Norm(P(animated)));
    return gD.owner ? &W(wOwner) : nullptr;
}
const char *__cdecl StandObjectName(void *object)
{
    Ev(evStand), Ev(0xae7900), Ev(Norm(P(object)));
    const int k = ObjectIndex(object, wObject, kActors);
    return k >= 0 ? kNames[gD.nameKind[k]] : nullptr;
}
void *__cdecl StandProbe()
{
    Ev(evStand), Ev(0x9c5b7e);
    return gD.probe ? &F(fProbe) : nullptr;
}
void *__cdecl StandTile()
{
    Ev(evStand), Ev(0x212e61);
    return &W(wTile);
}
void __cdecl StandCleanUp(void *object, uint32_t a, const char *name, uint32_t b, uint32_t flags)
{
    Ev(evStand), Ev(0xaeb0e0), Ev(Norm(P(object))), Ev(a), Ev(b), Ev(flags);
    LogText(name);
}
void __fastcall StandStart(cRZStopwatch *watch, void *) { Ev(evStand), Ev(0x1c982b), Ev(Norm(P(watch))); }

template <size_t> using Word = uint32_t;
// The stand-ins' argument words as logged (svcfx::DumpWords; Record from its third word).
constexpr char kBlendKinds[] = "ppdd-ddd dddd";
constexpr char kAttachKinds[] = "pppppdd- ddd-pddd ---p-d";
constexpr char kRecordKinds[] = "-pd-dddp d---dddd dddddd-- d--d";
template <class Seq> struct Stand;
template <size_t... I> struct Stand<std::index_sequence<I...>> {
    // 0xae4ef0: (out, animation, 10 words)
    static void __cdecl Blend(Word<I>... a)
    {
        const uint32_t args[] = {a...};
        Ev(evStand), Ev(0xae4ef0);
        svcfx::DumpWords(svcfx::gEv, args, sizeof(args), kBlendKinds);
        const bool give = gD.blendMask >> (gBlendCalls++ & 31) & 1;
        *reinterpret_cast<uint32_t *>(uintptr_t(args[0])) = give ? P(&W(wBlend)) : 0u;
    }
    // 0xae1f50: (out, target, scene, placement, animation, now, start, ...)
    static void __cdecl Attach(Word<I>... a)
    {
        const uint32_t args[] = {a...};
        Ev(evStand), Ev(0xae1f50);
        svcfx::DumpWords(svcfx::gEv, args, sizeof(args), kAttachKinds);
        LogWords(reinterpret_cast<void *>(uintptr_t(args[3])), 7);
        const bool give = gD.attachMask >> (gAttachCalls++ & 31) & 1;
        *reinterpret_cast<uint32_t *>(uintptr_t(args[0])) = give ? P(&W(wAttached)) : 0u;
    }
    // 0x26cbe0: 30 words, the first two texts.
    static void __cdecl Record(Word<I>... a)
    {
        const uint32_t args[] = {a...};
        Ev(evStand), Ev(0x26cbe0);
        LogText(reinterpret_cast<const char *>(uintptr_t(args[0])));
        LogText(reinterpret_cast<const char *>(uintptr_t(args[1])));
        svcfx::DumpWords(svcfx::gEv, args + 2, sizeof(args) - 8, kRecordKinds);
    }
};
using Stand12 = Stand<std::make_index_sequence<12>>;
using Stand22 = Stand<std::make_index_sequence<22>>;
using Stand30 = Stand<std::make_index_sequence<30>>;

struct Fixture {
    Globals globals;
    shape::CodePatch animatedOf{GameAddress<void>(0xae1ed0), shape::CodeOf(&StandAnimatedOf)};
    shape::CodePatch channelOwner{GameAddress<void>(0xae42c0), shape::CodeOf(&StandChannelOwner)};
    shape::CodePatch objectName{GameAddress<void>(0xae7900), shape::CodeOf(&StandObjectName)};
    shape::CodePatch blend{GameAddress<void>(0xae4ef0), shape::CodeOf(&Stand12::Blend)};
    shape::CodePatch attach{GameAddress<void>(0xae1f50), shape::CodeOf(&Stand22::Attach)};
    shape::CodePatch record{GameAddress<void>(0x26cbe0), shape::CodeOf(&Stand30::Record)};
    shape::CodePatch cleanUp{GameAddress<void>(0xaeb0e0), shape::CodeOf(&StandCleanUp)};
    shape::CodePatch probe{GameAddress<void>(0x9c5b7e), shape::CodeOf(&StandProbe)};
    shape::CodePatch tile{GameAddress<void>(0x212e61), shape::CodeOf(&StandTile)};
    shape::StandIn start{0x1c982b, &cRZStopwatch::Start, &StandStart};
    Fixture()
    {
        for (uint32_t cell : kCells) globals.SetWord(cell, 0);
        for (uint32_t i = 0; i < cRZStopwatch::kSize; i += 4) globals.SetWord(kWatch + i, 0);
        gSelfVTable[26] = reinterpret_cast<void *>(&SelfFind);
        gSelfVTable[29] = reinterpret_cast<void *>(&SelfEnd);
        gFrameworkIndex = fFramework;
    }
};

void Wire()
{
    F(fService).Set(5, "ddp", 0u, &ServiceGet);
    F(fFramework).Set(3, "ddp", 0u, &FrameworkGet);
    F(fApp).Set(21, 0, gD.properties ? P(&F(fProperties)) : 0u);
    F(fProperties).Set(14, "pp");
    F(fProperties).Set(13, "pd");
    F(fSettings).Set(26, 0, gD.saved[0]);
    F(fSettings).Set(27, "d");
    F(fSettings).Set(28, 0, gD.saved[1]);
    F(fSettings).Set(29, "d");
    F(fSettings).Set(23, 0, gD.saved[2]);
    F(fSettings).Set(25, "dd");
    F(fProbe).Set(12, "p");
    F(fMessage).Set(5, "d");
    F(fMessage).Set(12, "dd");

    W(wB0).Base(2, &W(wB0Face));
    W(wB0).Set(7, 0, gD.restore);
    W(wB0).Set(1, "d");
    W(wB0Face).Set(11, 0, gD.now);
    W(wB4).Set(35, 1, uint32_t(gD.index));
    W(wB4).text[35] = 1;
    W(wB4).Set(36, "d", &W(wEntry));
    W(wEntry).Base(1, &W(wEntryBase));
    W(wEntryBase).Set(0, "dp", 0u, &EntryQuery);
    W(wAnimatedQ).Base(1, &W(wAnimatedQ));
    W(wScene).Set(18, 0, gD.sceneCamera ? P(&W(wCamera)) : 0u);
    W(wScene).Set(17, 0, &W(wSceneObject));
    W(wCameraFace).Base(1, &W(wCameraBase));
    W(wCameraFace).Set(1, "d-d--");
    W(wHolder).Set(54, "p", 0u, &HolderPlacement);
    W(wHolder).Set(120, 0, &W(wHolderModel));
    W(wHolderModel).Set(36, 0, gD.holderLevel);
    W(wHolderModel).Set(92, 0);
    W(wHolderModel).Set(42, "p", 0u, &HolderPose);
    W(wHolderModel).Set(43, 0, gD.holderValue);
    W(wCinematic).Set(6, 0, gD.parts);
    W(wCinematic).Set(13, "d", 0u, &Match);
    W(wCinematic).Set(14, "d", 0u, &Action);
    W(wCinematic).Set(15, "d", 0u, &List);
    W(wWall).Set(16, 0);
    W(wWall).Set(15, "p", 0u, &LogSpot);
    W(wWall).Set(21, "d");
    W(wOwner).Set(8, 0, gD.channel ? P(&W(wChannel)) : 0u);
    W(wOwner).Set(2, "pdd--", 0x1111u, [](sgwide::Wide *self, uint32_t *) { return self->ret[2]; });
    W(wChannel).Set(14, 0, gD.channelKind);
    W(wChannels).Set(11, "ddd");
    W(wBlend).Base(1, &W(wBlend));
    W(wAttached).Base(1, &W(wAttached));
    W(wAttached).Base(2, &W(wAttachedFace));
    W(wEvents).Base(1, &W(wEvents));
    W(wEvents).Base(2, &W(wEventsFace));
    W(wController).text[3] = 1;
    W(wController).Set(3, 1);
    W(wController).Set(4, "p");
    for (unsigned s : {7u, 8u, 9u, 10u, 12u}) W(wController).Set(s, "p");
    W(wOldController).Base(1, &W(wOldController));
    W(wAnimName).Set(13, 0, gD.nameId);
    W(wTile).Set(89, 0, gD.tileLevel);
    for (unsigned k = 0; k < kActors; ++k) {
        W(wObject + k).Set(120, 0, gD.model[k] ? P(&W(wModel + k)) : 0u);
        W(wObject + k).text[78] = 1;
        W(wObject + k).Set(78, "-d");
        W(wObject + k).Set(54, "p", 0u, &ObjectPlacement);
        W(wObject + k).Set(63, "p", 0u, &LogTarget);
        W(wObject + k).Values("d"); // value[0]: ObjectPlacement's moved flag
        W(wModel + k).Set(0, "dp", 0u, &ChannelsQuery);
        W(wModel + k).Set(171, "p-d", 0u, &LogPose);
        W(wModel + k).Set(170, "pd", 0u, &LogLevel);
        W(wModel + k).Set(47, 0, gD.kind[k]);
        W(wModel + k).Set(36, 0, gD.level[k]);
        W(wFace + k).Set(35, 0);
        W(wFace + k).Set(1, "d-d--");
    }
    for (unsigned k = 0; k < kExtras; ++k) W(wExtra + k).Set(97, "d");
    for (unsigned k = 0; k < kAnims; ++k) {
        sgwide::Wide &a = W(wAnim + k);
        a.Base(1, &W(wAnimBase + k));
        a.Set(17, 0, gD.animName[k] ? P(&W(wAnimName)) : 0u);
        a.Set(55, "d", 0xabcd0010u + k);
        a.Set(53, 0, 0xabcd0100u + k);
        a.Set(50, "d", 0x1230u + k);
        a.Set(52, 0, gD.animFlags[k]);
        a.SetFloat(36, 0, gD.animLength[k]);
        W(wAnimBase + k).Set(17, 0, kLabels[k]);
    }
    for (unsigned k = 0; k < kParts; ++k) {
        sgwide::Wide &p = W(wPart + k);
        p.Base(1, &W(wPartBase + k));
        p.Set(17, 0, gD.partName[k] ? P(&W(wAnimName)) : 0u);
        p.Set(53, 0, 0xabcd0200u + k);
        p.Set(52, 0, gD.partFlags[k]);
        p.SetFloat(36, 0, gD.partLength[k]);
        W(wPartBase + k).Set(17, 0, kLabels[(k + 1) % 3]);
    }
}

alignas(16) uint8_t gObject[kObjectSize];
// gObject's words as the outcome logs them (svcfx::DumpWords): 'p' pointer, 'd' data, 'x' not compared.
constexpr char kObjectKinds[] =
    "pddddddd dddddddd dddddddd ddppdddd dddddddd ddddppdd dddddddd dddddddd "
    "dddddddd pppppppp ppppdddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddd";
alignas(16) cTSSGCinematicActor gActorArray[kActors];
alignas(16) cTSSGCinematicActor gExtraArray[kExtras];
alignas(16) SGVirtualRef gPartArray[kParts];
cTSSGSystem *Ours() { return reinterpret_cast<cTSSGSystem *>(gObject); }

void Build(const uint8_t *start)
{
    std::memcpy(gObject, start, kObjectSize);
    auto put = [](uint32_t offset, const void *p) { std::memcpy(gObject + offset, &p, 4); };
    put(0, gSelfVTable);
    put(0x68, &W(wScene));
    put(0x6c, gD.wall ? &W(wWall) : nullptr);
    put(0xb0, &W(wB0));
    put(0xb4, gD.b4 ? &W(wB4) : nullptr);
    put(0x120, &W(wCinematic));
    put(0x124, gD.oldController ? &W(wOldController) : nullptr);
    put(0x128, &W(wHolder));
    gObject[0x160] = gD.hiding;
    gObject[0x162] = uint8_t(gD.held);
    gObject[0x163] = gD.field163;
    gObject[0x190] = gD.option8;
    for (unsigned k = 0; k < kActors; ++k) {
        gActorArray[k].mObject.mObject = gD.present[k] ? &W(wObject + k) : nullptr;
        gActorArray[k].mValue = gD.value[k];
        gActorArray[k].mRestore = uint8_t(k);
    }
    for (unsigned k = 0; k < kExtras; ++k) {
        gExtraArray[k].mObject.mObject = &W(wExtra + k);
        gExtraArray[k].mValue = gD.value[(k + 1) % kActors] >> 3;
    }
    for (unsigned k = 0; k < kParts; ++k) gPartArray[k].mObject = &W(wPart + k);
    auto vector = [&](uint32_t offset, const void *first, size_t bytes, size_t capacity) {
        put(offset, first);
        put(offset + 4, static_cast<const uint8_t *>(first) + bytes);
        put(offset + 8, static_cast<const uint8_t *>(first) + capacity);
    };
    vector(0x12c, gActorArray, gD.actors * sizeof(gActorArray[0]), sizeof(gActorArray));
    vector(0x138, gExtraArray, gD.extras * sizeof(gExtraArray[0]), sizeof(gExtraArray));
    vector(0x144, gPartArray, gD.parts * sizeof(gPartArray[0]), sizeof(gPartArray));
}

uint32_t Pick(std::mt19937 &gen, std::initializer_list<uint32_t> values) { return values.begin()[gen() % values.size()]; }
float PickFloat(std::mt19937 &gen, std::initializer_list<float> values) { return values.begin()[gen() % values.size()]; }

// Float bits from their ordered value (the inverse of the original's compare).
uint32_t FromOrdered(int32_t o) { return o >= 0 ? uint32_t(o) : (uint32_t(o) - 1u) ^ 0x7fffffffu; }
int32_t ToOrdered(uint32_t bits) { return bits >> 31 ? int32_t((bits ^ 0x7fffffffu) + 1u) : int32_t(bits); }
// `bits` moved by `delta` units in the last place: within the tolerance up to 31 above and 32 below.
uint32_t Nudge(uint32_t bits, int32_t delta) { return FromOrdered(ToOrdered(bits) + delta); }

void DrawPlacement(std::mt19937 &gen)
{
    for (uint32_t &t : gD.target) t = Pick(gen, {0x00000000u, 0x80000000u, 0x3f800000u, 0xbf000000u, 0x00000003u, 0x80000005u}) ^
                                     (gen() % 2 ? gen() & 0x007fffffu : 0u);
    // How the current placement differs: the same, positions nudged, the rotation the same / negated / mixed.
    const int32_t deltas[] = {0, 0, 1, 31, 32, -32, -33, 1000};
    const unsigned mode = gen() % 5;
    for (int i = 0; i < 7; ++i) {
        const int32_t delta = gen() % 2 ? deltas[gen() % 8] : 0;
        uint32_t base = gD.target[i];
        if (i >= 3 && (mode == 2 || (mode == 3 && gen() % 2))) base ^= 0x80000000u;
        gD.current[i] = mode == 4 && i < 3 ? Nudge(base, deltas[gen() % 8]) : Nudge(base, mode == 0 ? 0 : delta);
    }
    for (uint32_t &m : gD.moved) m = gen();
}

std::string MakeList(std::mt19937 &gen)
{
    if (gen() % 5 == 0) return "";
    const std::string delimiter = GameAddress<const char>(0xdaa6c0);
    const char *const tokens[] = {"walk", "  run ", "", "\t", "an_animation_name_past_sixteen"}; // test text
    std::string list;
    const unsigned n = 1 + gen() % 4;
    for (unsigned i = 0; i < n; ++i) {
        if (i) list += delimiter;
        list += tokens[gen() % 5];
    }
    if (gen() % 6 == 0) list += delimiter;
    return list;
}

void DrawCase(std::mt19937 &gen)
{
    gD = Draw{};
    gD.actors = gen() % (kActors + 1);
    gD.extras = gen() % (kExtras + 1);
    gD.parts = gen() % (kParts + 1);
    for (unsigned k = 0; k < kActors; ++k) {
        gD.present[k] = gen() % 4 != 0;
        gD.model[k] = gen() % 12 != 0;
        gD.face[k] = gen() % 4 != 0;
        gD.value[k] = gen();
        gD.match[k] = Pick(gen, {0, 1, 1, 0x100});
        gD.kind[k] = Pick(gen, {0xabcd0005u, 0x0000ffffu, 0x12348000u, 0});
        gD.level[k] = Pick(gen, {0xabcd0000u, 0x00000001u, 0x12340002u, 0x0000fffdu, 0x00007fffu, 0x00008000u});
        gD.nameKind[k] = int(gen() % 4);
        gAction[k] = gen() % 2 ? "" : "act"; // test text
        gList[k] = MakeList(gen);
    }
    gD.held = Pick(gen, {1, 1, 1, 0, 0x80});
    gD.b4 = gen() % 6 != 0;
    gD.index = int32_t(Pick(gen, {0, 0, 0, 2, 0xffffffffu, 0xfffffffeu}));
    gD.qi = Pick(gen, {1, 1, 1, 0, 0x100});
    gD.animatedWritten = gen() % 5 != 0;
    gD.sceneCamera = gen() % 8 != 0;
    gD.cameraFace = gen() % 8 != 0;
    gD.owner = gen() % 3 != 0;
    gD.channel = gen() % 4 != 0;
    gD.channelKind = Pick(gen, {0x26b1, 0x26b2, 0, 0x126b1});
    gD.channelsQi = Pick(gen, {1, 1, 0, 0x100});
    gD.channelsWritten = gen() % 4 != 0;
    DrawPlacement(gen);
    gD.holderLevel = Pick(gen, {0xabcd0000u, 0x00000001u, 0x12340003u, 0x0000ffffu, 0x00008000u});
    gD.holderValue = gen();
    gD.tileLevel = gen();
    gD.findMask = gen() % 6 == 0 ? 0u : gen() | gen();
    gD.findWrites = gen();
    gD.blendMask = gen() % 4 == 0 ? 0u : gen() | gen();
    gD.attachMask = gen() % 4 == 0 ? 0u : gen() | gen();
    gD.eventsMask = gen() % 4 == 0 ? 0u : gen() | gen();
    gD.nameId = gen();
    for (unsigned k = 0; k < kAnims; ++k) {
        gD.animName[k] = gen() % 3 != 0;
        gD.animLength[k] = PickFloat(gen, {0.0f, 1.5f, 33.25f, -2.75f, 0.4f, 3.0e9f, 1.0e-3f});
        gD.animFlags[k] = gen();
    }
    for (unsigned k = 0; k < kParts; ++k) {
        gD.partName[k] = gen() % 3 != 0;
        gD.partLength[k] = PickFloat(gen, {0.0f, 2.5f, 120.75f, -7.5f, 0.6f, 3.0e9f});
        gD.partFlags[k] = gen();
    }
    gD.settings = gen() % 5 != 0;
    gD.properties = gen() % 3 != 0;
    gD.wall = gen() % 4 != 0;
    gD.probe = gen() % 2;
    gD.message = gen() % 3 != 0;
    gD.oldController = gen() % 2;
    gD.hiding = uint8_t(Pick(gen, {0, 0, 1, 0x40}));
    gD.option8 = uint8_t(Pick(gen, {0, 0, 0, 1, 0x80}));
    gD.field163 = uint8_t(Pick(gen, {0, 1, 0xff}));
    gD.now = gen();
    gD.restore = gen();
    for (uint32_t &s : gD.saved) s = gen();
    gD.start = int32_t(Pick(gen, {0, 1, 100, 0xffffff00u, 0x7fffff00u, uint32_t(gen())}));
    gD.error = int32_t(Pick(gen, {0, 5, 0xffffffffu}));
    gD.endError = int32_t(Pick(gen, {0xffffffffu, 0xffffffffu, 0x42}));
}

} // namespace

// 0x250530 PlayCinematic: 0..4 actors (null, model gone, animated face or not, name null/empty/short/long, action or
// not, match flag (al), animation lists empty / one / several tokens, blank and long tokens, a trailing delimiter),
// 0..2 extras, 0..3 parts; held or not, mRefB4, its index (-1, -2), the query, the scene camera and its face; the
// channel owner, its channel and kind (0x26b1 kept, 0x126b1 not), the channels query; placements equal, nudged by 1, 31,
// 32, -32, -33 units in the last place (the tolerance's edges), the rotation negated or mixed; animations found or
// not (written or not), blend, attach and events given or not, lengths (fractions, negative, past the int range); the
// settings, properties, wall manager, probe and message present or not, hiding and render option 8 set or not; the
// old controller; start and *error values; slot 29 writing *error.
DIFF_TEST(sg_tssg_system_play_cinematic, 0x250530)
{
    Fixture fixture;
    using Fn = uint32_t(__thiscall *)(void *, int32_t, int32_t *);
    auto original = ctx.Original<Fn>(0x250530);
    gEv.reserve(1 << 18); // the log must not reallocate (and free) while frees are recorded
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x250530u);
        DrawCase(gen);
        uint8_t start[kObjectSize];
        for (uint8_t &byte : start) byte = uint8_t(gen());
        uint8_t watch[cRZStopwatch::kSize];
        for (uint8_t &byte : watch) byte = uint8_t(gen());
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            ResetAll();
            sgwide::ResetWide();
            Wire();
            *GameAddress<void *>(kAppCell) = &F(fApp);
            *GameAddress<void *>(kServiceCell) = &F(fService);
            *GameAddress<void *>(kSettingsCell) = gD.settings ? &F(fSettings) : nullptr;
            std::memcpy(GameAddress<void>(kWatch), watch, sizeof(watch));
            Build(start);
            AddRange(gObject, kObjectSize, kObjectTag);
            AddRange(gActorArray, sizeof(gActorArray), kArrayTag);
            AddRange(gExtraArray, sizeof(gExtraArray), kArrayTag | 0x100);
            AddRange(gPartArray, sizeof(gPartArray), kArrayTag | 0x200);
            gError = gD.error;
            gFindCalls = gBlendCalls = gAttachCalls = gEventsCalls = 0;
            ctx.Mark(side ? "ours" : "original");
            difftest::FreeRecorder frees;
            const uint32_t result =
                side == 0 ? original(gObject, gD.start, &gError) & 0xff : Ours()->PlayCinematic(gD.start, &gError);
            frees.Stop();
            Blob b = gEv;
            b.push_back(0xeeee);
            b.push_back(result);
            b.push_back(uint32_t(gError));
            b.push_back(uint32_t(frees.Count()));
            size_t sizes[256];
            const size_t n = frees.SortedSizes(sizes, 256);
            for (size_t i = 0; i < n; ++i) b.push_back(uint32_t(sizes[i]));
            b.push_back(frees.Overflowed());
            svcfx::DumpWords(b, gObject, sizeof(gObject), kObjectKinds);
            for (uint32_t i = 0; i < cRZStopwatch::kSize; i += 4) b.push_back(*GameAddress<uint32_t>(kWatch + i));
            CaptureFakes(b);
            sgwide::CaptureWide(b);
            svcfx::DumpCells(b, kCells, kCellKinds);
            outcome[side] = b;
        }
        Same(ctx, outcome[0], outcome[1], "play_cinematic", seed);
    }
    // The arrays' references belong to the fakes: nothing to release when the program ends.
    for (cTSSGCinematicActor &a : gActorArray) a.mObject.mObject = nullptr;
    for (cTSSGCinematicActor &a : gExtraArray) a.mObject.mObject = nullptr;
    for (SGVirtualRef &r : gPartArray) r.mObject = nullptr;
}
