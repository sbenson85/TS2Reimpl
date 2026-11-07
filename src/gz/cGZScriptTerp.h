#pragma once
// The UI script runtime (lane MC, foundations F5): cGZScriptVocab (COM class 0xba2e7954) and cGZScriptTerp (class
// 0xba2e7953), the script director's two classes (src/rz/COMDirectorData.cpp), in the standalone shape
// (docs/standalone-shape.md). RVAs are 1.18.0.187; the code is the size profile (/O1).
//
// The terp reads the UI's script text: a markup of tags `<id attr=value attr2="text" ...>` and text between them. Each
// tag id, attribute id and unquoted value is looked up in the terp's vocabularies (a stack, the top searched first; ids
// are numbers or names a vocabulary maps to a variant of type 6, a uint32) and reported to the target the caller set:
// the target (cIGZScriptTarget) hears the run begin and end and the text between tags, its interpreting face
// (cIGZScriptInterpretingTarget) each tag's opening, closing and attributes, its compiling face
// (cIGZScriptCompilingTarget) each closed tag with its attribute list. The UI script service (gz/cGZUIScriptService.h)
// is the target the game uses; its ReadScriptNode (0x85bc39) fills the first vocabulary.
//
// Creation: the creators 0x859fe1 (terp) and 0x859ff7 (vocabulary), the director's factories, tail-jump to the
// constructors 0x85a202 and 0x85a288; the terp's constructor also builds a vocabulary. Nothing else calls the
// constructors or stores the vtables (0xe47e50, 0xe47e28) but the destructors 0x85a2c7 and 0x85a31d, so with the two
// creators hooked every object carries ours and the slots are in effect through those callers.
#include "gz/cGZUIScriptService.h" // cIGZScriptTarget and the two target faces
#include "gz/cGZDefaultAllocator.h"
#include "gz/cIGZAllocator.h"
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

class cIGZString;

// The game's cGZArray-like container over a cIGZAllocator (24 bytes): begin, end, capacity end, a spare word, the
// allocator (a new cGZDefaultAllocator, counted) and a spare word. The constructors (0x85a19a for 8-byte elements,
// 0x85bf91 for 4-byte ones) reserve 8 elements; push_back grows to twice the size (1 when empty); the destructor
// (0x85a2a1, shared by both) hands the block back and releases the allocator. The elements are copied as words.
template <class T> struct cGZArray {
    static constexpr uint32_t kInitialCount = 8;

    T *mBegin;
    T *mEnd;
    T *mCapacity;
    uint32_t mSpare0c;
    cIGZAllocator *mAllocator;
    uint32_t mSpare14;

    cGZArray();
    ~cGZArray();
    cGZArray(const cGZArray &) = delete;
    cGZArray &operator=(const cGZArray &) = delete;

    uint32_t Size() const { return static_cast<uint32_t>(mEnd - mBegin); }
    // 0x85ad26 / 0x85c365 (with the growth paths 0x85abdf / 0x85c238).
    void PushBack(const T &value);
    // 0x85a535 / 0x85c127: [first, last) removed, the tail moved down.
    void Erase(T *first, T *last);
};

template <class T> inline cGZArray<T>::cGZArray() : mBegin(nullptr), mEnd(nullptr), mCapacity(nullptr), mSpare0c(0), mSpare14(0)
{
    mAllocator = new cGZDefaultAllocator;
    mAllocator->AddRef();
    mBegin = static_cast<T *>(mAllocator->Allocate(kInitialCount * sizeof(T)));
    mEnd = mBegin;
    mCapacity = mBegin + kInitialCount;
}

template <class T> inline cGZArray<T>::~cGZArray()
{
    mAllocator->Deallocate(mBegin);
    mCapacity = nullptr;
    mEnd = nullptr;
    mBegin = nullptr;
    mAllocator->Release();
    mAllocator = nullptr;
}

