// nGZSceneGraph::cDataListExtension (lane OY). Layout and the hook decision: src/sg/cDataListExtension.h.
#include "sg/cDataListExtension.h"

#include "game/game.h"
#include "gz/cIGZCOM.h"
#include "gz/cIGZString.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject: the COM
#include "rz/cRZString.h"
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName

#include <cstring>

namespace nGZSceneGraph {

static_assert(sizeof(cDataListExtension) == 0x30, "cDataListExtension size (the factory and Clone allocate 0x30)");

namespace {

// A new list item from the COM director (class 0x6a7b33c3, interface 0x6a7b33bb), counted once; null if it makes none.
cIDataListItem *NewItem()
{
    void *item = nullptr;
    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())->GetClassObject(cDataListItem::kClassId, cIDataListItem::kIid, &item);
    return static_cast<cIDataListItem *>(item);
}

// The value byte of a bool item passed on as it is (the original pushes the byte GetBool returned; a bool conversion
// would turn a stored 2 into 1).
bool RawBool(uint8_t byte)
{
    bool value;
    std::memcpy(&value, &byte, 1);
    return value;
}

// The named setters' shape (0xa99430 .. 0xa99910): the found item set; else a new item named, set, added (counted by
// the list) and the setter's own reference dropped.
template <class Set> void SetNamed(cIDataListItem *list, const char *name, Set &&set)
{
    const int32_t index = list->FindByName(name);
    if (index != -1) {
        set(list->GetItem(index));
        return;
    }
    cIDataListItem *const item = NewItem();
    item->SetName(name);
    set(item);
    list->AddItem(item);
    if (item) item->Release();
}

// The named getters' shape (0xa98b50 .. 0xa99f30): the item called `name` if it is there with `type`, else null.
cIDataListItem *FindTyped(cIDataListItem *list, const char *name, uint32_t type)
{
    const int32_t index = list->FindByName(name);
    if (index == -1) return nullptr;
    if (list->GetItem(index)->GetType() != type) return nullptr;
    return list->GetItem(index);
}

} // namespace

// 0xa990b0: new and the constructor (most-derived), answered as the cIExtension (reached through the
// cIDataListExtension's own virtual base, the same subobject).
cIGZUnknown *__cdecl cDataListExtension::Create()
{
    cDataListExtension *const extension = new cDataListExtension;
    cIExtension *const face = static_cast<cIDataListExtension *>(extension);
    return face;
}

// 0xa98950: cExtension's part (0xa3b7d0), then the list item through the COM director, made a list (slot 28).
cDataListExtension::cDataListExtension()
{
    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())
        ->GetClassObject(cDataListItem::kClassId, cIDataListItem::kIid, reinterpret_cast<void **>(&mItem));
    mItem->MakeList();
}

// 0xa98840: cExtension's copy (0xa3b750: nothing copied), a new list item as the constructor's, then `other`'s list
// copied into it (0xa98c90).
cDataListExtension::cDataListExtension(const cDataListExtension &other) : cExtension(other)
{
    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())
        ->GetClassObject(cDataListItem::kClassId, cIDataListItem::kIid, reinterpret_cast<void **>(&mItem));
    mItem->MakeList();
    CopyList(other.mItem, mItem);
}

// 0xa98a50: every item removed (0xa993f0, last first), the list item released, then cExtension's destructor.
cDataListExtension::~cDataListExtension()
{
    cDataListExtension::RemoveAll();
    if (cIDataListItem *const item = mItem) {
        mItem = nullptr;
        item->Release();
    }
}

// 0xa98c90: `to` named as `from`; each of `from`'s items (slot 30) copied into a new item by its type (a list item made
// a list and copied the same way) and added to `to`; an item of no known type is added unset, with its name.
// ORIGINAL-BUG: an unnamed item (an empty name reads back null) is named with SetName(null), which the item's SetName
// (0xac6ae0) dereferences: cloning a data-list extension whose list or any item in it has no name faults.
void cDataListExtension::CopyList(cIDataListItem *from, cIDataListItem *to)
{
    to->SetName(from->GetName());
    const int32_t count = int32_t(from->GetCount());
    for (int32_t i = 0; i < count; ++i) {
        cIDataListItem *const copy = NewItem();
        cIDataListItem *const item = from->GetItemAt(i);
        copy->SetName(item->GetName());
        switch (item->GetType()) {
        case cIDataListItem::kBool: copy->SetBool(RawBool(item->GetBool())); break;
        case cIDataListItem::kSint32: copy->SetSint32(item->GetSint32()); break;
        case cIDataListItem::kFloat32: copy->SetFloat32(item->GetFloat32()); break;
        case cIDataListItem::kVector2: {
            float value[2];
            copy->SetVector2(item->GetVector2(value));
            break;
        }
        case cIDataListItem::kVector3: {
            float value[3];
            copy->SetVector3(item->GetVector3(value));
            break;
        }
        case cIDataListItem::kString: copy->SetString(item->GetString()); break;
        case cIDataListItem::kList:
            copy->MakeList();
            CopyList(item, copy);
            break;
        case cIDataListItem::kQuaternion: {
            float value[4];
            copy->SetQuaternion(item->GetQuaternion(value));
            break;
        }
        case cIDataListItem::kByteArray: {
            uint32_t size = 0;
            const void *const data = item->GetByteArray(&size);
            copy->SetByteArray(data, size);
            break;
        }
        default: break;
        }
        to->AddItem(copy);
        if (copy) copy->Release();
    }
}

