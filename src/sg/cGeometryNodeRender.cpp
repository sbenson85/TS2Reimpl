// nGZSceneGraph::cGeometryNode's render face (lane QZ, scenegraph D3 part 6): slots 3 / 6 / 8 of cIGeometryRender
// (0xa41ee0 IsSupported, 0xa429b0 Prepare, 0xa435d0 Render) and the device-object helper 0xa419e0. Layout and the switch:
// src/sg/cGeometryNode.h.
//
// The order of the calls follows the original's: the tests compare the device, state, declaration, format and vertex
// array call streams, so every re-fetch of the declarations manager and every repeated question to a format is kept
// where the original has it. The device format 0x18daa0, the stream format 0x18d990 and the vertex copy 0xc627e0 are
// src/sg/cGeometryNodeFormats.cpp.
#include "sg/cGeometryNode.h"
#include "sg/cGeometryNodeFormats.h"

#include "gz/GraphicServices.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceEnum.h"
#include "gz/cDeviceStateDX.h"
#include "gz/cIGZCOM.h"
#include "gz/cSurfaceDX.h"
#include "gz/cVertexArrayDX.h"
#include "gz/cVertexFormat.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject

#include <cstdint>

namespace nGZSceneGraph {

using nGZGraphic4::cIDeviceDX;
using nGZGraphic4::cIDeviceEnum;
using nGZGraphic4::cIDeviceResource;
using nGZGraphic4::cIDeviceState;
using nGZGraphic4::cIDynamicVertexArray;
using nGZGraphic4::cIStaticVertexArray;
using nGZGraphic4::cIVertexArray;
using nGZGraphic4::cIVertexDeclarationDX;
using nGZGraphic4::cIVertexDeclarationsManagerDX;
using nGZGraphic4::cIVertexFormat;

namespace {

constexpr uint32_t kStaticVertexArrayClass = 0x3bffec52; // the COM class the second vertex stream's array is made of
constexpr uint32_t kIidStaticVertexArray = 0xfbffec42;
constexpr uint32_t kStateBlockGroups = 2;                 // what the state blocks record (the vertex input)

// The vertex-processing questions to the device enumeration (HasProcessingCap): software first, then hardware.
constexpr uint32_t kCapHardware = 0, kCapSoftware = 1, kCapHardwareMixed = 2, kCapSoftwareMixed = 3;

using FormatRef = cGeometryNodeRef<cIVertexFormat>; // a counted format, released at the end of its scope

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

template <class T> T *At(uint32_t word) { return reinterpret_cast<T *>(static_cast<uintptr_t>(word)); }

// Wrapping 32-bit products (the original's imul).
int32_t Mul(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b)); }

// The format `format` takes on `device`, counted into *out; false when there is none (src/sg/cGeometryNodeFormats.h).
bool DeviceFormat(FormatRef &out, cIDeviceDX *device, cIVertexFormat *format)
{
    return MakeDeviceVertexFormat(&out.mPtr, device, format);
}

// The format of `format`'s stream 0 alone, counted.
void FormatVariant(FormatRef &out, cIVertexFormat *format) { MakeStreamVertexFormat(&out.mPtr, format, 0, 0); }

// A record's vertices copied from its source format's layout into the device format's stream.
void CopyVertices(void *stream0, uint32_t source, cIVertexFormat *sourceFormat, uint32_t sourceFlag, void *stream1,
                  uint32_t start, cIVertexFormat *deviceFormat, uint32_t deviceFlag, uint32_t count)
{
    CopyVertexStream(stream0, source, sourceFormat, sourceFlag, stream1, start, deviceFormat, deviceFlag, count);
}

// A device object that still holds its Direct3D object.
bool IsCreated(const cGeometryNodeRef<cIGZUnknown> &ref)
{
    return ref.mPtr && static_cast<cIDeviceResource *>(ref.mPtr)->IsCreated();
}

// `ref` made again: the declarations manager's declaration of `format` on `device` (the manager fetched before the old one
// is dropped). ORIGINAL-BUG: the manager is used without a null check.
bool Declare(cGeometryNodeRef<cIGZUnknown> &ref, cIDeviceDX *device, cIVertexFormat *format)
{
    cIVertexDeclarationsManagerDX *const manager =
        static_cast<cIVertexDeclarationsManagerDX *>(nGZGraphic4::GetVertexDeclarationsManagerCached());
    ref.Reset();
    return manager->GetDeclaration(device, format, reinterpret_cast<cIVertexDeclarationDX **>(&ref.mPtr));
}

