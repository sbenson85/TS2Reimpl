// nGZSceneGraph::BoxProjection::Project (0xaf25a0, lane 261111d). See sg/SGUtilities.h.
#include "sg/SGUtilities.h"

#include "compgeom/CompGeom.h"
#include "compgeom/ConvexMath.h"
#include "compgeom/Rigid.h"

#include <cmath>

namespace nGZSceneGraph {

namespace {

using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::DivS;
using nGZCompGeom::convex::MulS;
using nGZCompGeom::convex::SubS;

constexpr float kNear = 1e-5f;         // 0x00da7658
constexpr float kNearScale = 100000.0f; // 0x00dbdff8
constexpr float kFar = 1e6f;            // 0x00da456c; -1e6 at 0x00e7d3e0

// The box's edges as corner pairs (0x00e7d380).
constexpr uint8_t kEdges[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7},
                                   {7, 6}, {6, 4}, {4, 0}, {5, 1}, {7, 3}, {6, 2}};

// minss / maxss with p first: the second operand when either is NaN.
float MinSS(float p, float m) { return p < m ? p : m; }
float MaxSS(float p, float m) { return p > m ? p : m; }

struct Clip {
    float x, y, z, w;
};

void Fold(BoxProjection &self, float x, float y, float z)
{
    self.mLo[0] = MinSS(x, self.mLo[0]);
    self.mLo[1] = MinSS(y, self.mLo[1]);
    self.mLo[2] = MinSS(z, self.mLo[2]);
    self.mHi[0] = MaxSS(x, self.mHi[0]);
    self.mHi[1] = MaxSS(y, self.mHi[1]);
    self.mHi[2] = MaxSS(z, self.mHi[2]);
}

void SetAll(float *v, float value) { v[0] = v[1] = v[2] = value; }

} // namespace

// 0xaf25a0
bool BoxProjection::Project(const void *box, const float *matrix)
{
    SetAll(mLo, kFar);
    SetAll(mHi, -kFar);
    mDepthMin = kFar;
    mDepthMax = -kFar;

    const auto *bound = static_cast<const nGZCompGeom::BoundBox *>(box);
    const float *depthRow = matrix + (mOrthographic ? 8 : 12);
    float points[8][4];
    float w[8];
    for (uint32_t i = 0; i < 8; ++i) {
        nGZCompGeom::Vec3 corner;
        const nGZCompGeom::Vec3 *c = bound->Corner(&corner, i);
        points[i][0] = c->x;
        points[i][1] = c->y;
        points[i][2] = c->z;
        points[i][3] = 1.0f;
        w[i] = nGZCompGeom::Dot4(points[i], depthRow);
        mDepthMin = MinSS(w[i], mDepthMin);
        mDepthMax = MaxSS(w[i], mDepthMax);
    }

    bool ok = false;
    if (mOrthographic) {
        for (int i = 0; i < 8; ++i) {
            const float x = nGZCompGeom::Dot4(points[i], matrix);
            const float y = nGZCompGeom::Dot4(points[i], matrix + 4);
            Fold(*this, x, y, w[i]);
        }
        ok = true;
    } else if (mDepthMax > kNear) {
        Clip clip[8];
        bool front[8];
        int inFront = 0, behind = 0;
        for (int i = 0; i < 8; ++i) {
            clip[i].x = nGZCompGeom::Dot4(points[i], matrix);
            clip[i].y = nGZCompGeom::Dot4(points[i], matrix + 4);
            clip[i].z = nGZCompGeom::Dot4(points[i], matrix + 8);
            clip[i].w = w[i];
            front[i] = w[i] > kNear;
            if (front[i]) ++inFront;
            else ++behind;
        }
        if (behind == 8) {
            ok = false;
        } else if (inFront == 8) {
            for (const Clip &c : clip) {
                // cvtps2pd, divsd, cvtpd2ps: w is > 1e-5 here, so r is never NaN and the product order is moot.
                const float r = static_cast<float>(1.0 / static_cast<double>(c.w));
                float x = c.x, y = c.y, z = c.z;
                if (r != 1.0f) {
                    x = MulS(r, x);
                    y = MulS(y, r);
                    z = MulS(z, r);
                }
                Fold(*this, x, y, z);
            }
            ok = true;
        } else {
            for (const auto &edge : kEdges) {
                const Clip &a = clip[edge[0]], &b = clip[edge[1]];
                const bool frontA = front[edge[0]], frontB = front[edge[1]];
                if (frontA) {
                    const float r = DivS(1.0f, a.w);
                    float x = a.x, y = a.y, z = a.z;
                    if (r != 1.0f) {
                        x = MulS(x, r);
                        y = MulS(y, r);
                        z = MulS(z, r);
                    }
                    Fold(*this, x, y, z);
                }
                if (frontA != frontB) {
                    const float d = SubS(b.w, a.w);
                    const float t = std::fabs(d) > kNear ? DivS(SubS(kNear, a.w), d) : (frontA ? 0.0f : 1.0f);
                    const float x = MulS(AddS(MulS(SubS(b.x, a.x), t), a.x), kNearScale);
                    const float y = MulS(AddS(MulS(SubS(b.y, a.y), t), a.y), kNearScale);
                    const float z = MulS(AddS(MulS(SubS(b.z, a.z), t), a.z), kNearScale);
                    Fold(*this, x, y, z);
                }
            }
            ok = true;
        }
    }

    if (ok && mHi[0] >= mLo[0]) {
        for (int k = 0; k < 3; ++k) {
            mLo[k] = MaxSS(-kFar, mLo[k]);
            mHi[k] = MinSS(kFar, mHi[k]);
        }
        return true;
    }
    SetAll(mLo, -1.0f);
    SetAll(mHi, -1.0f);
    return false;
}

} // namespace nGZSceneGraph
