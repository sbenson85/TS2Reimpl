// nGZSceneGraph::cDataListItem (lane OX); see the header.
#include "sg/cDataListItem.h"

#include "gz/cIGZCOM.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "rz/StaticGetters.h"   // GetDirectorFrameworkObject: the COM
#include "rz/StringKeyParser.h" // cRZStringHelpers::EndsWith
#include "rz/cRZIniFile.h"      // cRZIniStringHelpers: TrimBoth, SplitOff
#include "rz/cRZString.h"
#include "rz/cRZStringSub.h"    // Mid

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace nGZSceneGraph {

namespace {

// The type names of the text form (the image's table 0xe78318), by type.
constexpr const char *kTypeNames[10] = {"Undefined", "Bool", "Sint32", "Float32", "Vector2f",
                                        "Vector3f", "String", "List", "Quaternionf", "ByteArray"};

// 0xac8000: the type a name stands for (an exact match), 0 for none.
uint8_t TypeFromName(const char *name)
{
    if (name == nullptr) return 0;
    for (uint8_t type = 0; type < 10; ++type) {
        if (std::strcmp(name, kTypeNames[type]) == 0) return type;
    }
    return 0;
}

// A cRZString local, as the original's: the game's vtable (the stream calls it through it), constructed by our
// cRZString bodies. Out() gives storage for a helper that constructs into it (SplitOff, Mid).
class Text {
public:
    Text() = default;
    explicit Text(const char *text) { Get()->Construct(text); mLive = true; }
    Text(const Text &) = delete;
    Text &operator=(const Text &) = delete;
    ~Text() { Drop(); }

    cRZString *Get() { return reinterpret_cast<cRZString *>(mRaw); }
    cRZString *operator->() { return Get(); }
    cRZString *Out()
    {
        Drop();
        mLive = true;
        return Get();
    }
    const char *Chars() { return Get()->ToChar(); }
    uint32_t Length() { return Get()->Strlen(); }
    void TrimBoth() { reinterpret_cast<cRZIniStringHelpers *>(Get())->TrimBoth(); }
    // The text before the first space goes to `out` and leaves this together with the space (none: `out` is empty).
    void SplitOff(Text &out) { reinterpret_cast<cRZIniStringHelpers *>(Get())->SplitOff(out.Out(), " "); }

private:
    void Drop()
    {
        if (mLive) Get()->Destruct();
        mLive = false;
    }
    alignas(cRZString) unsigned char mRaw[sizeof(cRZString)];
    bool mLive = false;
};

// The original builds these strings from the item's name and string value, which are null when empty; the library
// string constructor it calls would fault on null, so ours stands an empty text in.
const char *OrEmpty(const char *text) { return text ? text : ""; }

} // namespace

// ---- lifetime and cIGZUnknown ----

cIGZUnknown *__cdecl cDataListItem::Create() { return new cDataListItem; }

cDataListItem::~cDataListItem()
{
    Clear(); // a list's items released
    mName.Free();
}

bool cDataListItem::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kIid && riid != kClassId && riid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cDataListItem::AddRef() { return ++mRefCount; }

uint32_t cDataListItem::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

// ---- the name ----

char *cDataListItem::Name::Heap() const
{
    char *text;
    std::memcpy(&text, mText + 4, sizeof(text));
    return text;
}

void cDataListItem::Name::SetHeap(char *text) { std::memcpy(mText + 4, &text, sizeof(text)); }

const char *cDataListItem::Name::Get() const { return mText[0] != 0 ? mText : Heap(); }

void cDataListItem::Name::Free()
{
    if (mText[0] == 0) std::free(Heap());
}

void cDataListItem::Name::Set(const char *text)
{
    Free();
    if (text[0] == 0) {
        mText[0] = 0;
        SetHeap(nullptr);
        return;
    }
    const size_t size = std::strlen(text) + 1;
    if (size <= sizeof(mText)) {
        std::memcpy(mText, text, size);
        return;
    }
    // ORIGINAL-BUG: the first byte is not zeroed to mark the heap copy. Over a short name it still says "held here":
    // the name then reads as that name's first four bytes, the pointer's bytes and on into the type word, and the copy
    // is never freed.
    char *copy = static_cast<char *>(std::malloc(size));
    SetHeap(copy);
    std::memcpy(copy, text, size);
}

const char *cDataListItem::GetName() { return mName.Get(); }

void cDataListItem::SetName(const char *name) { mName.Set(name); }

uint32_t cDataListItem::GetType() { return mType; }

// ---- the value ----

void cDataListItem::Clear()
{
    switch (mType) {
    case kNone:
        return;
    case kString:
    case kByteArray:
        std::free(reinterpret_cast<void *>(uintptr_t(mValue[0])));
        break;
    case kList:
        // each item released as its node goes, first to last (the list's _Tidy, 0x46f7f0 / 0x46f6d0)
        for (cIDataListItem *item : Items())
            if (item) item->Release();
        Items().~List();
        break;
    default:
        break;
    }
    mType = kNone;
}

void cDataListItem::SetType(uint32_t type)
{
    if (mType == type) return;
    Clear();
    mType = type;
}

cDataListItem::List &cDataListItem::Items() { return *std::launder(reinterpret_cast<List *>(mValue)); }

void cDataListItem::SetBool(bool value)
{
    SetType(kBool);
    reinterpret_cast<uint8_t *>(mValue)[0] = value;
}

uint8_t cDataListItem::GetBool() { return reinterpret_cast<const uint8_t *>(mValue)[0]; }

void cDataListItem::SetSint32(int32_t value)
{
    SetType(kSint32);
    mValue[0] = uint32_t(value);
}

int32_t cDataListItem::GetSint32() { return int32_t(mValue[0]); }

void cDataListItem::SetFloat32(float value)
{
    SetType(kFloat32);
    std::memcpy(&mValue[0], &value, 4);
}

float cDataListItem::GetFloat32()
{
    float value;
    std::memcpy(&value, &mValue[0], 4);
    return value;
}

void cDataListItem::SetVector2(const float *value)
{
    SetType(kVector2);
    std::memcpy(mValue, value, 8);
}

float *cDataListItem::GetVector2(float *out) { std::memcpy(out, mValue, 8); return out; } // returns out: callers read eax

void cDataListItem::SetVector3(const float *value)
{
    SetType(kVector3);
    std::memcpy(mValue, value, 12);
}

float *cDataListItem::GetVector3(float *out) { std::memcpy(out, mValue, 12); return out; } // returns out: callers read eax

void cDataListItem::SetQuaternion(const float *value)
{
    SetType(kQuaternion);
    std::memcpy(mValue, value, 16);
}

float *cDataListItem::GetQuaternion(float *out) { std::memcpy(out, mValue, 16); return out; } // returns out: callers read eax

void cDataListItem::SetString(const char *text)
{
    Clear(); // a string is always copied afresh, also over a string
    mType = kString;
    mValue[0] = 0;
    if (text == nullptr) return;
    const size_t size = std::strlen(text) + 1;
    void *copy = std::malloc(size);
    std::memcpy(copy, text, size);
    mValue[0] = uint32_t(uintptr_t(copy));
}

const char *cDataListItem::GetString() { return reinterpret_cast<const char *>(uintptr_t(mValue[0])); }

void cDataListItem::SetByteArray(const void *data, uint32_t size)
{
    if (mType == kByteArray && mValue[1] == size) { // same size: copied over the old bytes
        if (size != 0) std::memcpy(reinterpret_cast<void *>(uintptr_t(mValue[0])), data, size);
        return;
    }
    Clear();
    mType = kByteArray;
    mValue[1] = size;
    mValue[0] = 0;
    if (size == 0) return;
    void *copy = std::malloc(size);
    mValue[0] = uint32_t(uintptr_t(copy));
    std::memcpy(copy, data, size);
}

const void *cDataListItem::GetByteArray(uint32_t *size)
{
    *size = mValue[1];
    return reinterpret_cast<const void *>(uintptr_t(mValue[0]));
}

// ---- the list ----

void cDataListItem::MakeList()
{
    if (mType == kList) return;
    Clear();
    mType = kList;
    new (mValue) List();
}

void cDataListItem::Append(cIDataListItem *item)
{
    Items().push_back(item);
    if (item) item->AddRef();
}

cIDataListItem *cDataListItem::GetItem(int32_t index)
{
    List &items = Items();
    auto it = items.begin();
    for (; index > 0 && it != items.end(); --index) ++it;
    // Past the end the original answers the list head's unused value word; ours answers none.
    return it == items.end() ? nullptr : *it;
}

cIDataListItem *cDataListItem::GetItemAt(int32_t index) { return GetItem(index); }

int32_t cDataListItem::IndexOf(cIDataListItem *item)
{
    int32_t index = 0;
    for (cIDataListItem *each : Items()) {
        if (each == item) return index;
        ++index;
    }
    return -1;
}

int32_t cDataListItem::FindByName(const char *name)
{
    int32_t index = 0;
    for (cIDataListItem *each : Items()) {
        const char *own = each->GetName();
        if (own != nullptr && std::strcmp(name, own) == 0) return index;
        ++index;
    }
    return -1;
}

int32_t cDataListItem::AddItem(cIDataListItem *item)
{
    MakeList();
    Append(item);
    return int32_t(Items().size()) - 1;
}

void cDataListItem::RemoveAt(int32_t index)
{
    List &items = Items();
    auto it = items.begin();
    for (; index != 0; --index) { // a negative index walks to the end: nothing removed
        if (it == items.end()) return;
        ++it;
    }
    if (it == items.end()) return;
    cIDataListItem *item = *it;
    items.erase(it);
    if (item) item->Release();
}

void cDataListItem::Remove(cIDataListItem *item)
{
    List &items = Items();
    for (auto it = items.begin(); it != items.end(); ++it) {
        if (*it != item) continue;
        items.erase(it);
        if (item) item->Release();
        return;
    }
}

// The second value word, whatever the type (the list's size for a list).
uint32_t cDataListItem::GetCount() { return mValue[1]; }

cIDataListItem *cDataListItem::NewItem()
{
    void *item = nullptr;
    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())->GetClassObject(kClassId, kIid, &item);
    return static_cast<cIDataListItem *>(item);
}