// A declaration's bytes per vertex in `stream`, 0 when its format has no such stream (the format asked each time).
uint32_t StreamSize(cIVertexDeclarationDX *declaration, uint32_t stream)
{
    return static_cast<int32_t>(declaration->GetFormat()->GetStreamCount()) > static_cast<int32_t>(stream)
               ? declaration->GetStreamSize(stream)
               : 0;
}

uint32_t StreamSize(cIVertexFormat *format, uint32_t stream)
{
    return static_cast<int32_t>(format->GetStreamCount()) > static_cast<int32_t>(stream) ? format->GetStreamSize(stream) : 0;
}

cIVertexArray *Array(const cGeometryNodeRef<cIGZUnknown> &ref) { return static_cast<cIVertexArray *>(ref.mPtr); }

} // namespace

// ---- the device objects ------------------------------------------------------------------------------------------

bool cGeometryNode::ResetDeviceState(bool declarations, bool stateBlocks)
{
    if (mDeviceReady) return false;
    cIDeviceDX *const device = nGZGraphic4::GetCurrentDeviceCached();
    if (!device) return true;
    // ORIGINAL-BUG: the enumeration and the state are used without a null check.
    cIDeviceEnum *const caps = static_cast<cIDeviceEnum *>(device->GetDeviceEnumConst());
    cIDeviceState *const state = device->GetStateConst();
    const bool software = caps->HasProcessingCap(kCapSoftware) || caps->HasProcessingCap(kCapSoftwareMixed);
    const bool hardware = caps->HasProcessingCap(kCapHardware) || caps->HasProcessingCap(kCapHardwareMixed);

    for (auto it = mElements.begin(); it != mElements.end(); ++it) {
        cGeometryNodeElement &element = *it;
        cIVertexFormat *const format = At<cIVertexFormat>(element.mWords[3]);
        if (software) {
            if (!IsCreated(element.mRefs[0]) && !Declare(element.mRefs[0], device, format)) return false;
            if (!IsCreated(element.mRefs[1])) {
                FormatRef variant;
                FormatVariant(variant, format);
                if (!Declare(element.mRefs[1], device, variant.mPtr)) return false;
            }
        } else {
            element.mRefs[0].Reset();
            element.mRefs[1].Reset();
        }

        if (!hardware) {
            element.mRefs[2].Reset();
            element.mRefs[3].Reset();
            element.mRefs[4].Reset();
            element.mRefs[5].Reset();
            continue;
        }
        FormatRef deviceFormat;
        // Only the word's low byte (the original's byte store).
        reinterpret_cast<uint8_t *>(&element.mPrepared)[0] = DeviceFormat(deviceFormat, device, format);
        if (declarations) {
            if (!IsCreated(element.mRefs[2]) && !Declare(element.mRefs[2], device, deviceFormat.mPtr)) return false;
            if (!IsCreated(element.mRefs[3])) {
                FormatRef variant;
                FormatVariant(variant, deviceFormat.mPtr);
                if (!Declare(element.mRefs[3], device, variant.mPtr)) return false;
            }
        }
        if (stateBlocks) {
            // The state block drawing both streams (mRefs[4]), then the one drawing the first only (mRefs[5]).
            // ORIGINAL-BUG: the declarations are used unchecked (a call with stateBlocks but not declarations, before any
            // declaration was made, faults).
            if (!IsCreated(element.mRefs[4])) {
                cIVertexDeclarationDX *const declaration = static_cast<cIVertexDeclarationDX *>(element.mRefs[2].mPtr);
                state->Reset(kStateBlockGroups);
                state->SetVertexDeclaration(declaration);
                state->SetStreamSource(0, mRefBC.mPtr, StreamSize(declaration, 0), 0);
                if (mRefC4.mPtr && static_cast<int32_t>(declaration->GetFormat()->GetStreamCount()) > 1)
                    state->SetStreamSource(1, mRefC4.mPtr, StreamSize(declaration, 1), 0);
                state->SetIndices(mRefB8.mPtr);
                element.mRefs[4].Reset();
                state->CreateStateBlock(kStateBlockGroups, Word(&element.mRefs[4].mPtr));
            }
            if (!IsCreated(element.mRefs[5])) {
                cIVertexDeclarationDX *const declaration = static_cast<cIVertexDeclarationDX *>(element.mRefs[3].mPtr);
                state->Reset(kStateBlockGroups);
                state->SetVertexDeclaration(declaration);
                state->SetStreamSource(0, mRefBC.mPtr, StreamSize(declaration, 0), 0);
                state->SetIndices(mRefB8.mPtr);
                element.mRefs[5].Reset();
                state->CreateStateBlock(kStateBlockGroups, Word(&element.mRefs[5].mPtr));
            }
        }
    }
    return true;
}

