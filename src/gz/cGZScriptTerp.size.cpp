// cGZScriptVocab and cGZScriptTerp (lane MC): src/gz/cGZScriptTerp.h. Built for size, as the game's (0x85a06d..0x85b1e8
// are packed).
#include "gz/cGZScriptTerp.h"

#include "gz/cGZDefaultAllocator.h"
#include "gz/cIGZString.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZPersistDBSerialRecord.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "game/vtable.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/StringKeyParser.h"
#include "rz/cIGZVariant.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "rz/cRZVariant.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kUnknownIid = 1;
constexpr uint32_t kInterpretingIid = 0x1a2bdeb9;     // cIGZScriptInterpretingTarget
constexpr uint32_t kCompilingIid = 0x1a2bdeba;        // cIGZScriptCompilingTarget
constexpr uint32_t kVariantUint32 = 6;                // cIGZVariant's type of an id
constexpr uint32_t kResourceManagerId = 0x056b906e;   // the persist resource manager (system service)
constexpr uint32_t kResourceManagerIid = 0x656b8efc;
constexpr uint32_t kSerialRecordIid = 0xc56b8f08;     // cIGZPersistDBSerialRecord
constexpr uint32_t kOpenForReading = 0;               // OpenRecord's `write`
constexpr uint32_t kFileAccessRead = 1;               // cRZFile::Open (as the original passes them)
constexpr uint32_t kFileOpenExisting = 2;
constexpr uint32_t kFileShareRead = 1;
constexpr char kTagOpen = '<';
constexpr char kTagClose = '>';
constexpr char kEquals = '=';
constexpr char kQuote = '"';
constexpr char kEntityStart = '&';
constexpr char kEntityEnd = ';';
constexpr uint32_t kLongestEntityName = 8;            // a longer name between '&' and ';' is not looked up
const char kSpace[] = " \t\n\r";                       // skipped before a token
const char kWordEnd[] = " \t\n\r\"<>=";               // a word ends at one of these (or the end)
const char kOpenText[] = "<";
const char kCloseText[] = ">";
const char kEqualsText[] = "=";

// The five entities the decoder knows, in the original's table order (0xe47e90).
struct Entity {
    const char *name;
    const char *text;
};
constexpr Entity kEntities[] = {
    {"lt", "<"},
    {"gt", ">"},
    {"amp", "&"},
    {"quot", "\""},
    {"apos", "'"},
};

// A cRZString on the stack, built and torn down by its own (hook-shaped) members.
struct LocalString {
    union {
        cRZString s;
    };
    explicit LocalString(const std::string &text)
    {
        s.Construct();
        s.FromChar(text.data(), static_cast<uint32_t>(text.size()));
    }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
    ~LocalString() { s.Destruct(); }
    const cIGZString &Face() { return *s.AsInterface(); }
};

// The persist resource manager as the getter 0x20db69 fetches it (the service object's slot 5), released at scope end
// (0x1466e0).
struct ResourceManagerRef {
    void *p = nullptr;
    ResourceManagerRef()
    {
        if (void *const service = GetServiceObject())
            As<ifaces::cIGZFrameWork>(service)->GetSystemService(kResourceManagerId, kResourceManagerIid, &p);
    }
    ResourceManagerRef(const ResourceManagerRef &) = delete;
    ResourceManagerRef &operator=(const ResourceManagerRef &) = delete;
    ~ResourceManagerRef()
    {
        if (p) static_cast<cIGZUnknown *>(p)->Release();
    }
    ifaces::cIGZPersistResourceManager *operator->() const { return As<ifaces::cIGZPersistResourceManager>(p); }
};

std::string KeyOf(const cIGZString &name)
{
    const uint32_t length = name.Strlen();
    return std::string(name.Data(), length);
}

bool IsSpace(char c) { return c != 0 && std::strchr(kSpace, c) != nullptr; }

} // namespace

// ---- cGZScriptVocab

bool cGZScriptNameLess::operator()(const std::string &a, const std::string &b) const
{
    return _stricmp(a.c_str(), b.c_str()) < 0;
}

cGZScriptVocab::~cGZScriptVocab() { cGZScriptVocab::Clear(); }

cIGZScriptVocab *__cdecl cGZScriptVocab::Create() { return new cGZScriptVocab; }

bool cGZScriptVocab::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kUnknownIid && riid != kIID) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cGZScriptVocab::AddRef() { return ++mRefCount; }

uint32_t cGZScriptVocab::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

bool cGZScriptVocab::AddId(const cIGZString &name, uint32_t id)
{
    cRZVariant *const value = new cRZVariant(id);
    const bool added = mNames.try_emplace(KeyOf(name), value).second;
    if (added)
        value->AddRef();
    else if (value)
        delete value;
    return added;
}

