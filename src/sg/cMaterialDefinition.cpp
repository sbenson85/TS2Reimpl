// nGZSceneGraph::cMaterialDefinition as a whole class (lane RP). Layout and the switch: src/sg/cMaterialDefinition.h.
#include "sg/cMaterialDefinition.h"

#include "game/game.h"
#include "game/original.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "rz/RZFastFixedAllocators.h"
#include "rz/RZFixedAllocator.h"
#include "sg/SGLocalString.h"
#include "sg/cMaterialAttributes.h"  // OpenNameCell (0xae87d0)
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName

#include <algorithm>
#include <cstring>
#include <new>
#include <string>

namespace nGZSceneGraph {

static_assert(sizeof(cMaterialDefinition) == cMaterialDefinition::kSize, "cMaterialDefinition size (0x58)");
static_assert(offsetof(cMaterialDefinition, mName) == 0x30, "name");
static_assert(offsetof(cMaterialDefinition, mType) == 0x34, "type");
static_assert(offsetof(cMaterialDefinition, mProperties) == 0x38, "properties");
static_assert(offsetof(cMaterialDefinition, mTextures) == 0x44, "textures");

namespace {

// Its slot 24 (thiscall (out, id), ret 8): the texture's record, 8 bytes written to out; returns the record. The first
// word is the texture's name text.
__forceinline const uint32_t *TextureRecord(void *manager, uint32_t *out, uint32_t id)
{
    using Fn = const uint32_t *(__thiscall *)(void *, uint32_t *, uint32_t);
    return (*static_cast<Fn **>(manager))[24](manager, out, id);
}

// A cell's text (Dispatch8, 0xaeb800: the string pool's slot 8).
__forceinline const char *TextOf(const OwnedCell &cell)
{
    return reinterpret_cast<const char *>(Dispatch8(const_cast<OwnedCell *>(&cell)));
}

__forceinline const OwnedCell &KeyOf(const OwnedCell &cell) { return cell; }
__forceinline const OwnedCell &KeyOf(const MaterialProperty &entry) { return entry.mName; }
__forceinline const OwnedCell &KeyOf(const MaterialTexture &entry) { return entry.mName; }

// The lists' order (0xaacea0, std::equal_range): the names' texts, strcmp.
struct TextLess {
    template <class A, class B> bool operator()(const A &a, const B &b) const
    {
        return std::strcmp(TextOf(KeyOf(a)), TextOf(KeyOf(b))) < 0;
    }
};

// `target` set to the cell of `name` opened with `open` (a temporary cell, closed after the assignment).
template <ServiceCell *(*Open)(ServiceCell *, uint32_t)> __forceinline void AssignCell(OwnedCell &target, const char *name)
{
    ServiceCell temp;
    Open(&temp, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name)));
    target.cell.Assign(&temp.mValue);
    temp.Close();
}

// 0xae7340 ((out, text, group, 0), ours: sg/cTextureManager.h): the text of `out` for a "##" text or a group-prefixed
// name.
__forceinline void ResolveGroupText(cRZString *out, const char *text, uint32_t group)
{
    ResolveResourceName(out->AsInterface(), text, group, nullptr);
}

// The reader's slot 28 (the group of the stream being read; the generated interface stops at slot 26).
__forceinline uint32_t ReaderGroup(ifaces::cIGZPersistReader *reader)
{
    using Fn = uint32_t(__thiscall *)(void *);
    return (*reinterpret_cast<Fn **>(reader))[28](reader);
}

// An old file's vector value, "(1 2 3)" or "[1  2]", becomes "1,2,3": with no comma in it, the first and last character go,
// then the first space after each word that another word follows becomes a comma (the rest of a run of spaces stays).
void NormaliseOldVectorText(std::string &value)
{
    if (value.size() < 5) return;
    if (value.front() != '(' && value.front() != '[') return;
    if (value.back() != ')' && value.back() != ']') return;
    if (value.find(',') != std::string::npos) return;
    value.erase(0, 1);
    value.pop_back();
    if (value.empty()) return;
    size_t start = value.find_first_not_of(' ');
    while (start != std::string::npos) {
        const size_t space = value.find(' ', start);
        if (space == std::string::npos) break;
        const size_t next = value.find_first_not_of(' ', space);
        if (next == std::string::npos) break;
        value[space] = ',';
        start = next;
    }
}

} // namespace

