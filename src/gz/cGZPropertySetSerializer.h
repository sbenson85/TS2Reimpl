#pragma once
#include "gz/cIGZPropertySet.h" // cKeyStringTraits
#include "gz/cIGZPropertySetSerializer.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>

class cGZPropertySetString;
template <class Traits> class cGZPropertySetT;
using cGZPropertySetUint32 = cGZPropertySetT<cKeyUint32Traits>;

// Serializers of the string-keyed property set (cGZPropertySetSerializerT / BinaryT / XmlT <cKeyStringTraitsT>,
// cIGZPropertySetSerializer iid 0xebe701f3). Structure pass: lane DM (notes/feedback/DM.md). RVAs 1.18.0.187;
// this instantiation is /O2 code in the 0x551000-0x557400 module (cTSWantTreeManager's translation unit); the
// uint32 instantiation (0x236be6-0x2429xx, /O1) has the same shapes and is listed in the handoff.
// cIGZPropertySetSerializer: 0 QI, 1 AddRef, 2 Release, 3 bool Read(cIGZIStream*, set*), 4 bool Write(cIGZOStream*,
// set*, bool binary) (ret 0xc everywhere; the delegates ignore the flag), 5 DeletingDestructor(flags).
//
// Record formats (string keys; checked against N001_Neighborhood.package: 269 XML 0xebfee33f records, one binary
// 0xebfee342 record, 1851 binary BINX records):
//   Binary: u32 magic 0xcbe750e0, u16 version (written 2; read must be < 3), u32 count, then per property
//     u32 type id, key (u32 length + bytes, no NUL), value: Uint32/Sint32 u32, Float32 f32, Boolean u8 (!= 0),
//     String u32 length + bytes, StringSet u32 count + strings. Key order: the set's (offset of first interning).
//   XML: <?xml version="1.0" encoding="UTF-8"?>\n<cGZPropertySetString>\n then per property
//     "  <AnyString key=\"name\" type=\"0x0b8bea18\">text</AnyString>\n" (element = the Any's type name; Uint32
//     "%u", Sint32 decimal, Float32 nGZCommandParser::FormatDecimal(.., 10), Boolean "true"/"false", StringSet
//     <Element> children), then </cGZPropertySetString>. Text produced by a framework XML writer object.
namespace gzpropser {
constexpr uint32_t kIid = 0xebe701f3;
constexpr uint32_t kBinaryMagic = 0xcbe750e0;
constexpr uint32_t kOtherMagic = 0x2bea4862;          // accepted by the dispatcher; the string BinaryT rejects it
constexpr uint16_t kBinaryVersionWritten = 2;
constexpr uint32_t kSeekableStreamIid = 0xe56b8f03;   // stream QI in the dispatcher and after writing
constexpr uint32_t kXmlParserClass = 0x6b6b01d1;      // framework class / iid of the expat parser object
constexpr uint32_t kXmlParserIid = 0x6b6b01cd;
constexpr uint32_t kXmlWriterClass = 0xed50b6ac;      // framework class of the XML writer; QI 0x8c0f3d0d
constexpr uint32_t kXmlWriterIid = 0x8c0f3d0d;
} // namespace gzpropser

// The dispatcher cGZPropertySetSerializerT<string>, 0x10 bytes: only ever a base of cGZPropertySetResourceString (+0x40), no
// object of its own (the uint32 resource embeds the uint32 instantiation, still hook-shaped: below). Its vtable 0xdd4270 is
// stored only while one is destroyed (0x5520e0, 0x552190): 0 QI 0x555400 (folded with BinaryT's), 1 AddRef 0x15e520 (folded;
// db/names calls it a CRT helper: a byte-identical fold), 2 Release 0x242810 (folded), 3 Read 0x555c90, 4 Write 0x5568c0,
// 5 the deleting destructor 0x552190. In the resource's vtable 0xdd4304 slots 0-2 and 5 are thunks to the resource's.
//
// Standalone shape (docs/standalone-shape.md, lane EL): a real C++ class, the original's template instantiation by name (an
// explicit specialization, as lane EF's delegates). Built by the resource's constructor; the RVA hooks are adapters
// (cGZPropertySetSerializerStringAdapters) for direct calls and for resources the game built with its own vtables.
template <class Traits> class cGZPropertySetSerializerT;
using cGZPropertySetSerializerString = cGZPropertySetSerializerT<cKeyStringTraits>;

