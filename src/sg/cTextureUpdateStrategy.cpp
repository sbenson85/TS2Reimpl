#include "sg/cTextureUpdateStrategy.h"
#include "gz/cSurfaceDX.h"
#include "sg/DrawMissingTextureName.h"

#include <cstring>

namespace nRZSceneGraph {

using nGZGraphic4::cILockableSurface;
using nGZGraphic4::cSurfaceLock;

bool cTextureUpdateStrategy::QueryInterface(uint32_t riid, void **out)
{
    if (!out || riid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cTextureUpdateStrategy::Release()
{
    if (--mRefs != 0) return mRefs;
    delete this;
    return 0;
}

void cTextureUpdateStrategy::Update(cILockableSurface *surface, uint32_t face, bool first)
{
    FillWhite(surface, face, first);
}

void cTextureUpdateStrategy::FillWhite(cILockableSurface *surface, uint32_t, bool)
{
    cSurfaceLock lock{};
    surface->Lock(&lock, false);
    auto *const bits = static_cast<uint8_t *>(lock.bits);
    for (uint32_t row = 0; row < lock.rows; ++row) std::memset(bits + lock.pitch * row, 0xff, lock.rowBytes);
    // The lock record's end: unlock, let the surface go when the unlock left it set, clear the record.
    if (!lock.surface) return;
    static_cast<cILockableSurface *>(lock.surface)->Unlock(&lock);
    if (void *const held = lock.surface) {
        lock.surface = nullptr;
        static_cast<cIGZUnknown *>(held)->Release();
    }
}

} // namespace nRZSceneGraph

namespace nGZSceneGraph {

cMissingTextureUpdateStrategy::cMissingTextureUpdateStrategy(const char *name)
{
    cRZString *const text = Name();
    text->Construct(name);
    if (text->mString.empty()) text->mString.assign("(empty)");
    else _strupr(text->mString.data()); // 0x1ad17a: the string's buffer through _strupr
}

cMissingTextureUpdateStrategy::~cMissingTextureUpdateStrategy() { Name()->Destruct(); }

void cMissingTextureUpdateStrategy::Update(nGZGraphic4::cILockableSurface *surface, uint32_t, bool)
{
    DrawMissingTextureName(surface, Name()->AsInterface());
}

} // namespace nGZSceneGraph