template <class T> inline void cGZArray<T>::PushBack(const T &value)
{
    if (mEnd != mCapacity) {
        *mEnd++ = value;
        return;
    }
    const uint32_t size = Size();
    const uint32_t count = size ? size * 2 : 1;
    T *const block = static_cast<T *>(mAllocator->Allocate(count * sizeof(T)));
    T *out = block;
    for (T *p = mBegin; p != mEnd; ++p) *out++ = *p;
    *out++ = value;
    mAllocator->Deallocate(mBegin);
    mBegin = block;
    mEnd = out;
    mCapacity = block + count;
}

template <class T> inline void cGZArray<T>::Erase(T *first, T *last)
{
    T *out = first;
    for (T *p = last; p != mEnd; ++p) *out++ = *p;
    mEnd -= last - first;
}

// One attribute of a tag (8 bytes): its id and value (counted while in the list). The compiling face's slot 3 gets the
// terp's list of them (cGZUIScriptService reads it as cGZUIScriptArgs: begin and end).
struct cGZScriptAttribute {
    uint32_t mId;
    cIGZUnknown *mValue;
};
static_assert(sizeof(cGZScriptAttribute) == 8, "cGZScriptAttribute size");
using cGZScriptAttributes = cGZArray<cGZScriptAttribute>;
static_assert(sizeof(cGZScriptAttributes) == 0x18, "cGZArray size");

// cIGZScriptVocab (iid 0x1a2bdebb): names (case-insensitive) mapped to counted objects.
class cIGZScriptVocab : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x1a2bdebb;
    // slot 3 (ret 8): `name` mapped to a new uint32 variant of `id`; false (and the variant deleted) when the name is
    // there already.
    virtual bool AddId(const cIGZString &name, uint32_t id) = 0;
    // slot 4 (ret 8): `name` mapped to `object` (counted); false when the name is there already.
    virtual bool AddObject(const cIGZString &name, cIGZUnknown *object) = 0;
    // slot 5 (ret 4): the name's object released and the name removed; false when there was none.
    virtual bool Remove(const cIGZString &name) = 0;
    // slot 6 (ret 8): the name's object, counted, in *out; false (out untouched) when there is none.
    virtual bool Lookup(const cIGZString &name, cIGZUnknown **out) = 0;
    // slot 7: every object released, the map emptied; true.
    virtual bool Clear() = 0;
};

// The vocabulary's name order: the original's comparator 0x725865 (`_stricmp` of the two texts, as C strings).
struct cGZScriptNameLess {
    bool operator()(const std::string &a, const std::string &b) const;
};

class cGZScriptVocab : public cIGZScriptVocab {
public:
    static constexpr uint32_t kVTable = 0xe47e28; // the game's, for tests
    static constexpr uint32_t kClassId = 0xba2e7954;
    static constexpr uint32_t kSize = 0x10;

    cGZScriptVocab() = default; // 0x85a288
    // 0x85a31d: Clear, then the map's nodes freed. Slot 8 the deleting destructor 0x85a355.
    virtual ~cGZScriptVocab();
    cGZScriptVocab(const cGZScriptVocab &) = delete;
    cGZScriptVocab &operator=(const cGZScriptVocab &) = delete;

    // 0x859ff7 (cdecl, the script director's factory): a new vocabulary (null when new failed), not counted.
    static cIGZScriptVocab *__cdecl Create();

    bool QueryInterface(uint32_t riid, void **out) override; // 0x85ad9c: 1 and kIID, counted
    uint32_t AddRef() override;                              // shared 0x148fb0
    uint32_t Release() override;                             // shared_vf_85adc0

    bool AddId(const cIGZString &name, uint32_t id) override;                 // 0x85a3b3
    bool AddObject(const cIGZString &name, cIGZUnknown *object) override;    // 0x85a430
    bool Remove(const cIGZString &name) override;                            // 0x85aded
    bool Lookup(const cIGZString &name, cIGZUnknown **out) override;         // 0x85ab13
    bool Clear() override;                                                   // 0x85a482

