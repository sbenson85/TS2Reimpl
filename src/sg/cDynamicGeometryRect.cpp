// nGZSceneGraph::cDynamicGeometryRect (lane QR). Layout and the hook decision: src/sg/cDynamicGeometryRect.h.
#include "sg/cDynamicGeometryRect.h"

#include "gz/GraphicServices.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceStateDX.h"
#include "gz/cVertexArrayDX.h"

#include <xmmintrin.h>

#include <cstring>
#include <new>

namespace nGZSceneGraph {

namespace {

constexpr uint32_t kUsagePosition = 0x5b830781;
constexpr uint32_t kUsageTexture = 0xbb8307ab;
constexpr uint32_t kIidWhole = 0x89d39f12;

// cvttss2si: the truncation the original's compiler emitted (0x80000000 for NaN and overflow).
int32_t Truncate(float value) { return _mm_cvttss_si32(_mm_set_ss(value)); }

void PutFloat(uint8_t *at, float value) { std::memcpy(at, &value, sizeof value); }

cBounds Bounds()
{
    return {{-1000.0f, -1000.0f, -1000.0f}, {1000.0f, 1000.0f, 1000.0f}};
}

} // namespace

// 0xa43ec0: the fields, then the vertex format (position and texture coordinate in stream 0) with its stride and offsets.
cDynamicGeometryRect::cDynamicGeometryRect()
{
    mRefs = 0;
    mFlags = 1;
    mZeros[0] = mZeros[1] = mZeros[2] = mZeros[3] = 0;
    mBoundsA = Bounds();
    mDrawInfo[0] = 4;
    mDrawInfo[1] = 1;
    mZeros2[0] = mZeros2[1] = mZeros2[2] = 0;
    mBoundsB = Bounds();
    mShort = 1;
    mFormat = nullptr;
    mDeclaration = nullptr;
    mUvFlag = 0;

    auto formats = [] { return static_cast<nGZGraphic4::cIVertexFormatsManager *>(nGZGraphic4::GetVertexFormatsManagerCached()); };
    formats()->BeginFormat();
    formats()->AddElement(0, 2, kUsagePosition, 0);
    formats()->AddElement(0, 1, kUsageTexture, 0);
    formats()->EndFormat(&mFormat);
    mStride = mFormat->GetStreamSize(0);
    mPositionOffset = mFormat->GetOffsetAny(kUsagePosition, 0);
    mTextureOffset = mFormat->GetOffsetAny(kUsageTexture, 0);
    mZ = 0.0f;
    mFormatRaw = mFormat;
    mX0 = -1.0f;
    mX1 = 1.0f;
    mY0 = -1.0f;
    mY1 = 1.0f;
    mU0 = 0.0f;
    mU1 = 1.0f;
    mV0 = 0.0f;
    mV1 = 1.0f;
    mCountX = 1.0f;
    mCountY = 1.0f;
}

// 0xa440f0: the declaration, then the format.
cDynamicGeometryRect::~cDynamicGeometryRect()
{
    if (mDeclaration) mDeclaration->Release();
    if (mFormat) mFormat->Release();
}

// folded 0x3af660: the two ids this answers are the interface (1) and the class.
bool cDynamicGeometryRect::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid != 1 && iid != kIidWhole) return false;
    *out = this;
    AddRef();
    return true;
}

// folded 0x153f50: the last release deletes through the deleting destructor.
uint32_t cDynamicGeometryRect::Release()
{
    if (--mRefs == 0) {
        delete this;
        return 0;
    }
    return mRefs;
}

// 0xa44180: two triangles as a strip from vertex `first`.
void cDynamicGeometryRect::Begin(uint32_t, cGeometryRenderContext *context, uint32_t first)
{
    context->mDevice->DrawPrimitive(5, 2, first);
}

