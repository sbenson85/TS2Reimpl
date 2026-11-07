#pragma once
// The bodies of the property-set serializer delegates, cGZPropertySetSerializerBinaryT<Traits> and
// cGZPropertySetSerializerXmlT<Traits> (declared in src/gz/cGZPropertySetSerializer.h), one template for both key types as in
// the original. Include only where an instantiation is made: cGZPropertySetSerializer.cpp / cGZPropertySetSerializerXml.cpp
// (string keys, /O2 like the original's 0x551000-0x557400 module) and cGZPropertySetSerializerUint32Delegates.size.cpp (uint32
// keys, /O1 like 0x236be6-0x242e4d). Record formats: cGZPropertySetSerializer.h. Lanes DU / DV / DY (string bodies), EF (the
// shape), EU (the template, the uint32 instantiation).
#include "gz/cGZPropertySetSerializer.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/CommandParser.h" // nGZCommandParser::FormatDecimal
#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "gz/cIGZPropertySet.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/cIGZAny.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cIGZPersistDBRecord.h"
#include "rz/StaticGetters.h"
#include "rz/StringKeyParser.h"
#include "misc/HexParse.h"
#include "rz/cRZIniFile.h" // cRZIniStringHelpers::TrimBoth
#include "rz/cRZString.h"
#include "rz/RZTextParse.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

namespace gzpropser_detail {

// The set through its interface (the set argument is the interface pointer).
template <class Traits> cIGZPropertySetT<Traits> *Properties(void *set) { return static_cast<cIGZPropertySetT<Traits> *>(set); }

// A counted local released on scope exit (slot 2), also on failure.
struct CountedPtr {
    void *object = nullptr;
    CountedPtr() = default;
    CountedPtr(const CountedPtr &) = delete;
    CountedPtr &operator=(const CountedPtr &) = delete;
    ~CountedPtr()
    {
        if (object) VirtualCall<uint32_t(__thiscall *)(void *), 2>(object);
    }
};

// A cRZString on the stack (built inline by the original: vtable, empty text, refcount 0).
struct LocalString {
    cRZString text;
    LocalString() { text.Construct(); }
    ~LocalString() { text.Destruct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
};

inline cIGZString *Text(cRZString &s) { return s.AsInterface(); }
inline const char *GameText(uint32_t cell) { return *GameAddress<const char *>(cell); }
// The XML element names are the Any classes' type names, the text stored right after each class's 6-slot vtable.
inline const char *TypeName(uint32_t vtable) { return GameAddress<const char>(vtable + 0x18); }

// What the key type changes. String keys are texts (a binary key is cut at its first NUL, through ToChar); uint32 keys
// numbers.
template <class Binary> bool ReadKey(Binary *self, void *stream, GZServiceString *key)
{
    LocalString name;
    if (!self->ReadText(stream, &name.text)) return false;
    *key = GZServiceString(name.text.ToChar());
    return true;
}
template <class Binary> bool ReadKey(Binary *, void *stream, uint32_t *key)
{
    uint32_t value;
    if (!static_cast<ifaces::cIGZIStream *>(stream)->GetUint32(&value)) return false;
    *key = value;
    return true;
}
template <class Binary> bool WriteKey(Binary *self, void *stream, const GZServiceString *key)
{
    // (The original also gives up on a heap key with a null buffer, which a std::string never has.)
    return self->WriteText(stream, key->c_str());
}
template <class Binary> bool WriteKey(Binary *, void *stream, const uint32_t *key)
{
    return static_cast<ifaces::cIGZOStream *>(stream)->SetUint32(*key);
}

// XML: a key from the key attribute's text, and back.
inline void KeyFromText(cIGZString *text, GZServiceString *key)
{
    const uint32_t length = text->Strlen();
    key->assign(text->ToChar(), length);
}
inline void KeyFromText(cIGZString *text, uint32_t *key)
{
    *key = hexparse::ParseHex32(text->ToChar());
}
constexpr uint32_t kTypeIdFormat = 0xda3e60; // "0x%08x": a type id, and a uint32 key
inline void KeyText(const GZServiceString *key, cRZString *text) { text->mString.assign(key->data(), key->size()); }
inline void KeyText(const uint32_t *key, cRZString *text) { text->Sprintf(GameAddress<const char>(kTypeIdFormat), *key); }

// The XML writer (framework class 0xed50b6ac, built by 0x9c515d): the framework hands out its cIGZXmlBufferWriter face (+0x18),
// whose QI 0x8c0f3d0d gives the cIGZXmlWriter face (+0).
constexpr int kSlotCreateInstance = 3;   // framework: CreateInstance(class, iid, out)
constexpr int kSlotSetHandler = 8;       // parser: bool SetContentHandler(handler)
constexpr int kSlotParse = 3;            // parser: bool Parse(data, length, final)
constexpr int kSlotStreamGetUint8 = 4;   // cIGZIStream: bool GetUint8(uint8_t *)
constexpr int kSlotStreamGetVoid = 17;   // cIGZIStream: bool GetVoid(void *, length)
constexpr int kSlotSeekableSize = 10;    // seekable stream: uint32 size
constexpr uint32_t kInitialReserve = 0x1fa0; // the byte-by-byte path's first reserve
constexpr int kSlotWriterStartDocument = 3; // cIGZXmlWriter: bool StartDocument() (0x9c5906: the XML declaration)
constexpr int kSlotWriterStartElement = 4;  // bool StartElement(name)
constexpr int kSlotWriterEndElement = 5;    // bool EndElement(name)
constexpr int kSlotWriterAttribute = 8;     // bool Attribute(name, value)
constexpr int kSlotWriterText = 9;          // bool Text(characters)
constexpr int kSlotBufferText = 4;          // cIGZXmlBufferWriter: const char *Text() (0x9c52bb: its string's ToChar)
constexpr int kSlotBufferLength = 5;        // uint32 Length() (0x9c52c3: its string's Strlen)
constexpr int kSlotStreamSetVoid = 17;      // cIGZOStream: bool SetVoid(data, size)
constexpr uint32_t kUint32Format = 0xd9d510; // an AnyUint32's text ("%u")
constexpr uint32_t kFalseText = 0xda2d3c;    // "false"
constexpr uint32_t kElementName = 0xda7ce8;  // "Element": a StringSet member's element (both modules)
constexpr int kDecimalBufferSize = 0x100;   // an AnyFloat32's text: FormatDecimal into a 256-byte stack buffer
constexpr int kDecimalPrecision = 10;

// The cIGZXmlWriter calls (their results are never read).
inline void WriterStartElement(void *writer, const char *name)
{
    VirtualCall<uint32_t(__thiscall *)(void *, const char *), kSlotWriterStartElement>(writer, name);
}
inline void WriterEndElement(void *writer, const char *name)
{
    VirtualCall<uint32_t(__thiscall *)(void *, const char *), kSlotWriterEndElement>(writer, name);
}
inline void Attribute(void *writer, const char *name, const char *value)
{
    VirtualCall<uint32_t(__thiscall *)(void *, const char *, const char *), kSlotWriterAttribute>(writer, name, value);
}
inline void Characters(void *writer, const char *text)
{
    VirtualCall<uint32_t(__thiscall *)(void *, const char *), kSlotWriterText>(writer, text);
}

} // namespace gzpropser_detail

// =============================================================================================================================
// Binary delegate

// string: the compiler's scalar deleting destructor 0x552160 around it; uint32: 0x236edd (the vtable store) and 0x237015.
template <class Traits> cGZPropertySetSerializerBinaryT<Traits>::~cGZPropertySetSerializerBinaryT() = default;

// slot 0 (string folded 0x555400, uint32 folded 0x237d6f: one body each with the dispatcher's): AddRef through slot 1.
template <class Traits> bool cGZPropertySetSerializerBinaryT<Traits>::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != gzpropser::kIid) return false;
    *out = static_cast<cIGZPropertySetSerializerT<Set> *>(this);
    AddRef();
    return true;
}

