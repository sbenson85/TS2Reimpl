// nRZSTD::cNotificationHelp1 (lane MC): src/rz/cNotificationHelp1.h. Built for speed, as the game's (matchcheck: /O2).
#include "rz/cNotificationHelp1.h"

#include "gz/cGZMessageServer.h" // cIGZMessageServer
#include "gz/cIGZFrameWork.h"
#include "gz/cIGZMessageTarget.h"
#include "gz/cIGZUnknown.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject, ScopedRef

namespace nRZSTD {
namespace {

using Each = bool (cIGZMessageServer::*)(cIGZMessageTarget *, uint32_t);

// A counted face of `object` (the original's RZAutoRef copy: AddRef, QueryInterface, Release), or null.
template <class T> __forceinline T *FaceOf(cIGZUnknown *object, uint32_t iid)
{
    T *face = nullptr;
    if (object) {
        object->AddRef();
        object->QueryInterface(iid, reinterpret_cast<void **>(&face));
        object->Release();
    }
    return face;
}

__forceinline bool ForEachId(const std::set<uint32_t> &ids, cIGZUnknown *server, cIGZUnknown *target, Each each)
{
    ScopedRef held{nullptr};
    if (auto *framework = static_cast<cIGZFrameWork *>(GetServiceObject()))
        framework->GetSystemService(cGZMessageServer::kServiceId, cIGZMessageServer::kIid, &held.mObject);
    auto *const servers = FaceOf<cIGZMessageServer>(server ? server : static_cast<cIGZUnknown *>(held.mObject),
                                                    cIGZMessageServer::kIid);
    bool done = false;
    if (servers) {
        auto *const targets = FaceOf<cIGZMessageTarget>(target, cIGZMessageTarget::kIid);
        if (targets) {
            for (const uint32_t id : ids) (servers->*each)(targets, id);
            done = true;
            targets->Release();
        }
        servers->Release();
    }
    held.ReleaseObject();
    return done;
}

} // namespace

bool cNotificationHelp1::AddNotifications(cIGZUnknown *server, cIGZUnknown *target)
{
    return ForEachId(mIds, server, target, &cIGZMessageServer::AddNotification);
}

bool cNotificationHelp1::RemoveNotifications(cIGZUnknown *server, cIGZUnknown *target)
{
    return ForEachId(mIds, server, target, &cIGZMessageServer::RemoveNotification);
}

} // namespace nRZSTD