// ---- the persist writer and reader ----

void cDataListItem::Write(ifaces::cIGZPersistWriter *writer)
{
    if (GetType() == kNone) return;
    writer->SetByte(uint8_t(GetType()));
    writer->WriteRZCharStr(const_cast<char *>(GetName()));
    switch (GetType()) {
    case kBool:
        writer->SetUint8(GetBool());
        break;
    case kSint32:
        writer->SetSint32(GetSint32());
        break;
    case kFloat32:
        writer->SetFloat32(GetFloat32());
        break;
    case kVector2: {
        float v[2];
        GetVector2(v);
        writer->SetFloat32Array(v, 2);
        break;
    }
    case kVector3: {
        float v[3];
        GetVector3(v);
        writer->SetFloat32Array(v, 3);
        break;
    }
    case kString: {
        Text text(OrEmpty(GetString()));
        writer->WriteGZStr(text.Get());
        break;
    }
    case kList:
        writer->SetUint32(uint32_t(Items().size()));
        for (cIDataListItem *item : Items()) item->Write(writer);
        break;
    case kQuaternion: {
        float v[4];
        GetQuaternion(v);
        writer->SetFloat32Array(v, 4);
        break;
    }
    case kByteArray: {
        uint32_t size = 0;
        const void *data = GetByteArray(&size);
        writer->SetUint32(size);
        if (size != 0) writer->SetVoid(const_cast<void *>(data), size);
        break;
    }
    default:
        break;
    }
}