// slot 1 (folded 0x148fb0)
template <class Traits> uint32_t cGZPropertySetSerializerBinaryT<Traits>::AddRef()
{
    return ++mRefCount;
}

// slot 2 (folded 0x238690): the count goes down first; at 0 or below (signed) the deleting destructor (slot 5 with 1).
template <class Traits> uint32_t cGZPropertySetSerializerBinaryT<Traits>::Release()
{
    const int32_t count = static_cast<int32_t>(--mRefCount);
    if (count > 0) return static_cast<uint32_t>(count);
    delete this;
    return 0;
}

template <class Traits> bool cGZPropertySetSerializerBinaryT<Traits>::ReadKey(void *stream, Key *key)
{
    return gzpropser_detail::ReadKey(this, stream, key);
}

template <class Traits> bool cGZPropertySetSerializerBinaryT<Traits>::WriteKey(void *stream, const Key *key)
{
    return gzpropser_detail::WriteKey(this, stream, key);
}

// u32 length, then that many bytes into the text (Resize first; no read for an empty text).
template <class Traits> bool cGZPropertySetSerializerBinaryT<Traits>::ReadText(void *stream, cRZString *text)
{
    auto *in = static_cast<ifaces::cIGZIStream *>(stream);
    uint32_t length;
    if (!in->GetUint32(&length)) return false;
    text->AsInterface()->Resize(length);
    if (length == 0) return true;
    return in->GetVoid(text->mString.data(), length);
}

