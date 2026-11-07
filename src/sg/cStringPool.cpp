// nGZSceneGraph::cStringPool and its handle iterator (lane OX); see the header.
#include "sg/cStringPool.h"

#include "game/game.h"
#include "rz/RZFastFixedAllocators.h"
#include "rz/RZFixedAllocator.h"
#include "rz/StringKeyParser.h" // Crc32
#include "rz/cRZString.h"      // the empty text 0xd9b9d0

#include <cstring>

namespace nGZSceneGraph {

namespace {

constexpr uint32_t kNoHandle = 0xffffffffu;
constexpr uint32_t kGoldenRatio = 0x9e3779b9;

// lookup2's mix of three words.
inline void Mix(uint32_t &a, uint32_t &b, uint32_t &c)
{
    a -= b; a -= c; a ^= c >> 13;
    b -= c; b -= a; b ^= a << 8;
    c -= a; c -= b; c ^= b >> 13;
    a -= b; a -= c; a ^= c >> 12;
    b -= c; b -= a; b ^= a << 16;
    c -= a; c -= b; c ^= b >> 5;
    a -= b; a -= c; a ^= c >> 3;
    b -= c; b -= a; b ^= a << 10;
    c -= a; c -= b; c ^= b >> 15;
}

inline uint32_t Word(const uint8_t *k)
{
    return k[0] | (uint32_t(k[1]) << 8) | (uint32_t(k[2]) << 16) | (uint32_t(k[3]) << 24);
}

} // namespace

uint32_t JenkinsHash(const void *data, uint32_t length, uint32_t seed)
{
    const uint8_t *k = static_cast<const uint8_t *>(data);
    uint32_t a = kGoldenRatio, b = kGoldenRatio, c = seed;
    uint32_t left = length;
    while (left >= 12) {
        a += Word(k);
        b += Word(k + 4);
        c += Word(k + 8);
        Mix(a, b, c);
        k += 12;
        left -= 12;
    }
    c += length;
    switch (left) { // the first byte of c is the length's, so the tail's c bytes start at the second
    case 11: c += uint32_t(k[10]) << 24; [[fallthrough]];
    case 10: c += uint32_t(k[9]) << 16; [[fallthrough]];
    case 9: c += uint32_t(k[8]) << 8; [[fallthrough]];
    case 8: b += uint32_t(k[7]) << 24; [[fallthrough]];
    case 7: b += uint32_t(k[6]) << 16; [[fallthrough]];
    case 6: b += uint32_t(k[5]) << 8; [[fallthrough]];
    case 5: b += k[4]; [[fallthrough]];
    case 4: a += uint32_t(k[3]) << 24; [[fallthrough]];
    case 3: a += uint32_t(k[2]) << 16; [[fallthrough]];
    case 2: a += uint32_t(k[1]) << 8; [[fallthrough]];
    case 1: a += k[0]; break;
    default: break;
    }
    Mix(a, b, c);
    return c;
}

// ---- cStringHandleIterator ----

void *cStringHandleIterator::operator new(size_t) noexcept { return reinterpret_cast<RZFixedAllocatorBase *>(&ts2_rva_10f8c60)->Alloc(); }

void cStringHandleIterator::operator delete(void *block) noexcept { reinterpret_cast<RZFastFixed0x10 *>(&ts2_rva_10f8c60)->Free(block); }

cStringHandleIterator::cStringHandleIterator(const std::vector<void *> *table, uint32_t index)
    : mCount(0), mIndex(index), mTable(table)
{
    if (index < table->size() && (*table)[index] == nullptr) Next();
}

cStringHandleIterator::cStringHandleIterator(const cStringHandleIterator &other)
    : mCount(0), mIndex(other.mIndex), mTable(other.mTable)
{
}

bool cStringHandleIterator::QueryInterface(uint32_t riid, void **out)
{
    if (out == nullptr || riid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cStringHandleIterator::AddRef() { return ++mCount; }

uint32_t cStringHandleIterator::Release()
{
    if (--mCount != 0) return mCount;
    delete this;
    return 0;
}

nGZEvent::cIAttributeIDIterator *cStringHandleIterator::Clone() { return new cStringHandleIterator(*this); }

bool cStringHandleIterator::IsEqual(const cIAttributeIDIterator *other)
{
    return mIndex == static_cast<const cStringHandleIterator *>(other)->mIndex;
}

uint32_t cStringHandleIterator::Get() { return mIndex; }

void cStringHandleIterator::Next()
{
    do {
        ++mIndex;
        if (mIndex >= mTable->size()) return;
    } while ((*mTable)[mIndex] == nullptr);
}

// ---- cStringPool ----

cStringPool *cStringPool::Instance()
{
    static cStringPool pool;
    return &pool;
}

cStringPool::cStringPool()
{
    mTable.reserve(200);
    mFree.reserve(100);
}

cStringPool::~cStringPool() = default;

bool cStringPool::QueryInterface(uint32_t riid, void **out)
{
    if (out == nullptr || (riid != 1 && riid != kIid)) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cStringPool::AddRef() { return 1; }
uint32_t cStringPool::Release() { return 1; }

void cStringPool::Init() { mStarted = true; }

void cStringPool::Shutdown()
{
    mEntries.clear();
    mTable.clear();
    mFree.clear();
    mStarted = false;
}

cStringPool::Entry *cStringPool::Find(uint32_t handle) const
{
    if (!mStarted || handle >= mTable.size()) return nullptr;
    return static_cast<Entry *>(mTable[handle]);
}

uint32_t cStringPool::Intern(const char *text, bool caseSensitive)
{
    if (!mStarted || text == nullptr) return kNoHandle;
    std::string key(text);
    if (key.empty()) return kNoHandle;
    if (!caseSensitive) _strlwr(key.data());

    auto found = mEntries.find(key);
    if (found != mEntries.end()) return found->second.mIndex;

    uint32_t index;
    if (mFree.empty()) {
        index = static_cast<uint32_t>(mTable.size());
        mTable.push_back(nullptr);
    } else {
        index = mFree.back();
        mFree.pop_back();
    }
    const uint32_t hash = JenkinsHash(key.data(), static_cast<uint32_t>(key.size()), 0);
    auto added = mEntries.emplace(std::move(key), Entry{0, hash, 0, index, nullptr}).first;
    added->second.mText = &added->first;
    mTable[index] = &added->second;
    return index;
}

void cStringPool::AddRefHandle(uint32_t handle)
{
    if (handle == kNoHandle) return;
    if (Entry *entry = Find(handle)) ++entry->mCount;
}

void cStringPool::ReleaseHandle(uint32_t handle)
{
    if (handle == kNoHandle) return;
    Entry *entry = Find(handle);
    if (entry == nullptr || --entry->mCount != 0) return;
    mEntries.erase(mEntries.find(*entry->mText)); // by iterator: the key is the erased node's own
    mTable[handle] = nullptr;
    mFree.push_back(handle);
}

const char *cStringPool::GetText(uint32_t handle)
{
    const Entry *entry = Find(handle);
    return entry ? entry->mText->c_str() : ts2_rva_d9b9d0;
}

uint32_t cStringPool::GetLength(uint32_t handle)
{
    const Entry *entry = Find(handle);
    return entry ? static_cast<uint32_t>(entry->mText->size()) : 0;
}

uint32_t cStringPool::GetHash(uint32_t handle)
{
    const Entry *entry = Find(handle);
    return entry ? entry->mHash : 0;
}

uint32_t cStringPool::GetCrc(uint32_t handle)
{
    Entry *entry = Find(handle);
    if (entry == nullptr) return 0;
    if (entry->mCrc == 0) {
        // Through this pool's own slot 8, as the original.
        const char *text = GetText(handle);
        if (text != nullptr && *text != '\0') entry->mCrc = StringKeyParser::Crc32(text, 1);
    }
    return entry->mCrc;
}

nGZEvent::cAttributeIDRange cStringPool::GetHandles()
{
    auto *end = new cStringHandleIterator(&mTable, static_cast<uint32_t>(mTable.size()));
    auto *begin = new cStringHandleIterator(&mTable, 0);
    return nGZEvent::MakeIteratorRange(begin, end);
}

} // namespace nGZSceneGraph
