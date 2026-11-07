#pragma once
#include <cstdint>

// cIGZAny (vtable 0xd9c654): a typed value (the property sets' values, messages' arguments), for classes in the
// standalone shape (docs/standalone-shape.md) to derive from. The original's name and slot order (global, as its
// RTTI); not a cIGZUnknown (no reference count, no QueryInterface).
//
// The interface's own vtable has the destructor in slot 0 (0x14c840: the vtable reset to cIGZAny's, then a 4-byte
// delete on flags & 1) and _purecall in the others. The values derive from it through a class template
// cGZAny<Value> (RTTI: AnyBoolean : cGZAny<AnyBoolean> : cIGZAny), which has no vtable of its own in the image.
class cIGZAny {
public:
    // A user-provided body, not `= default`: with a defaulted one MSVC drops the destructor's store of cIGZAny's
    // vtable, which the original's destructors leave in the object (0x14c5f0, 0x1bedcb, the statics' thunks).
    virtual ~cIGZAny() {}                                        // slot 0: deleting destructor (ret 4)
    virtual bool Assign(const cIGZAny &source) = 0;              // slot 1 (ret 4): copies from the same type id only
    virtual int32_t Compare(const cIGZAny &other) const = 0;     // slot 2 (ret 4): -1 / 0 / 1
    virtual cIGZAny *Clone() const = 0;                          // slot 3: a new copy (operator new)
    virtual uint32_t GetType() const = 0;                        // slot 4: the type id
    virtual const char *GetTypeName() const = 0;                 // slot 5: the class's name
};