// 0xa441b0: nx by ny quads into the context's dynamic arrays, then the draw.
uint32_t cDynamicGeometryRect::Render(uint32_t, cGeometryRenderContext *context, uint8_t flag, uint32_t)
{
    using namespace nGZGraphic4;

    if (!mDeclaration || !mDeclaration->IsCreated()) {
        auto *const declarations = static_cast<cIVertexDeclarationsManagerDX *>(GetVertexDeclarationsManagerCached());
        if (mDeclaration) {
            cIVertexDeclarationDX *const old = mDeclaration;
            mDeclaration = nullptr;
            old->Release();
        }
        declarations->GetDeclaration(GetCurrentDeviceCached(), mFormat, &mDeclaration);
    }

    cIDynamicVertexArray *const vertices = flag ? context->mVertices : context->mVerticesB;
    cIDynamicVertexArray *const indices = flag ? context->mIndices : context->mIndicesB;

    const float total = mCountY * mCountX;
    const int32_t vertexCount = Truncate(total * 4.0f);
    const uint32_t first = vertices->Reserve(mStride, vertexCount);
    const int32_t indexCount = Truncate(total * 6.0f);
    const uint32_t firstIndex = indices->Reserve(2, indexCount);
    uint8_t *vertexBytes = static_cast<uint8_t *>(vertices->Lock(1, 0, 0));
    uint16_t *indexWords = static_cast<uint16_t *>(indices->Lock(1, 0, 0));

    const float dx = (mX1 - mX0) / mCountX;
    const float dy = (mY1 - mY0) / mCountY;
    const float du = (mU1 - mU0) / mCountX;
    const float dv = (mV1 - mV0) / mCountY;
    const bool shifted = mUvFlag != 0;
    const float uStart = mU0 + (shifted ? du * 0.5f : 0.0f);
    float u = 0.5f / static_cast<float>(static_cast<double>(context->mWidth)) + uStart;
    float x = mX0;
    int32_t i = 0;

    if (mCountX > 0.0f) {
        do {
            float y = mY0;
            const float vStart = mV1 - (shifted ? dv * 0.5f : 0.0f);
            float v = 0.5f / static_cast<float>(static_cast<double>(context->mHeight)) + vStart;
            int32_t j = 0;
            float jf = 0.0f;
            const float fi = static_cast<float>(i);
            if (mCountY > 0.0f) {
                const float x1 = x + dx;
                do {
                    uint8_t *p = vertexBytes;
                    // Vertex 0.
                    PutFloat(p + mPositionOffset, x);
                    PutFloat(p + mPositionOffset + 4, y);
                    PutFloat(p + mPositionOffset + 8, mZ);
                    PutFloat(p + mTextureOffset, u);
                    PutFloat(p + mTextureOffset + 4, v);
                    p += mStride;
                    // Vertex 1 (y advances after its position).
                    PutFloat(p + mPositionOffset, x1);
                    PutFloat(p + mPositionOffset + 4, y);
                    PutFloat(p + mPositionOffset + 8, mZ);
                    y += dy;
                    PutFloat(p + mTextureOffset, shifted ? u : u + du);
                    PutFloat(p + mTextureOffset + 4, v);
                    p += mStride;
                    // Vertex 2.
                    PutFloat(p + mPositionOffset, x);
                    PutFloat(p + mPositionOffset + 4, y);
                    PutFloat(p + mPositionOffset + 8, mZ);
                    PutFloat(p + mTextureOffset, u);
                    PutFloat(p + mTextureOffset + 4, shifted ? v : v - dv);
                    p += mStride;
                    // Vertex 3.
                    PutFloat(p + mPositionOffset, x1);
                    PutFloat(p + mPositionOffset + 4, y);
                    PutFloat(p + mPositionOffset + 8, mZ);
                    PutFloat(p + mTextureOffset, shifted ? u : u + du);
                    PutFloat(p + mTextureOffset + 4, shifted ? v : v - dv);
                    p += mStride;
                    vertexBytes = p;
                    v -= dv;

                    // ORIGINAL-BUG: the quad's first index is i * nx + j, but the quads are written in i * ny + j order,
                    // so the indices point at the wrong vertices whenever nx and ny differ. Reproduced.
                    const uint32_t base = static_cast<uint32_t>(Truncate(fi * mCountX + jf)) << 2;
                    indexWords[0] = static_cast<uint16_t>(base);
                    indexWords[1] = static_cast<uint16_t>(base + 1);
                    indexWords[2] = static_cast<uint16_t>(base + 2);
                    indexWords[3] = static_cast<uint16_t>(base + 2);
                    indexWords[4] = static_cast<uint16_t>(base + 1);
                    indexWords[5] = static_cast<uint16_t>(base + 3);
                    indexWords += 6;

                    ++j;
                    jf = static_cast<float>(j);
                } while (mCountY > jf);
            }
            ++i;
            x += dx;
            u += du;
        } while (mCountX > static_cast<float>(i));
    }

    vertices->Unlock();
    indices->Unlock();
    context->mState->SetStreamSource(0, vertices, mStride, 0);
    context->mState->SetVertexDeclaration(mDeclaration);
    context->mState->SetIndices(indices);
    const float cells = mCountX * mCountY;
    context->mDevice->DrawIndexedPrimitive(3, static_cast<uint32_t>(Truncate(cells + cells)), first, 0, vertexCount, firstIndex);
    return first;
}

// ---- adapters ----

using Adapters = cDynamicGeometryRectAdapters;
using Self = cDynamicGeometryRect;

Self *__fastcall Adapters::Construct(Self *memory, void *)
{
    return ::new (memory) Self;
}

void __fastcall Adapters::Destruct(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
}

// 0xa44150: the destructor and, with bit 0, the sized delete.
Self *__fastcall Adapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}

void __fastcall Adapters::Begin(Self *self, void *, uint32_t index, cGeometryRenderContext *context, uint32_t first)
{
    [[msvc::forceinline_calls]] self->Self::Begin(index, context, first);
}

uint32_t __fastcall Adapters::Render(Self *self, void *, uint32_t unused0, cGeometryRenderContext *context, uint32_t flag,
                                     uint32_t unused3)
{
    [[msvc::forceinline_calls]] return self->Self::Render(unused0, context, static_cast<uint8_t>(flag), unused3);
}

} // namespace nGZSceneGraph
