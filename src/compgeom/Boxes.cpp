#include <algorithm>

#include "compgeom/CompGeom.h"
#include "compgeom/Frames.h"

// nGZNumerics' box helpers (src/compgeom/CompGeom.h). RVAs 1.18.0.187.
namespace nGZCompGeom {

void BoundBox::Grow(const BoundBox &other)
{
    min.x = std::min(min.x, other.min.x);
    min.y = std::min(min.y, other.min.y);
    min.z = std::min(min.z, other.min.z);
    max.x = std::max(max.x, other.max.x);
    max.y = std::max(max.y, other.max.y);
    max.z = std::max(max.z, other.max.z);
}

void BoundBox::Extend(const Vec3 &p)
{
    // Each side reads the whole point before it stores (the point may lie inside this box).
    const Vec3 forMin = p;
    min.x = std::min(min.x, forMin.x);
    min.y = std::min(min.y, forMin.y);
    min.z = std::min(min.z, forMin.z);
    const Vec3 forMax = p;
    max.x = std::max(max.x, forMax.x);
    max.y = std::max(max.y, forMax.y);
    max.z = std::max(max.z, forMax.z);
}

bool BoundBox::Contains(const BoundBox &other) const
{
    // 0x19d2f0: comiss + ja per pair; an unordered pair (a NaN) does not branch.
    if (min.x > other.min.x) return false;
    if (min.y > other.min.y) return false;
    if (min.z > other.min.z) return false;
    if (other.max.x > max.x) return false;
    if (other.max.y > max.y) return false;
    if (other.max.z > max.z) return false;
    return true;
}

BoundBox Cuboid::BoxAt(const Vec3 &centre) const
{
    BoundBox box;
    box.min = {centre.x - half.x, centre.y - half.y, centre.z - half.z};
    box.max = {AddInOrder(half.x, centre.x), AddInOrder(half.y, centre.y), AddInOrder(half.z, centre.z)};
    return box;
}

} // namespace nGZCompGeom
