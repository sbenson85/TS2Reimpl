// Primary-vtable slots of cTSObject, second batch (field map and conventions: src/ts/cTSObject.h). Slot and field names are tentative.
#include "ts/cTSObject.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "ifaces/AttributeTable.h"
#include "ifaces/FootprintPlacer.h"
#include "ifaces/cTSObject.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "simantics/SimGeometry.h"
#include "ts/cTSObjectErrorText.h"
#include "ts/cTSObjectHeld.h"

#include <cstdint>

using namespace tsobject_held;

// Slot 325, 0x540fc0 (ret 4; meaning tentative: ask the first helper for a value, or the default facing offset). The sibling of vf324.
uint32_t cTSObject::vf325(uint32_t a)
{
    using FillFn = void(__thiscall *)(void *, HeldRefList *);
    using QueryFn = uint8_t(__thiscall *)(void *, uint32_t, void **);
    constexpr uint32_t kHelperInterface = 0xaae90adb;

    HeldRef helper;
    HeldRefList list;
    void *const sub = &At<uint8_t>(this, 0x74);
    VirtualCall<FillFn, 10>(sub, &list);
    const uint32_t count = static_cast<uint32_t>(list.mLast - list.mFirst);
    for (uint32_t i = 0; i < count; ++i) {
        void *const entry = list.mFirst[i];
        void **const out = helper.Renew();
        if (VirtualCall<QueryFn, 0>(entry, kHelperInterface, out) != 0) {
            void *const found = helper.Get();
            return VirtualCall<OneArgRet32, 5>(found, a);
        }
    }
    // No helper: facing 0 as a notch offset.
    return static_cast<uint32_t>(simgeom::FacingToNotchOffset(0));
}