// ---- the referent face ----

// 0xa992e0 (ret 8): a null out answers false; kIid the cIDataListExtension face, counted (through the referent face's
// AddRef: the one count); anything else cExtension's 0xa3be60.
bool cDataListExtension::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid != cIDataListExtension::kIid) return cExtension::QueryInterface(iid, out);
    *out = static_cast<cIDataListExtension *>(this);
    cExtension::AddRef();
    return true;
}

// 0xa99330 (ret 4): the class's type-info record read (and its name released last), then cExtension's (0x9dc310), then
// the list item's own Read (slot 7).
void cDataListExtension::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cExtension::Read(reader);
    mItem->Read(reader);
    if (info.mName) info.mName->Release();
}

// 0xa99dd0 (hidden out): {"cDataListExtension" (a new counted cRZString), the class id, version 1}.
cPersistTypeInfo cDataListExtension::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cDataListExtension");
    info.mWord4 = kClassId;
    info.mVersion = kVersion;
    return info;
}

// 0xa99fa0 (ret 4): the class's record (0xa99dd0 directly) through the writer's slot 28, its name released; then
// cExtension's Write (0xa3c210), then the list item's Write (slot 6).
void cDataListExtension::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cDataListExtension::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    cExtension::WriteExtension(writer);
    mItem->Write(writer);
}

// ---- cIExtension ----

// 0xa98c10: a copy (the copy constructor 0xa98840), uncounted, as its cIExtension.
cIExtension *cDataListExtension::Clone()
{
    cDataListExtension *const copy = new cDataListExtension(*this);
    return static_cast<cIDataListExtension *>(copy);
}

// 0xa99db0 (ret 4): kIid, else cExtension's 0xa3c130.
bool cDataListExtension::IsKindOf(uint32_t iid)
{
    if (iid == cIDataListExtension::kIid) return true;
    return cExtension::IsKindOf(iid);
}

// 0xa99030: the extension's type name.
const char *cDataListExtension::GetTypeName() { return "data_list"; }

// 0xa98f50 (ret 4): "(%s n%d)" of the list's name and count (asked count first) into a local string, then the text's
// operator= (slot 16) from it.
void cDataListExtension::GetDescription(cIGZString *text)
{
    const uint32_t count = mItem->GetCount();
    const char *const name = mItem->GetName();
    cRZString description;
    description.Construct();
    description.Sprintf("(%s n%d)", name, count);
    *text = *description.AsInterface();
    description.Destruct();
}

// ---- cIDataListExtension ----

void cDataListExtension::SetListName(const char *name) { mItem->SetName(name); }

void cDataListExtension::SetBool(const char *name, bool value)
{
    SetNamed(mItem, name, [&](cIDataListItem *item) { item->SetBool(value); });
}

void cDataListExtension::SetSint32(const char *name, int32_t value)
{
    SetNamed(mItem, name, [&](cIDataListItem *item) { item->SetSint32(value); });
}

void cDataListExtension::SetFloat32(const char *name, float value)
{
    SetNamed(mItem, name, [&](cIDataListItem *item) { item->SetFloat32(value); });
}

void cDataListExtension::SetVector2(const char *name, const float *value)
{
    SetNamed(mItem, name, [&](cIDataListItem *item) { item->SetVector2(value); });
}

void cDataListExtension::SetVector3(const char *name, const float *value)
{
    SetNamed(mItem, name, [&](cIDataListItem *item) { item->SetVector3(value); });
}

void cDataListExtension::SetString(const char *name, const char *text)
{
    SetNamed(mItem, name, [&](cIDataListItem *item) { item->SetString(text); });
}

void cDataListExtension::SetQuaternion(const char *name, const float *value)
{
    SetNamed(mItem, name, [&](cIDataListItem *item) { item->SetQuaternion(value); });
}

