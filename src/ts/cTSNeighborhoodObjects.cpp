// cTSNeighborhood::RemoveListedObjects (slot 27, 0x476f90), lane CL.
#include "ts/cTSNeighborhood.h"
#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/GscObjectDefRecord.h"
#include "ifaces/GscObjectDefTable.h"
#include "ifaces/GscObjectTable.h"
#include "ifaces/LotReport.h"
#include "ifaces/TokenObjectInfo.h"
#include "ifaces/cITSGlobals.h"
#include "ifaces/cTSObject.h"
#include "rz/Singleton_20ddfb.h"

#include <cstring>
#include <string>

namespace {
constexpr uint32_t kSelectionName = 0xdca22c;          // the name the definition table selects the removable objects by (game data)
constexpr uint32_t kMissingInstanceText = 0xdca208;    // 32 characters of report text (game data)
constexpr uint32_t kMissingInstanceTextLength = 0x20;
constexpr uint32_t kMissingInstanceSuffix = 0xd9f880;  // one closing character (game data)
constexpr uint32_t kReportCapacity = 0x2f;

// The object definitions every lot is expected to have an instance of, ending in 0.
constexpr uint32_t kRequiredDefinitions[] = {0x81e6bef9, 0x865a6812, 0xef121974, 0xa4258067, 0xaed879c5, 0};

// The object table's list: a std::list head (the sentinel node) and its size; each node holds the object after its two links.
struct ObjectListNode {
    ObjectListNode *mNext;
    ObjectListNode *mPrevious;
    ifaces::cTSObject *mObject;
};
struct ObjectList {
    ObjectListNode *mHead;
    uint32_t mSize;
};
} // namespace

void cTSNeighborhood::RemoveListedObjects(void *report)
{
    auto *app = As<ifaces::cITSGlobals>(GetCachedSingleton());
    auto *objects = app->GetObjectTable();
    auto *definitions = app->GetObjectDefTable();
    definitions->SelectByName(GameAddress<const char>(kSelectionName));
    // Deleting an object changes the list: start over after every delete, leave when a walk finds nothing to delete.
    for (;;) {
        auto *list = static_cast<ObjectList *>(objects->GetObjectList());
        ObjectListNode *node = list->mHead->mNext;
        for (; node != list->mHead; node = node->mNext)
            if (definitions->IsSelected(node->mObject->GetObjectInfo())) break;
        if (node == list->mHead) break;
        objects->DeleteObject(node->mObject->GetObjectId());
    }
    // The first required definition the lot has no instance of is reported (and only that one).
    app = As<ifaces::cITSGlobals>(GetCachedSingleton());
    objects = app->GetObjectTable();
    definitions = app->GetObjectDefTable();
    uint32_t guid = kRequiredDefinitions[0];
    uint32_t next = 1;
    void *record;
    while (objects->FindObjectWithGuid(guid) != nullptr || (record = definitions->FindRecord(guid)) == nullptr) {
        guid = kRequiredDefinitions[next++];
        if (guid == 0) return;
    }
    std::string text;
    text.reserve(kReportCapacity);
    text.append(GameAddress<const char>(kMissingInstanceText), kMissingInstanceTextLength);
    const char *name = static_cast<const char *>(As<ifaces::GscObjectDefRecord>(record)->GetName());
    text.append(name, std::strlen(name));
    text.append(GameAddress<const char>(kMissingInstanceSuffix), 1);
    cRZString message{GameAddress<void>(cRZString::kVTable), std::move(text), 0};
    As<ifaces::LotReport>(report)->ShowText(&message);
}

// ---- adapters (the RVA hooks; src/hook/tables/cTSNeighborhood.cpp): next to the bodies they inline ----

using Adapters = cTSNeighborhoodAdapters;

void __fastcall Adapters::RemoveListedObjects(Self *self, void *, void *report)
{
    [[msvc::forceinline_calls]] self->Self::RemoveListedObjects(report);
}
