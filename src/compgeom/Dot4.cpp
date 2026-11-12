// nGZCompGeom::Dot4 (0x1a6840, lane 261111d). See compgeom/Rigid.h.
#include "compgeom/ConvexMath.h"
#include "compgeom/Rigid.h"

namespace nGZCompGeom {

float __cdecl Dot4(const float *a, const float *b)
{
    using convex::AddS;
    using convex::MulS;
    const float p0 = MulS(a[0], b[0]);
    const float p1 = MulS(a[1], b[1]);
    float sum = AddS(p1, p0);
    sum = AddS(sum, MulS(a[2], b[2]));
    return AddS(sum, MulS(a[3], b[3]));
}

} // namespace nGZCompGeom
