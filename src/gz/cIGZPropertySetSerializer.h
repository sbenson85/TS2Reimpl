#pragma once
#include "gz/cIGZPropertySet.h" // cKeyUint32Traits
#include "gz/cIGZUnknown.h"

#include <cstdint>

// cIGZPropertySetSerializerT<Traits> (interface id 0xebe701f3): reads a property set from a stream and writes one to a stream;
// for classes in the standalone shape (docs/standalone-shape.md) to derive from. The original is a template over the key
// traits; here over the set class the traits go with (cGZPropertySetString for cKeyStringTraitsT). Slots as every
// implementer's vtable has them (the dispatcher 0xdd4270, BinaryT 0xdd4418, XmlT 0xdd4320). Callers keep using the generated
// ifaces::cIGZPropertySetSerializer view. Lane EF.
template <class Set> class cIGZPropertySetSerializerT : public cIGZUnknown {
public:
    // slot 3 (ret 8): the set's properties from a cIGZIStream
    virtual bool Read(void *stream, Set *set) = 0;
    // slot 4 (ret 0xc): the set into a cIGZOStream; the dispatcher reads the flag's low byte (binary or XML), the delegates
    // ignore it
    virtual bool Write(void *stream, Set *set, uint8_t binary) = 0;
    // slot 5: the scalar deleting destructor
    virtual ~cIGZPropertySetSerializerT() = default;
};

class cGZPropertySetString;
using cIGZPropertySetSerializerString = cIGZPropertySetSerializerT<cGZPropertySetString>;

// The uint32-keyed serializers' interface (the dispatcher 0xdadc24, BinaryT 0xdac8f0, XmlT 0xdadc40). Lane EQ.
template <class Traits> class cGZPropertySetT;
using cIGZPropertySetSerializerUint32 = cIGZPropertySetSerializerT<cGZPropertySetT<cKeyUint32Traits>>;

// cIGZExpatContentHandler (interface id 0x4b6b9c9b): the callbacks of the framework's expat parser object. The interface's own
// vtable (0xda23ec) has default bodies for slots 6-11 (each `return false`, shared by every handler: 0x147480 for slots 6/7,
// 0x1474b0 for 8/10, 0x18a7a0 for 9, 0x147110 for 11); their meanings are not known (expat's processing-instruction,
// comment, CDATA and default handlers by arity). Lane EF.
class cIGZExpatContentHandler : public cIGZUnknown {
public:
    // slot 3 (ret 0x10): an element's name and expat's attribute list (name, value pairs, null-terminated)
    virtual bool StartElement(const char *name, const char **attributes, uint32_t a3, uint32_t a4) = 0;
    virtual bool EndElement(const char *name) = 0;                     // slot 4 (ret 4)
    virtual bool CharacterData(const char *text, uint32_t length) = 0; // slot 5 (ret 8)
    virtual bool Callback6() { return false; }                         // slot 6 (shared 0x147480)
    virtual bool Callback7() { return false; }                         // slot 7 (shared 0x147480)
    virtual bool Callback8(uint32_t, uint32_t) { return false; }       // slot 8 (shared 0x1474b0, ret 8)
    virtual bool Callback9(uint32_t) { return false; }                 // slot 9 (shared 0x18a7a0, ret 4)
    virtual bool Callback10(uint32_t, uint32_t) { return false; }      // slot 10 (shared 0x1474b0, ret 8)
    virtual bool Callback11(uint32_t, uint32_t, uint32_t, uint32_t) { return false; } // slot 11 (shared 0x147110, ret 0x10)
};