void cDataListItem::Read(ifaces::cIGZPersistReader *reader)
{
    const uint8_t type = reader->GetUint8();
    Text name("");
    reader->GetGZStr(name.Get());
    SetName(name.Chars());
    Clear();
    switch (type) {
    case kBool:
        SetBool(reader->GetBool());
        break;
    case kSint32:
        SetSint32(reader->GetSint32());
        break;
    case kFloat32:
        SetFloat32(reader->GetFloat32());
        break;
    case kVector2: {
        float v[2];
        reader->GetFloat32Array(v, 2);
        SetVector2(v);
        break;
    }
    case kVector3: {
        float v[3];
        reader->GetFloat32Array(v, 3);
        SetVector3(v);
        break;
    }
    case kString: {
        Text text("");
        reader->GetGZStr(text.Get());
        SetString(text.Chars());
        break;
    }
    case kList: {
        MakeList();
        Items().clear(); // (empty: Clear dropped the old list)
        const uint32_t count = reader->GetUint32();
        for (uint32_t i = 0; i < count; ++i) {
            cIDataListItem *item = NewItem();
            item->Read(reader);
            Append(item);
            item->Release();
        }
        break;
    }
    case kQuaternion: {
        float v[4];
        reader->GetFloat32Array(v, 4);
        SetQuaternion(v);
        break;
    }
    case kByteArray: {
        const uint32_t size = reader->GetUint32();
        std::vector<uint8_t> bytes(size);
        if (size != 0) reader->GetVoid(bytes.data(), size);
        SetByteArray(bytes.data(), size);
        break;
    }
    default: // an unknown type: the item stays empty (type none)
        break;
    }
}

// ---- the binary stream ----