// ---- 0xae88d0 ----

ServiceCell *OpenNameCellCaseSensitive(ServiceCell *cell, uint32_t name)
{
    if (name != 0)
        cell->Open(name, 1);
    else
        cell->mValue = 0xffffffffu;
    return cell;
}

// ---- life ----

cMaterialDefinition::cMaterialDefinition() = default;

// The members go in reverse order (the textures, the properties, the type, the name: the game's order), then the
// cSGResource part.
cMaterialDefinition::~cMaterialDefinition() = default;

void *cMaterialDefinition::operator new(size_t size)
{
    if (size != kSize) return ::operator new(size);
    return reinterpret_cast<RZFixedAllocatorBase *>(&ts2_rva_10f6704)->Alloc();
}

void cMaterialDefinition::operator delete(void *block, size_t size)
{
    if (size != kSize) {
        ::operator delete(block);
        return;
    }
    reinterpret_cast<RZFastFixed0x58 *>(&ts2_rva_10f6704)->Free(block);
}

// (The pool's allocation does not fail, so the original's null branch is dead.)
cIMaterialDefinition *__cdecl cMaterialDefinition::Create() { return (new cMaterialDefinition)->Face(); }

// ---- cIGZUnknown ----

bool cMaterialDefinition::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == cIMaterialDefinition::kIid) {
        *out = Face();
        AddRef();
        return true;
    }
    if (iid == kIidSelf) {
        *out = this;
        return true;
    }
    return cSGResource::QueryInterface(iid, out);
}

uint32_t cMaterialDefinition::AddRef() { return cSGResource::AddRef(); }
uint32_t cMaterialDefinition::Release() { return cSGResource::Release(); }

// ---- persistence ----

// The record, the resource part, the name, (before version 8: a string dropped), the type, the properties as {name, value}
// string pairs (the count, then each pair), and from version 9 the texture names. The properties and textures are
// resized to the stored counts first, and the entries are overwritten in place; a texture keeps its handle. Before
// version 10 the lists are sorted afterwards (the old files stored them unsorted). A "textureName" property read from a
// stream with a group has its value resolved in that group (0xae7340, not ours). The value's cell is opened before the
// name's. The textures stay as they were when the version is 8 or less.
void cMaterialDefinition::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cSGResource::Read(reader);
    {
        nTSSG::SGLocalString text;
        reader->GetGZStr(&text.value);
        AssignCell<&OpenNameCell>(mName, text.Text());
    }
    if (info.mVersion < 8) {
        nTSSG::SGLocalString dropped;
        reader->GetGZStr(&dropped.value);
    }
    {
        nTSSG::SGLocalString text;
        reader->GetGZStr(&text.value);
        AssignCell<&OpenNameCellCaseSensitive>(mType, text.Text());
    }

    const uint32_t propertyCount = reader->GetUint32();
    mProperties.resize(propertyCount);
    for (uint32_t i = 0; i < propertyCount; ++i) {
        nTSSG::SGLocalString name;
        nTSSG::SGLocalString value;
        reader->GetGZStr(&name.value);
        reader->GetGZStr(&value.value);
        if (name.value.IsEqual("textureName", 0xffffffffu, false) && ReaderGroup(reader) != 0) {
            nTSSG::SGLocalString resolved;
            ResolveGroupText(&resolved.value, value.Text(), ReaderGroup(reader));
            value.value.mString = resolved.value.mString;
        }
        if (info.mVersion < 11) NormaliseOldVectorText(value.value.mString);
        OwnedCell valueCell;
        OwnedCell nameCell;
        OpenNameCellCaseSensitive(&valueCell.cell, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value.Text())));
        OpenNameCellCaseSensitive(&nameCell.cell, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name.Text())));
        mProperties[i].mName = nameCell;
        mProperties[i].mValue = valueCell;
    }
    if (info.mVersion < 10) std::sort(mProperties.begin(), mProperties.end(), TextLess{});

    if (info.mVersion > 8) {
        const uint32_t textureCount = reader->GetUint32();
        mTextures.resize(textureCount);
        for (uint32_t i = 0; i < textureCount; ++i) {
            nTSSG::SGLocalString text;
            reader->GetGZStr(&text.value);
            AssignCell<&OpenNameCell>(mTextures[i].mName, text.Text());
        }
    }
    if (info.mVersion < 10) std::sort(mTextures.begin(), mTextures.end(), TextLess{});
    if (info.mName) info.mName->Release();
}

