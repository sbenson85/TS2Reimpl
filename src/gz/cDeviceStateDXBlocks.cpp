// nGZGraphic4::cDeviceStateDX's state blocks (slots 10 and 11, rendering.md item 2b): a block is the state that differs
// from the defaults, recorded (StateDelta::Build, then Serialize into the block's record, src/gz/cStateBlockDX.h);
// applying it sends the record's state to the device and, for what the previous block changed and this one does not,
// the defaults.
//
// Original functions folded into this file, all reached only from the two slots and the block's own slots:
//   0x17cc00 CreateStateBlock          0x17d510 StateDelta::Build        0x182d90 StateDelta::Serialize
//   0x17bdc0 the list's equal range    0x17fc00 ApplyStateBlock          0x17f180 ApplyRecordStates
//   0x17ee30 ApplyRecordStreams        0x181820 RestoreChangedStates     0x1816d0 RestoreChangedStreams
#include <algorithm>
#include <bit>
#include <cstring>

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cDeviceStateDX.h"
#include "gz/cDeviceStateDXDetail.h"
#include "gz/cStateBlockDX.h"
#include "ifaces/cIDevice.h"
#include "ifaces/cIDeviceResourceManager.h"

namespace nGZGraphic4 {

using namespace devstate;
using namespace devstate_detail;

namespace {

uint32_t PtrWord(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
template <class T> T *WordPtr(uint32_t word) { return reinterpret_cast<T *>(static_cast<uintptr_t>(word)); }

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

// The record's two sections: where the header says they start.
const uint8_t *SectionA(const cStateBlockDX &block) { return block.mRecord.data() + Half(block.mRecord.data() + 4); }
const uint8_t *SectionB(const cStateBlockDX &block) { return block.mRecord.data() + Half(block.mRecord.data() + 6); }

// A record under construction: the bytes land at `length` (the first section starts after the 8-byte header); with no
// buffer it only counts.
struct Writer {
    explicit Writer(uint8_t *buffer) : base(buffer) {}
    void U8(uint32_t v)
    {
        if (base) base[length] = static_cast<uint8_t>(v);
        ++length;
    }
    void U32(uint32_t v)
    {
        if (base) std::memcpy(base + length, &v, 4);
        length += 4;
    }
    void Bytes(const void *p, uint16_t n)
    {
        if (base) std::memcpy(base + length, p, n);
        length += n;
    }
    // A Direct3D object the record names takes a reference (only when writing).
    void Hold(uint32_t object)
    {
        if (base && object) WordPtr<IUnknown>(object)->AddRef();
    }
    uint8_t *base;
    uint16_t length = 8;
};

} // namespace

// ---- StateDelta ----

void StateDelta::Build(uint32_t groups, const DeviceState &defaults, const DeviceState &wanted)
{
    Clear();
    flags = groups;
    if (groups & 1) {
        for (uint32_t i = 0; i < 16; ++i) {
            if (wanted.textures[i] == defaults.textures[i]) continue;
            textureMask |= 1u << i;
            textures[textureCount] = {i, PtrWord(wanted.textures[i])};
            ++textureCount;
            ++changeCount;
        }
        softwareVertexProcessing = wanted.softwareVertexProcessing != 0;
        vertexShaderChanged = !(wanted.softwareVertexProcessing == defaults.softwareVertexProcessing &&
                                wanted.vertexShader == defaults.vertexShader);
        vertexShader = PtrWord(wanted.vertexShader);
        changeCount += vertexShaderChanged;
        pixelShaderChanged = wanted.pixelShader != defaults.pixelShader;
        pixelShader = PtrWord(wanted.pixelShader);
        changeCount += pixelShaderChanged;
        for (uint32_t i = 0; i < 0x59; ++i) {
            if (wanted.renderStates[i] == defaults.renderStates[i]) continue;
            renderStateMask[i >> 5] |= 1u << (i & 31);
            renderStates[renderStateCount] = {i, wanted.renderStates[i]};
            ++renderStateCount;
            ++changeCount;
        }
        for (uint32_t stage = 0; stage < 8; ++stage) {
            for (uint32_t k = 0; k < 0x12; ++k) {
                if (wanted.stageStates[stage][k] == defaults.stageStates[stage][k]) continue;
                stageMask |= 1u << stage;
                stageBits[stage] |= 1u << k;
                stageStates[stage][stageCounts[stage]] = {k, wanted.stageStates[stage][k]};
                ++stageCounts[stage];
                ++changeCount;
            }
        }
        for (uint32_t sampler = 0; sampler < 16; ++sampler) {
            for (uint32_t k = 0; k < 0xd; ++k) {
                if (wanted.samplerStates[sampler][k] == defaults.samplerStates[sampler][k]) continue;
                samplerMask |= 1u << sampler;
                samplerBits[sampler] |= 1u << k;
                samplerStates[sampler][samplerCounts[sampler]] = {k, wanted.samplerStates[sampler][k]};
                ++samplerCounts[sampler];
                ++changeCount;
            }
        }
        for (uint32_t t = 0; t < 8; ++t) {
            if (std::memcmp(&wanted.textureTransforms[t], &defaults.textureTransforms[t], sizeof(D3DMATRIX)) == 0) continue;
            transformMask |= 1u << t;
            transforms[transformCount].index = t;
            transforms[transformCount].matrix = wanted.textureTransforms[t];
            ++transformCount;
            ++changeCount;
        }
        materialChanged = std::memcmp(&wanted.material, &defaults.material, sizeof(D3DMATERIAL9)) != 0;
        material = wanted.material;
        changeCount += materialChanged;
    }
    if (groups & 2) {
        declarationChanged = wanted.declaration != defaults.declaration;
        declaration = PtrWord(wanted.declaration);
        changeCount += declarationChanged;
        for (uint32_t i = 0; i < 16; ++i) {
            const StreamSource &w = wanted.streams[i], &d = defaults.streams[i];
            if (w.buffer == d.buffer && w.stride == d.stride && w.offset == d.offset) continue;
            streamMask |= 1u << i;
            streams[streamCount] = {i, PtrWord(w.buffer), w.stride, w.offset};
            ++streamCount;
            ++changeCount;
        }
        indicesChanged = wanted.indices != defaults.indices;
        indices = PtrWord(wanted.indices);
        changeCount += indicesChanged;
    }
}

uint16_t StateDelta::Serialize(uint8_t *buffer) const
{
    Writer w(buffer);
    uint16_t offsetA = 0xffff, offsetB = 0xffff;
    if (flags & 1) {
        offsetA = w.length;
        // The head: the change bits the block sets (a ChangeRecord's shape).
        ChangeRecord head;
        std::memset(&head, 0, sizeof(head));
        for (unsigned i = 0; i < 3; ++i) head.renderStates[i] = renderStateMask[i];
        head.stages = static_cast<uint16_t>(stageMask);
        head.samplers = static_cast<uint16_t>(samplerMask);
        head.textures = static_cast<uint16_t>(textureMask);
        head.transforms = static_cast<uint8_t>(transformMask);
        head.misc = static_cast<uint8_t>((materialChanged != 0 ? ChangeRecord::kMaterial : 0) |
                                         (pixelShaderChanged != 0 ? ChangeRecord::kPixelShader : 0) |
                                         (vertexShaderChanged != 0 ? ChangeRecord::kSoftwareVertexProcessing | ChangeRecord::kVertexShader : 0));
        for (unsigned i = 0; i < 8; ++i) head.stageStates[i] = stageBits[i];
        for (unsigned i = 0; i < 16; ++i) head.samplerStates[i] = static_cast<uint16_t>(samplerBits[i]);
        w.Bytes(&head, sizeof(head));

        w.U8(textureCount);
        for (uint32_t i = 0; i < textureCount; ++i) {
            w.U8(textures[i].index);
            w.U32(textures[i].value);
            w.Hold(textures[i].value);
        }
        w.U8(vertexShaderChanged);
        if (vertexShaderChanged) {
            w.U8(softwareVertexProcessing);
            w.U32(vertexShader);
            w.Hold(vertexShader);
        }
        w.U8(pixelShaderChanged);
        if (pixelShaderChanged) {
            w.U32(pixelShader);
            w.Hold(pixelShader);
        }
        w.U8(renderStateCount);
        for (uint32_t i = 0; i < renderStateCount; ++i) {
            w.U8(renderStates[i].index);
            w.U32(renderStates[i].value);
        }
        w.U8(std::popcount(stageMask));
        for (uint32_t stage = 0; stage < 8; ++stage) {
            if (!(stageMask & (1u << stage))) continue;
            w.U8(stage);
            w.U8(stageCounts[stage]);
            for (uint32_t k = 0; k < stageCounts[stage]; ++k) {
                w.U8(stageStates[stage][k].index);
                w.U32(stageStates[stage][k].value);
            }
        }
        w.U8(std::popcount(samplerMask));
        for (uint32_t sampler = 0; sampler < 16; ++sampler) {
            if (!(samplerMask & (1u << sampler))) continue;
            w.U8(sampler);
            w.U8(samplerCounts[sampler]);
            for (uint32_t k = 0; k < samplerCounts[sampler]; ++k) {
                w.U8(samplerStates[sampler][k].index);
                w.U32(samplerStates[sampler][k].value);
            }
        }
        w.U8(transformCount);
        for (uint32_t i = 0; i < transformCount; ++i) {
            w.U8(transforms[i].index);
            w.Bytes(&transforms[i].matrix, sizeof(D3DMATRIX));
        }
        w.U8(materialChanged);
        if (materialChanged) w.Bytes(&material, sizeof(D3DMATERIAL9));
    }
    if (flags & 2) {
        offsetB = w.length;
        w.U32(static_cast<uint16_t>(streamMask) | static_cast<uint32_t>(declarationChanged) << 16 |
              static_cast<uint32_t>(indicesChanged) << 24);
        w.U8(declarationChanged);
        if (declarationChanged) {
            w.U32(declaration);
            w.Hold(declaration);
        }
        w.U8(streamCount);
        for (uint32_t i = 0; i < streamCount; ++i) {
            w.U8(streams[i].index);
            w.U32(streams[i].buffer);
            w.Hold(streams[i].buffer);
            w.U32(streams[i].stride);
            w.U32(streams[i].offset);
        }
        w.U8(indicesChanged);
        if (indicesChanged) {
            w.U32(indices);
            w.Hold(indices);
        }
    }
    if (buffer) {
        std::memcpy(buffer, &w.length, 2);
        buffer[2] = static_cast<uint8_t>(flags);
        buffer[3] = static_cast<uint8_t>(changeCount);
        std::memcpy(buffer + 4, &offsetA, 2);
        std::memcpy(buffer + 6, &offsetB, 2);
    }
    return w.length;
}

// ---- Slot 10 ----

uint32_t cDeviceStateDX::CreateStateBlock(uint32_t groups, uint32_t out)
{
    Flush(groups);
    mDelta.Build(groups, mDefaults, mWanted);

    cStateBlockDX *block = new cStateBlockDX;
    block->mRecord.resize(mDelta.Serialize(nullptr));
    mDelta.Serialize(block->mRecord.data());
    block->AddRef();

    // An equal block made before stands in for this one; the new one joins the list, in order, and the device's
    // resources.
    const auto range = std::equal_range(mList.begin(), mList.end(), block, &cStateBlockDX::RecordLess);
    cStateBlockDX *result;
    if (range.first == range.second) {
        block->mOwner = this;
        As<ifaces::cIDeviceResourceManager>(As<ifaces::cIDevice>(GetDeviceConst())->GetResourceManager())->Insert(block);
        mList.insert(range.first, block);
        result = block;
    } else {
        result = *range.first;
    }
    *reinterpret_cast<cStateBlockDX **>(static_cast<uintptr_t>(out)) = result;
    result->AddRef();
    return block->Release();
}

// ---- Slot 11 ----

namespace {

// The change record as words, and the stream changes (+0x70: the mask, the declaration and index flags) as one.
uint32_t ChangeWord(const ChangeRecord &changes, unsigned i)
{
    uint32_t v;
    std::memcpy(&v, reinterpret_cast<const uint8_t *>(&changes) + i * 4, 4);
    return v;
}
bool StatesPending(const ChangeRecord &changes)
{
    // The render, stage / sampler, texture / transform / misc words: not the per-stage change words.
    return (ChangeWord(changes, 0) | ChangeWord(changes, 1) | ChangeWord(changes, 2) | ChangeWord(changes, 3) |
            ChangeWord(changes, 4)) != 0;
}

} // namespace

void cDeviceStateDX::ApplyStateBlock(uint32_t blockAddress)
{
    auto *block = reinterpret_cast<cStateBlockDX *>(static_cast<uintptr_t>(blockAddress));
    const uint8_t groups = block->mRecord[2];
    if (mPending.Any()) Flush(groups);

    auto streamWord = [this]() {
        uint32_t v;
        std::memcpy(&v, reinterpret_cast<uint8_t *>(this) + 0x70, 4);
        return v;
    };
    auto setStreamWord = [this](uint32_t v) { std::memcpy(reinterpret_cast<uint8_t *>(this) + 0x70, &v, 4); };

    if (groups & 1) {
        if (mStateBlock98 != block) {
            block->AddRef();
            if (cStateBlockDX *old = mStateBlock98) {
                // What the old block set and this one may not cover is changed again.
                const uint8_t *head = SectionA(*old);
                for (unsigned i = 0; i < sizeof(ChangeRecord) / 4; ++i) {
                    const uint32_t v = ChangeWord(mChanges, i) | Word(head + i * 4);
                    std::memcpy(reinterpret_cast<uint8_t *>(&mChanges) + i * 4, &v, 4);
                }
                old->Release();
            }
            mStateBlock98 = block;
            ApplyRecordStates();
        } else if (StatesPending(mChanges)) {
            ApplyRecordStates();
        }
        if (StatesPending(mChanges)) RestoreChangedStates();
    }
    if (groups & 2) {
        if (mStateBlock9c != block) {
            block->AddRef();
            if (cStateBlockDX *old = mStateBlock9c) {
                setStreamWord(streamWord() | Word(SectionB(*old)));
                old->Release();
            }
            mStateBlock9c = block;
            ApplyRecordStreams();
            if (streamWord() != 0) RestoreChangedStreams();
        } else if (streamWord() != 0) {
            ApplyRecordStreams();
            if (streamWord() != 0) RestoreChangedStreams();
        }
    }
    ++mChangeCount;
}

// The state of the held block's first section: its change bits are taken off the cache's, and what it records is sent
// where it differs from the wanted state.
void cDeviceStateDX::ApplyRecordStates()
{
    const uint8_t *head = SectionA(*mStateBlock98);
    ChangeRecord covered;
    std::memcpy(&covered, head, sizeof(covered));
    for (unsigned i = 0; i < 3; ++i) mChanges.renderStates[i] &= ~covered.renderStates[i];
    mChanges.textures &= static_cast<uint16_t>(~covered.textures);
    mChanges.transforms &= static_cast<uint8_t>(~covered.transforms);
    mChanges.misc &= static_cast<uint8_t>(~covered.misc);

    const uint8_t *p = head + sizeof(ChangeRecord);
    for (uint8_t n = *p++; n != 0; --n, p += 5) {
        const uint32_t index = p[0], value = Word(p + 1);
        if (PtrWord(mWanted.textures[index]) == value) continue;
        mWanted.textures[index] = WordPtr<IDirect3DBaseTexture9>(value);
        Report(mD3D->SetTexture(index, mWanted.textures[index]), 0xcbb);
    }
    if (*p++ != 0) {
        const uint32_t software = *p++, shader = Word(p);
        p += 4;
        if (mWanted.softwareVertexProcessing != software) {
            mWanted.softwareVertexProcessing = software;
            Report(mD3D->SetSoftwareVertexProcessing(software), 0xcc9);
        }
        if (PtrWord(mWanted.vertexShader) != shader) {
            mWanted.vertexShader = WordPtr<IDirect3DVertexShader9>(shader);
            Report(mD3D->SetVertexShader(mWanted.vertexShader), 0xccf);
        }
    }
    if (*p++ != 0) {
        const uint32_t shader = Word(p);
        p += 4;
        if (PtrWord(mWanted.pixelShader) != shader) {
            mWanted.pixelShader = WordPtr<IDirect3DPixelShader9>(shader);
            Report(mD3D->SetPixelShader(mWanted.pixelShader), 0xcdb);
        }
    }
    for (uint8_t n = *p++; n != 0; --n, p += 5) {
        const uint32_t index = p[0], value = Word(p + 1);
        if (mWanted.renderStates[index] == value) continue;
        mWanted.renderStates[index] = value;
        Report(mD3D->SetRenderState(static_cast<D3DRENDERSTATETYPE>(RenderStateMap()[index]), value), 0xce9);
    }
    for (uint8_t stages = *p++; stages != 0; --stages) {
        const uint32_t stage = p[0], entries = p[1];
        p += 2;
        mChanges.stageStates[stage] &= ~covered.stageStates[stage];
        if (mChanges.stageStates[stage] == 0) mChanges.stages &= static_cast<uint16_t>(~(1u << (stage & 31)));
        for (uint32_t k = 0; k < entries; ++k, p += 5) {
            const uint32_t index = p[0], value = Word(p + 1);
            if (mWanted.stageStates[stage][index] == value) continue;
            mWanted.stageStates[stage][index] = value;
            Report(mD3D->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(StageStateMap()[index]), value),
                   0xd01);
        }
    }
    for (uint8_t samplers = *p++; samplers != 0; --samplers) {
        const uint32_t sampler = p[0], entries = p[1];
        p += 2;
        mChanges.samplerStates[sampler] &= static_cast<uint16_t>(~covered.samplerStates[sampler]);
        if (mChanges.samplerStates[sampler] == 0) mChanges.samplers &= static_cast<uint16_t>(~(1u << (sampler & 31)));
        for (uint32_t k = 0; k < entries; ++k, p += 5) {
            const uint32_t index = p[0], value = Word(p + 1);
            if (mWanted.samplerStates[sampler][index] == value) continue;
            mWanted.samplerStates[sampler][index] = value;
            Report(mD3D->SetSamplerState(sampler, static_cast<D3DSAMPLERSTATETYPE>(SamplerStateMap()[index]), value), 0xd1d);
        }
    }
    for (uint8_t n = *p++; n != 0; --n, p += 1 + sizeof(D3DMATRIX)) {
        const uint32_t index = p[0];
        uint32_t matrix[16];
        std::memcpy(matrix, p + 1, sizeof(matrix));
        if (CopyWords(&mWanted.textureTransforms[index], matrix, 16))
            Report(mD3D->SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0 + index),
                                      &mWanted.textureTransforms[index]),
                   0xd2c);
    }
    if (*p++ != 0) {
        uint32_t material[sizeof(D3DMATERIAL9) / 4];
        std::memcpy(material, p, sizeof(material));
        if (CopyWords(&mWanted.material, material, sizeof(material) / 4))
            Report(mD3D->SetMaterial(&mWanted.material), 0xd38);
    }
}