    uint32_t mRefCount = 0;                                       // +4
    std::map<std::string, cIGZUnknown *, cGZScriptNameLess> mNames; // +8 (the original's keys are cRZStrings)
};
static_assert(sizeof(cGZScriptVocab) == cGZScriptVocab::kSize, "cGZScriptVocab size");

// cIGZScriptTerp (iid 0x5a2c1570).
class cIGZScriptTerp : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x5a2c1570;
    // slot 3 (ret 4): the three targets released; with `target`, it counted and its interpreting (0x1a2bdeb9) and
    // compiling (0x1a2bdeba) faces asked for. True.
    virtual bool SetTarget(cIGZScriptTarget *target) = 0;
    virtual bool SetContext(cIGZUnknown *context) = 0; // slot 4 (ret 4): counted; true
    virtual cIGZUnknown *GetContext() = 0;             // slot 5 (shared 0x15e550): not counted
    // slots 6-8 (ret 4): a run over the text, the file at `path` or the resource `key`; true when the text parsed.
    virtual bool RunText(const cIGZString &text) = 0;
    virtual bool RunFile(const cIGZString &path) = 0;
    virtual bool RunResource(const void *key) = 0;
    // slots 9-12: the top vocabulary's AddId (as AddObject of a new uint32 variant), AddObject, Remove, Clear.
    virtual bool AddId(const cIGZString &name, uint32_t id) = 0;
    virtual bool AddObject(const cIGZString &name, cIGZUnknown *object) = 0;
    virtual bool Remove(const cIGZString &name) = 0;
    virtual bool Clear() = 0;
    // slot 13 (ret 4): `vocab` counted and pushed (searched first); false for null.
    virtual bool PushVocab(cIGZScriptVocab *vocab) = 0;
    // slot 14: the top vocabulary released and popped, unless it is the last; true when one was popped.
    virtual bool PopVocab() = 0;
};

class cGZScriptTerp : public cIGZScriptTerp {
public:
    static constexpr uint32_t kVTable = 0xe47e50; // the game's, for tests
    static constexpr uint32_t kClassId = 0xba2e7953;
    static constexpr uint32_t kSize = 0x5c;

    // The parser's states (+0x24); 0 between runs.
    enum State : int32_t {
        kIdle = 0,
        kText = 1,       // between tags: text goes to the target
        kTagStart = 2,   // after '<'
        kInTag = 3,      // after the tag id or an attribute's value
        kAttrName = 4,   // after an attribute id
        kAttrValue = 5,  // after '='
        kSkipTag = 6,    // an unknown tag id: up to '>'
        kSkipAttr = 7,   // after an unknown attribute id
        kSkipValue = 8,  // after '=' of an unknown attribute
        kDone = 9,       // (never set)
        kError = 10,
    };
    // The tokenizer's kinds.
    enum Token : int32_t { kTokText = 0, kTokWord = 1, kTokOpen = 2, kTokClose = 3, kTokEquals = 4 };

    // 0x85a202: zeroed, the attribute list made, a new vocabulary pushed.
    cGZScriptTerp();
    // 0x85a2c7: the pending value released, the targets dropped, the vocabularies popped and the last one released.
    // Slot 15 the deleting destructor 0x85a336.
    virtual ~cGZScriptTerp();
    cGZScriptTerp(const cGZScriptTerp &) = delete;
    cGZScriptTerp &operator=(const cGZScriptTerp &) = delete;

    // 0x859fe1 (cdecl, the script director's factory): a new terp (null when new failed), not counted.
    static cIGZScriptTerp *__cdecl Create();

    bool QueryInterface(uint32_t riid, void **out) override; // 0x85ad78: 1 and kIID, counted
    uint32_t AddRef() override;                              // shared 0x148fb0
    uint32_t Release() override;                             // shared 0x1af4d8