// Slot 261, 0x5290e0 (ret 4; meaning tentative: describe the tree sim's current error). The error code is the tree sim's slot 17
// (sub-object at +4, a signed word); its text comes from TreeErrorText, with these exceptions: 0x9b and 0xa1 take the text of the
// cached service C's slot 27 string; 0x25 gets the routing service's text (slot 39) appended; 0x80 and 0x81 (route callback errors)
// become the text followed by one or two data references (routing service slot 37, kinds 0 and 1; element, node, on-object and stack
// numbers) when the first one is set. A code without text, or no message at all, gives the game's "unrecognized code" format with the
// code. The message is assigned to `out` (its slot 16); except for 0x9b/0xa1 the cached service's slot 27 string (the current tree
// location) is then added with out->Sprintf(location format, message, location) when it is not empty. The format strings are read
// from the game's data (RVAs below).
void cTSObject::vf261(cIGZString *out)
{
    constexpr uint32_t kOneRecordFormat = 0xdd20c4;    // text, then one data reference
    constexpr uint32_t kTwoRecordFormat = 0xdd2078;    // text, then two
    constexpr uint32_t kUnrecognizedFormat = 0xdd265c; // the code
    constexpr uint32_t kLocationFormat = 0xdd2680;     // message, location
    using DataFn = void(__thiscall *)(void *, uint32_t, uint32_t *, uint32_t *, uint32_t *, uint32_t *);
    constexpr int32_t kServiceTextCode = 0x9b;
    constexpr int32_t kServiceTextCode2 = 0xa1;
    constexpr int32_t kRoutingSlotCode = 0x25;
    constexpr int32_t kBeforeRouteCode = 0x80;
    constexpr int32_t kAlongRouteCode = 0x81;

    void *const treeSim = &At<uint8_t>(this, 4);
    const int32_t code = static_cast<int16_t>(VirtualCall<NoArgRet32, 17>(treeSim));
    HeldEmptyString message;
    std::string &text = message.Text();
    // GetRoutingService (0x20dee0, tentative name): a service that describes the data references of the routing slot, or null.
    const auto routingService = [] { return OriginalFunction<void *(__cdecl *)(), 0x20dee0>()(); };

    if (code == kServiceTextCode || code == kServiceTextCode2) {
        void *const cache = GetCachedSingletonC();
        void *const source = VirtualCall<NoArgPtr, 27>(cache);
        message.Get()->AsInterface()->operator=(*static_cast<cIGZString *>(source));
    } else if (const char *const fixed = TreeErrorText(code)) {
        if (code == kBeforeRouteCode || code == kAlongRouteCode) {
            HeldEmptyString routeText; // the fixed text waits here; the message is built from it or replaces it
            routeText.Text().assign(fixed);
            void *const service = routingService();
            uint32_t first[4] = {}, second[4] = {}; // four dword locals each: element, node, on-object, stack (read as words)
            if (service) {
                VirtualCall<DataFn, 37>(service, 0, &first[0], &first[1], &first[2], &first[3]);
                VirtualCall<DataFn, 37>(service, 1, &second[0], &second[1], &second[2], &second[3]);
            }
            if (static_cast<uint16_t>(first[0]) != 0) {
                const char *const prefix = routeText.Get()->ToChar();
                const int32_t el = static_cast<int16_t>(first[0]), nd = static_cast<uint16_t>(first[1]);
                const int32_t onObj = static_cast<int16_t>(first[2]), stk = static_cast<int16_t>(first[3]);
                if (static_cast<uint16_t>(second[0]) == 0) {
                    message.Get()->Sprintf(GameAddress<const char>(kOneRecordFormat), prefix, el, nd, onObj, stk);
                } else {
                    message.Get()->Sprintf(GameAddress<const char>(kTwoRecordFormat), prefix, el, nd,
                                           onObj, stk, static_cast<int32_t>(static_cast<int16_t>(second[0])),
                                           static_cast<int32_t>(static_cast<uint16_t>(second[1])),
                                           static_cast<int32_t>(static_cast<int16_t>(second[2])),
                                           static_cast<int32_t>(static_cast<int16_t>(second[3])));
                }
            } else {
                text.assign(routeText.Text().data(), routeText.Text().size());
            }
        } else {
            text.assign(fixed);
            if (code == kRoutingSlotCode) {
                if (void *const service = routingService()) {
                    const char *const detail = VirtualCall<const char *(__thiscall *)(void *), 39>(service);
                    HeldString extra(detail);
                    text.append(extra.Get()->mString);
                }
            }
        }
    }
    if (text.empty()) message.Get()->Sprintf(GameAddress<const char>(kUnrecognizedFormat), code);

    out->operator=(*message.Get()->AsInterface());
    if (code == kServiceTextCode || code == kServiceTextCode2) return;

    // The location of the tree sim, from the cached service C.
    void *const cache = GetCachedSingletonC();
    void *const location = VirtualCall<NoArgPtr, 27>(cache);
    const uint32_t locationLength = VirtualCall<NoArgRet32, 7>(location);
    const char *const locationText = VirtualCall<const char *(__thiscall *)(void *), 6>(location);
    HeldEmptyString locationString;
    locationString.Text().assign(locationText, locationLength);
    if (!locationString.Text().empty()) out->Sprintf(GameAddress<const char>(kLocationFormat), text.c_str(), locationString.Text().c_str());
}

namespace {

// The six bits of attribute 0x2a that the first scan counts, in the order the game tests them; the same values (in the same
// order) as the game's table at 0xdd3688, which the second scan reads from the image.
constexpr uint32_t kCountedBits[] = {0x1, 0x2, 0x4000, 0x4, 0x800, 0x2000};

constexpr uint32_t kBitsTable = 0xdd3688;  // 6 dwords
constexpr uint32_t kKindsTable = 0xdd36a0; // 8 dwords (object kinds 2 3 4 7 8 9 0xa 0xc)
constexpr int kBitsCount = 6;
constexpr int kKindsCount = 8;

} // namespace