// The held block's second section: the declaration, the streams and the indices.
void cDeviceStateDX::ApplyRecordStreams()
{
    const uint8_t *head = SectionB(*mStateBlock9c);
    const uint32_t covered = Word(head);
    uint32_t changes;
    std::memcpy(&changes, reinterpret_cast<uint8_t *>(this) + 0x70, 4);
    changes &= ~covered;
    std::memcpy(reinterpret_cast<uint8_t *>(this) + 0x70, &changes, 4);

    const uint8_t *p = head + 4;
    if (*p++ != 0) {
        const uint32_t declaration = Word(p);
        p += 4;
        if (PtrWord(mWanted.declaration) != declaration) {
            mWanted.declaration = WordPtr<IDirect3DVertexDeclaration9>(declaration);
            Report(mD3D->SetVertexDeclaration(mWanted.declaration), 0xd4e);
        }
    }
    for (uint8_t n = *p++; n != 0; --n, p += 13) {
        const uint32_t index = p[0], buffer = Word(p + 1), stride = Word(p + 5), offset = Word(p + 9);
        StreamSource &stream = mWanted.streams[index];
        if (PtrWord(stream.buffer) == buffer && stream.stride == stride && stream.offset == offset) continue;
        stream.buffer = WordPtr<IDirect3DVertexBuffer9>(buffer);
        stream.stride = stride;
        stream.offset = offset;
        Report(mD3D->SetStreamSource(index, stream.buffer, offset, stride), 0xd62);
    }
    if (*p++ != 0) {
        const uint32_t indices = Word(p);
        if (PtrWord(mWanted.indices) != indices) {
            mWanted.indices = WordPtr<IDirect3DIndexBuffer9>(indices);
            Report(mD3D->SetIndices(mWanted.indices), 0xd6e);
        }
    }
}

