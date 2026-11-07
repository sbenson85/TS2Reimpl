#include "rz/RZMessageServer.h"

#include "rz/MiscHelpersB.h"
#include "rz/RZAutoRef.h"

void *GetCachedMessageServer()
{
    if (!ts2_rva_10f519c) {
        ServiceHolder holder;
        ts2_rva_10f519c = holder.QueryFromService()->mObject;
        reinterpret_cast<RZAutoRef *>(&holder)->ReleaseObject();
    }
    return ts2_rva_10f519c;
}