    bool SetTarget(cIGZScriptTarget *target) override;                       // 0x85ae66
    bool SetContext(cIGZUnknown *context) override;                          // 0x85ae41
    cIGZUnknown *GetContext() override;                                      // shared 0x15e550
    bool RunText(const cIGZString &text) override;                           // 0x85a8e9
    bool RunFile(const cIGZString &path) override;                           // 0x85a76b
    bool RunResource(const void *key) override;                              // 0x85a801
    bool AddId(const cIGZString &name, uint32_t id) override;                // 0x85a374
    bool AddObject(const cIGZString &name, cIGZUnknown *object) override;   // 0x85a3a8
    bool Remove(const cIGZString &name) override;                           // 0x85ade2
    bool Clear() override;                                                  // 0x85add7
    bool PushVocab(cIGZScriptVocab *vocab) override;                        // 0x85ad51
    bool PopVocab() override;                                               // 0x85acfe

    // The parser (thiscall members of the original, each returning true unless said).
    bool Run();                                                   // 0x85a563: over mText
    bool NextToken(cIGZString **out, int32_t *kind, bool textMode); // 0x85a939
    bool LookupName(const cIGZString &name, cIGZUnknown **out);   // 0x85a903: top vocabulary first
    bool ParseId(const cIGZString &token, uint32_t *id);          // 0x85ab63
    bool BeginRun();                                              // 0x85b1d2
    bool OnText(cIGZString *token);                               // 0x85b0b1
    bool BeginTag(const cIGZString &token);                       // 0x85b0c9: false for an unknown id
    bool BeginAttribute(const cIGZString &token);                 // 0x85b10c: false for an unknown id
    bool FlushAttribute();                                        // 0x85b045
    bool CloseTag();                                              // 0x85aff3
    bool StringValue(const cIGZString &token);                    // 0x85b12e
    bool WordValue(const cIGZString &token);                      // 0x85b167
    bool ClearAttributes();                                       // 0x85a4b8
    bool EndRunFailed();                                          // 0x85b02a
    bool EndRunSucceeded();                                       // 0x85b1e8

    uint32_t mRefCount = 0;                                  // +0x04
    cIGZScriptTarget *mTarget = nullptr;                     // +0x08 counted
    cIGZScriptInterpretingTarget *mInterpreting = nullptr;   // +0x0c counted (the target's face)
    cIGZScriptCompilingTarget *mCompiling = nullptr;         // +0x10 counted (the target's face)
    std::vector<cIGZScriptVocab *> mVocabs;                  // +0x14 counted; the back is the top
    cIGZUnknown *mContext = nullptr;                         // +0x20 counted
    int32_t mState = kIdle;                                  // +0x24
    const char *mText = nullptr;                             // +0x28 the text of the run
    const char *mCursor = nullptr;                           // +0x2c
    // +0x30 and +0x34: left as the memory had them by the constructor (as the original's, 0x85a202), set by BeginRun
    // and BeginTag before any read.
    bool mTagOpen;                                           // +0x30
    uint32_t mTagId;                                         // +0x34
    bool mAttrPending = false;                               // +0x38
    uint32_t mAttrId = 0;                                    // +0x3c
    cIGZUnknown *mAttrValue = nullptr;                       // +0x40 counted
    cGZScriptAttributes mAttributes;                         // +0x44
};
static_assert(sizeof(cGZScriptTerp) == cGZScriptTerp::kSize, "cGZScriptTerp size");
static_assert(offsetof(cGZScriptTerp, mVocabs) == 0x14, "cGZScriptTerp layout");
static_assert(offsetof(cGZScriptTerp, mState) == 0x24, "cGZScriptTerp layout");
static_assert(offsetof(cGZScriptTerp, mAttrValue) == 0x40, "cGZScriptTerp layout");
static_assert(offsetof(cGZScriptTerp, mAttributes) == 0x44, "cGZScriptTerp layout");

// 0x85aed3 (cdecl, returns `out`): `in` with the five XML entities (&lt; &gt; &amp; &quot; &apos;) replaced, scanning
// on after each replacement.
std::string DecodeScriptEntities(const std::string &in);
