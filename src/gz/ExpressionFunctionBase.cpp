// The command parser's expression-function bases (lane LF). Layout and slots: src/gz/ExpressionFunctionBase.h.
#include "gz/ExpressionFunctionBase.h"

namespace gzexpr {

// 0xaf7c60: the face for its own id and cIGZUnknown's (counted through slot 1); false without an out pointer.
bool cExpressionFunction::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid != 1 && riid != cIExpressionFunction::kIid) return false;
    *out = static_cast<cIExpressionFunction *>(this);
    AddRef();
    return true;
}

// 0x148fb0
uint32_t cExpressionFunction::AddRef() { return ++mRefCount; }

// 0xaf7d80: no floor at 0; at 0 the object is deleted through the function's vtable (slot 1 with 1).
uint32_t cExpressionFunction::Release()
{
    const uint32_t count = --mRefCount;
    if (count != 0) return count;
    delete this;
    return 0;
}

// 0x1b9a50
void cExpressionFunction::SetContext(void *, void *) {}

// 0x142700
cFunction *cExpressionFunction::GetFunction() { return this; }

// 0x12e40
uint32_t cExpressionFunction::Slot6(uint32_t) { return 0; }

// 0x24c860: the base's (nothing), then the state object (the face minus 4; null stays null).
void cFunctionT::SetContext(void *parser, void *state)
{
    cExpressionFunction::SetContext(parser, state);
    mState = state ? static_cast<uint8_t *>(state) - 4 : nullptr;
}

} // namespace gzexpr
