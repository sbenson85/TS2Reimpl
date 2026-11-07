// nTSSG::cNHoodOccupantManager (lane GO): the message handler and the occupant refresh, highlight and text helpers.
// Split from one file along these concerns; the shared helpers are in nTSSGNHoodOccupantManagerInternal.h.
#include "sg/DetailObject.h"
#include "sg/cRenderGroupNameIterator.h" // RenderGroupRegistry
#include "std/StdInstances.h"
#include "ts/nTSSGNHoodOccupantManager.h"
#include "ts/nTSSGNHoodOccupantManagerInternal.h"

namespace nTSSG {

using namespace occupant_detail;

namespace {

// The message ids DoMessage handles (Initialize listens for these, and the property notifier sends the fade watch id).
constexpr uint32_t kMsgOccupantAdded = 0x0d6429da;
constexpr uint32_t kMsgOccupantChanged = 0x4c095286;
constexpr uint32_t kMsgOccupantRemoved = 0x2c7812db;
constexpr uint32_t kMsgOccupantHighlight = 0x4d501e65;
constexpr uint32_t kMsgOccupantUnhighlight = 0x6d501e6e;
constexpr uint32_t kMsgTextsChanged = 0x2d5c6e19;
constexpr uint32_t kMsgRef78Setting = 0xed5c6df0;
constexpr uint32_t kMsgRebuild = 0xb23b7d66;
constexpr uint32_t kMsgRebuildInMode = 0xb23b7d6f;
constexpr uint32_t kMsgModeChanged = 0x522ced95;

constexpr uint32_t kStandardMessageIid = 0x65297976;
constexpr uint32_t kOccupantIid = 0x0bfeb839;

// Slots of the standard message.
constexpr unsigned kMsgTypeSlot = 4;
constexpr unsigned kMsgWordSlot = 9;   // the highlight colour
constexpr unsigned kMsgFlagSlot = 13;  // the property message: a value is there
constexpr unsigned kMsgValueSlot = 14; // kMsgRebuildInMode's value
constexpr unsigned kMsgAnySlot = 15;   // the property value (its +4 is the integer)
constexpr unsigned kMsgDataSlot = 32;  // the object the message is about

constexpr unsigned kSingletonModeSlot = 33;
constexpr unsigned kModeKindSlot = 17;
constexpr unsigned kModeStateSlot = 31;

constexpr unsigned kRef78TextSlot = 34;
constexpr unsigned kRef78SettingSlot = 35;
constexpr unsigned kTextToCharSlot = 5; // cRZString::ToChar
constexpr uint32_t kRef78SettingGlobal = 0xffac34; // a pointer in the image's data (to 0xdb3304)

// The message's object as an occupant (QI 0x0bfeb839 into `occupant`).
bool MessageOccupant(void *standard, SGRef &occupant)
{
    void *data = reinterpret_cast<void *>(uintptr_t(VirtualCall<Get0, kMsgDataSlot>(standard)));
    return VirtualCall<Query, 0>(data, kOccupantIid, &occupant.mObject) & 0xff;
}

} // namespace

namespace occupant_detail {
// The render-group registry (0xaeb370, ours: sg/cRenderGroupManager.cpp): the bit of the "ShapeColor" group.
uint32_t ShapeColorMask()
{
    const uint32_t bit = nGZSceneGraph::RenderGroupRegistry()->BitOfName("ShapeColor");
    return 1u << (bit & 31);
}
} // namespace occupant_detail

// 0x2a7670.
bool cNHoodOccupantManager::DoMessage(void *message)
{
    if (!message) return false;
    SGRef standard;
    if (!(VirtualCall<Query, 0>(message, kStandardMessageIid, &standard.mObject) & 0xff)) return false;
    bool rebuild = false;
    switch (VirtualCall<Get0, kMsgTypeSlot>(standard.mObject)) {
    case kMsgOccupantAdded:
        if (mDirty) {
            SGRef occupant;
            if (MessageOccupant(standard.mObject, occupant)) {
                AddOccupant(occupant.mObject);
            }
        }
        break;
    case kMsgOccupantRemoved:
        if (mDirty) {
            SGRef occupant;
            if (MessageOccupant(standard.mObject, occupant)) {
                RemoveOccupant(occupant.mObject);
            }
        }
        break;
    case kMsgOccupantChanged:
        if (mDirty) {
            SGRef occupant;
            if (MessageOccupant(standard.mObject, occupant)) {
                if (VirtualCall<Get0, kOccupantTypeSlot>(occupant.mObject) == 1 ||
                    VirtualCall<Get0, kOccupantTypeSlot>(occupant.mObject) == 2)
                    mFloraPending = 1;
                RefreshOccupant(occupant.mObject);
            }
        }
        break;
    case kMsgOccupantHighlight: {
        SGRef occupant;
        if (MessageOccupant(standard.mObject, occupant))
            HighlightOccupant(occupant.mObject, VirtualCall<Get0, kMsgWordSlot>(standard.mObject));
        break;
    }
    case kMsgOccupantUnhighlight: {
        SGRef occupant;
        if (MessageOccupant(standard.mObject, occupant)) ClearOccupantHighlight(occupant.mObject);
        break;
    }
    case kMsgTextsChanged:
        ApplyTexts();
        break;
    case kMsgRef78Setting:
        ApplyRef78Setting();
        break;
    case kFadeDistanceWatchId: {
        if (!(VirtualCall<Get0, kMsgFlagSlot>(standard.mObject) & 0xff)) break;
        const auto *value = reinterpret_cast<const int32_t *>(uintptr_t(VirtualCall<Get0, kMsgAnySlot>(standard.mObject)));
        if (!value) break;
        const int32_t distance = value[1];
        mFadeDistanceSq = uint32_t(distance * distance);
        if (mFadeDistanceSq) break;
        // no fading: every faded prop back to full colour
        const uint32_t mask = ShapeColorMask();
        for (SGNodeEntry *entry = mNodes.data(), *end = entry + mNodes.size(); entry != end; ++entry) {
            cIGZUnknown *node = entry->mNode.mObject;
            const uint8_t faded = entry->mFaded;
            if (node) node->AddRef();
            SGRef shape;
            if ((VirtualCall<Query, 0>(node, kShapeNodeIid, &shape.mObject) & 0xff) && faded) {
                const float opaque[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                SetNodeColor(shape.mObject, 0, opaque);
                ChangeNodeFlags(shape.mObject, 0, mask);
                entry->mFaded = 0;
            }
            shape.Reset();
            if (node) node->Release();
        }
        break;
    }
    case kMsgModeChanged: {
        void *mode = reinterpret_cast<void *>(uintptr_t(VirtualCall<Get0, kSingletonModeSlot>(GetCachedSingleton())));
        rebuild = VirtualCall<Get0, kModeKindSlot>(mode) == 3 && VirtualCall<Get0, kModeStateSlot>(mode) == 1;
        break;
    }
    case kMsgRebuild:
        rebuild = true;
        break;
    case kMsgRebuildInMode: {
        void *mode = reinterpret_cast<void *>(uintptr_t(VirtualCall<Get0, kSingletonModeSlot>(GetCachedSingleton())));
        const uint32_t value = VirtualCall<Get0, kMsgValueSlot>(standard.mObject);
        rebuild = VirtualCall<Get0, kModeKindSlot>(mode) == 3 &&
                  (VirtualCall<Get0, kModeStateSlot>(mode) == 1 || value == 1);
        break;
    }
    default:
        return false;
    }
    // built for one lot only: everything again
    if (rebuild && !mBuildAll) static_cast<cINHoodOccupantManager *>(this)->BuildScene(0);
    return true;
}

// 0x2a9ba0.
void cNHoodOccupantManager::RefreshOccupant(cIGZUnknown *occupant)
{
    if (!OccupantNode(occupant) && VirtualCall<Get0, kOccupantTypeSlot>(occupant) != 1 &&
        VirtualCall<Get0, kOccupantTypeSlot>(occupant) != 2)
        return;
    RemoveOccupant(occupant);
    AddOccupant(occupant);
}

// 0x2a8040.
void cNHoodOccupantManager::HighlightOccupant(cIGZUnknown *occupant, uint32_t argb)
{
    void *node = OccupantNode(occupant);
    if (!node) return;
    const uint32_t mask = ShapeColorMask();
    const float color[4] = {float(int32_t(argb >> 24)) / 255.0f, float(int32_t((argb >> 16) & 0xff)) / 255.0f,
                            float(int32_t((argb >> 8) & 0xff)) / 255.0f, float(int32_t(argb & 0xff)) / 255.0f};
    SetNodeColor(node, 0, color);
    ChangeNodeFlags(node, mask, 0);
}

// 0x2a8e30.
void cNHoodOccupantManager::ClearOccupantHighlight(cIGZUnknown *occupant)
{
    void *node = OccupantNode(occupant);
    if (!node) return;
    ChangeNodeFlags(node, 0, ShapeColorMask());
}

// 0x2a9260.
void cNHoodOccupantManager::ApplyTexts()
{
    if (!mRef78.mObject) return;
    for (SGTextPair *pair = mTexts.data(), *end = pair + mTexts.size(); pair != end; ++pair) {
        const auto *first = reinterpret_cast<const char *>(uintptr_t(VirtualCall<Get0, kTextToCharSlot>(&pair->mFirst.mString)));
        const auto *second = reinterpret_cast<const char *>(uintptr_t(VirtualCall<Get0, kTextToCharSlot>(&pair->mSecond.mString)));
        VirtualCall<uint32_t(__thiscall *)(void *, const char *, const char *), kRef78TextSlot>(mRef78.mObject, second, first);
    }
}

// 0x2a8020.
void cNHoodOccupantManager::ApplyRef78Setting()
{
    if (cIGZUnknown *ref = mRef78.mObject)
        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), kRef78SettingSlot>(ref, *GameAddress<uint32_t>(kRef78SettingGlobal));
}

// ---- adapters ----

using Adapters = cNHoodOccupantManagerAdapters;
using Self = cNHoodOccupantManager;

bool __fastcall Adapters::DoMessage(cIGZMessageTarget2 *face, void *, void *message)
{
    __assume(face != nullptr);
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::DoMessage(message);
}

} // namespace nTSSG