cPersistTypeInfo cMaterialDefinition::GetTypeInfo()
{
    cPersistTypeInfo info;
    GetTypeInfoAt(Face(), nullptr, &info);
    return info;
}

cPersistTypeInfo *__fastcall cMaterialDefinition::GetTypeInfoAt(void *, void *, cPersistTypeInfo *out)
{
    out->mName = NewTypeName("cMaterialDefinition");
    out->mWord4 = kClassId;
    out->mVersion = kVersion;
    return out;
}

// The record, the resource part, the name and the type texts, the properties (the count, then each name and its value
// through 0xae7340 with group 0, which strips a leading "##"-style prefix), the texture count and names.
void cMaterialDefinition::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info;
    GetTypeInfoAt(Face(), nullptr, &info);
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    cSGResource::WriteResource(writer);
    writer->WriteRZCharStr(const_cast<char *>(TextOf(mName)));
    writer->WriteRZCharStr(const_cast<char *>(TextOf(mType)));
    writer->SetUint32(static_cast<uint32_t>(mProperties.size()));
    for (const MaterialProperty &property : mProperties) {
        writer->WriteRZCharStr(const_cast<char *>(TextOf(property.mName)));
        nTSSG::SGLocalString text;
        ResolveGroupText(&text.value, TextOf(property.mValue), 0);
        writer->WriteRZCharStr(const_cast<char *>(text.Text()));
    }
    writer->SetUint32(static_cast<uint32_t>(mTextures.size()));
    for (const MaterialTexture &texture : mTextures) writer->WriteRZCharStr(const_cast<char *>(TextOf(texture.mName)));
}

uint32_t cMaterialDefinition::GetOwnerInfo() { return cSGResource::GetOwnerInfo(); }

const void *cMaterialDefinition::GetRecord18() { return "_txmt"; }
const void *cMaterialDefinition::GetRecord19() { return "5tm"; }

// ---- cIMaterialDefinition ----

// A new definition with this one's name, type, properties and textures (not the resource part); uncounted.
cIMaterialDefinition *cMaterialDefinition::Clone()
{
    cMaterialDefinition *const copy = new cMaterialDefinition;
    copy->mName.cell.Assign(&mName.cell.mValue);
    copy->mType.cell.Assign(&mType.cell.mValue);
    copy->mProperties = mProperties;
    copy->mTextures = mTextures;
    return copy->Face();
}

cISGResource *cMaterialDefinition::GetResourceConst() { return static_cast<cISGResource *>(this); }
cISGResource *cMaterialDefinition::GetResource() { return static_cast<cISGResource *>(this); }

void cMaterialDefinition::SetMaterialName(const char *name) { AssignCell<&OpenNameCell>(mName, name); }

// The type goes and the properties are cleared; the name and the textures stay.
void cMaterialDefinition::ClearProperties()
{
    mType.cell.Reset();
    mProperties.clear();
}

void cMaterialDefinition::SetMaterialType(const char *type) { AssignCell<&OpenNameCellCaseSensitive>(mType, type); }

