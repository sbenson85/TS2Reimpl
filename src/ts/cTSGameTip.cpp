// cTSGameTip (lane KX). Layout and slots: src/ts/cTSGameTip.h.
#include "ts/cTSGameTip.h"

#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "ts/cTSGameTipService.h" // GetGameTipService

// 0xcb5940: the base part, then the fields (string ids 0, category 0x1000, value 0) and the key's type.
cTSGameTip::cTSGameTip() : mStringIds{0, 0, 0}, mCategory(kDefaultCategory), mValue(0)
{
    cGZPersistResource::SetType(kType);
}

// 0xcb58a0: as above, then the key's instance; the tips CreateTip makes (kind 0x1000) get category 0x11.
cTSGameTip::cTSGameTip(uint32_t instance, uint32_t kind) : cTSGameTip()
{
    cGZPersistResource::SetInstance(instance);
    if (kind == kUserTipKind) mCategory = kUserTipCategory;
}

// 0xcb5a40 (the vtable stores, then the base's destructor); slot 12 (0xcb5ae0) is the compiler's scalar deleting
// destructor around it (delete 0x30 on bit 0).
cTSGameTip::~cTSGameTip() = default;

// 0xcb62d0: cIGZSerializable -> +0x1c, cITSGameTip -> +0x20 (counted through the primary face's AddRef), else the base's
// (1 and cIGZPersistResource -> this).
bool cTSGameTip::QueryInterface(uint32_t riid, void **out)
{
    if (riid == kSerializableIid) {
        *out = static_cast<cIGZSerializable *>(this);
    } else if (riid == kIid) {
        *out = static_cast<cITSGameTip *>(this);
    } else {
        return cGZPersistResource::QueryInterface(riid, out);
    }
    AddRef();
    return true;
}

uint32_t cTSGameTip::AddRef() { return cGZPersistResource::AddRef(); }
uint32_t cTSGameTip::Release() { return cGZPersistResource::Release(); }

// ---- cIGZSerializable ----

// 0xcb6e20: the version (2), the three string ids, the category and the value; the stream's results are ignored.
bool cTSGameTip::Write(ifaces::cIGZOStream *stream)
{
    stream->SetUint16(kVersion);
    stream->SetSint16(mStringIds[0]);
    stream->SetSint16(mStringIds[1]);
    stream->SetSint16(mStringIds[2]);
    stream->SetUint16(mCategory);
    stream->SetUint32(mValue);
    return true;
}

// 0xcb6370: the same fields back; string id 0 only from version 2 on (version 1 records leave it as it is).
bool cTSGameTip::Read(ifaces::cIGZIStream *stream)
{
    uint16_t version = 0;
    stream->GetUint16(&version);
    if (version >= kVersion) stream->GetSint16(&mStringIds[0]);
    stream->GetSint16(&mStringIds[1]);
    stream->GetSint16(&mStringIds[2]);
    stream->GetUint16(&mCategory);
    stream->GetUint32(&mValue);
    return true;
}

uint32_t cTSGameTip::GetGZCLSID() { return kType; } // 0xcb5dc0

// ---- cITSGameTip ----

// 0xcb6080: a tail jump to the primary face's slot 7.
uint32_t cTSGameTip::GetId() { return static_cast<cIGZPersistResource *>(this)->GetInstance(); }

uint32_t cTSGameTip::GetCategory() { return mCategory; }   // folded 0xa32a40
int32_t cTSGameTip::GetStringId0() { return mStringIds[0]; } // 0xcb60d0
int32_t cTSGameTip::GetStringId1() { return mStringIds[1]; } // folded 0xa29260
int32_t cTSGameTip::GetStringId2() { return mStringIds[2]; } // 0xcb6120
uint32_t cTSGameTip::GetValue() { return mValue; }         // folded 0x145770

// 0xcb6090 / 0xcb6130 / 0xcb60e0: string `id` (none for 0) from the game tip service (GetText, slot 6).
bool cTSGameTip::GetString(int16_t id, cIGZString *out)
{
    if (id == 0) return false;
    cITSGameTipService *service = GetGameTipService();
    return service && service->GetText(id, out);
}

bool cTSGameTip::GetString0(cIGZString *out) { return GetString(mStringIds[0], out); } // 0xcb6090
bool cTSGameTip::GetString1(cIGZString *out) { return GetString(mStringIds[1], out); } // 0xcb6130
bool cTSGameTip::GetString2(cIGZString *out) { return GetString(mStringIds[2], out); } // 0xcb60e0

void cTSGameTip::SetCategory(uint16_t category) { mCategory = category; } // folded 0x4350e0
void cTSGameTip::SetStringId0(int16_t id) { mStringIds[0] = id; }        // folded 0x2b7070
void cTSGameTip::SetStringId1(int16_t id) { mStringIds[1] = id; }        // folded 0x664b40
void cTSGameTip::SetStringId2(int16_t id) { mStringIds[2] = id; }        // folded 0x4351b0
void cTSGameTip::SetValue(uint32_t value) { mValue = value; }            // folded 0x149c60