// A null text fails; else u32 strlen, then the bytes (none for an empty text).
template <class Traits> bool cGZPropertySetSerializerBinaryT<Traits>::WriteText(void *stream, const char *text)
{
    if (text == nullptr) return false;
    auto *out = static_cast<ifaces::cIGZOStream *>(stream);
    const uint32_t length = static_cast<uint32_t>(std::strlen(text));
    if (!out->SetUint32(length)) return false;
    if (length == 0) return true;
    return out->SetVoid(const_cast<char *>(text), length);
}

// Reads the magic (the traits' own: 0xcbe750e0 string, 0x2bea4862 uint32), the version (kept in mVersion; above 2 fails) and the
// count, then each property: type, key, value by type, stored with the set's SetProperty (its result ignored). Any failed read
// or unknown type returns 0 with the properties before it already set.
template <class Traits> bool cGZPropertySetSerializerBinaryT<Traits>::Read(void *stream, Set *set)
{
    using gzpropser_detail::LocalString;
    auto *in = static_cast<ifaces::cIGZIStream *>(stream);
    auto *properties = gzpropser_detail::Properties<Traits>(set);
    uint32_t magic;
    if (!in->GetUint32(&magic)) return false;
    Key key{};
    if (magic != Policy::kMagic) return false;
    if (!in->GetUint16(&mVersion) || mVersion > 2) return false;
    uint32_t count;
    if (!in->GetUint32(&count)) return false;
    while (count != 0) {
        --count;
        uint32_t type;
        if (!in->GetUint32(&type)) return false;
        if (!ReadKey(stream, &key)) return false;
        switch (type) {
        case gzany::kTypeStringSet: {
            uint32_t strings;
            if (!in->GetUint32(&strings)) return false;
            // An empty set stored under the key (the set keeps a clone), then the strings go into the stored one.
            // (Raw storage: the game's constructor and destructor bodies run on it, as in the original.)
            alignas(AnyStringSet) unsigned char storage[sizeof(AnyStringSet)];
            auto *empty = reinterpret_cast<AnyStringSet *>(storage);
            empty->mVTable = GameAddress<const void>(gzany::kStringSetVTable);
            reinterpret_cast<GZStringSetOps *>(&empty->mSet)->Construct();
            properties->SetProperty(&key, empty);
            reinterpret_cast<GZStringSetOps *>(&empty->mSet)->Destruct();
            // ORIGINAL-BUG: when the key already holds a value of another type, SetProperty fails and this is that
            // value: the strings are inserted into whatever lies at its +4 (a corrupt or hand-made record only).
            void *stored = properties->GetProperty(&key);
            for (uint32_t i = 0; i < strings; ++i) {
                LocalString text;
                if (!ReadText(stream, &text.text)) return false;
                if (stored) {
                    const GZString element(text.text.ToChar());
                    reinterpret_cast<GZStringSetOps *>(&static_cast<AnyStringSet *>(stored)->mSet)->Insert(element);
                }
            }
            break;
        }
        case gzany::kTypeString: {
            LocalString text;
            if (!ReadText(stream, &text.text)) return false;
            AnyString value;
            value.Construct(*text.text.AsInterface());
            properties->SetProperty(&key, &value);
            value.Destruct();
            break;
        }
        case gzany::kTypeSint32: {
            int32_t number;
            if (!in->GetSint32(&number)) return false;
            InlineAny value{GameAddress<const void>(gzany::kSint32VTable), static_cast<uint32_t>(number)};
            properties->SetProperty(&key, &value);
            break;
        }
        case gzany::kTypeFloat32: {
            float number;
            if (!in->GetFloat32(&number)) return false;
            InlineAny value{GameAddress<const void>(gzany::kFloat32VTable), 0};
            std::memcpy(&value.mValue, &number, 4);
            properties->SetProperty(&key, &value);
            break;
        }
        case gzany::kTypeBoolean: {
            // The original reads into the top byte of its stream argument's stack slot.
            uint8_t flag = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(stream) >> 24);
            if (!in->GetUint8(&flag)) return false;
            InlineAny value{GameAddress<const void>(gzany::kBooleanVTable), flag != 0 ? 1u : 0u};
            properties->SetProperty(&key, &value);
            break;
        }
        case gzany::kTypeUint32: {
            uint32_t number;
            if (!in->GetUint32(&number)) return false;
            InlineAny value{GameAddress<const void>(gzany::kUint32VTable), number};
            properties->SetProperty(&key, &value);
            break;
        }
        default:
            return false;
        }
    }
    return true;
}

