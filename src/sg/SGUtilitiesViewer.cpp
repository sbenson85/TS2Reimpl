// nGZSceneGraph's viewer texture state helpers (lane 261111l): what a viewer shows, read and given back. See
// sg/SGUtilities.h.
#include "sg/SGUtilities.h"

#include "game/vtable.h"
#include "sg/SGResourceBlockers.h"
#include "sg/cIViewerRefNode.h"
#include "sg/cTextureManager.h"
#include "sg/cViewerRefNodeBaseImpl.h" // cViewerTargetSize

#include <cstring>

namespace nGZSceneGraph {

namespace {

bool HandleIsNone(const uint32_t *handle)
{
    return reinterpret_cast<const SGResourceBlockers::cInvalidDwordTag *>(handle)->IsInvalid() != 0;
}

// A counted cell set to `value` (read the old one after the getter answered): new counted, stored, old released, when
// they differ.
void Hold(cIGZUnknown *&cell, cIGZUnknown *value)
{
    cIGZUnknown *old = cell;
    if (value == old) return;
    if (value) value->AddRef();
    cell = value;
    if (old) old->Release();
}

} // namespace

// 0xaf2450
void CaptureViewerTextures(ViewerTextureState *out, cIViewerRefNodeBase *viewer)
{
    out->mCount = 0;
    out->mScale = 1.0f;
    // Slot113's whole answer byte is stored and tested (the original keeps al as it comes).
    out->mDeviceView = VirtualCall<uint8_t(__thiscall *)(void *), 113>(viewer);
    if (out->mDeviceView) return;
    {
        const cTextureHandle made(viewer->Slot108());
        *cTextureHandle::At(&out->mHandle) = made;
    }
    if (!HandleIsNone(&out->mHandle)) return;
    out->mCount = viewer->GetTextureCount();
    cViewerTargetSize size;
    const cViewerTargetSize *answered = viewer->GetTargetSize(&size);
    std::memcpy(&out->mScale, &answered->mScale, sizeof(float));
    Hold(out->mRef94, viewer->GetTextureRef94());
    // ORIGINAL-BUG: nothing bounds the count by the four cells: a viewer with more textures writes past mTextures into
    // mRef94, the handle and the flag (and beyond), as the original does.
    cIGZUnknown **cells = out->mTextures;
    for (uint32_t i = 0; i < out->mCount; ++i) Hold(cells[i], viewer->GetTexture(int32_t(i)));
}

// 0xaf23f0
void ApplyViewerTextures(cIViewerRefNodeBase *viewer, const ViewerTextureState *state)
{
    if (state->mDeviceView) {
        viewer->Slot92(nullptr, nullptr, 1.0f);
    } else if (!HandleIsNone(&state->mHandle)) {
        viewer->Slot94(state->mHandle);
    } else {
        viewer->Slot93(state->mCount, state->mTextures, state->mRef94, 1.0f);
    }
}

} // namespace nGZSceneGraph