// ---- cIGeometryRender -----------------------------------------------------------------------------------------------

// The records (+0x84) are laid out in two vertex streams: each record's place in both is the first vertex index past
// what the records before it use, aligned to both streams' vertex sizes; the second stream's array is made to fit, then
// the declarations and state blocks (ResetDeviceState), then the records' vertices copied in (0xc627e0). Any failure
// marks the node failed (mDeviceReady) and drops what was made.
bool cGeometryNode::IsSupported(uint32_t what)
{
    if (!(what & 0xff) || mResourcesReady) return true;
    if (mDeviceReady) return false;

    bool laidOut = false;
    cIDeviceDX *const device = nGZGraphic4::GetCurrentDeviceCached();
    if (!device || !device->IsDeviceReady()) {
        mDeviceReady = 1;
    } else {
        laidOut = true;
        int32_t offset0 = 0, offset1 = 0; // the bytes each stream uses so far
        for (auto it = mRecords.begin(); it != mRecords.end(); ++it) {
            cIVertexFormat *const source = static_cast<cIVertexFormat *>(it->mRef.mPtr);
            if (!source) continue;
            FormatRef format;
            if (!DeviceFormat(format, device, source)) {
                mDeviceReady = 1;
                break;
            }
            const int32_t size0 = static_cast<int32_t>(StreamSize(format.mPtr, 0));
            const int32_t size1 = static_cast<int32_t>(StreamSize(format.mPtr, 1));
            if (size0 != 0 && offset0 % size0 != 0) offset0 = Mul(offset0 / size0 + 1, size0);
            if (size1 != 0 && offset1 % size1 != 0) offset1 = Mul(offset1 / size1 + 1, size1);
            // ORIGINAL-BUG: the original divides by the first stream's size unchecked (a format without a first stream
            // faults); ours takes index 0.
            const int32_t index0 = size0 != 0 ? offset0 / size0 : 0;
            const int32_t index1 = size1 > 0 ? offset1 / size1 : 0;
            if (index0 < index1) {
                it->mWords[2] = static_cast<uint32_t>(index1);
                offset0 = Mul(size0, index1);
            } else {
                it->mWords[2] = static_cast<uint32_t>(index0);
                if (size1 > 0) offset1 = Mul(size1, index0);
            }
            const int32_t count = static_cast<int32_t>(it->mWords[0]);
            offset0 += Mul(count, size0);
            offset1 += Mul(count, size1);
        }
        for (auto it = mElements.begin(); it != mElements.end(); ++it) it->mTail[1] = it->mTail[0];
        mRefC4.Reset();
        if (!mDeviceReady && offset1 > 0) {
            cIGZCOM *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
            mRefC4.Reset();
            // ORIGINAL-BUG: the director and the new array are used without a null check.
            com->GetClassObject(kStaticVertexArrayClass, kIidStaticVertexArray, reinterpret_cast<void **>(&mRefC4.mPtr));
            if (!static_cast<cIStaticVertexArray *>(mRefC4.mPtr)->Create(device, static_cast<uint32_t>(offset1), 1))
                mDeviceReady = 1;
        }
    }

    // (`what`'s low byte is set here, so the state blocks are always made.)
    if (!mDeviceReady && !ResetDeviceState(true, (what & 0xff) != 0)) mDeviceReady = 1;

    if (laidOut && !mDeviceReady) {
        void *stream0 = nullptr;
        void *stream1 = nullptr;
        if (mRefC0.mPtr) {
            stream0 = Array(mRefC0)->Lock(0, 0, 0);
            // ORIGINAL-BUG: the second array is locked without a null check.
            stream1 = Array(mRefC4)->Lock(1, 0, 0);
        }
        for (auto it = mRecords.begin(); it != mRecords.end(); ++it) {
            cIVertexFormat *const source = static_cast<cIVertexFormat *>(it->mRef.mPtr);
            if (!source) continue;
            FormatRef format;
            if (!DeviceFormat(format, device, source)) {
                mDeviceReady = 1;
                break;
            }
            if (source->GetStreamCount() == 2)
                CopyVertices(stream0, it->mWords[1], source, 1, stream1, it->mWords[2], format.mPtr, 1, it->mWords[0]);
        }
        if (mRefC0.mPtr) {
            Array(mRefC0)->Unlock();
            Array(mRefC4)->Unlock();
        }
    }

    if (mDeviceReady) {
        mRefC4.Reset();
        for (auto it = mElements.begin(); it != mElements.end(); ++it) {
            it->mRefs[2].Reset();
            it->mRefs[3].Reset();
            it->mRefs[4].Reset();
            it->mRefs[5].Reset();
        }
    }
    mResourcesReady = !mDeviceReady;
    return mResourcesReady != 0;
}