// The value's cell is opened first, then the name's; an existing name takes the new cells, else the pair goes in at its
// sorted place.
void cMaterialDefinition::SetProperty(const char *name, const char *value)
{
    const MaterialProperty pair = [&] {
        OwnedCell valueCell;
        OwnedCell nameCell;
        OpenNameCellCaseSensitive(&valueCell.cell, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value)));
        OpenNameCellCaseSensitive(&nameCell.cell, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name)));
        return MaterialProperty{nameCell, valueCell};
    }();
    const auto range = std::equal_range(mProperties.begin(), mProperties.end(), pair.mName, TextLess{});
    if (range.first == range.second) {
        mProperties.insert(range.first, pair);
    } else {
        range.first->mName = pair.mName;
        range.first->mValue = pair.mValue;
    }
}

// Runs of two or three properties named "<base>X", "<base>Y", "<base>Z" (the same base, in that order, next to each other
// in the sorted list) become one "<base>" with the values joined by commas, unless a property "<base>" is there already:
// then nothing changes and, if its value differs from the joined one, the answer is false. The list is sorted at the end.
// A merge opens the value's cell before the name's, and the entries after the first of the run go.
bool cMaterialDefinition::MergeVectorProperties()
{
    static constexpr char kSuffix[3] = {'X', 'Y', 'Z'};
    bool differs = false;
    uint32_t index = 0;
    // ORIGINAL-BUG: the loop bound is the unsigned `count - 2` (the game's own), so a list of 0 or 1 properties wraps to
    // about 4 billion rounds (each one finds no run), and a list of exactly 2 skips the loop; kept as the game has it.
    if (mProperties.size() != 2) {
        do {
            uint32_t run = 0;
            const char *previous = nullptr;
            size_t previousLength = 0;
            while (index + run < mProperties.size()) {
                const char *const text = TextOf(mProperties[index + run].mName);
                const size_t length = std::strlen(text);
                if (length < 2 || text[length - 1] != kSuffix[run]) break;
                if (previous && (previousLength != length || std::strncmp(text, previous, length - 1) != 0)) break;
                previous = text;
                previousLength = length;
                if (++run > 2) break;
            }
            if (run > 1) {
                std::string base(TextOf(mProperties[index].mName));
                base.pop_back();
                std::string merged(TextOf(mProperties[index].mValue));
                for (uint32_t part = 1; part < run; ++part) {
                    std::string piece(",");
                    piece.append(TextOf(mProperties[index + part].mValue));
                    merged.append(piece);
                }
                const char *const existing = FindProperty(base.c_str());
                if (!existing) {
                    OwnedCell valueCell;
                    OwnedCell nameCell;
                    OpenNameCellCaseSensitive(&valueCell.cell, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(merged.c_str())));
                    OpenNameCellCaseSensitive(&nameCell.cell, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(base.c_str())));
                    mProperties[index].mName = nameCell;
                    mProperties[index].mValue = valueCell;
                    mProperties.erase(mProperties.begin() + index + 1, mProperties.begin() + index + run);
                } else if (std::strcmp(merged.c_str(), existing) != 0) {
                    differs = true;
                }
            }
            ++index;
        } while (index < static_cast<uint32_t>(mProperties.size()) - 2u);
    }
    std::sort(mProperties.begin(), mProperties.end(), TextLess{});
    return !differs;
}

// The texture's name from the manager's record of `id`, and a handle on it; an existing name takes both.
void cMaterialDefinition::AddTextureById(uint32_t id)
{
    MaterialTexture entry;
    {
        void *const manager = TextureManager();
        uint32_t record[2];
        const uint32_t *const found = TextureRecord(manager, record, id);
        ServiceCell temp;
        OpenNameCell(&temp, found[0]);
        entry.mName.cell.Assign(&temp.mValue);
        temp.Close();
    }
    {
        const cTextureHandle handle(id);
        entry.mHandle = handle;
    }
    const auto range = std::equal_range(mTextures.begin(), mTextures.end(), entry.mName, TextLess{});
    if (range.first == range.second) {
        mTextures.insert(range.first, entry);
    } else {
        range.first->mName = entry.mName;
        range.first->mHandle = entry.mHandle;
    }
}