// Writes the set in its own order (GetFirst / GetNext): magic, version 2, count, then per property the type, the key and the
// value. An empty set writes nothing and returns 0 (GetFirst fails first). Texts are written up to their first NUL. At the end
// a stream with the record face is cut to its position (SetSize(GetPosition())).
template <class Traits> bool cGZPropertySetSerializerBinaryT<Traits>::Write(void *stream, Set *set, uint8_t)
{
    auto *out = static_cast<ifaces::cIGZOStream *>(stream);
    auto *properties = gzpropser_detail::Properties<Traits>(set);
    Key key{};
    void *value;
    if (!properties->GetFirst(&key, &value)) return false;
    if (!out->SetUint32(Policy::kMagic)) return false;
    if (!out->SetUint16(gzpropser::kBinaryVersionWritten)) return false;
    if (!out->SetUint32(properties->GetCount())) return false;
    do {
        auto *any = As<ifaces::cIGZAny>(value);
        if (!out->SetUint32(any->GetType())) return false;
        if (!WriteKey(stream, &key)) return false;
        bool written;
        switch (any->GetType()) {
        case gzany::kTypeStringSet: {
            const auto *strings = static_cast<const AnyStringSet *>(value);
            if (!out->SetUint32(static_cast<uint32_t>(strings->mSet.size()))) return false;
            for (const GZString &text : strings->mSet)
                if (!WriteText(stream, text.c_str())) return false;
            written = true;
            break;
        }
        case gzany::kTypeString:
            written = WriteText(stream, static_cast<const AnyString *>(value)->mString->ToChar());
            break;
        case gzany::kTypeSint32:
            written = out->SetSint32(static_cast<int32_t>(static_cast<const InlineAny *>(value)->mValue));
            break;
        case gzany::kTypeFloat32: {
            float number;
            std::memcpy(&number, &static_cast<const InlineAny *>(value)->mValue, 4);
            written = out->SetFloat32(number);
            break;
        }
        case gzany::kTypeBoolean:
            written = out->SetUint8(static_cast<uint8_t>(static_cast<const InlineAny *>(value)->mValue) != 0);
            break;
        case gzany::kTypeUint32:
            written = out->SetUint32(static_cast<const InlineAny *>(value)->mValue);
            break;
        default:
            written = false;
        }
        if (!written) return false;
    } while (properties->GetNext(&key, &value));

    gzpropser_detail::CountedPtr record;
    if (As<cIGZUnknown>(stream)->QueryInterface(gzpropser::kSeekableStreamIid, &record.object)) {
        auto *face = As<ifaces::cIGZPersistDBRecord>(record.object);
        face->SetSize(face->GetPosition());
    }
    return true;
}

// ---- the binary delegate's RVA hooks' adapters (qualified calls, bodies inlined; see the header) ----

template <class Traits>
cGZPropertySetSerializerBinaryT<Traits> *__fastcall cGZPropertySetSerializerBinaryAdapters<Traits>::Construct(void *memory, void *)
{
    return ::new (memory) Self;
}

template <class Traits> void __fastcall cGZPropertySetSerializerBinaryAdapters<Traits>::Destruct(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
}

template <class Traits>
bool __fastcall cGZPropertySetSerializerBinaryAdapters<Traits>::Read(Self *self, void *, void *stream, Set *set)
{
    [[msvc::forceinline_calls]] return self->Self::Read(stream, set);
}

template <class Traits>
bool __fastcall cGZPropertySetSerializerBinaryAdapters<Traits>::Write(Self *self, void *, void *stream, Set *set, uint8_t binary)
{
    [[msvc::forceinline_calls]] return self->Self::Write(stream, set, binary);
}

// The deleting destructor has no name to call without dispatch: its two steps by hand.
template <class Traits>
cGZPropertySetSerializerBinaryT<Traits> *__fastcall cGZPropertySetSerializerBinaryAdapters<Traits>::DeletingDestructor(Self *self, void *,
                                                                                                                     uint32_t flags)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}

template <class Traits>
bool __fastcall cGZPropertySetSerializerBinaryAdapters<Traits>::ReadKey(Self *self, void *, void *stream, Key *key)
{
    [[msvc::forceinline_calls]] return self->Self::ReadKey(stream, key);
}

template <class Traits>
bool __fastcall cGZPropertySetSerializerBinaryAdapters<Traits>::ReadText(Self *self, void *, void *stream, cRZString *text)
{
    [[msvc::forceinline_calls]] return self->Self::ReadText(stream, text);
}

template <class Traits>
bool __fastcall cGZPropertySetSerializerBinaryAdapters<Traits>::WriteKey(Self *self, void *, void *stream, const Key *key)
{
    [[msvc::forceinline_calls]] return self->Self::WriteKey(stream, key);
}

// =============================================================================================================================
// XML delegate

