// cTSNeighborhood::GetLotValue (slot 23; lane CQ): the worth of a lot from its simulator record, else from its size.
#include "game/game.h"
#include "game/original.h"
#include "ifaces/ComFramework.h"
#include "ifaces/SimServiceC.h"
#include "ifaces/SimTuning.h"
#include "ifaces/cITSBaseLotInfo.h"
#include "ifaces/cITSCatalog.h"
#include "ifaces/cITSGlobals.h"
#include "ifaces/cITSLotInfo.h"
#include "ifaces/cITSNeighborhood.h"
#include "ifaces/cITSNeighborhoodInfo.h"
#include "ifaces/cITSObjectFileRef.h"
#include "ifaces/cITSPersistSystem.h"
#include "ifaces/cTSSimulator.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "ts/cTSNeighborhood.h"
#include "ts/cTSNeighborhoodInternal.h"

namespace {
using namespace nbhood;

constexpr uint32_t kSimulatorClass = 0x53494d49;
constexpr uint32_t kSimulatorInterface = 0x7a3e555d;
constexpr uint32_t kFileRefClass = 0xb54e37d;
constexpr uint32_t kFileRefInterface = 0xeb54e473;
constexpr uint32_t kBindForRead = 4;
constexpr uint32_t kLotFlagNoExtras = 8;           // the base info flag that leaves out the simulator's third part
constexpr uint32_t kGlobalFlagA = 0x16;             // the simulator globals that become the two answered flags
constexpr uint32_t kGlobalFlagB = 0x15;
constexpr uint32_t kOwnerKindWithScale = 2;
constexpr uint32_t kLotTypeResidential = 0, kLotTypeNoValue = 2, kLotTypeCommunity = 3;
constexpr uint32_t kTuningOwnerScale = 3;
constexpr uint32_t kCatalogCache = 0x10cc2d4;

// 0x20ddbf: the catalog, cached the first time (slot 16 of the app singleton).
ifaces::cITSCatalog *Catalog()
{
    void **cache = GameAddress<void *>(kCatalogCache);
    if (*cache == nullptr) *cache = As<ifaces::cITSGlobals>(GetCachedSingleton())->GetCatalog();
    return As<ifaces::cITSCatalog>(*cache);
}

// A length in 1/10 units, converted as the original does: unsigned to double, narrowed to float, times ten, truncated.
int32_t Tenths(int32_t length)
{
    return static_cast<int32_t>(static_cast<float>(static_cast<double>(static_cast<uint32_t>(length))) * 10.0f);
}
} // namespace

uint8_t cTSNeighborhood::GetLotValue(int32_t lotNumber, int32_t *valueOut, uint8_t *flagAOut, uint8_t *flagBOut, uint8_t applyOwnerScale)
{
    int32_t value = 0;
    uint8_t flagA = 0;
    uint8_t flagB = 0;
    if (auto *lot = As<ifaces::cITSLotInfo>(GetLot(lotNumber))) {
        flagB = 1;
        StateController();   // fetched and dropped
        ifaces::cITSPersistSystem *persist = PersistSystem();
        Ref simulatorRef;
        As<ifaces::ComFramework>(GetDirectorFrameworkObject())->GetClassObject(kSimulatorClass, kSimulatorInterface, &simulatorRef.object);
        Ref fileRef;
        As<ifaces::ComFramework>(GetDirectorFrameworkObject())->GetClassObject(kFileRefClass, kFileRefInterface, &fileRef.object);
        auto *simulator = As<ifaces::cTSSimulator>(simulatorRef.object);
        bool fromRecord = false;
        if (As<ifaces::cITSObjectFileRef>(fileRef.object)->BindFile(lot->GetFileName(), kBindForRead) == 0)
            fromRecord = persist->ReadResourceFromSegment(simulatorRef.object, fileRef.object, 1, 0) == 0;
        if (fromRecord) {
            const float scale = Catalog()->GetValueScale();
            const int32_t first = simulator->GetValueAC();
            const float scaled = static_cast<float>(first) * scale;
            const int32_t second = simulator->GetValueA4();
            const int32_t third = simulator->GetValueA0();
            value = static_cast<int32_t>(scaled) + second + third;
            if (!lot->GetBaseLotInfo()->HasFlags(kLotFlagNoExtras)) value += simulator->GetValueA8();
            flagA = simulator->GetGlobal(kGlobalFlagA) != 0;
            flagB = simulator->GetGlobal(kGlobalFlagB) == 0;
        } else {
            // No usable record: the worth comes from the lot's inner size.
            int32_t rect[4];
            lot->GetInnerBounds(rect);
            const int32_t width = Tenths(rect[2] - rect[0]);
            const int32_t height = Tenths(rect[3] - rect[1]);
            value = OriginalFunction<int32_t(__cdecl *)(int32_t, int32_t), 0x7e2c80>()(width, height);
        }
        if (applyOwnerScale != 0 && mOwner && As<ifaces::cITSNeighborhoodInfo>(mOwner)->GetKind() == kOwnerKindWithScale) {
            switch (lot->GetLotType()) {
            case kLotTypeResidential:
            case kLotTypeCommunity: {
                float scale = 1.0f;
                if (GetCachedSingletonC() && As<ifaces::SimServiceC>(GetCachedSingletonC())->GetTuning()) {
                    As<ifaces::SimServiceC>(GetCachedSingletonC())->GetTuning()->GetFloat(kTuningOwnerScale, &scale);
                    value = static_cast<int32_t>(static_cast<float>(value) * scale);
                }
                break;
            }
            case kLotTypeNoValue:
                value = 0;
                break;
            default:
                break;
            }
        }
        value += OriginalFunction<int32_t(__cdecl *)(int32_t), 0x54ca20>()(lotNumber);
    }
    *valueOut = value;
    *flagAOut = flagA;
    *flagBOut = flagB;
    return 1;
}

// ---- adapters (the RVA hooks; src/hook/tables/cTSNeighborhood.cpp): next to the bodies they inline ----

using Adapters = cTSNeighborhoodAdapters;

uint8_t __fastcall Adapters::GetLotValue(Self *self, void *, int32_t lot, int32_t *value, uint8_t *flagA, uint8_t *flagB, uint8_t applyOwnerScale)
{
    [[msvc::forceinline_calls]] return self->Self::GetLotValue(lot, value, flagA, flagB, applyOwnerScale);
}
