#include "simantics/AutonomyHelpers.h"

#include "game/game.h"
#include "app/AppInit.h"
#include "ifaces/BconTable.h"
#include "ifaces/KnownObjectSet.h"
#include "ifaces/SelectionLevels.h"
#include "ifaces/SimServiceA.h"
#include "ifaces/SimServiceD.h"
#include "ifaces/InteractionTableEntry.h"
#include "ifaces/TreeOwner.h"
#include "ifaces/cTSBehaviorTree.h"
#include "ifaces/cTSInteraction.h"
#include "ifaces/cTSObject.h"
#include "rz/Singleton_20ddfb.h"
#include "simantics/BconTable.h"
#include "simantics/PrimPersonCommon.h"

#include <map>

namespace {

// Game option bit tested before anything else (0xb39490, __cdecl: the option mask has all the given bits).
constexpr uint32_t kOptionDisableScoreFactor = 0x80;

// The factor table: game global 0x10dac68, 4 x 4 floats (row = first level, column = second level), and the byte that says
// it has been filled (0x10dac5a). The cached service D lives in 0x10cc45c.
constexpr uint32_t kFactorTable = 0x10dac68;
constexpr uint32_t kFactorTableReady = 0x10dac5a;
constexpr uint32_t kCachedServiceD = 0x10cc45c;
constexpr int kTableSide = 4;
constexpr uint32_t kFactorBconInstance = 0x141;
constexpr int32_t kFactorBconFirstEntry = 0x41;
constexpr int kFactorBconEntriesPerRow = 3;
constexpr int kFactorBconLastEntry = 0x4a;
constexpr float kBconPercentDivisor = 100.0f;
// The map the check-tree flags are remembered in (0x516e00): key as built there, value the tree's flags after preparation.
constexpr uint32_t kTreeFlagCache = 0x10dac2c;
constexpr uint32_t kActionStringShown = 0x10da96c;
constexpr uint32_t kTreeFlagNoPreparation = 1; // header flag bit 0: the flags need no preparation for an owner
constexpr uint32_t kServiceDIdFirst = 0x4a037ee3;
constexpr uint32_t kServiceDIdSecond = 0x49ff7d6f;

// The cached service D, acquired like the other cached services (0x20ddfb): from the service object, stored without keeping
// the reference the acquiring local holds.
ifaces::SimServiceD *GetCachedServiceD()
{
    void **const cached = GameAddress<void *>(kCachedServiceD);
    if (*cached == nullptr) {
        ScopedRef ref{nullptr};
        if (void *const service = GetServiceObject()) {
            using Acquire = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
            VirtualCall<Acquire, 5>(service, kServiceDIdFirst, kServiceDIdSecond, &ref.mObject);
        }
        *cached = ref.mObject;
        ref.ReleaseObject();
    }
    return As<ifaces::SimServiceD>(*cached);
}

// Fills the factor table: all ones, then rows 1..3 / columns 1..3 from the BCON table (entry / 100) when it exists.
void InitialiseFactorTable()
{
    float *const table = GameAddress<float>(kFactorTable);
    for (int i = 0; i < kTableSide * kTableSide; ++i) table[i] = 1.0f;
    *GameAddress<uint8_t>(kFactorTableReady) = 1;

    PHeld constants;
    constants.mArmed = true;
    if (GetBconTable(kFactorBconInstance, kBconTableInterfaceId, &constants.mObject) != 0) {
        int32_t entry = kFactorBconFirstEntry;
        float *row = table + kTableSide + 1;
        do {
            for (int column = 0; column < kFactorBconEntriesPerRow; ++column) {
                const int16_t value = As<ifaces::BconTable>(constants.mObject)->GetValue(static_cast<uint32_t>(entry));
                ++entry;
                row[column] = static_cast<float>(value) / kBconPercentDivisor;
            }
            row += kTableSide;
        } while (entry < kFactorBconLastEntry);
    }
    constants.ReleaseNow();
}

} // namespace

void __cdecl SetActionStringShown(uint32_t shown)
{
    *GameAddress<uint8_t>(kActionStringShown) = static_cast<uint8_t>(shown);
}

uint8_t __cdecl WasActionStringShown()
{
    return *GameAddress<uint8_t>(kActionStringShown);
}

bool __stdcall InteractionTreeHasFlags(ifaces::cTSInteraction *interaction, uint32_t mask)
{
    ifaces::InteractionTableEntry *const entry = interaction->GetTableEntry();
    ifaces::TreeOwner *const owner = interaction->GetStackObject()->GetTreeOwner();
    const uint16_t treeId = entry->GetCheckTreeId();
    ifaces::cTSBehaviorTree *const tree = owner->GetTree(treeId);
    if (!tree) return false;

    uint8_t flags = tree->GetFlags();
    if ((flags & kTreeFlagNoPreparation) == 0) {
        // The key: the id sign-extended to 64 bits; for ids outside 0x100..0xfff the tree's identity joins the upper word.
        const int32_t signedId = static_cast<int16_t>(treeId);
        const uint32_t signBits = static_cast<uint32_t>(signedId >> 31);
        uint32_t upper = signBits;
        const uint32_t identity = owner->GetTreeIdentity(treeId);
        if (static_cast<uint16_t>(treeId - 0x100) > 0xeff) upper |= identity;
        const uint64_t key = (static_cast<uint64_t>(upper) << 32) | static_cast<uint32_t>(signedId);

        std::map<uint64_t, uint8_t> &known = *GameAddress<std::map<uint64_t, uint8_t>>(kTreeFlagCache);
        auto found = known.find(key);
        if (found != known.end()) {
            flags = found->second;
        } else {
            tree->Prepare(owner);
            flags = tree->GetFlags();
            found = known.find(key);
            if (found == known.end()) found = known.emplace(key, uint8_t(0)).first;
            found->second = flags;
        }
    }
    return (static_cast<uint8_t>(mask) & flags) != 0;
}

float GetStackObjectScoreFactor(ifaces::cTSObject *stackObject)
{
    if (static_cast<uint8_t>(appinit::HasOptionBits(kOptionDisableScoreFactor)) != 0) return 1.0f;

    if (*GameAddress<uint8_t>(kFactorTableReady) == 0) InitialiseFactorTable();

    ifaces::KnownObjectSet *const known = GetCachedServiceD()->GetKnownObjects();
    uint8_t definitionKey[12];
    if (known->Contains(stackObject->GetDefinitionKey(definitionKey)) != 0) {
        if (ifaces::SelectionLevels *const selection = As<ifaces::SimServiceA>(GetCachedSingleton())->GetSelection()) {
            const int32_t first = selection->GetFirstLevel();
            const int32_t second = selection->GetSecondLevel();
            // (The levels index the table unchecked.)
            return GameAddress<float>(kFactorTable)[second + first * kTableSide];
        }
    }
    return 1.0f;
}