// The compiler stores both vtables, then each string is built. (The original writes every dword of the object, the small-
// string buffers included: the STL's string constructor zeroes its 16-byte buffer too, so nothing is left as it was.)
template <class Traits> cGZPropertySetSerializerXmlT<Traits>::cGZPropertySetSerializerXmlT()
{
    mKey.Construct();
    mText.Construct();
}

// (The compiler stores the two vtables back), then the strings, mText first. Slot 5 is the compiler's scalar deleting
// destructor around it.
template <class Traits> cGZPropertySetSerializerXmlT<Traits>::~cGZPropertySetSerializerXmlT()
{
    mText.Destruct();
    mKey.Destruct();
}

// slot 0 (the handler face's is the compiler's thunk): AddRef through slot 1.
template <class Traits> bool cGZPropertySetSerializerXmlT<Traits>::QueryInterface(uint32_t riid, void **out)
{
    if (riid == 1 || riid == gzpropser::kIid) {
        *out = static_cast<cIGZPropertySetSerializerT<Set> *>(this);
    } else if (riid == gzxml::kHandlerIid) {
        *out = static_cast<cIGZExpatContentHandler *>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

// slot 1 (folded 0x145260)
template <class Traits> uint32_t cGZPropertySetSerializerXmlT<Traits>::AddRef()
{
    return ++mRefCount;
}

// slot 2 (folded 0x242830): the count goes down first; at 0 or below (signed) the deleting destructor (slot 5 with 1).
template <class Traits> uint32_t cGZPropertySetSerializerXmlT<Traits>::Release()
{
    const int32_t count = static_cast<int32_t>(--mRefCount);
    if (count > 0) return static_cast<uint32_t>(count);
    delete this;
    return 0;
}

template <class Traits> bool cGZPropertySetSerializerXmlT<Traits>::KeyFromString(void *text, void *key)
{
    gzpropser_detail::KeyFromText(static_cast<cIGZString *>(text), static_cast<Key *>(key));
    return true;
}

template <class Traits> void cGZPropertySetSerializerXmlT<Traits>::AssignRootName(cRZString *text, const void *)
{
    text->mString.assign(gzpropser_detail::GameText(Policy::kRootNameCell));
}

template <class Traits> bool cGZPropertySetSerializerXmlT<Traits>::KeyToText(const Key *key, cRZString *text)
{
    gzpropser_detail::KeyText(key, text);
    return true;
}

// slot 3: the whole stream into a buffer (in one read when the stream is seekable, else byte by byte), then one Parse call
// through a new expat parser object whose content handler is this object.
template <class Traits> bool cGZPropertySetSerializerXmlT<Traits>::Read(void *stream, Set *set)
{
    using namespace gzpropser_detail;
    CountedPtr parser;
    void *framework = GetDirectorFrameworkObject();
    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **), kSlotCreateInstance>(
        framework, gzpropser::kXmlParserClass, gzpropser::kXmlParserIid, &parser.object);
    if (!parser.object) return false;
    if (!static_cast<uint8_t>(VirtualCall<uint32_t(__thiscall *)(void *, void *), kSlotSetHandler>(
            parser.object, static_cast<cIGZExpatContentHandler *>(this))))
        return false;

    std::vector<char> data;
    CountedPtr seekable;
    if (static_cast<uint8_t>(VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, void **), 0>(
            stream, gzpropser::kSeekableStreamIid, &seekable.object))) {
        data.resize(VirtualCall<uint32_t(__thiscall *)(void *), kSlotSeekableSize>(seekable.object));
        char *begin = data.data();
        const uint32_t length = uint32_t(data.size());
        if (!static_cast<uint8_t>(VirtualCall<uint32_t(__thiscall *)(void *, void *, uint32_t), kSlotStreamGetVoid>(
                stream, begin, length)))
            return false;
    } else {
        data.reserve(kInitialReserve);
        uint8_t byte;
        while (static_cast<uint8_t>(VirtualCall<uint32_t(__thiscall *)(void *, uint8_t *), kSlotStreamGetUint8>(stream, &byte)))
            data.push_back(char(byte));
    }
    mTarget = set;
    return static_cast<uint8_t>(VirtualCall<uint32_t(__thiscall *)(void *, const char *, uint32_t, uint32_t), kSlotParse>(
               parser.object, data.data(), uint32_t(data.size()), 1u)) != 0;
}