// Slot 195, 0x543ae0 (ret 4; meaning tentative: give a list-like target the entries that attributes 4 and 0x2a select). The
// director framework (0xfba4da53, 0xfba4da4d) gives a service whose slot 11 maps a kind to a bit mask; attribute 4 (a) is tested against
// those masks. Returns 0 for a null target. Attribute 0x2a (b) selects up to six bits, attribute 4 up to eight kinds: with exactly one
// selection in all the cache service (GetCachedSingletonC) is asked for it directly (slot 37 for the bit, 38 for the kind); with
// several, it makes a container (slot 34) and every selected bit and kind is looked up (slots 35, 36) and added to the target
// through the target's own slot 19, after the container itself. With no selection the answer is 1 and nothing else happens.
uint8_t cTSObject::vf195(void **target)
{
    using ServiceFn = void(__thiscall *)(void *, uint32_t, uint32_t, void **);
    using MaskFn = uint32_t(__thiscall *)(void *, uint32_t);
    using Ask1Fn = uint8_t(__thiscall *)(void *, void *);
    using Ask2Fn = uint8_t(__thiscall *)(void *, uint32_t, void *);
    using AskOutFn = uint8_t(__thiscall *)(void *, void **);
    using AskIdOutFn = uint8_t(__thiscall *)(void *, uint32_t, void **);
    using AddFn = void(__thiscall *)(void *, void *);

    if (!target) return 0;
    const uint32_t a = VirtualCall<OneArgRet32, 13>(this, 4) & 0xffff;
    const uint32_t b = VirtualCall<OneArgRet32, 13>(this, 0x2a) & 0xffff;
    const int32_t aSigned = static_cast<int16_t>(a);
    const int32_t bSigned = static_cast<int16_t>(b);

    HeldRef masks; // the service mapping a kind to its mask
    void *const framework = GetDirectorFrameworkObject();
    VirtualCall<ServiceFn, 3>(framework, 0xfba4da53, 0xfba4da4d, masks.Renew());
    void *const mapper = masks.Get();
    const auto maskOf = [&](uint32_t kind) { return VirtualCall<MaskFn, 11>(mapper, kind); };

    const uint32_t gate = maskOf(1) & static_cast<uint32_t>(aSigned);
    int32_t bitCount = 0;
    uint32_t lastBit = 0;
    if (gate != 0) {
        // The last of the six bits that b has is the one named when only one is.
        for (const uint32_t bit : kCountedBits) {
            if (static_cast<uint32_t>(bSigned) & bit) {
                ++bitCount;
                lastBit = bit;
            }
        }
    }

    const uint32_t *const kinds = GameAddress<uint32_t>(kKindsTable);
    int32_t kindCount = 0;
    uint32_t lastKind = 0;
    if (static_cast<uint16_t>(a) != 0) {
        for (int i = 0; i < kKindsCount; ++i) {
            if ((maskOf(kinds[i]) & static_cast<uint32_t>(aSigned)) != 0) {
                ++kindCount;
                lastKind = kinds[i];
            }
        }
    }

    void *const cache = GetCachedSingletonC();
    const int32_t total = bitCount + kindCount;
    if (total == 1) {
        if (kindCount > 0) return VirtualCall<Ask2Fn, 38>(cache, lastKind, target);
        return VirtualCall<Ask2Fn, 37>(cache, lastBit, target);
    }
    if (total < 1) return 1;

    if (VirtualCall<Ask1Fn, 33>(cache, target) == 0) return 0;
    HeldRef container;
    if (VirtualCall<AskOutFn, 34>(cache, container.Out()) == 0) return 0;
    // The out reference is reused by every lookup without being dropped in between (the game releases only the last).
    HeldRef found;
    const auto addToTarget = [&](void *object) { VirtualCall<AddFn, 19>(*target, object); };
    if (gate != 0) {
        const uint32_t *const bits = GameAddress<uint32_t>(kBitsTable);
        for (int i = 0; i < kBitsCount; ++i) {
            if ((bits[i] & static_cast<uint32_t>(bSigned)) == 0) continue;
            addToTarget(container.Get());
            if (VirtualCall<AskIdOutFn, 35>(cache, bits[i], found.Out()) != 0) addToTarget(found.Get());
        }
    }
    if (static_cast<uint16_t>(a) != 0) {
        for (int i = 0; i < kKindsCount; ++i) {
            if ((static_cast<uint32_t>(aSigned) & maskOf(kinds[i])) == 0) continue;
            addToTarget(container.Get());
            if (VirtualCall<AskIdOutFn, 36>(cache, kinds[i], found.Out()) != 0) addToTarget(found.Get());
        }
    }
    return 1;
}

