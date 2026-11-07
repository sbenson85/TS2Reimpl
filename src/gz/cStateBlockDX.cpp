#include "gz/cStateBlockDX.h"

#include <algorithm>
#include <cstring>

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cDeviceStateDX.h"
#include "ifaces/cIDevice.h"
#include "ifaces/cIDeviceResourceManager.h"
#include "rz/RZFastFixedAllocators.h"

namespace nGZGraphic4 {

namespace {

// The record, field by field (the layout is in cStateBlockDX.h).
constexpr size_t kGroupsAt = 2, kOffsetAAt = 4, kOffsetBAt = 6;

uint16_t Half(const uint8_t *p)
{
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
uint32_t Word(const uint8_t *p)
{
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
// Releases a Direct3D object the record names (a raw word; null is skipped).
void ReleaseWord(const uint8_t *p)
{
    if (auto *object = reinterpret_cast<IUnknown *>(static_cast<uintptr_t>(Word(p)))) object->Release();
}

} // namespace

void *cStateBlockDX::operator new(size_t) { return reinterpret_cast<RZFixedAllocatorBase *>(&Pool())->Alloc(); }
void cStateBlockDX::operator delete(void *block, size_t) { reinterpret_cast<RZFastFixed0x18 *>(&Pool())->Free(block); }

cStateBlockDX::cStateBlockDX() : mRefCount(0), mOwner(nullptr) {}

cStateBlockDX::~cStateBlockDX() { Detach(); }

bool cStateBlockDX::QueryInterface(uint32_t riid, void **out)
{
    if (!out || (riid != 1 && riid != kIidStateBlock && riid != 0xda02e09f)) return false;
    *out = static_cast<cIGZUnknown *>(this);
    AddRef();
    return true;
}

uint32_t cStateBlockDX::AddRef() { return ++mRefCount; }

uint32_t cStateBlockDX::Release()
{
    if (--mRefCount == 0) {
        delete this;
        return 0;
    }
    return mRefCount;
}

void cStateBlockDX::ReleaseResource() { Detach(); }

// The owner's device (the cache's slot 4); both slots tail-call it.
cIDeviceDX *cStateBlockDX::GetDevice() { return reinterpret_cast<cIDeviceDX *>(mOwner->GetDeviceConst()); }
cIDeviceDX *cStateBlockDX::GetDeviceConst() { return reinterpret_cast<cIDeviceDX *>(mOwner->GetDeviceConst()); }
bool cStateBlockDX::IsCreated() { return mOwner != nullptr; }
uint32_t cStateBlockDX::GetMemorySize() { return static_cast<uint32_t>(mRecord.size()) + sizeof(cStateBlockDX); }
bool cStateBlockDX::SurvivesReset() { return false; }
bool cStateBlockDX::SurvivesModeChange() { return false; }
uint8_t cStateBlockDX::GetGroups() { return mRecord[kGroupsAt]; }

bool cStateBlockDX::RecordLess(const cStateBlockDX *a, const cStateBlockDX *b)
{
    if (a->mRecord.size() != b->mRecord.size()) return a->mRecord.size() < b->mRecord.size();
    return std::memcmp(a->mRecord.data(), b->mRecord.data(), a->mRecord.size()) < 0;
}

void cStateBlockDX::Detach()
{
    if (IsCreated()) {
        // Off the cache's list: the blocks of this record's size are searched for this one (the list is in record order,
        // so the equal sizes sit together).
        auto &list = mOwner->mList;
        auto range = std::equal_range(list.begin(), list.end(), this, [](const cStateBlockDX *a, const cStateBlockDX *b) {
            return a->mRecord.size() < b->mRecord.size();
        });
        for (auto it = range.first; it != range.second; ++it) {
            if (*it == this) {
                list.erase(it);
                break;
            }
        }
        auto *manager = As<ifaces::cIDeviceResourceManager>(
            As<ifaces::cIDevice>(mOwner->GetDeviceConst())->GetResourceManager());
        manager->Erase(this);
        mOwner = nullptr;
    }
    if (!mRecord.empty()) {
        ReleaseRecord();
        mRecord.clear();
    }
}

// Releases the Direct3D objects the record names (the order of Serialize) and empties its header.
void cStateBlockDX::ReleaseRecord()
{
    uint8_t *record = mRecord.data();
    const uint8_t groups = record[kGroupsAt];
    if (groups & 1) {
        const uint8_t *p = record + Half(record + kOffsetAAt) + 0x54; // past the change record
        for (uint8_t n = *p++; n != 0; --n, p += 5) ReleaseWord(p + 1); // textures
        if (*p++ != 0) {                                               // vertex shader (and the software flag)
            ReleaseWord(p + 1);
            p += 5;
        }
        if (*p++ != 0) ReleaseWord(p); // pixel shader
    }
    if (groups & 2) {
        const uint8_t *p = record + Half(record + kOffsetBAt) + 4; // past the change bits
        if (*p++ != 0) {                                           // declaration
            ReleaseWord(p);
            p += 4;
        }
        for (uint8_t n = *p++; n != 0; --n, p += 13) ReleaseWord(p + 1); // stream buffers
        if (*p != 0) ReleaseWord(p + 1);                                  // indices
    }
    std::memset(record, 0, kOffsetAAt);
    std::memset(record + kOffsetAAt, 0xff, 4);
}

} // namespace nGZGraphic4