void cDataListItem::WriteStream(ifaces::cIGZOStream *stream)
{
    if (GetType() == kNone) return;
    stream->SetUint8(uint8_t(GetType()));
    Text name(OrEmpty(GetName()));
    stream->SetGZStr(name.Get());
    switch (GetType()) {
    case kBool:
        stream->SetUint8(GetBool());
        break;
    case kSint32:
        stream->SetSint32(GetSint32());
        break;
    case kFloat32:
        stream->SetFloat32(GetFloat32());
        break;
    case kVector2: {
        float v[2];
        GetVector2(v);
        for (float f : v) stream->SetFloat32(f);
        break;
    }
    case kVector3: {
        float v[3];
        GetVector3(v);
        for (float f : v) stream->SetFloat32(f);
        break;
    }
    case kString: {
        Text text(OrEmpty(GetString()));
        stream->SetGZStr(text.Get());
        break;
    }
    case kList:
        stream->SetUint32(uint32_t(Items().size()));
        for (cIDataListItem *item : Items()) item->WriteStream(stream);
        break;
    case kQuaternion: {
        float v[4];
        GetQuaternion(v);
        for (float f : v) stream->SetFloat32(f);
        break;
    }
    case kByteArray: {
        uint32_t size = 0;
        const auto *data = static_cast<const uint8_t *>(GetByteArray(&size));
        stream->SetUint32(size);
        for (uint32_t i = 0; i < size; ++i) stream->SetUint8(data[i]);
        break;
    }
    default:
        break;
    }
}

void cDataListItem::ReadStream(ifaces::cIGZIStream *stream)
{
    uint8_t type = 0;
    stream->GetUint8(&type);
    Text name("");
    stream->GetGZStr(name.Get());
    SetName(name.Chars());
    Clear();
    switch (type) {
    case kBool: {
        uint8_t value = 0;
        stream->GetUint8(&value);
        SetBool(value != 0);
        break;
    }
    case kSint32: {
        int32_t value = 0;
        stream->GetSint32(&value);
        SetSint32(value);
        break;
    }
    case kFloat32: {
        float value = 0;
        stream->GetFloat32(&value);
        SetFloat32(value);
        break;
    }
    case kVector2: {
        float v[2] = {};
        for (float &f : v) stream->GetFloat32(&f);
        SetVector2(v);
        break;
    }
    case kVector3: {
        float v[3] = {};
        for (float &f : v) stream->GetFloat32(&f);
        SetVector3(v);
        break;
    }
    case kString: {
        Text text("");
        stream->GetGZStr(text.Get());
        SetString(text.Chars());
        break;
    }
    case kList: {
        uint32_t count = 0;
        stream->GetUint32(&count);
        MakeList();
        Items().clear();
        for (uint32_t i = 0; i < count; ++i) {
            cIDataListItem *item = NewItem();
            item->ReadStream(stream);
            Append(item);
            item->Release();
        }
        break;
    }
    case kQuaternion: {
        float v[4] = {};
        for (float &f : v) stream->GetFloat32(&f);
        SetQuaternion(v);
        break;
    }
    case kByteArray: {
        uint32_t size = 0;
        stream->GetUint32(&size);
        std::vector<uint8_t> bytes(size);
        for (uint32_t i = 0; i < size; ++i) stream->GetUint8(&bytes[i]);
        SetByteArray(bytes.data(), size);
        break;
    }
    default:
        break;
    }
}

// ---- the text form ----

void cDataListItem::Describe(ifaces::cIGZOStream *stream, int32_t indent)
{
    if (GetType() == kNone) return;
    Text margin("");
    for (int32_t level = indent; level > 0; --level) margin->mString.append("   ", 3);
    Text name(OrEmpty(GetName()));
    // "<margin><Type> <name> = " (a list: "<margin>List <name>   ", its count and its items on the lines below)
    stream->SetGZStr(margin.Get());
    stream->SetRZCharStr(kTypeNames[GetType()]);
    stream->SetRZCharStr(" ");
    stream->SetGZStr(name.Get());
    stream->SetRZCharStr(GetType() == kList ? "   " : " = ");
    switch (GetType()) {
    case kBool:
        stream->SetRZCharStr(GetBool() ? "true" : "false");
        break;
    case kSint32:
        stream->SetSint32(GetSint32());
        break;
    case kFloat32:
        stream->SetFloat32(GetFloat32());
        break;
    case kVector2:
    case kVector3:
    case kQuaternion: {
        float v[4];
        uint32_t count = 4;
        if (GetType() == kVector2) GetVector2(v), count = 2;
        else if (GetType() == kVector3) GetVector3(v), count = 3;
        else GetQuaternion(v);
        for (uint32_t i = 0; i < count; ++i) {
            if (i != 0) stream->SetRZCharStr(", ");
            stream->SetFloat32(v[i]);
        }
        break;
    }
    case kString: {
        Text text(OrEmpty(GetString()));
        stream->SetRZCharStr("'");
        stream->SetGZStr(text.Get());
        stream->SetRZCharStr("'");
        stream->SetRZCharStr("\r\n");
        return;
    }
    case kList:
        stream->SetUint32(uint32_t(Items().size()));
        stream->SetRZCharStr("\r\n");
        for (cIDataListItem *item : Items()) item->Describe(stream, indent + 1);
        return;
    case kByteArray: {
        uint32_t size = 0;
        const auto *data = static_cast<const uint8_t *>(GetByteArray(&size));
        stream->SetUint32(size);
        for (uint32_t i = 0; i < size; ++i) {
            stream->SetRZCharStr(" ");
            stream->SetUint8(data[i]);
        }
        break;
    }
    default:
        return;
    }
    stream->SetRZCharStr("\r\n");
}