// slot 4: the set as XML through a new framework XML writer, in the set's own order (format in cGZPropertySetSerializer.h),
// then the writer's text into the stream in one SetVoid; a stream with the record face is then cut to its position. An empty
// set writes nothing and returns 0. A value of a type it does not know stops the writing with 0 and nothing reaches the
// stream. The writer escapes the texts, not this code.
template <class Traits> bool cGZPropertySetSerializerXmlT<Traits>::Write(void *stream, Set *set, uint8_t)
{
    using namespace gzpropser_detail;
    auto *properties = Properties<Traits>(set);
    if (properties->GetCount() == 0) return false;

    CountedPtr document; // the cIGZXmlBufferWriter face
    CountedPtr writer;   // its cIGZXmlWriter face (released first)
    void *framework = GetDirectorFrameworkObject();
    if (!static_cast<uint8_t>(VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **), kSlotCreateInstance>(
            framework, gzpropser::kXmlWriterClass, gzpropser::kXmlWriterClass, &document.object)))
        return false;
    // (The QI's result is not checked: the writer class always has the face.)
    As<cIGZUnknown>(document.object)->QueryInterface(gzpropser::kXmlWriterIid, &writer.object);
    VirtualCall<uint32_t(__thiscall *)(void *), kSlotWriterStartDocument>(writer.object);

    Key key{};
    LocalString root;
    AssignRootName(&root.text, &key);
    WriterStartElement(writer.object, root.text.ToChar());

    LocalString valueText; // a value's text (reused from property to property)
    LocalString keyText;
    LocalString typeText;
    void *value = nullptr;
    properties->GetFirst(&key, &value); // (its result is not checked: the count was not 0)
    do {
        KeyToText(&key, &keyText.text);
        auto *any = As<ifaces::cIGZAny>(value);
        typeText.text.Sprintf(GameAddress<const char>(kTypeIdFormat), any->GetType());
        const uint32_t type = any->GetType();
        const char *element;
        switch (type) {
        case gzany::kTypeFloat32: element = TypeName(gzany::kFloat32VTable); break;
        case gzany::kTypeSint32: element = TypeName(gzany::kSint32VTable); break;
        case gzany::kTypeString: element = TypeName(gzany::kStringVTable); break;
        case gzany::kTypeStringSet: element = TypeName(gzany::kStringSetVTable); break;
        case gzany::kTypeBoolean: element = TypeName(gzany::kBooleanVTable); break;
        case gzany::kTypeUint32: element = TypeName(gzany::kUint32VTable); break;
        default: return false;
        }
        WriterStartElement(writer.object, element);
        Attribute(writer.object, GameText(Policy::kKeyAttributeCell), keyText.text.ToChar());
        Attribute(writer.object, GameText(Policy::kTypeAttributeCell), typeText.text.ToChar());
        switch (type) {
        case gzany::kTypeFloat32: {
            char digits[kDecimalBufferSize];
            digits[0] = 0;
            nGZCommandParser::FormatDecimal(double(static_cast<const AnyFloat32 *>(value)->mValue), digits, kDecimalBufferSize,
                                            kDecimalPrecision);
            Characters(writer.object, digits);
            break;
        }
        case gzany::kTypeSint32:
            valueText.text.Sprintf(GameAddress<const char>(gzxml::kSint32Format), static_cast<const AnySint32 *>(value)->mValue);
            Characters(writer.object, valueText.text.ToChar());
            break;
        case gzany::kTypeString:
            *Text(valueText.text) = *static_cast<const AnyString *>(value)->mString;
            Characters(writer.object, valueText.text.ToChar());
            break;
        case gzany::kTypeStringSet:
            valueText.text.mString.clear();
            for (const GZString &member : static_cast<const AnyStringSet *>(value)->mSet) {
                WriterStartElement(writer.object, GameAddress<const char>(kElementName));
                Characters(writer.object, member.c_str());
                WriterEndElement(writer.object, GameAddress<const char>(kElementName));
            }
            break;
        case gzany::kTypeBoolean:
            Characters(writer.object,
                       GameAddress<const char>(static_cast<const AnyBoolean *>(value)->mValue ? gzxml::kTrueText : kFalseText));
            break;
        default: // kTypeUint32
            valueText.text.Sprintf(GameAddress<const char>(kUint32Format), static_cast<const AnyUint32 *>(value)->mValue);
            Characters(writer.object, valueText.text.ToChar());
            break;
        }
        WriterEndElement(writer.object, element);
    } while (properties->GetNext(&key, &value));
    WriterEndElement(writer.object, root.text.ToChar());

    const uint32_t length = VirtualCall<uint32_t(__thiscall *)(void *), kSlotBufferLength>(document.object);
    const char *text = VirtualCall<const char *(__thiscall *)(void *), kSlotBufferText>(document.object);
    if (!static_cast<uint8_t>(
            VirtualCall<uint32_t(__thiscall *)(void *, const char *, uint32_t), kSlotStreamSetVoid>(stream, text, length)))
        return false;

    CountedPtr record;
    if (As<cIGZUnknown>(stream)->QueryInterface(gzpropser::kSeekableStreamIid, &record.object)) {
        auto *face = As<ifaces::cIGZPersistDBRecord>(record.object);
        face->SetSize(face->GetPosition());
    }
    return true;
}