template <> class cGZPropertySetSerializerT<cKeyStringTraits> : public cIGZPropertySetSerializerString {
public:
    static constexpr uint32_t kOriginalVTable = 0xdd4270;

    cGZPropertySetSerializerT() = default;      // inline in the resource's constructor 0x5519c0
    ~cGZPropertySetSerializerT() override;      // slot 5: 0x552190 (releases the binary delegate, then the XML one)
    cGZPropertySetSerializerT(const cGZPropertySetSerializerT &) = delete;
    cGZPropertySetSerializerT &operator=(const cGZPropertySetSerializerT &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override; // slot 0, folded 0x555400 (ret 8): 1 and 0xebe701f3 give this
    uint32_t AddRef() override;                              // slot 1, folded 0x15e520 (+0xc)
    uint32_t Release() override;                             // slot 2, folded 0x242810: at 0 or below, delete this
    bool Read(void *stream, cGZPropertySetString *set) override;                  // slot 3, 0x555c90 (ret 8, EH)
    bool Write(void *stream, cGZPropertySetString *set, uint8_t binary) override; // slot 4, 0x5568c0 (ret 0xc)
    // 0x555b10 (ret 0xc): the delegate for the format (created on first use), its Read; flag: low byte != 0
    bool ReadWith(void *stream, cGZPropertySetString *set, uint8_t binary);

    // +0x00 the vtable pointer
    void *mXml = nullptr;    // +0x04 cGZPropertySetSerializerXmlString*, counted, created on first XML use
    void *mBinary = nullptr; // +0x08 cGZPropertySetSerializerBinaryString*, counted
    uint32_t mRefCount = 0;  // +0x0c
};
static_assert(sizeof(cGZPropertySetSerializerString) == 0x10);
static_assert(offsetof(cGZPropertySetSerializerString, mXml) == 4);
static_assert(offsetof(cGZPropertySetSerializerString, mRefCount) == 0xc);

// The dispatcher's RVA hooks' entry points: original calling convention, our members without virtual dispatch.
struct cGZPropertySetSerializerStringAdapters {
    using Self = cGZPropertySetSerializerString;
    static bool __fastcall ReadWith(Self *self, void *, void *stream, cGZPropertySetString *set, uint8_t binary); // 0x555b10
};

// The uint32-keyed dispatcher cGZPropertySetSerializerT<cKeyUint32Traits>, 0x10 bytes, the base of cGZPropertySetResourceUint32
// (+0x34) as the string one is of the string resource; the same layout and the same bodies. /O1 code (0x236be6-0x2429xx;
// src/gz/cGZPropertySetSerializerUint32.size.cpp; constructor / destructor lane DY). vtable 0xdadc24: 0 QI 0x237d6f (folded with
// BinaryT<uint32>'s), 1 AddRef 0x15e520 (folded, +0xc), 2 Release 0x242810 (folded), 3 Read 0x2425ad (ret 8, EH), 4 Write
// 0x242899 (ret 0xc), 5 the deleting destructor 0x241fc9 (0x10). Constructor 0x241e79 (called only by the uint32 resource's
// constructor 0xc2bb25), destructor 0x241f2c.
//
// Standalone shape (docs/standalone-shape.md, lane EQ): a real C++ class, the original's template instantiation by name. Its
// delegates (cGZPropertySetSerializerBinaryT / XmlT <cKeyUint32Traits>, our classes since lane EU) are built by ReadWith / Write
// with `new` (the original's constructors 0x236be6, 0xc bytes / 0x241ea6, 0x5c bytes) and called through their vtables.
template <> class cGZPropertySetSerializerT<cKeyUint32Traits> : public cIGZPropertySetSerializerUint32 {
public:
    static constexpr uint32_t kOriginalVTable = 0xdadc24;

    cGZPropertySetSerializerT() = default;  // 0x241e79
    ~cGZPropertySetSerializerT() override;  // 0x241f2c (the binary delegate released, then the XML one); slot 5 0x241fc9
    cGZPropertySetSerializerT(const cGZPropertySetSerializerT &) = delete;
    cGZPropertySetSerializerT &operator=(const cGZPropertySetSerializerT &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override; // slot 0, folded 0x237d6f (ret 8): 1 and 0xebe701f3 give this
    uint32_t AddRef() override;                              // slot 1, folded 0x15e520 (+0xc)
    uint32_t Release() override;                             // slot 2, folded 0x242810: at 0 or below, delete this
    bool Read(void *stream, cGZPropertySetUint32 *set) override;                  // slot 3, 0x2425ad (ret 8, EH)
    bool Write(void *stream, cGZPropertySetUint32 *set, uint8_t binary) override; // slot 4, 0x242899 (ret 0xc)
    // 0x242547 (ret 0xc): the delegate for the format (created on first use), its Read; flag: low byte != 0
    bool ReadWith(void *stream, cGZPropertySetUint32 *set, uint8_t binary);

    // +0x00 the vtable pointer
    void *mXml = nullptr;    // +0x04 cGZPropertySetSerializerXmlT<uint32>, counted, created on first XML use
    void *mBinary = nullptr; // +0x08 cGZPropertySetSerializerBinaryT<uint32>, counted
    uint32_t mRefCount = 0;  // +0x0c
};
using cGZPropertySetSerializerUint32 = cGZPropertySetSerializerT<cKeyUint32Traits>;
static_assert(sizeof(cGZPropertySetSerializerUint32) == 0x10);
static_assert(offsetof(cGZPropertySetSerializerUint32, mXml) == 4);
static_assert(offsetof(cGZPropertySetSerializerUint32, mRefCount) == 0xc);

// The uint32 dispatcher's RVA hooks' entry points: original calling convention, our members without virtual dispatch.
struct cGZPropertySetSerializerUint32Adapters {
    using Self = cGZPropertySetSerializerUint32;
    static Self *__fastcall Construct(void *memory, void *);                                                    // 0x241e79
    static void __fastcall Destruct(Self *self, void *);                                                        // 0x241f2c
    static bool __fastcall Read(Self *self, void *, void *stream, cGZPropertySetUint32 *set);                   // 0x2425ad
    static bool __fastcall ReadWith(Self *self, void *, void *stream, cGZPropertySetUint32 *set, uint8_t binary); // 0x242547
    static bool __fastcall Write(Self *self, void *, void *stream, cGZPropertySetUint32 *set, uint8_t binary);    // 0x242899
};

// What the two instantiations of the delegates (string keys, uint32 keys) differ in: the set and key types, the binary
// magic, the game's vtables and the cells of the XML names (each module has its own copies). The bodies are one template
// (src/gz/cGZPropertySetSerializerDelegates.h) as in the original, instantiated in cGZPropertySetSerializer.cpp /
// cGZPropertySetSerializerXml.cpp (string, /O2) and cGZPropertySetSerializerUint32Delegates.size.cpp (uint32, /O1). Lane EU.
template <class Traits> struct cGZPropertySetSerializerTraits;

template <> struct cGZPropertySetSerializerTraits<cKeyStringTraits> {
    using Set = cGZPropertySetString;
    using Key = GZServiceString;
    static constexpr uint32_t kMagic = gzpropser::kBinaryMagic;
    static constexpr uint32_t kBinaryVTable = 0xdd4418;
    static constexpr uint32_t kXmlVTable = 0xdd4320;
    static constexpr uint32_t kXmlHandlerVTable = 0xdd433c;
    static constexpr uint32_t kRootNameCell = 0x1015de0;    // const char * "cGZPropertySetString"
    static constexpr uint32_t kKeyAttributeCell = 0x1015dd4; // "key"
    static constexpr uint32_t kTypeAttributeCell = 0x1015dd8; // "type"
    static constexpr uint32_t kElementNameCell = 0x1015ddc;  // "Element"
};

template <> struct cGZPropertySetSerializerTraits<cKeyUint32Traits> {
    using Set = cGZPropertySetUint32;
    using Key = uint32_t;
    static constexpr uint32_t kMagic = gzpropser::kOtherMagic; // 0x237cf2 (ret 4) answers it; Read takes no other
    static constexpr uint32_t kBinaryVTable = 0xdac8f0;
    static constexpr uint32_t kXmlVTable = 0xdadc40;
    static constexpr uint32_t kXmlHandlerVTable = 0xdadc5c;
    static constexpr uint32_t kRootNameCell = 0xff6cc0;
    static constexpr uint32_t kKeyAttributeCell = 0xff6cb4;
    static constexpr uint32_t kTypeAttributeCell = 0xff6cb8;
    static constexpr uint32_t kElementNameCell = 0xff6cbc;
};

// cGZPropertySetSerializerBinaryT<Traits>, 0xc bytes: vtable, refcount, last version read.
// string (vtable 0xdd4418): no constructor function, built inline by the dispatcher's ReadWith 0x555b10 / Write 0x5568c0 (ours:
//   `new`); 0 QI 0x555400 (folded with the dispatcher's), 1 AddRef 0x148fb0, 2 Release 0x238690 (folded), 3 Read 0x555510,
//   4 Write 0x556570, 5 DeletingDestructor 0x552160. Its key / text helpers are inlined.
// uint32 (vtable 0xdac8f0, /O1): constructor 0x236be6 (the uint32 dispatcher's ReadWith / Write, ours: `new`, and two game
//   functions that build one on the stack, 0x238159 / 0x2386c5, through the constructor hook), destructor 0x236edd; 0 QI
//   0x237d6f (folded), 1 AddRef 0x148fb0, 2 Release 0x238690 (folded), 3 Read 0x237dc2, 4 Write 0x23879c, 5 DeletingDestructor
//   0x237015; helpers ReadKey 0x237c98, ReadText 0x23822c, WriteKey 0x23876b, WriteText 0x238987.
//
// Standalone shape (docs/standalone-shape.md, lanes EF / EU): a real C++ class template, the original's by name (RTTI). The RVA
// hooks are adapters (cGZPropertySetSerializerBinaryAdapters<Traits>).
template <class Traits>
class cGZPropertySetSerializerBinaryT : public cIGZPropertySetSerializerT<typename cGZPropertySetSerializerTraits<Traits>::Set> {
public:
    using Policy = cGZPropertySetSerializerTraits<Traits>;
    using Set = typename Policy::Set;
    using Key = typename Policy::Key;
    static constexpr uint32_t kOriginalVTable = Policy::kBinaryVTable;

    cGZPropertySetSerializerBinaryT() = default; // uint32 0x236be6; string inline
    ~cGZPropertySetSerializerBinaryT() override; // uint32 0x236edd; slot 5 the scalar deleting destructor
    cGZPropertySetSerializerBinaryT(const cGZPropertySetSerializerBinaryT &) = delete;
    cGZPropertySetSerializerBinaryT &operator=(const cGZPropertySetSerializerBinaryT &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override; // slot 0 (ret 8): 1 and 0xebe701f3 give this
    uint32_t AddRef() override;                              // slot 1
    uint32_t Release() override;                             // slot 2: at 0 or below, delete this
    bool Read(void *stream, Set *set) override;                  // slot 3 (ret 8, EH)
    bool Write(void *stream, Set *set, uint8_t binary) override; // slot 4 (ret 0xc, EH); the flag is not read

    // Helpers (members by their calling convention: thiscall, `this` unused).
    bool ReadKey(void *stream, Key *key);              // uint32 0x237c98 (ret 8): u32; string: a text (cut at its NUL)
    bool ReadText(void *stream, cRZString *text);      // uint32 0x23822c (ret 8): u32 length, Resize, the bytes
    bool WriteKey(void *stream, const Key *key);       // uint32 0x23876b (ret 8): u32; string: the key's text
    bool WriteText(void *stream, const char *text);    // uint32 0x238987 (ret 8): null fails; u32 strlen, the bytes

    // +0 the vtable pointer
    uint32_t mRefCount = 0; // +4
    uint16_t mVersion = 0;  // +8 last version read
};
using cGZPropertySetSerializerBinaryString = cGZPropertySetSerializerBinaryT<cKeyStringTraits>;
using cGZPropertySetSerializerBinaryUint32 = cGZPropertySetSerializerBinaryT<cKeyUint32Traits>;
extern template class cGZPropertySetSerializerBinaryT<cKeyStringTraits>;
extern template class cGZPropertySetSerializerBinaryT<cKeyUint32Traits>;
static_assert(sizeof(cGZPropertySetSerializerBinaryString) == 0xc);
static_assert(offsetof(cGZPropertySetSerializerBinaryString, mVersion) == 8);
static_assert(sizeof(cGZPropertySetSerializerBinaryUint32) == 0xc);

// The binary delegate's RVA hooks' entry points: original calling convention, our members without virtual dispatch.
template <class Traits> struct cGZPropertySetSerializerBinaryAdapters {
    using Self = cGZPropertySetSerializerBinaryT<Traits>;
    using Set = typename Self::Set;
    using Key = typename Self::Key;
    static Self *__fastcall Construct(void *memory, void *);                                    // uint32 0x236be6
    static void __fastcall Destruct(Self *self, void *);                                        // uint32 0x236edd
    static bool __fastcall Read(Self *self, void *, void *stream, Set *set);
    static bool __fastcall Write(Self *self, void *, void *stream, Set *set, uint8_t binary);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static bool __fastcall ReadKey(Self *self, void *, void *stream, Key *key);                 // uint32 0x237c98
    static bool __fastcall ReadText(Self *self, void *, void *stream, cRZString *text);         // uint32 0x23822c
    static bool __fastcall WriteKey(Self *self, void *, void *stream, const Key *key);          // uint32 0x23876b
    // (WriteText 0x238987 is not hooked: not a function start in db/functions.jsonl, it lies in Write's chunk.)
};
using cGZPropertySetSerializerBinaryStringAdapters = cGZPropertySetSerializerBinaryAdapters<cKeyStringTraits>;
using cGZPropertySetSerializerBinaryUint32Adapters = cGZPropertySetSerializerBinaryAdapters<cKeyUint32Traits>;

// cGZPropertySetSerializerXmlT<string>, 0x5c bytes (created by the dispatcher's 0x555b10 / 0x5568c0). Two faces:
// +0 cIGZPropertySetSerializer (vtable 0xdd4320: 0 QI 0x555430, 1 AddRef 0x145260 (+8), 2 Release 0x242830,
// 3 Read 0x555d50, 4 Write 0x556a30, 5 DeletingDestructor 0x552200) and +4 cIGZExpatContentHandler (vtable
// 0xdd433c: 0 QI 0x555479, 1/2 thunks, 3 StartElement 0x552f30 (ret 0x10), 4 EndElement 0x552630 (ret 4),
// 5 CharacterData 0x5525f0 (ret 8), 6-11 no-op defaults shared with every handler).
// Reading is a state machine on mState (0 outside the root, 1 inside cGZPropertySetString, 2 inside a property,
// 3 inside a StringSet <Element>); EndElement converts mText by mValueType and sets it under mKey on mTarget.
// Implementation: src/gz/cGZPropertySetSerializerXml.cpp (lane DV; Write 0x556a30 lane DY).
namespace gzxml {
constexpr uint32_t kRootNameCell = 0x1015de0;   // const char * "cGZPropertySetString"
constexpr uint32_t kKeyAttributeCell = 0x1015dd4; // const char * "key"
constexpr uint32_t kTypeAttributeCell = 0x1015dd8; // const char * "type"
constexpr uint32_t kElementNameCell = 0x1015ddc;  // const char * "Element" (a StringSet's member)
constexpr uint32_t kTrueText = 0xda2d44;           // "true"
constexpr uint32_t kSint32Format = 0xd9b648;       // sscanf format of an AnySint32's text
constexpr uint32_t kHandlerIid = 0x4b6b9c9b;       // cIGZExpatContentHandler: QI answers the +4 face
} // namespace gzxml

enum class XmlReadState : uint32_t {
    kOutside = 0,   // before <cGZPropertySetString> (or after it closed)
    kInRoot = 1,    // between properties
    kInProperty = 2, // inside a property element, collecting its text
    kInElement = 3, // inside a StringSet's <Element>
};

// cGZPropertySetSerializerXmlT<uint32> (/O1, 0x241ea6-0x242e4d) has the same layout and bodies with uint32 keys: constructor
// 0x241ea6 (called only by the uint32 dispatcher), destructor 0x241f82; +0 vtable 0xdadc40: 0 QI 0x24249f, 1 AddRef 0x145260,
// 2 Release 0x242830 (folded), 3 Read 0x242641, 4 Write 0x242901, 5 DeletingDestructor 0x241fe8; +4 vtable 0xdadc5c: 0 QI
// thunk 0x2424f2, 3 StartElement 0x2422ae, 4 EndElement 0x242090, 5 CharacterData 0x242064. Its helpers: KeyFromString
// 0x24287e (the text's number, 0x1b0e2c), AssignRootName 0x24204a, KeyToText 0x242482 (Sprintf "0x%08x"). The only
// difference in behaviour: keys are numbers.
//
// Standalone shape (docs/standalone-shape.md, lanes EF / EU): a real C++ class template (the original's RTTI name) with the
// two interfaces as bases; the compiler emits both vtables, the +4 face's thunks (QI string 0x555479 / uint32 0x2424f2;
// AddRef / Release 0x1448e9 / 0x242845 are folded with other classes') and the deleting destructor. Creation sites: the
// dispatchers' ReadWith / Write (ours: `new`), the constructors 0x551a60 (called by the game's 0xcb1cf0 and 0xcb28b0) and
// 0x241ea6 (hooked to our construction), and the want-tree loader's derived class (cTSWantTreeLoader::vf3 0x553210, 0x64 bytes,
// vtables 0xdd4370 / 0xdd438c, string keys), whose base part the game builds inline: those objects keep the game's vtables
// and reach our code through the slot adapters only. The cRZStrings are still hook-shaped (Construct / Destruct), so they sit
// in anonymous unions, built and destroyed by hand as the original's inlined code does.
template <class Traits>
class cGZPropertySetSerializerXmlT : public cIGZPropertySetSerializerT<typename cGZPropertySetSerializerTraits<Traits>::Set>,
                                     public cIGZExpatContentHandler {
public:
    using Policy = cGZPropertySetSerializerTraits<Traits>;
    using Set = typename Policy::Set;
    using Key = typename Policy::Key;
    static constexpr uint32_t kOriginalVTable = Policy::kXmlVTable;
    static constexpr uint32_t kOriginalHandlerVTable = Policy::kXmlHandlerVTable;
    static constexpr uint32_t kSize = 0x5c;

    // Both vtables, refcount and state 0, two empty cRZStrings (their small buffers zeroed), the last three words 0. (The
    // original first stores the handler interface's vtable 0xda23ec at +4, overwritten at once.)
    cGZPropertySetSerializerXmlT();
    ~cGZPropertySetSerializerXmlT() override; // mText, then mKey; slot 5 the deleting destructor
    cGZPropertySetSerializerXmlT(const cGZPropertySetSerializerXmlT &) = delete;
    cGZPropertySetSerializerXmlT &operator=(const cGZPropertySetSerializerXmlT &) = delete;

    // slot 0: 1 / 0xebe701f3 -> this, 0x4b6b9c9b -> the handler face (+4), counted through slot 1
    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t AddRef() override;  // slot 1, folded 0x145260 (+8)
    uint32_t Release() override; // slot 2, folded 0x242830: at 0 or below, delete this
    bool Read(void *stream, Set *set) override; // slot 3 (ret 8, EH)
    // slot 4 (ret 0xc, EH): an empty set writes nothing and returns 0
    bool Write(void *stream, Set *set, uint8_t binary) override;

    // The handler face (+4), called by the expat parser object with this = the face.
    // slot 3 (ret 0x10): the element name and expat's attribute list (name, value pairs, null-terminated); the last two
    // arguments are not read.
    bool StartElement(const char *name, const char **attributes, uint32_t unused3, uint32_t unused4) override;
    bool EndElement(const char *name) override;                     // slot 4 (ret 4, EH)
    bool CharacterData(const char *text, uint32_t length) override; // slot 5 (ret 8): states 2/3 append to mText

    // string 0x556540 / uint32 0x24287e (ret 8): a key from a cIGZString's text (string: its characters; uint32: its number);
    // returns 1.
    bool KeyFromString(void *text, void *key);
    // uint32 0x24204a (ret 8; inline in the string one): the root element's name assigned to text (0x1adad0,
    // assign(const char *)); the second argument is
    // not read.
    void AssignRootName(cRZString *text, const void *unused);
    // uint32 0x242482 (ret 8; inline in the string one): the key as the key attribute's text; returns 1.
    bool KeyToText(const Key *key, cRZString *text);

    // +0x00 the serializer vtable, +0x04 the handler vtable
    uint32_t mRefCount = 0;                         // +0x08
    XmlReadState mState = XmlReadState::kOutside;   // +0x0c
    union { cRZString mKey; };                      // +0x10 current property's key attribute
    union { cRZString mText; };                     // +0x30 accumulated character data
    uint32_t mValueType = 0;                        // +0x50 current property's type attribute (a cIGZAny type id)
    void *mStringSet = nullptr;                     // +0x54 the AnyStringSet (the set's stored clone) being filled
    Set *mTarget = nullptr;                         // +0x58 set being read (stored by Read)
};
using cGZPropertySetSerializerXmlString = cGZPropertySetSerializerXmlT<cKeyStringTraits>;
using cGZPropertySetSerializerXmlUint32 = cGZPropertySetSerializerXmlT<cKeyUint32Traits>;
extern template class cGZPropertySetSerializerXmlT<cKeyStringTraits>;
extern template class cGZPropertySetSerializerXmlT<cKeyUint32Traits>;
static_assert(sizeof(cGZPropertySetSerializerXmlString) == 0x5c);
static_assert(offsetof(cGZPropertySetSerializerXmlString, mRefCount) == 8);
static_assert(offsetof(cGZPropertySetSerializerXmlString, mKey) == 0x10);
static_assert(offsetof(cGZPropertySetSerializerXmlString, mText) == 0x30);
static_assert(offsetof(cGZPropertySetSerializerXmlString, mTarget) == 0x58);
static_assert(sizeof(cGZPropertySetSerializerXmlUint32) == 0x5c);
static_assert(offsetof(cGZPropertySetSerializerXmlUint32, mTarget) == 0x58);

// The XML delegate's RVA hooks' entry points (src/hook/tables/cGZPropertySetSerializerXml.cpp): reached by direct calls and by
// objects carrying the game's vtables (the want-tree loader's derived class, or any object while the constructor hook is off).
template <class Traits> struct cGZPropertySetSerializerXmlAdapters {
    using Self = cGZPropertySetSerializerXmlT<Traits>;
    using Set = typename Self::Set;
    using Key = typename Self::Key;
    static Self *__fastcall Construct(void *memory, void *);                          // 0x551a60 / 0x241ea6
    static void __fastcall Destruct(Self *self, void *);                              // 0x551e20 / 0x241f82
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);   // 0x552200 / 0x241fe8
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out); // 0x555430 / 0x24249f
    static bool __fastcall Read(Self *self, void *, void *stream, Set *set);          // 0x555d50 / 0x242641
    static bool __fastcall Write(Self *self, void *, void *stream, Set *set, uint8_t binary); // 0x556a30 / 0x242901
    static bool __fastcall KeyFromString(Self *self, void *, void *text, void *key); // 0x556540 / 0x24287e
    static void __fastcall AssignRootName(Self *self, void *, cRZString *text, const void *unused); // uint32 0x24204a
    static bool __fastcall KeyToText(Self *self, void *, const Key *key, cRZString *text);         // uint32 0x242482
    static bool __fastcall StartElement(cIGZExpatContentHandler *face, void *, const char *name, const char **attributes,
                                        uint32_t unused3, uint32_t unused4);          // 0x552f30 / 0x2422ae
    static bool __fastcall EndElement(cIGZExpatContentHandler *face, void *, const char *name); // 0x552630 / 0x242090
    static bool __fastcall CharacterData(cIGZExpatContentHandler *face, void *, const char *text, uint32_t length); // 0x5525f0 / 0x242064
};
using cGZPropertySetSerializerXmlStringAdapters = cGZPropertySetSerializerXmlAdapters<cKeyStringTraits>;
using cGZPropertySetSerializerXmlUint32Adapters = cGZPropertySetSerializerXmlAdapters<cKeyUint32Traits>;
