#pragma once
#include <cstdint>

// Person Data fields (the cITSPerson subobject's slot 10 reads one, slot 11 writes one) that the person's interaction
// bookkeeping (src/simantics/PersonInteractions.cpp) keeps in step with its queues. Names say what the code stores in
// them; the SimAntics trees read them by number.
namespace PersonData {

constexpr uint32_t kCurrentInteractionId = 0x15;      // the current interaction's id (cTSInteraction slot 18)
constexpr uint32_t kQueueSize = 0x1a;                 // element count of the action queue
constexpr uint32_t kPriority = 0x21;                  // the current interaction's priority (slot 22)
constexpr uint32_t kInteractionInitialized = 0x27;    // cleared whenever an interaction is selected or started
constexpr uint32_t kPendingHeadInteractionId = 0x2d;  // id of the head of the pending queue
constexpr uint32_t kFlag47 = 0x47;                    // cleared whenever an interaction is selected or started
constexpr uint32_t kPendingHeadFlag = 0x48;           // slot 33 of the head of the pending queue
constexpr uint32_t kFlag49 = 0x49;                    // cleared (and the person's slot 221 run) when set
constexpr uint32_t kCurrentStackObjectId = 0x50;      // object id of the current interaction's stack object
constexpr uint32_t kPendingHeadStackObjectId = 0x4f;  // object id of the stack object of the pending queue's head
constexpr uint32_t kActiveStackObjectId = 0x4b;       // stack object id of the current interaction (cITSPerson slot 132)
constexpr uint32_t kActiveInteractionId = 0x4c;       // id of the same interaction
constexpr uint32_t kQueueHeadInteractionId = 0x4d;    // id of the head of the action queue
constexpr uint32_t kQueueHeadStackObjectId = 0x4e;    // object id of the stack object of the action queue's head
constexpr uint32_t kLastInteractionId = 0x66;         // id of the selected (else the current) interaction
constexpr uint32_t kLastStackObjectId = 0x67;         // stack object id of the same interaction
constexpr uint32_t kFlag6d = 0x6d;                    // the current interaction's slot 68 word
constexpr uint32_t kFlag6e = 0x6e;                    // cleared whenever an interaction is selected or started
constexpr uint32_t kSelectedStackObjectId = 0x9d;     // object id of the selected interaction's stack object
constexpr uint32_t kCurrentIsSimOnly = 0xa4;          // the current interaction's table entry says slot 14
constexpr uint32_t kCurrentFlagA6 = 0xa6;             // the current interaction's slot 47 byte

} // namespace PersonData
