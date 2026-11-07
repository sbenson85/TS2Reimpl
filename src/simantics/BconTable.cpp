#include "simantics/BconTable.h"

#include "ifaces/BconTable.h"
#include "ifaces/SimServiceC.h"
#include "rz/StaticGetters2.h"
#include "simantics/PrimPersonCommon.h"

uint8_t GetBconTable(uint32_t instance, uint32_t interfaceId, void **table)
{
    ResourceKey key = {kResourceTypeBcon, kResourceGroupDefault, instance, 0};
    return As<ifaces::SimServiceC>(GetCachedSingletonC())->GetResource(&key, interfaceId, table);
}

uint8_t GetBconValue(uint32_t instance, int32_t index, uint16_t *value)
{
    // The table reference is released when the frame unwinds too (an exception from the service or the table).
    PHeld table;
    table.mArmed = true;
    ResourceKey key = {kResourceTypeBcon, kResourceGroupDefault, instance, 0};
    uint8_t found = 0;
    if (As<ifaces::SimServiceC>(GetCachedSingletonC())->GetResource(&key, kBconTableInterfaceId, &table.mObject) != 0 && index >= 0) {
        ifaces::BconTable *const constants = As<ifaces::BconTable>(table.mObject);
        if (index < constants->GetCount()) {
            *value = static_cast<uint16_t>(constants->GetValue(static_cast<uint32_t>(index)));
            found = 1;
        }
    }
    table.ReleaseNow();
    return found;
}