bool cGZScriptVocab::AddObject(const cIGZString &name, cIGZUnknown *object)
{
    const bool added = mNames.try_emplace(KeyOf(name), object).second;
    if (added) object->AddRef();
    return added;
}

bool cGZScriptVocab::Remove(const cIGZString &name)
{
    const auto it = mNames.find(KeyOf(name));
    if (it == mNames.end()) return false;
    it->second->Release();
    mNames.erase(it);
    return true;
}

bool cGZScriptVocab::Lookup(const cIGZString &name, cIGZUnknown **out)
{
    const auto it = mNames.find(KeyOf(name));
    if (it == mNames.end()) return false;
    *out = it->second;
    it->second->AddRef();
    return true;
}

bool cGZScriptVocab::Clear()
{
    for (auto &entry : mNames) entry.second->Release();
    mNames.clear();
    return true;
}

// ---- cGZScriptTerp: the object and its slots

cGZScriptTerp::cGZScriptTerp()
{
    cIGZScriptVocab *const vocab = new cGZScriptVocab;
    vocab->AddRef();
    mVocabs.push_back(vocab);
}

cGZScriptTerp::~cGZScriptTerp()
{
    if (mAttrValue) {
        mAttrValue->Release();
        mAttrValue = nullptr;
    }
    cGZScriptTerp::SetTarget(nullptr);
    while (cGZScriptTerp::PopVocab()) {
    }
    mVocabs.back()->Release();
    mVocabs.clear();
    // mAttributes and mVocabs go with the members; the attribute values still listed are not released (as the
    // original's).
}

cIGZScriptTerp *__cdecl cGZScriptTerp::Create() { return new cGZScriptTerp; }

bool cGZScriptTerp::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kUnknownIid && riid != kIID) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cGZScriptTerp::AddRef() { return ++mRefCount; }

uint32_t cGZScriptTerp::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

bool cGZScriptTerp::SetTarget(cIGZScriptTarget *target)
{
    if (mTarget) {
        mTarget->Release();
        mTarget = nullptr;
    }
    if (mInterpreting) {
        mInterpreting->Release();
        mInterpreting = nullptr;
    }
    if (mCompiling) {
        mCompiling->Release();
        mCompiling = nullptr;
    }
    if (target) {
        target->AddRef();
        mTarget = target;
        target->QueryInterface(kInterpretingIid, reinterpret_cast<void **>(&mInterpreting));
        mTarget->QueryInterface(kCompilingIid, reinterpret_cast<void **>(&mCompiling));
    }
    return true;
}

bool cGZScriptTerp::SetContext(cIGZUnknown *context)
{
    if (mContext) mContext->Release();
    mContext = context;
    if (context) context->AddRef();
    return true;
}

cIGZUnknown *cGZScriptTerp::GetContext() { return mContext; }

bool cGZScriptTerp::RunText(const cIGZString &text)
{
    mText = text.Data();
    return Run();
}

bool cGZScriptTerp::RunFile(const cIGZString &path)
{
    cRZFile storage;
    cRZFile *const file = storage.Construct(path);
    bool ok = false;
    if (file->Exists()) {
        file->Open(kFileAccessRead, kFileOpenExisting, kFileShareRead);
        if (file->Length() != 0) {
            uint32_t length = file->Length();
            char *const text = new char[length + 1];
            mText = text;
            file->ReadWithCount(text, length);
            text[length] = 0;
            ok = Run();
            ::operator delete[](const_cast<char *>(mText));
        }
    }
    file->Destruct();
    return ok;
}

bool cGZScriptTerp::RunResource(const void *key)
{
    ResourceManagerRef manager;
    void *record = nullptr;
    auto *const resourceKey = static_cast<const cGZPersistResourceKey *>(key);
    bool ok = false;
    if (manager->OpenRecord(resourceKey, &record, kOpenForReading)) {
        void *stream = nullptr;
        ok = As<cIGZUnknown>(record)->QueryInterface(kSerialRecordIid, &stream);
        if (ok) {
            auto *const serial = As<ifaces::cIGZPersistDBSerialRecord>(stream);
            const uint32_t size = serial->GetSize();
            char *const data = new char[size];
            serial->GetFieldVoid(data, size);
            serial->Release();
            const std::string text(data, size);
            ::operator delete[](data);
            LocalString script(text);
            ok = RunText(script.Face());
        }
        manager->CloseRecord(resourceKey, &record);
    }
    return ok;
}

bool cGZScriptTerp::AddId(const cIGZString &name, uint32_t id)
{
    cRZVariant *const value = new cRZVariant(id);
    return mVocabs.back()->AddObject(name, value);
}

bool cGZScriptTerp::AddObject(const cIGZString &name, cIGZUnknown *object)
{
    return mVocabs.back()->AddObject(name, object);
}

bool cGZScriptTerp::Remove(const cIGZString &name) { return mVocabs.back()->Remove(name); }