namespace {

constexpr float kAttributeStep = 0.01f; // game constant at 0xda74e8
constexpr float kAttributeBase = 1.0f;  // game constant at 0xd9c698
constexpr float kNoLimit = 3.4028235e+38f; // game constant at 0xda7660 (FLT_MAX)

// 1.0 + 0.01 * (attribute 10, a signed word): computed in single precision like the game's (convert, multiply, add).
float AttributeWeight(ifaces::cTSObject *object)
{
    const float scaled = static_cast<float>(static_cast<int32_t>(static_cast<int16_t>(object->GetAttribute(10)))) * kAttributeStep;
    return scaled + kAttributeBase;
}

// The footprint rectangle list a slot hands out: begin and end of 16-byte rectangles (x1, y1, x2, y2 floats).
struct FootprintRects {
    const float *mBegin;
    const float *mEnd;
};

// The game skips a rectangle unless x1 < x2 and y1 < y2 with `jae` after comiss: a NaN passes (the flags say "below").
bool RectUsable(const float *rect)
{
    return !(rect[0] >= rect[2]) && !(rect[1] >= rect[3]);
}

constexpr uint32_t kHelperInterfaceId = 0xaae90adb;
constexpr uint32_t kGroupInterfaceId = 1; // the interface id slot 0 is asked for on the group path

} // namespace

// The callback vf330 hands the placer (0x545f90, cdecl): asks `owner` (a counted object) for interface 0x9b25d761 and whether it
// accepts event 5 for the object id; if so, *outFlag = 0 and *outWeight = AttributeWeight and the answer is true. Without an id
// pointer the answer is false at once. The queried interface is not checked for null (as in the original).
uint8_t __cdecl PlacementEventCheck(int32_t *outFlag, float *outWeight, void *owner, const uint16_t *objectId)
{
    using QueryFn = uint8_t(__thiscall *)(void *, uint32_t, void **);
    constexpr uint32_t kEventInterface = 0x9b25d761;
    constexpr uint32_t kPlacementEvent = 5;

    if (!objectId) return 0;
    HeldRef held;
    if (owner) VirtualCall<QueryFn, 0>(owner, kEventInterface, held.Out());
    auto *const object = As<ifaces::cTSObject>(held.Get());
    const uint32_t id = *objectId;
    uint8_t accepted = 0;
    if (object->NotifyObjectEvent(kPlacementEvent, id, 0, 1, 0) != 0) {
        *outFlag = 0;
        *outWeight = AttributeWeight(object);
        accepted = 1;
    }
    return accepted;
}

