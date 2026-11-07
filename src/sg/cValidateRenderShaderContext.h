#pragma once
// nGZSceneGraph::cValidateRenderShaderContext (lane TM, scenegraph M3a): a shader pass part that only checks, in Init,
// that the render shader context the pass would draw in has what the pass needs: vertex format elements present or
// absent, context flags set or clear, light counts per light type, and the transform, morph channel and viewer render
// type counts. Whole in the standalone shape: the material's shader makes it through the COM director (clsid
// 0x2d2f66ad, the factory 0xac3370 hooked to make ours); the game then reaches its bodies only through our vtable, so
// the image's constructor, destructor and slot bodies are in effect through the factory hook
// (db/status/cValidateRenderShaderContext.jsonl). Slots 17 to 19 are 10-byte setters folded with unrelated classes
// (0x230b70, 0x3bdbc0, 0x209e20): ours are ours, the folded bodies stay.
#include "sg/SGShaderAttributes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nGZSceneGraph {

// 0xaeed90 (cdecl, hooked): a light type's name ("ambient", "directional"... "user5"; "UNKNOWN" past 12).
const char *__cdecl LightTypeName(uint32_t type);

// vtable 0xe76a0c, 0x6c bytes, factory 0xac3370.
class cValidateRenderShaderContext : public cShaderControllerT<cIValidateRenderShaderContext> {
public:
    static cIGZUnknown *__cdecl Create(); // 0xac3370

    static constexpr uint32_t kLightTypes = 13;
    static constexpr uint32_t kFlags = 4;
    enum : uint8_t { kForbidden = 0, kRequired = 1 }; // an element's or flag's mode; others: either
    static constexpr int32_t kAny = -1;                // a count's: any (every negative count)

    // One vertex element requirement (8 bytes, the last two zero: the hash and the comparison read them).
    struct Element {
        uint32_t usage;
        uint8_t index;
        uint8_t mode;
        uint8_t pad[2];
    };
    static_assert(sizeof(Element) == 8);

    cValidateRenderShaderContext() = default; // 0xabdeb0
    ~cValidateRenderShaderContext() override; // 0xabe390 (deleting 0xabeaa0, slot 20)

    // 8 (0xac4740): `context` is the render shader context's key, three words: words 0 and 1 hold 13 four-bit light
    // counts, running totals by light type; word 2 holds the flags (bits 0-3), the vertex format id (4-10), the viewer
    // render type (11-14), the transform count (15-21) and the morph channel count (22-27). Each requirement checked in
    // that order (a report line for the first failure when `report`'s low byte is set), then the template's hash.
    bool Init(void *caps, void *context, void *table, void *report) override;

    void AddVertexElement(uint32_t usage, uint8_t index, uint8_t mode) override; // 14 (0xabeb40)
    void SetFlag(uint32_t flag, int32_t mode) override { mFlags[flag] = mode; }   // 15 (0xabeac0; eax-ignored: the index register left over, a setter)
    void SetLightCount(uint32_t type, int32_t count) override { mLights[type] = count; } // 16 (0xabead0; eax-ignored: the index register left over, a setter)
    void SetTransformCount(int32_t count) override { mTransforms = count; }       // 17 (folded 0x230b70)
    void SetMorphChannelCount(int32_t count) override { mMorphChannels = count; } // 18 (folded 0x3bdbc0)
    void SetViewerRenderType(int32_t type) override { mViewerRenderType = type; } // 19 (folded 0x209e20)
    uint32_t HashParams() override;                                  // 21 (0xac3660)
    bool EqualParams(cIValidateRenderShaderContext *other) override; // 22 (0xac54e0)

    std::vector<Element> mElements;        // +0x10
    int32_t mFlags[kFlags] = {2, 2, 2, 2}; // +0x1c: either
    int32_t mLights[kLightTypes] = {kAny, kAny, kAny, kAny, kAny, kAny, kAny,
                                    kAny, kAny, kAny, kAny, kAny, kAny}; // +0x2c
    int32_t mTransforms = kAny;            // +0x60
    int32_t mMorphChannels = kAny;         // +0x64
    int32_t mViewerRenderType = kAny;      // +0x68
};
static_assert(sizeof(cValidateRenderShaderContext) == 0x6c);
static_assert(offsetof(cValidateRenderShaderContext, mElements) == 0x10);
static_assert(offsetof(cValidateRenderShaderContext, mFlags) == 0x1c);
static_assert(offsetof(cValidateRenderShaderContext, mLights) == 0x2c);
static_assert(offsetof(cValidateRenderShaderContext, mTransforms) == 0x60);
static_assert(offsetof(cValidateRenderShaderContext, mViewerRenderType) == 0x68);

} // namespace nGZSceneGraph