// A texture by name, with no handle; an existing name loses its handle.
void cMaterialDefinition::AddTextureByName(const char *name)
{
    MaterialTexture entry;
    AssignCell<&OpenNameCell>(entry.mName, name);
    const auto range = std::equal_range(mTextures.begin(), mTextures.end(), entry.mName, TextLess{});
    if (range.first == range.second)
        mTextures.insert(range.first, entry);
    else
        range.first->mHandle.Reset();
}

// Equal properties, then the same texture cells and handles (the raw words).
bool cMaterialDefinition::IsEqual(cIMaterialDefinition *other)
{
    if (!IsEqualProperties(other)) return false;
    void *self = other;
    other->QueryInterface(kIidSelf, &self); // unchecked, as the game: IsEqualProperties has seen it answer
    const cMaterialDefinition *const that = static_cast<const cMaterialDefinition *>(static_cast<cSGResource *>(self));
    if (mTextures.size() != that->mTextures.size()) return false;
    for (size_t i = 0; i < mTextures.size(); ++i) {
        if (mTextures[i].mName.cell.mValue != that->mTextures[i].mName.cell.mValue) return false;
        if (mTextures[i].mHandle.mId != that->mTextures[i].mHandle.mId) return false;
    }
    return true;
}

// The same name, type and property cells (the raw words); false when `other` is not a material definition.
bool cMaterialDefinition::IsEqualProperties(cIMaterialDefinition *other)
{
    void *self = nullptr;
    if (!other->QueryInterface(kIidSelf, &self)) return false;
    const cMaterialDefinition *const that = static_cast<const cMaterialDefinition *>(static_cast<cSGResource *>(self));
    if (mName.cell.mValue != that->mName.cell.mValue) return false;
    if (mType.cell.mValue != that->mType.cell.mValue) return false;
    if (mProperties.size() != that->mProperties.size()) return false;
    for (size_t i = 0; i < mProperties.size(); ++i) {
        if (mProperties[i].mName.cell.mValue != that->mProperties[i].mName.cell.mValue) return false;
        if (mProperties[i].mValue.cell.mValue != that->mProperties[i].mValue.cell.mValue) return false;
    }
    return true;
}

const char *cMaterialDefinition::GetMaterialName()
{
    return mName.cell.mValue == 0xffffffffu ? nullptr : TextOf(mName);
}

const char *cMaterialDefinition::GetMaterialType()
{
    return mType.cell.mValue == 0xffffffffu ? nullptr : TextOf(mType);
}

uint32_t cMaterialDefinition::CountProperties() { return static_cast<uint32_t>(mProperties.size()); }

// No bounds check (the game's): the value's text is asked for first.
MaterialPropertyText *cMaterialDefinition::GetProperty(MaterialPropertyText *out, uint32_t index)
{
    const MaterialProperty &entry = mProperties.data()[index];
    const char *const value = TextOf(entry.mValue);
    out->mName = TextOf(entry.mName);
    out->mValue = value;
    return out;
}

// The value of a property by its name's cell (case-sensitive), null when there is none.
const char *cMaterialDefinition::FindProperty(const char *name)
{
    OwnedCell cell;
    OpenNameCellCaseSensitive(&cell.cell, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name)));
    const auto range = std::equal_range(mProperties.begin(), mProperties.end(), cell, TextLess{});
    return range.first != range.second ? TextOf(range.first->mValue) : nullptr;
}

// The value of the first property whose name's text matches ignoring case, null when there is none.
const char *cMaterialDefinition::FindPropertyNoCase(const char *name)
{
    for (const MaterialProperty &entry : mProperties)
        if (_stricmp(TextOf(entry.mName), name) == 0) return TextOf(entry.mValue);
    return nullptr;
}

uint32_t cMaterialDefinition::CountTextures() { return static_cast<uint32_t>(mTextures.size()); }

// No bounds check (the game's).
const char *cMaterialDefinition::GetTextureName(uint32_t index) { return TextOf(mTextures.data()[index].mName); }

} // namespace nGZSceneGraph