bool cGZScriptTerp::Clear() { return mVocabs.back()->Clear(); }

bool cGZScriptTerp::PushVocab(cIGZScriptVocab *vocab)
{
    if (!vocab) return false;
    vocab->AddRef();
    mVocabs.push_back(vocab);
    return true;
}

bool cGZScriptTerp::PopVocab()
{
    if (mVocabs.size() <= 1) return false;
    mVocabs.back()->Release();
    mVocabs.pop_back();
    return true;
}

// ---- the parser

bool cGZScriptTerp::Run()
{
    if (mState != kIdle) return false;
    mCursor = mText;
    mState = kText;
    BeginRun();
    cIGZString *token = nullptr;
    int32_t kind = kTokText;
    bool more = NextToken(&token, &kind, mState == kText);
    while (more && mState != kError && mState != kDone) {
        switch (mState) {
        case kText:
            if (kind == kTokText || kind == kTokWord)
                OnText(token);
            else if (kind == kTokOpen)
                mState = kTagStart;
            else
                mState = kError;
            break;
        case kTagStart:
            if (kind == kTokWord)
                mState = BeginTag(*token) ? kInTag : kSkipTag;
            else if (kind == kTokClose)
                mState = kText;
            else
                mState = kError;
            break;
        case kInTag:
        case kAttrName:
            if (kind == kTokWord) {
                FlushAttribute();
                mState = BeginAttribute(*token) ? kAttrName : kSkipAttr;
            } else if (kind == kTokClose) {
                FlushAttribute();
                CloseTag();
                mState = kText;
            } else if (kind == kTokEquals && mState == kAttrName) {
                mState = kAttrValue;
            } else {
                mState = kError;
            }
            break;
        case kAttrValue:
            if (kind == kTokText) {
                StringValue(*token);
                mState = kInTag;
            } else if (kind == kTokWord) {
                WordValue(*token);
                mState = kInTag;
            } else {
                mState = kError;
            }
            break;
        case kSkipTag:
            if (kind == kTokClose) mState = kText;
            break;
        case kSkipAttr:
            if (kind == kTokWord) {
                mState = BeginAttribute(*token) ? kAttrName : kSkipAttr;
            } else if (kind == kTokClose) {
                CloseTag();
                mState = kText;
            } else if (kind == kTokEquals) {
                mState = kSkipValue;
            } else {
                mState = kError;
            }
            break;
        case kSkipValue:
            mState = (kind == kTokWord || kind == kTokText) ? kInTag : kError;
            break;
        default:
            mState = kError;
            break;
        }
        token->Release();
        kind = kTokText;
        more = NextToken(&token, &kind, mState == kText);
    }
    mCursor = nullptr;
    if (mState == kError)
        EndRunFailed();
    else
        EndRunSucceeded();
    const bool ok = mState != kError;
    mState = kIdle;
    return ok;
}

bool cGZScriptTerp::NextToken(cIGZString **out, int32_t *kind, bool textMode)
{
    if (!mCursor) return false;
    cRZString *const made = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
    cIGZString *const token = made ? made->Construct()->AsInterface() : nullptr;
    *out = token;
    if (!token) return false;
    token->AddRef();
    if (textMode && *mCursor != 0 && *mCursor != kTagOpen) {
        const char *const start = mCursor;
        while (*mCursor != 0 && *mCursor != kTagOpen) ++mCursor;
        token->FromChar(start, static_cast<uint32_t>(mCursor - start));
        *kind = kTokText;
        return true;
    }
    while (IsSpace(*mCursor)) ++mCursor;
    const char c = *mCursor;
    if (c == 0) {
        token->Release();
        *out = nullptr;
        return false;
    }
    if (c == kQuote) {
        const char *const start = ++mCursor;
        if (*start == 0) {
            token->Release(); // ORIGINAL-BUG: *out keeps the released token
            return false;
        }
        while (*mCursor != 0 && *mCursor != kQuote) ++mCursor;
        const std::string raw(start, static_cast<size_t>(mCursor - start));
        if (*mCursor == kQuote) ++mCursor;
        LocalString decoded(DecodeScriptEntities(raw));
        token->operator=(decoded.Face());
        *kind = kTokText;
        return true;
    }
    if (c == kTagOpen || c == kTagClose || c == kEquals) {
        token->FromChar(c == kTagOpen ? kOpenText : c == kTagClose ? kCloseText : kEqualsText);
        *kind = c == kTagOpen ? kTokOpen : c == kTagClose ? kTokClose : kTokEquals;
        ++mCursor;
        return true;
    }
    const char *const start = mCursor;
    while (!std::strchr(kWordEnd, *mCursor)) {
        ++mCursor;
        if (*mCursor == 0) break;
    }
    token->FromChar(start, static_cast<uint32_t>(mCursor - start));
    *kind = kTokWord;
    return true;
}