// handler slot 5: text inside a property or an <Element> is appended to mText (its slot 20).
template <class Traits> bool cGZPropertySetSerializerXmlT<Traits>::CharacterData(const char *text, uint32_t length)
{
    if (mState == XmlReadState::kInProperty || mState == XmlReadState::kInElement)
        gzpropser_detail::Text(mText)->Append(text, length);
    return true;
}

// handler slot 3.
template <class Traits>
bool cGZPropertySetSerializerXmlT<Traits>::StartElement(const char *name, const char **attributes, uint32_t, uint32_t)
{
    using namespace gzpropser_detail;
    switch (mState) {
    case XmlReadState::kOutside: {
        // Only the root element (case-insensitive) opens the set.
        LocalString root;
        AssignRootName(&root.text, &attributes);
        const bool isRoot = root.text.IsEqual(name, false);
        if (isRoot) mState = XmlReadState::kInRoot;
        return isRoot;
    }
    case XmlReadState::kInRoot: {
        // A property: its key and type attributes (case-insensitive names; the last of each wins).
        mValueType = 0;
        mKey.mString.clear();
        while (attributes && *attributes) {
            const char *attribute = attributes[0];
            const char *value = attributes[1];
            attributes += 2;
            if (_stricmp(GameText(Policy::kKeyAttributeCell), attribute) == 0) {
                mKey.mString.assign(value, std::strlen(value)); // (uint32: 0x1adad0, assign(const char *))
            } else if (_stricmp(GameText(Policy::kTypeAttributeCell), attribute) == 0) {
                mValueType = hexparse::ParseHex32(value);
            }
        }
        if (mKey.mString.size() == 0 || mValueType == 0) return false;
        mState = XmlReadState::kInProperty;
        mText.mString.clear();
        if (mValueType != gzany::kTypeStringSet) return true;

        // A StringSet: an empty one is stored now, and its members are inserted into the stored copy.
        Key key{};
        if (!KeyFromString(Text(mKey), &key)) return false;
        // The empty AnyStringSet local (0x236cab), built and torn down by hand as the original does (its set's destructor
        // 0x551d90, no vtable reset): raw storage, so no C++ constructor or destructor runs as well.
        alignas(AnyStringSet) uint8_t storage[sizeof(AnyStringSet)];
        auto *empty = reinterpret_cast<AnyStringSet *>(storage);
        empty->mVTable = GameAddress<const void>(gzany::kStringSetVTable);
        reinterpret_cast<GZStringSetOps *>(&empty->mSet)->Construct();
        const bool stored = Properties<Traits>(mTarget)->SetProperty(&key, empty);
        reinterpret_cast<GZStringSetOps *>(&empty->mSet)->Destruct();
        if (!stored) return false;
        void *held = Properties<Traits>(mTarget)->GetProperty(&key);
        if (!held) return false;
        mStringSet = held;
        return true;
    }
    case XmlReadState::kInProperty:
        // Only a StringSet property has children: <Element>.
        if (!mStringSet || _stricmp(GameText(Policy::kElementNameCell), name) != 0) return false;
        mText.mString.clear();
        mState = XmlReadState::kInElement;
        return true;
    case XmlReadState::kInElement:
        return false;
    default:
        return true;
    }
}

