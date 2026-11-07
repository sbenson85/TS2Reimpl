#pragma once
#include "rz/cRZString.h"
#include <vector>

class TreeParameterFlags : public std::vector<uint8_t> {
public:
    // 0x41bb70 (assign bytes) is std::vector<uint8_t>::assign: library code, called directly.
};
static_assert(sizeof(TreeParameterFlags) == 12);

// cITSTreeProperties at +0, cIGZPersistResource at +4; deleting destructor frees 0x60 bytes.
class cTSTreeProperties {
public:
    cTSTreeProperties *Construct(void *owner, uint16_t tree); // 0x41bcd0: initialize and create both name tables
    void Destruct() noexcept; // 0x41be70 body: release vector, description, tables and owner
    cTSTreeProperties *DeletingDestructor(uint8_t flags) noexcept; // 0x41be70: destroy and optionally free this
    uint8_t QueryInterface(uint32_t id, void **out); // 0x41c740: expose primary or persist interface and AddRef; returns AL, upper EAX unspecified
    uint32_t Release(); // 0x41ca70: decrement or dispatch deletion for counts zero and one
    uint32_t GetGZCLSID(); // 0x41c3d0: return TPRP resource class identifier
    uint8_t Initialize(void *owner, uint16_t tree); // 0x41c620: replace owner and create name tables; returns AL=1
    void CopyFrom(void *other); // 0x41bfb0: query and copy another tree's editable properties
    bool Equals(void *other); // 0x41c0e0: compare serialized properties and names (except field5C)
    void LoadFromService(); // 0x41c6f0: load tree properties and apply missing-resource defaults
    void SaveToService(); // 0x41cb90: save tree properties and register the resource key
    bool Read(void *stream); // 0x41c7a0: load versioned names and parameter flags
    bool Write(void *stream); // 0x41d0b0: serialize version 85
    void InitializeParameters(); // 0x41cc30: insert four default parameter names and flags
    void *GetTree(); // 0x41c3b0: ask owner for this tree
    void GetScopeName(int32_t scope, cIGZString *out); // 0x41c570: write parameter/local scope label
    int32_t GetNameCount(int32_t scope); // 0x41c0b0: dispatch count through selected table
    void GetName(int32_t scope, int16_t index, cIGZString *out); // 0x41c3e0: copy a name to out, empty for missing entries
    void SetName(int32_t scope, int16_t index, cIGZString *text); // 0x41cf20: store supplied name, or format an empty name as its index
    void RemoveName(int32_t scope, int16_t index); // 0x41caa0: erase the last name or blank an interior name
    bool GetParameterFlag(int16_t index); // 0x41c5b0: default true for indices outside the flags
    void SetParameterFlag(int16_t index, uint8_t value); // 0x41d070: store flag when index exists
    void ClearFlag23(); // 0x41d0a0: clear the serialized flag
    void SetDescription(const cIGZString *text); // 0x41cf00: copy description and mark modified
    void *GetNameTable(int32_t scope); // 0x41c5e0: select parameter/local table, or null

    const void *mVTable; // +0x00
    const void *mPersistVTable; // +0x04
    uint32_t mRefCount; // +0x08
    uint32_t mPersistFields[4]; // +0x0c..0x18: initialized to zero; further meanings unknown
    void *mOwner; // +0x1c
    uint16_t mTreeId; // +0x20
    uint8_t mModified; // +0x22
    uint8_t mFlag23; // +0x23: untouched by constructor
    uint32_t mField24; // +0x24: serialized from version 4
    void *mLocalNames; // +0x28
    void *mParameterNames; // +0x2c
    cRZString mDescription; // +0x30
    TreeParameterFlags mParameterFlags; // +0x50: helper 0x41d2b0 is library insert(count, value)
    uint32_t mField5C; // +0x5c: defaults to 5; serialized from version 3
};
static_assert(sizeof(cTSTreeProperties) == 0x60);
static_assert(offsetof(cTSTreeProperties, mVTable) == 0x00);
static_assert(offsetof(cTSTreeProperties, mPersistVTable) == 0x04);
static_assert(offsetof(cTSTreeProperties, mRefCount) == 0x08);
static_assert(offsetof(cTSTreeProperties, mPersistFields) == 0x0c);
static_assert(offsetof(cTSTreeProperties, mOwner) == 0x1c);
static_assert(offsetof(cTSTreeProperties, mTreeId) == 0x20);
static_assert(offsetof(cTSTreeProperties, mModified) == 0x22);
static_assert(offsetof(cTSTreeProperties, mFlag23) == 0x23);
static_assert(offsetof(cTSTreeProperties, mLocalNames) == 0x28);
static_assert(offsetof(cTSTreeProperties, mField24) == 0x24);
static_assert(offsetof(cTSTreeProperties, mParameterNames) == 0x2c);
static_assert(offsetof(cTSTreeProperties, mDescription) == 0x30);
static_assert(offsetof(cTSTreeProperties, mParameterFlags) == 0x50);
static_assert(offsetof(cTSTreeProperties, mField5C) == 0x5c);