// Slot 330, 0x53fbb0 (ret; meaning tentative: put the object's footprint into the placer). Does nothing for an object that is
// unavailable (slot 219), has a pending value (slot 71), is a kind 0x12/0xd one without the forwarding flag and with attribute 0x46
// set, has slot 145 true, or (kinds 8, 9, 0xc) whose attribute table flag 0x4b is not 0x93. Otherwise (after slots 356 when 225
// says so, and 355) and only when the cIEdithObject helper interface 0xaae90adb answers and slot 188 says it is available: the
// footprint rectangles of GetFootprintRects(position, facing) are given to the placer (slot 18 of the +0x40 context, slot 23)
// together with the object id, the level and a weight (1 + attribute 10 / 100, or no limit) and a mode (9 or 11; 0 for the first
// placement; 3 or 5 when the object answers slot 93(5) with a group, then with PlacementEventCheck as callback). When the flag
// bit 0 of the byte at +0x16a is set the tile of GetTileBytes is placed first as a unit rectangle.
void cTSObject::vf330()
{
    using ObjectFn = uint32_t(__thiscall *)(void *);
    using QueryFn = uint8_t(__thiscall *)(void *, uint32_t, void **);
    using InfoFn = void(__thiscall *)(void *);

    ifaces::cTSObject *const self = As<ifaces::cTSObject>(this);
    if (static_cast<uint8_t>(VirtualCall<ObjectFn, 219>(this)) != 0) return;
    if (VirtualCall<ObjectFn, 71>(this) != 0) return;
    int32_t kind = self->ObjectKind();
    if ((kind == 0x12 || kind == 0xd) && self->ForwardsPrimitives() == 0 && self->GetAttribute(0x46) != 0) return;
    if (static_cast<uint8_t>(VirtualCall<ObjectFn, 145>(this)) != 0) return;
    if (static_cast<uint8_t>(VirtualCall<ObjectFn, 225>(this)) != 0) VirtualCall<InfoFn, 356>(this);
    kind = self->ObjectKind();
    if (kind == 9 || kind == 0xc || kind == 8) {
        if (self->GetAttributeTable()->GetFlags(0x4b) != 0x93) return;
    }
    VirtualCall<InfoFn, 355>(this);

    HeldRef helper;
    if (self->QueryEdithInterface(kHelperInterfaceId, helper.Out()) == 0) return;
    if (self->IsAvailable() == 0) return;

    void *const info = self->GetObjectInfo();
    VirtualCall<InfoFn, 8>(info);
    void *const context = At<void *>(this, 0x40);
    auto *const placer = As<ifaces::FootprintPlacer>(VirtualCall<void *(__thiscall *)(void *), 18>(context));
    int32_t facing = self->GetFacingNotch();
    uint8_t scratch[12];
    const FootprintRects *const rects =
        static_cast<const FootprintRects *>(self->GetFootprintRects(self->GetDefinitionKey(scratch), &facing));

    // One placement: the level (GetCategory - 1) and then the object id (sign-extended) are asked for before the call.
    const auto place = [&](float y1, float x1, float y2, float x2, float weight, uint32_t mode, const void *callback, void *callbackObject,
                           uint32_t reserved) {
        const int32_t level = self->GetCategory() - 1;
        const int32_t id = static_cast<int16_t>(self->GetObjectId());
        placer->AddFootprintRect(id, y1, x1, y2, x2, level, weight, mode, callback, callbackObject, reserved);
    };

    if (At<uint8_t>(this, 0x16a) & 1) {
        // The object's own tile as a unit rectangle (y, x, y + 1, x + 1), with the weight, no mode, and 1 in the last argument.
        uint8_t bytes[3];
        const uint8_t *const tileBytes = self->GetTileBytes(bytes);
        int32_t tile[3];
        simgeom::TileFromBytes(tile, tileBytes);
        uint8_t ignored[12];
        self->GetDefinitionKey(ignored); // asked again; the answer is not used
        const float weight = AttributeWeight(self);
        place(static_cast<float>(tile[1]), static_cast<float>(tile[0]), static_cast<float>(tile[1] + 1), static_cast<float>(tile[0] + 1), weight, 0,
              nullptr, nullptr, 1);
    }

    const float *rect = rects->mBegin;
    const float *const scanEnd = rects->mEnd;
    for (;; rect += 4) {
        if (rect == scanEnd) return;
        if (RectUsable(rect)) break;
    }

    float weight;
    uint32_t mode;
    const auto slot100 = [&] { return static_cast<uint8_t>(VirtualCall<ObjectFn, 100>(this)); }; // flag test (bit 0 of +0xb6, bit 12 of +0xf6)
    const auto groupAnswer = [&] { return static_cast<uint16_t>(VirtualCall<OneArgRet32, 93>(this, 5)); };
    if (static_cast<uint8_t>(VirtualCall<ObjectFn, 119>(this)) != 0 && self->ObjectKind() != 9) {
        if (static_cast<int16_t>(self->GetAttribute(10)) <= 0 && groupAnswer() == 0) return;
        mode = 0;
        weight = AttributeWeight(self);
    } else {
        weight = kNoLimit;
        mode = (slot100() == 0) * 2 + 9;
    }

    if (groupAnswer() == 0) {
        for (const float *r = rects->mBegin; r != rects->mEnd; r += 4)
            if (RectUsable(r)) place(r[1], r[0], r[3], r[2], weight, mode, nullptr, nullptr, 0);
        return;
    }
    // The object belongs to a group: the placements carry the group interface (interface id 1) and the event check callback.
    HeldRef group;
    self->QueryInterface(kGroupInterfaceId, group.Out());
    if (mode > 1) mode = (slot100() == 0) * 2 + 3;
    for (const float *r = rects->mBegin; r != rects->mEnd; r += 4)
        if (RectUsable(r)) place(r[1], r[0], r[3], r[2], weight, mode, reinterpret_cast<const void *>(&PlacementEventCheck), group.Get(), 0);
}
