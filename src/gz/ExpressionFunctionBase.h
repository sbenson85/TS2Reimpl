#pragma once
// The command parser's expression functions as real C++ classes (lane LF), for functions in the standalone shape
// (docs/standalone-shape.md). The hook-shaped nGZCommandParser::cExpressionFunction (gz/CommandParser.h: Construct writes the
// game's vtables) holds the global name, so these live in namespace gzexpr; nothing in the image reads RTTI.
//
// Layout (0x10 bytes for a cFunctionT): +0 cFunction (vtable: Evaluate, the deleting destructor, then the subclass's
// GetHelp), +4 the cIExpressionFunction face (the face the parser holds: QueryInterface, AddRef, Release, SetContext,
// GetFunction, GetName, slot 6), +8 the reference count, +0xc (cFunctionT) the parser state SetContext stores.
//
// The game's bodies (nGZCommandParser::cExpressionFunction, vtables 0xe7d7c4 / 0xe7d7d0; the cFunctionT<cMaterialParser>
// face 0xdae9a8): QueryInterface 0xaf7c60, AddRef 0x148fb0 (folded), Release 0xaf7d80, SetContext 0x1b9a50 (folded, does
// nothing) / 0x24c860 (cFunctionT), GetFunction 0x142700 (folded), slot 6 0x12e40 (folded), the destructor 0xaf79a0 (stores
// cExpressionFunction's two vtables), the subclasses' deleting destructor 0x247a80 (folded). None hooked here: these classes
// are ours only for the objects our code builds (cTSCommonSystem's expression functions); the game's own functions keep
// the game's vtables.
#include "gz/cIGZUnknown.h"

#include <cstdint>

namespace gzexpr {

// An evaluated value (0x14 bytes): four floats and a type (1: a number).
struct Value {
    float v[4];
    int32_t type;
};
static_assert(sizeof(Value) == 0x14, "Value layout");

class cFunction;

// The face the parser holds (iid 0xcc1d5da0).
class __declspec(novtable) cIExpressionFunction : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xcc1d5da0;

    // 3 (ret 8): the parser and its state, handed on registration.
    virtual void SetContext(void *parser, void *state) = 0;
    // 4: the function object (this face - 4).
    virtual cFunction *GetFunction() = 0;
    // 5: the name the parser files the function under.
    virtual const char *GetName() = 0;
    // 6 (ret 4): 0.
    virtual uint32_t Slot6(uint32_t) = 0;
};

class __declspec(novtable) cFunction {
public:
    // 0 (ret 0xc: the hidden result pointer, then two): the value of a call; `cursor` points into the expression's text
    // after the function's name, `parser` is the parser evaluating it.
    virtual Value Evaluate(const char **cursor, void *parser) = 0;
    // 1: the deleting destructor.
    virtual ~cFunction() = default;
};

class cExpressionFunction : public cFunction, public cIExpressionFunction {
public:
    cExpressionFunction() : mRefCount(0) {} // 0xaf7840 (the vtables and a zero count)
    ~cExpressionFunction() override {}      // 0xaf79a0 (the class's two vtables stored)
    cExpressionFunction(const cExpressionFunction &) = delete;
    cExpressionFunction &operator=(const cExpressionFunction &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override; // 0xaf7c60
    uint32_t AddRef() override;                              // 0x148fb0
    uint32_t Release() override;                             // 0xaf7d80
    void SetContext(void *parser, void *state) override;     // 0x1b9a50
    cFunction *GetFunction() override;                       // 0x142700
    uint32_t Slot6(uint32_t) override;                       // 0x12e40

    uint32_t mRefCount; // +8
};

// cFunctionT<State>: the parser's state kept (the face SetContext gets is 4 bytes into the state object).
class cFunctionT : public cExpressionFunction {
public:
    cFunctionT() = default;

    void SetContext(void *parser, void *state) override; // 0x24c860
    // +0 slot 2: the help text.
    virtual const char *GetHelp() = 0;

    void *mState; // +0xc (not written before SetContext)
};

static_assert(sizeof(cFunctionT) == 0x10, "cFunctionT layout");

} // namespace gzexpr
