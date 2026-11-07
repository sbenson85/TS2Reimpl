// cTSPerson::vf198 (0x51da50): the person's override of cTSObject's slot 198.
#include "simantics/PersonVectors.h"
#include "simantics/PrimPerson.h"

#include "game/vtable.h"
#include "ifaces/PersonEnvironment.h"
#include "ifaces/PersonInitHelper.h"
#include "ifaces/PersonList.h"
#include "ifaces/PersonMotives.h"
#include "ifaces/PersonServices.h"
#include "ifaces/cITSPerson.h"
#include "ts/cTSObject.h"

void PrimPersonObject::Initialise198(uint32_t argument)
{
    reinterpret_cast<cTSObject *>(this)->vf198(argument); // the base, cTSObject's slot 198

    // mCandidates.reserve(512) and the motive increments' reserve(2), through the vectors' own workers.
    uint32_t candidates = 512;
    if (mCandidates.capacity() < candidates)
        ActionRecordsReallocateExactly(reinterpret_cast<ActionRecordVector *>(&mCandidates), nullptr, &candidates);
    uint32_t motives = 2;
    if (static_cast<uint32_t>((mMotiveLimit - mMotiveBegin) >> 4) < motives)
        MotiveReallocateExactly(reinterpret_cast<MotiveIncVector *>(&mMotiveBegin), nullptr, &motives);

    mRefsAInUse = nullptr;
    mRefsBInUse = nullptr;
    ifaces::cITSPerson *const person = As<ifaces::cITSPerson>(&mPersonVTable);
    As<ifaces::PersonMotives>(&mObject60cVTable)->AttachPerson(person);
    ifaces::PersonInitHelper *const helper = As<ifaces::PersonServices>(mHolder48)->GetInitHelper();
    if (helper->IsReady() == 0) helper->Prepare();
    As<ifaces::PersonEnvironment>(mEnvironment)->GetPersonList()->AddPerson(person);
    person->ApplyPetScale();
}