// handler slot 4.
template <class Traits> bool cGZPropertySetSerializerXmlT<Traits>::EndElement(const char *name)
{
    using namespace gzpropser_detail;
    switch (mState) {
    case XmlReadState::kOutside:
        return false;
    case XmlReadState::kInRoot:
        mState = XmlReadState::kOutside;
        return true;
    case XmlReadState::kInElement: {
        // The end of a StringSet member: its text joins the set.
        mState = XmlReadState::kInProperty;
        if (!mStringSet || _stricmp(GameText(Policy::kElementNameCell), name) != 0) return false;
        GZString member(mText.mString.c_str());
        reinterpret_cast<GZStringSetOps *>(&static_cast<AnyStringSet *>(mStringSet)->mSet)->Insert(member);
        return true;
    }
    case XmlReadState::kInProperty:
        break;
    default:
        return true;
    }

    // The end of a property: its trimmed text converted by type and set under its key.
    mState = XmlReadState::kInRoot;
    reinterpret_cast<cRZIniStringHelpers *>(&mText)->TrimBoth();
    Key key{};
    if (!KeyFromString(Text(mKey), &key)) return false;
    const char *text = mText.mString.c_str();
    auto *target = Properties<Traits>(mTarget);
    switch (mValueType) {
    case gzany::kTypeFloat32: {
        const float value = float(rztext::ParseDouble(text));
        AnyFloat32 any{GameAddress<const void>(gzany::kFloat32VTable), value};
        target->SetProperty(&key, &any);
        return true;
    }
    case gzany::kTypeSint32: {
        int32_t value = 0;
        if (std::sscanf(text, GameAddress<const char>(gzxml::kSint32Format), &value) == 1) {
            AnySint32 any{GameAddress<const void>(gzany::kSint32VTable), value};
            target->SetProperty(&key, &any);
        }
        return true;
    }
    case gzany::kTypeString: {
        AnyString any;
        any.Construct(*Text(mText));
        target->SetProperty(&key, &any);
        if (any.mString) any.mString->Release();
        return true;
    }
    case gzany::kTypeStringSet:
        mStringSet = nullptr; // its members were inserted as they ended
        return true;
    case gzany::kTypeBoolean: {
        AnyBoolean any;
        any.mVTable = GameAddress<const void>(gzany::kBooleanVTable);
        any.mValue = Text(mText)->IsEqual(GameAddress<const char>(gzxml::kTrueText), false);
        target->SetProperty(&key, &any);
        return true;
    }
    case gzany::kTypeUint32: {
        AnyUint32 any{GameAddress<const void>(gzany::kUint32VTable), StringKeyParser::ParseNumber(text)};
        target->SetProperty(&key, &any);
        return true;
    }
    default:
        return false; // an unknown type: nothing set
    }
}

// ---- the XML delegate's RVA hooks' adapters (qualified calls, bodies inlined; see the header) ----

template <class Traits>
cGZPropertySetSerializerXmlT<Traits> *__fastcall cGZPropertySetSerializerXmlAdapters<Traits>::Construct(void *memory, void *)
{
    return ::new (memory) Self;
}

template <class Traits> void __fastcall cGZPropertySetSerializerXmlAdapters<Traits>::Destruct(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
}

// The deleting destructor has no name to call without dispatch: its two steps by hand.
template <class Traits>
cGZPropertySetSerializerXmlT<Traits> *__fastcall cGZPropertySetSerializerXmlAdapters<Traits>::DeletingDestructor(Self *self, void *,
                                                                                                               uint32_t flags)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}

template <class Traits>
bool __fastcall cGZPropertySetSerializerXmlAdapters<Traits>::QueryInterface(Self *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(riid, out);
}

template <class Traits> bool __fastcall cGZPropertySetSerializerXmlAdapters<Traits>::Read(Self *self, void *, void *stream, Set *set)
{
    [[msvc::forceinline_calls]] return self->Self::Read(stream, set);
}

template <class Traits>
bool __fastcall cGZPropertySetSerializerXmlAdapters<Traits>::Write(Self *self, void *, void *stream, Set *set, uint8_t binary)
{
    [[msvc::forceinline_calls]] return self->Self::Write(stream, set, binary);
}

template <class Traits>
bool __fastcall cGZPropertySetSerializerXmlAdapters<Traits>::KeyFromString(Self *self, void *, void *text, void *key)
{
    [[msvc::forceinline_calls]] return self->Self::KeyFromString(text, key);
}

template <class Traits>
void __fastcall cGZPropertySetSerializerXmlAdapters<Traits>::AssignRootName(Self *self, void *, cRZString *text, const void *unused)
{
    [[msvc::forceinline_calls]] self->Self::AssignRootName(text, unused);
}

template <class Traits>
bool __fastcall cGZPropertySetSerializerXmlAdapters<Traits>::KeyToText(Self *self, void *, const Key *key, cRZString *text)
{
    [[msvc::forceinline_calls]] return self->Self::KeyToText(key, text);
}

// The handler face's adapters: `this` is the +4 face (never null: the __assume drops the cast's null test).
template <class Traits>
bool __fastcall cGZPropertySetSerializerXmlAdapters<Traits>::StartElement(cIGZExpatContentHandler *face, void *, const char *name,
                                                                         const char **attributes, uint32_t unused3, uint32_t unused4)
{
    __assume(face != nullptr);
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::StartElement(name, attributes, unused3, unused4);
}

template <class Traits>
bool __fastcall cGZPropertySetSerializerXmlAdapters<Traits>::EndElement(cIGZExpatContentHandler *face, void *, const char *name)
{
    __assume(face != nullptr);
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::EndElement(name);
}

template <class Traits>
bool __fastcall cGZPropertySetSerializerXmlAdapters<Traits>::CharacterData(cIGZExpatContentHandler *face, void *, const char *text,
                                                                          uint32_t length)
{
    __assume(face != nullptr);
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::CharacterData(text, length);
}