// What the change record still holds after a block replaced another (state the old block set and the new one does not
// record) goes back to the defaults. The indices are tested against the tables only (a change bit is set by a setter,
// which takes an index inside its table).
void cDeviceStateDX::RestoreChangedStates()
{
    if (mChanges.textures != 0) {
        for (uint32_t i = 0; i < 16; ++i) {
            if (!(mChanges.textures & (1u << i)) || !mWanted.textures[i]) continue;
            mWanted.textures[i] = nullptr;
            Report(mD3D->SetTexture(i, nullptr), 0xd85);
        }
        mChanges.textures = 0;
    }
    if ((mChanges.renderStates[0] | mChanges.renderStates[1] | mChanges.renderStates[2]) != 0) {
        for (uint32_t i = 0; i < 0x59; ++i) {
            if (!(mChanges.renderStates[i >> 5] & (1u << (i & 31))) || mWanted.renderStates[i] == mDefaults.renderStates[i])
                continue;
            mWanted.renderStates[i] = mDefaults.renderStates[i];
            Report(mD3D->SetRenderState(static_cast<D3DRENDERSTATETYPE>(RenderStateMap()[i]), mWanted.renderStates[i]),
                   0xda7);
        }
        mChanges.renderStates[0] = mChanges.renderStates[1] = mChanges.renderStates[2] = 0;
    }
    if (mChanges.stages != 0) {
        // The stages from the lowest to the highest in the mask, whether or not the one between has its bit.
        const unsigned low = std::countr_zero(mChanges.stages), high = 15 - std::countl_zero(mChanges.stages);
        mChanges.stages = 0;
        for (unsigned stage = low; stage <= high && stage < 8; ++stage) {
            const uint32_t bits = mChanges.stageStates[stage];
            if (bits == 0) continue;
            for (uint32_t k = 0; k < 0x12; ++k) {
                if (!(bits & (1u << k)) || mWanted.stageStates[stage][k] == mDefaults.stageStates[stage][k]) continue;
                mWanted.stageStates[stage][k] = mDefaults.stageStates[stage][k];
                Report(mD3D->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(StageStateMap()[k]),
                                                  mWanted.stageStates[stage][k]),
                       0xdcf);
            }
            mChanges.stageStates[stage] = 0;
        }
    }
    if (mChanges.samplers != 0) {
        const unsigned low = std::countr_zero(mChanges.samplers), high = 15 - std::countl_zero(mChanges.samplers);
        mChanges.samplers = 0;
        for (unsigned sampler = low; sampler <= high; ++sampler) {
            const uint32_t bits = mChanges.samplerStates[sampler];
            if (bits == 0) continue;
            for (uint32_t k = 0; k < 0xd; ++k) {
                if (!(bits & (1u << k)) || mWanted.samplerStates[sampler][k] == mDefaults.samplerStates[sampler][k]) continue;
                mWanted.samplerStates[sampler][k] = mDefaults.samplerStates[sampler][k];
                Report(mD3D->SetSamplerState(sampler, static_cast<D3DSAMPLERSTATETYPE>(SamplerStateMap()[k]),
                                             mWanted.samplerStates[sampler][k]),
                       0xdf9);
            }
            mChanges.samplerStates[sampler] = 0;
        }
    }
    if (mChanges.transforms != 0) {
        for (uint32_t t = 0; t < 8; ++t) {
            if (!(mChanges.transforms & (1u << t))) continue;
            if (CopyWords(&mWanted.textureTransforms[t], &mDefaults.textureTransforms[t], 16))
                Report(mD3D->SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0 + t), &mWanted.textureTransforms[t]),
                       0xe17);
        }
        mChanges.transforms = 0;
    }
    if (mChanges.misc & ChangeRecord::kMaterial) {
        if (CopyWords(&mWanted.material, &mDefaults.material, sizeof(D3DMATERIAL9) / 4))
            Report(mD3D->SetMaterial(&mWanted.material), 0xe26);
    }
    if ((mChanges.misc & ChangeRecord::kSoftwareVertexProcessing) &&
        mWanted.softwareVertexProcessing != mDefaults.softwareVertexProcessing) {
        mWanted.softwareVertexProcessing = mDefaults.softwareVertexProcessing;
        Report(mD3D->SetSoftwareVertexProcessing(mWanted.softwareVertexProcessing), 0xe2e);
    }
    if ((mChanges.misc & ChangeRecord::kVertexShader) && mWanted.vertexShader != nullptr) {
        mWanted.vertexShader = nullptr;
        Report(mD3D->SetVertexShader(nullptr), 0xe36);
    }
    if ((mChanges.misc & ChangeRecord::kPixelShader) && mWanted.pixelShader != nullptr) {
        mWanted.pixelShader = nullptr;
        Report(mD3D->SetPixelShader(nullptr), 0xe3e);
    }
    mChanges.misc = 0;
}

// The declaration, streams and indices the previous block changed go back to none.
void cDeviceStateDX::RestoreChangedStreams()
{
    if (mDeclarationChanged != 0 && mWanted.declaration != nullptr) {
        mWanted.declaration = nullptr;
        Report(mD3D->SetVertexDeclaration(nullptr), 0xe4b);
    }
    for (uint32_t i = 0; i < 16; ++i) {
        if (!(mStreamChanges & (1u << i))) continue;
        StreamSource &stream = mWanted.streams[i];
        if (stream.buffer == nullptr && stream.stride == 0 && stream.offset == 0) continue;
        stream.buffer = nullptr;
        stream.stride = 0;
        stream.offset = 0;
        Report(mD3D->SetStreamSource(i, nullptr, 0, 0), 0xe65);
    }
    if (mIndicesChanged != 0 && mWanted.indices != nullptr) {
        mWanted.indices = nullptr;
        Report(mD3D->SetIndices(nullptr), 0xe72);
    }
    const uint32_t none = 0;
    std::memcpy(reinterpret_cast<uint8_t *>(this) + 0x70, &none, 4);
}

} // namespace nGZGraphic4
