#include "hook/hooks.h"
#include "gz/cGZEventReflector.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZEvent_cGZEventReflector_Create", 0x1a9e0b, 32, 0xfd3df770,
     reinterpret_cast<void *>(&nGZEvent::cGZEventReflector::Create)},
    {"nGZEvent_cGZEventReflector_Construct", 0x1a999a, 32, 0xb7898f86,
     reinterpret_cast<void *>(&nGZEvent::cGZEventReflectorAdapters::Construct)},
    {"nGZEvent_cEventServiceRef_Acquire", 0x1a991d, 32, 0x42247f2d,
     reinterpret_cast<void *>(&nGZEvent::cGZEventReflectorAdapters::AcquireEventService)},
    {"nGZEvent_SyncActiveBit", 0x1aa148, 22, 0x95563615, reinterpret_cast<void *>(&nGZEvent::SyncActiveBit)},
    {"nGZEvent_MakeIteratorRange", 0x1a98e0, 32, 0xcd957dc4,
     reinterpret_cast<void *>(&nGZEvent::cGZEventReflectorAdapters::MakeIteratorRange)},
    {"nGZEvent_ReleaseIteratorRange", 0x146670, 32, 0x40abf4e3,
     reinterpret_cast<void *>(&nGZEvent::cGZEventReflectorAdapters::ReleaseIteratorRange)},
    {"nGZEvent_cAbstractBehaviorType_Registry", 0x1480d0, 32, 0x8ff279cf,
     reinterpret_cast<void *>(&nGZEvent::cAbstractBehaviorType::Registry)},
    // The registry itself and the two base events' registration (lane PD).
    {"nGZEvent_cAbstractBehaviorType_RegistryFor", 0x1485f0, 32, 0xf0153359,
     reinterpret_cast<void *>(&nGZEvent::cAbstractBehaviorType::RegistryFor)},
    {"nGZEvent_RegisterBaseEvents", 0x1464f0, 32, 0x05312655,
     reinterpret_cast<void *>(&nGZEvent::cAbstractBehaviorType::RegisterBaseEventsAdapter)},
    {"nGZEvent_RegisterConnectionChangedEvent", 0x1465d0, 32, 0x05312655,
     reinterpret_cast<void *>(&nGZEvent::cAbstractBehaviorType::RegisterConnectionChangedAdapter)},
    {"nGZEvent_RegisterAttributeInfosUpdatedEvent", 0x146580, 32, 0x4acdebba,
     reinterpret_cast<void *>(&nGZEvent::cAbstractBehaviorType::RegisterAttributeInfosUpdatedAdapter)},
};
REGISTER_HOOKS(kHooks);

} // namespace