uint32_t cGeometryNode::Prepare(uint32_t index, uint32_t contextWord, uint32_t declarationWord, uint32_t flag)
{
    cGeometryRenderContext *const context = At<cGeometryRenderContext>(contextWord);
    cIVertexDeclarationDX *const declaration = At<cIVertexDeclarationDX>(declarationWord);
    const cGeometryNodeElement &element = mElements.data()[index];
    const uint32_t stride = declaration->GetStreamSize(0);
    cIDynamicVertexArray *const vertices = (flag & 0xff) ? context->mVertices : context->mVerticesB;
    if (element.mWords[0] * stride > vertices->GetCapacity()) return 0xffffffff;

    context->mState->SetVertexDeclaration(element.mRefs[0].mPtr);
    context->mState->SetStreamSource(0, mRefBC.mPtr, element.mTail[2], 0);
    if (mRefC0.mPtr && element.mTail[3]) context->mState->SetStreamSource(1, mRefC0.mPtr, element.mTail[3], 0);
    const uint32_t first = vertices->Reserve(stride, element.mWords[0]);
    context->mDevice->ProcessVertices(element.mTail[0], first, element.mWords[0], vertices, declaration);
    context->mState->SetVertexDeclaration(declaration);
    context->mState->SetStreamSource(0, vertices, stride, 0);
    context->mState->SetIndices(mRefB8.mPtr);
    return first;
}

// `flag` low byte: the element drawn with the second stream (state block mRefs[4], or declaration mRefs[0] with mRefC0)
// rather than without it (mRefs[5], or mRefs[1]).
uint32_t cGeometryNode::Render(uint32_t index, cGeometryRenderContext *context, uint8_t hardware, uint32_t flag)
{
    const cGeometryNodeElement &element = mElements.data()[index];
    const bool second = (flag & 0xff) != 0;
    uint32_t base;
    if (hardware) {
        context->mState->ApplyStateBlock(Word(second ? element.mRefs[4].mPtr : element.mRefs[5].mPtr));
        base = element.mTail[1];
    } else {
        if (second) {
            context->mState->SetVertexDeclaration(element.mRefs[0].mPtr);
            if (mRefC0.mPtr && element.mTail[3]) context->mState->SetStreamSource(1, mRefC0.mPtr, element.mTail[3], 0);
        } else {
            context->mState->SetVertexDeclaration(element.mRefs[1].mPtr);
        }
        context->mState->SetStreamSource(0, mRefBC.mPtr, element.mTail[2], 0);
        context->mState->SetIndices(mRefB8.mPtr);
        base = element.mTail[0];
    }
    context->mDevice->DrawIndexedPrimitive(element.mWord4c, element.mWords[1], static_cast<int32_t>(base), 0, element.mWords[0],
                                           element.mWord50);
    return base;
}

// ---- the adapters -------------------------------------------------------------------------------------------------

bool __fastcall cGeometryNodeAdapters::ResetDeviceState(Self *self, void *, uint32_t declarations, uint32_t stateBlocks)
{
    [[msvc::forceinline_calls]] return self->cGeometryNode::ResetDeviceState((declarations & 0xff) != 0,
                                                                             (stateBlocks & 0xff) != 0);
}
bool __fastcall cGeometryNodeAdapters::IsSupported(void *face, void *, uint32_t what)
{
    [[msvc::forceinline_calls]] return FromFace(face, kRenderOffset)->cGeometryNode::IsSupported(what);
}
uint32_t __fastcall cGeometryNodeAdapters::Prepare(void *face, void *, uint32_t index, uint32_t context, uint32_t declaration,
                                                   uint32_t flag)
{
    [[msvc::forceinline_calls]] return FromFace(face, kRenderOffset)->cGeometryNode::Prepare(index, context, declaration, flag);
}
uint32_t __fastcall cGeometryNodeAdapters::Render(void *face, void *, uint32_t index, cGeometryRenderContext *context,
                                                  uint32_t hardware, uint32_t flag)
{
    [[msvc::forceinline_calls]] return FromFace(face, kRenderOffset)
        ->cGeometryNode::Render(index, context, static_cast<uint8_t>(hardware), flag);
}

} // namespace nGZSceneGraph