bool cDataListItem::Parse(ifaces::cIGZIStream *stream)
{
    Text line("");
    stream->GetGZStr(line.Get());
    if (reinterpret_cast<cRZStringHelpers *>(line.Get())->EndsWith("\n", 1, 1)) line->Erase(line.Length() - 1, 1);
    line.TrimBoth();

    // "<Type> <name> = <value>", a list "List <name> <count>"
    Text word;
    line.SplitOff(word);
    const uint8_t type = TypeFromName(word.Chars());
    if (type == kNone) return false;
    Text name;
    line.SplitOff(name);
    if (name.Length() == 0) return false;
    SetName(name.Chars());
    if (type != kList) {
        Text equals; // any one character
        line.SplitOff(equals);
        if (equals.Length() != 1) return false;
    }
    line.TrimBoth();
    Clear();

    switch (type) {
    case kBool:
        SetBool(uint32_t(line->Find("true", 0, true)) < line.Length());
        break;
    case kSint32: {
        int32_t value = 0;
        if (sscanf_s(line.Chars(), "%d", &value) != 1) return false;
        SetSint32(value);
        break;
    }
    case kFloat32: {
        float value = 0;
        if (sscanf_s(line.Chars(), "%f", &value) != 1) return false;
        SetFloat32(value);
        break;
    }
    case kVector2: {
        float v[2] = {};
        if (sscanf_s(line.Chars(), "%f, %f", &v[0], &v[1]) != 2) return false;
        SetVector2(v);
        break;
    }
    case kVector3: {
        float v[3] = {};
        if (sscanf_s(line.Chars(), "%f, %f, %f", &v[0], &v[1], &v[2]) != 3) return false;
        SetVector3(v);
        break;
    }
    case kString: {
        // 'text': from after the first quote to the next one; anything else an empty string
        uint32_t end = 0;
        if (!line->StartsWith("'", 1, true) || (end = uint32_t(line->Find("'", 1, true))) >= line.Length()) {
            SetString("");
            break;
        }
        Text text;
        static_cast<cRZStringSub *>(line.Get())->Mid(text.Out(), 1, end - 1);
        SetString(text.Chars());
        break;
    }
    case kList: {
        int32_t count = 0;
        if (sscanf_s(line.Chars(), "%d", &count) != 1 || count < 0) return false;
        MakeList();
        Items().clear();
        for (int32_t i = 0; i < count; ++i) {
            cIDataListItem *item = NewItem();
            item->Parse(stream); // its answer is not looked at
            Append(item);
            item->Release();
        }
        break;
    }
    case kQuaternion: {
        float v[4] = {};
        if (sscanf_s(line.Chars(), "%f, %f, %f, %f", &v[0], &v[1], &v[2], &v[3]) != 4) return false;
        SetQuaternion(v);
        break;
    }
    case kByteArray: {
        // "<count> <byte> <byte> ..."
        // ORIGINAL-BUG: each byte is split off up to the space after it, and the line was trimmed, so the last byte has
        // none: a line as Describe writes it fails on its last byte. Only an empty array, or a line with more text after
        // the bytes, parses.
        Text first;
        line.SplitOff(first);
        line.TrimBoth();
        int32_t count = 0;
        if (sscanf_s(first.Chars(), "%d", &count) != 1) return false;
        std::vector<uint8_t> bytes(static_cast<uint32_t>(count)); // (a negative count fails the allocation, as the original's)
        for (int32_t i = 0; i < count; ++i) {
            Text token;
            line.SplitOff(token);
            uint32_t value = 0;
            if (sscanf_s(token.Chars(), "%d", &value) != 1 || value > 0xff) return false;
            bytes[i] = uint8_t(value);
            line.TrimBoth();
        }
        SetByteArray(bytes.data(), uint32_t(bytes.size()));
        break;
    }
    default:
        break;
    }
    return true;
}

} // namespace nGZSceneGraph
