// nGZSceneGraph::ShapeMaterialHandle (lane 261111d; the members were cShape.cpp's). See sg/cShape.h.
#include "sg/cMaterialManager.h"
#include "sg/cShape.h"

#include "misc/UtilitiesE.h" // GetCachedQueriedObject: the material manager

#include <new>

namespace nGZSceneGraph {

namespace {

// 0xae8900, asked again for each call as the original does.
cIMaterialManager *MaterialManager() { return static_cast<cIMaterialManager *>(GetCachedQueriedObject()); }

} // namespace

// 0xae8820
ShapeMaterialHandle::ShapeMaterialHandle(int32_t index) : mIndex(index) { MaterialManager()->AddRefIndex(mIndex); }

// 0xae8840
ShapeMaterialHandle::ShapeMaterialHandle(const char *name)
    : mIndex(static_cast<int32_t>(MaterialManager()->FindOrLoad(name)))
{
    MaterialManager()->AddRefIndex(mIndex);
}

// 0xae8800
ShapeMaterialHandle::ShapeMaterialHandle(const ShapeMaterialHandle &other) : mIndex(other.mIndex)
{
    MaterialManager()->AddRefIndex(mIndex);
}

// 0xae88a0
ShapeMaterialHandle &ShapeMaterialHandle::Assign(const ShapeMaterialHandle &source)
{
    MaterialManager()->AddRefIndex(source.mIndex);
    MaterialManager()->ReleaseIndex(mIndex);
    mIndex = source.mIndex;
    return *this;
}

// 0xae8a00
void ShapeMaterialHandle::Release()
{
    if (mIndex != -1) {
        MaterialManager()->ReleaseIndex(mIndex);
        mIndex = -1;
    }
}

// 0xae8870
ShapeMaterialHandle::~ShapeMaterialHandle() { Release(); }

ShapeMaterialHandle *ShapeMaterialHandle::ConstructIndex(int32_t index) { return ::new (this) ShapeMaterialHandle(index); }
ShapeMaterialHandle *ShapeMaterialHandle::ConstructByName(const char *name)
{
    return ::new (this) ShapeMaterialHandle(name);
}
ShapeMaterialHandle *ShapeMaterialHandle::ConstructCopy(const ShapeMaterialHandle &other)
{
    return ::new (this) ShapeMaterialHandle(other);
}
void ShapeMaterialHandle::Destruct() { this->~ShapeMaterialHandle(); }

} // namespace nGZSceneGraph
