#pragma once
// cTSModelEffect (lane XD, rendering row E10): the effect a model description makes (0x30 bytes, vtables 0xe9139c, 0xe913dc). A
// scene-graph node under the owner's holding either a model resource (a name ending in "_cres": a copy of the resource's tree is put
// under the node) or a primitive made on the spot (axes, cube, sphere, cone) in a shape-reference node, tinted by the description's
// colours and the colour events. RVAs 1.18.0.187; all /O2.
//
// Standalone shape (docs/standalone-shape.md): only Create 0xc06c00 builds one (cTSModelDescription::CreateEffect), so every model
// effect carries our vtables and the slots are in effect through the Create hook. Start, Stop, IsRunning, AddToScene and Update are
// cTSNodeEffectBase's (the folded 0xc06bb0, 0xc06bc0, 0x150f10, 0xc05ad0, 0xc069e0).
//
// Primitives (Attach's second path). The shape made for a name is cached by the name's key, in a table the original keeps in a
// global (0x10fd3cc, constructed by 0x1403f0, reached by Attach alone), ours here: built on first use and never freed.
#include "ts/cTSEffectBase.h"

#include <cstddef>
#include <cstdint>

struct cTSModelDescription;

// The resource manager the service object hands out (class ids are the framework's): slot 3 loads a resource by key.
class cITSModelResources : public cIGZUnknown {
public:
    // 3 (ret 0x14): the model tree for `key` as interface `iid`, into *out (counted); the last two arguments are 0.
    virtual bool Load(const void *key, uint32_t iid, void **out, uint32_t a, uint32_t b) = 0;
};

class cTSModelEffect : public cTSNodeEffectBase {
public:
    static constexpr uint32_t kNodeClass = 0x65246462; // the director's group node
    static constexpr uint32_t kNodeIid = 0xe51c10f5;

    explicit cTSModelEffect(const cTSModelDescription *description); // 0xc05970
    ~cTSModelEffect() override;                                      // 0xc05a10; deleting 0xc05ab0, +4 thunk 0xc05a9c
    static cITSComponentEffect *__cdecl Create(const cTSModelDescription *description); // 0xc06c00: null on failure
    __declspec(noinline) static void **__cdecl MakeAxesPart(void **out, float size); // 0xaef310: with one reference
    __declspec(noinline) static uint32_t __cdecl GetWhiteMaterial();                 // 0xc05be0: the material's id

    void Attach(cIGZUnknown *owner) override;                   // 0: 0xc05cf0
    void Detach() override;                                     // 1: 0xc06b80: the node and the model dropped
    // 7: 0xc06710: 3 sets the first colour from a float, 4 the other three from three; the node is tinted unless all four are 1.
    bool HandleEvent(uint32_t kind, void *a, void *b) override;
    void SetFlagged(bool value) override;                       // 9: 0xc069c0: the node's persisted flag 10 through the tree
    virtual bool SetModel(const char *text);                    // 14: 0xc06550: the model resource for the key `text`

    fx::CountedRef<nGZSceneGraph::cICompositionTreeNode> mModel; // +0x1c
    float mColor[4];                                             // +0x20 the description's colours (first, then the three)

private:
    void AddPrimitive(const cTSModelDescription *description);
};
static_assert(offsetof(cTSModelEffect, mModel) == 0x1c && offsetof(cTSModelEffect, mColor) == 0x20 &&
                  sizeof(cTSModelEffect) == 0x30,
              "cTSModelEffect layout");