bool cGZScriptTerp::LookupName(const cIGZString &name, cIGZUnknown **out)
{
    for (size_t i = mVocabs.size(); i-- > 0;)
        if (mVocabs[i]->Lookup(name, out)) return true;
    return false;
}

bool cGZScriptTerp::ParseId(const cIGZString &token, uint32_t *id)
{
    cIGZUnknown *object = nullptr;
    if (LookupName(token, &object)) {
        auto *const value = static_cast<cIGZVariant *>(object);
        bool ok = false;
        if (value->GetType() == kVariantUint32) {
            *id = *value->AsUint32Ref();
            ok = true;
        }
        value->Release();
        return ok;
    }
    if (!std::isdigit(static_cast<unsigned char>(*token.Data()))) return false;
    *id = StringKeyParser::ParseNumber(token.Data());
    return true;
}

bool cGZScriptTerp::BeginRun()
{
    mTagOpen = false;
    if (mTarget) mTarget->Attach(this);
    return true;
}

bool cGZScriptTerp::OnText(cIGZString *token)
{
    if (mTarget) mTarget->OnText(this, token);
    return true;
}

bool cGZScriptTerp::BeginTag(const cIGZString &token)
{
    uint32_t id;
    if (!ParseId(token, &id)) return false;
    mTagOpen = true;
    mTagId = id;
    if (mInterpreting) mInterpreting->BeginCompile(this, id);
    ClearAttributes();
    return true;
}

bool cGZScriptTerp::BeginAttribute(const cIGZString &token)
{
    if (!ParseId(token, &mAttrId)) return false;
    mAttrPending = true;
    return true;
}

bool cGZScriptTerp::FlushAttribute()
{
    if (!mAttrPending) return true;
    mAttributes.PushBack(cGZScriptAttribute{0, nullptr});
    mAttributes.mEnd[-1].mId = mAttrId;
    mAttributes.mEnd[-1].mValue = mAttrValue;
    if (mAttrValue) mAttrValue->AddRef();
    if (mInterpreting) mInterpreting->OnAttribute(this, mAttrId, mAttrValue);
    if (mAttrValue) {
        mAttrValue->Release();
        mAttrValue = nullptr;
    }
    mAttrPending = false;
    return true;
}

bool cGZScriptTerp::CloseTag()
{
    if (!mTagOpen) return true;
    if (mInterpreting) mInterpreting->EndCompile(this, mTagId);
    if (mCompiling) mCompiling->OnCompile(this, static_cast<int32_t>(mTagId), &mAttributes);
    mTagOpen = false;
    return true;
}

bool cGZScriptTerp::StringValue(const cIGZString &token)
{
    cRZVariant *const value = new cRZVariant();
    mAttrValue = value;
    value->AddRef();
    value->SetValString(token);
    return true;
}

bool cGZScriptTerp::WordValue(const cIGZString &token)
{
    if (LookupName(token, &mAttrValue)) return true;
    uint32_t id;
    cRZVariant *value;
    if (ParseId(token, &id))
        value = new cRZVariant(id);
    else
        value = new cRZVariant(token);
    mAttrValue = value;
    value->AddRef();
    return true;
}

bool cGZScriptTerp::ClearAttributes()
{
    for (uint32_t i = 0; i < mAttributes.Size(); ++i)
        if (cIGZUnknown *const value = mAttributes.mBegin[i].mValue) value->Release();
    mAttributes.Erase(mAttributes.mBegin, mAttributes.mEnd);
    if (mAttrValue) {
        mAttrValue->Release();
        mAttrValue = nullptr;
    }
    return true;
}

bool cGZScriptTerp::EndRunFailed()
{
    ClearAttributes();
    if (mTarget) mTarget->Detach(this, false);
    return true;
}

bool cGZScriptTerp::EndRunSucceeded()
{
    ClearAttributes();
    if (mTarget) mTarget->Detach(this, true);
    return true;
}

// ---- the entity decoder

std::string DecodeScriptEntities(const std::string &in)
{
    std::string out(in);
    size_t amp = out.find(kEntityStart, 0);
    while (amp != std::string::npos) {
        const size_t semi = out.find(kEntityEnd, amp + 1);
        const size_t length = semi - amp - 1; // wraps (and is skipped) without a ';'
        size_t next = amp + 1;
        if (length <= kLongestEntityName) {
            const char *const name = out.c_str() + amp + 1;
            for (const Entity &entity : kEntities) {
                if (std::strlen(entity.name) == length && std::memcmp(name, entity.name, length) == 0) {
                    out.replace(amp, length + 2, entity.text);
                    next = amp + std::strlen(entity.text);
                    break;
                }
            }
        }
        amp = out.find(kEntityStart, next);
    }
    return out;
}