void cDataListExtension::SetByteArray(const char *name, const void *data, uint32_t size)
{
    SetNamed(mItem, name, [&](cIDataListItem *item) { item->SetByteArray(data, size); });
}

void cDataListExtension::MakeList(const char *name)
{
    SetNamed(mItem, name, [](cIDataListItem *item) { item->MakeList(); });
}

// 0xa993b0 (ret 4): the item called `name` removed (slot 34), if there is one.
void cDataListExtension::Remove(const char *name)
{
    const int32_t index = mItem->FindByName(name);
    if (index != -1) mItem->RemoveAt(index);
}

// 0xa993f0: from the last item to the first, each removed by index.
void cDataListExtension::RemoveAll()
{
    for (int32_t i = int32_t(mItem->GetCount()) - 1; i >= 0; --i) mItem->RemoveAt(i);
}

const char *cDataListExtension::GetListName() { return mItem->GetName(); }

bool cDataListExtension::GetBool(const char *name, bool *out)
{
    cIDataListItem *const item = FindTyped(mItem, name, cIDataListItem::kBool);
    if (!item) return false;
    const uint8_t byte = item->GetBool();
    std::memcpy(out, &byte, 1);
    return true;
}

bool cDataListExtension::GetSint32(const char *name, int32_t *out)
{
    cIDataListItem *const item = FindTyped(mItem, name, cIDataListItem::kSint32);
    if (!item) return false;
    *out = item->GetSint32();
    return true;
}

bool cDataListExtension::GetFloat32(const char *name, float *out)
{
    cIDataListItem *const item = FindTyped(mItem, name, cIDataListItem::kFloat32);
    if (!item) return false;
    *out = item->GetFloat32();
    return true;
}

// The vector getters copy the floats the item's getter answers (its out pointer) as words.
bool cDataListExtension::GetVector2(const char *name, float *out)
{
    cIDataListItem *const item = FindTyped(mItem, name, cIDataListItem::kVector2);
    if (!item) return false;
    float value[2];
    std::memcpy(out, item->GetVector2(value), sizeof value);
    return true;
}

bool cDataListExtension::GetVector3(const char *name, float *out)
{
    cIDataListItem *const item = FindTyped(mItem, name, cIDataListItem::kVector3);
    if (!item) return false;
    float value[3];
    std::memcpy(out, item->GetVector3(value), sizeof value);
    return true;
}

bool cDataListExtension::GetQuaternion(const char *name, float *out)
{
    cIDataListItem *const item = FindTyped(mItem, name, cIDataListItem::kQuaternion);
    if (!item) return false;
    float value[4];
    std::memcpy(out, item->GetQuaternion(value), sizeof value);
    return true;
}

// 0xa99d30 (ret 8): the text's FromChar (slot 4) of the value, or of the image's empty string for a null value.
bool cDataListExtension::GetString(const char *name, cIGZString *out)
{
    cIDataListItem *const item = FindTyped(mItem, name, cIDataListItem::kString);
    if (!item) return false;
    const char *const text = item->GetString();
    out->FromChar(text ? text : ts2_rva_d9b9d0);
    return true;
}

// 0xa98bb0 (ret 0xc): the item's own data pointer (its size through `size`).
bool cDataListExtension::GetByteArray(const char *name, const void **data, uint32_t *size)
{
    cIDataListItem *const item = FindTyped(mItem, name, cIDataListItem::kByteArray);
    if (!item) return false;
    *data = item->GetByteArray(size);
    return true;
}

// 0xa991b0 (ret 8): the list item called `name`, counted.
bool cDataListExtension::GetList(const char *name, cIDataListItem **out)
{
    cIDataListItem *const item = FindTyped(mItem, name, cIDataListItem::kList);
    if (!item) return false;
    item->AddRef();
    *out = item;
    return true;
}

const char *cDataListExtension::GetItemName(int32_t index) { return mItem->GetItem(index)->GetName(); }

uint32_t cDataListExtension::GetType(const char *name)
{
    const int32_t index = mItem->FindByName(name);
    if (index == -1) return 0;
    return mItem->GetItem(index)->GetType();
}

uint32_t cDataListExtension::GetItemType(int32_t index) { return mItem->GetItem(index)->GetType(); }

// 0xa99160 (ret 8): false at or past the count (unsigned); else the item, counted.
bool cDataListExtension::GetItem(uint32_t index, cIDataListItem **out)
{
    if (index >= mItem->GetCount()) return false;
    mItem->GetItem(int32_t(index))->AddRef();
    *out = mItem->GetItem(int32_t(index));
    return true;
}

uint32_t cDataListExtension::GetCount() { return mItem->GetCount(); }

} // namespace nGZSceneGraph
