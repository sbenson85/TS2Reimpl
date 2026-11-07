#include "ts/cTSLightEffect.h"

#include <new>

cTSLightEffect::cTSLightEffect(const cTSLightDescription *description)
    : cTSNodeEffectBase(description), mColor{1.0f, 1.0f, 1.0f, 1.0f}
{
}

cITSComponentEffect *__cdecl cTSLightEffect::Create(const cTSLightDescription *description)
{
    return new (std::nothrow) cTSLightEffect(description);
}

void cTSLightEffect::Attach(cIGZUnknown *owner) { AttachNode(owner, kNodeClass, kNodeIid); }

void cTSLightEffect::Detach() { mNode.Reset(); }

bool cTSLightEffect::HandleEvent(uint32_t kind, void *, void *) { return kind == 3 || kind == 4; }

void cTSLightEffect::SetFlagged(bool value) { mNode->SetFlag(10, value); }
