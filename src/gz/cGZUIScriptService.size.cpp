// cGZUIScriptService in the standalone shape (lane KS). Built for size, as the game's (0x85b2df..0x85bf91 are packed).
//
// The objects the slots talk to are other classes' (the script node, the interpreter, the compilers, the resource
// manager); their slots are called by number where nothing names them yet.
#include "gz/cGZUIScriptService.h"
#include "gz/cGZScriptTerp.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "misc/HexParse.h"
#include "misc/UtilitiesC.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"

#include <cstdio>
#include <new>
#include <string>

namespace {

constexpr uint32_t kScriptObjectIid = 0x9a4527cb; // what a compiled script's objects are asked for
constexpr uint32_t kCompilerIid = 0x1a356083;     // the compilers' (and the new script objects') interface
constexpr uint32_t kScriptObjectClass = 0xfa450242;
constexpr uint32_t kInterpreterClass = 0xba2e7954, kInterpreterIid = 0x1a2bdebb;
constexpr uint32_t kResultClass = 0x5a354595, kResultIid = 0x1a35608a;
constexpr uint32_t kResourceManagerId = 0x056b906e, kResourceManagerIid = 0x656b8efc;
constexpr uint32_t kInstanceFormat = 0xe48144; // the resource instance as text from the two words (sprintf)
constexpr uint32_t kTextSourceType = 0x800e;   // a compile argument whose slot 56 writes the source text

// A result tested in al (the original's `test al, al` on a slot it declares as a byte).
bool Byte(uint32_t value) { return static_cast<uint8_t>(value) != 0; }

cIGZCOM *GZCOM() { return static_cast<cIGZCOM *>(GetDirectorFrameworkObject()); }

// The names ReadScriptNode gives the script node's first vocabulary (lane MC), in the original's order: each id as
// the text at its address (the game's), before and after the target is set.
struct ScriptName {
    uint32_t text; // RVA of the name
    uint32_t id;
};
constexpr ScriptName kMarkupNames[] = {
    {0xe480dc, 0}, // _null
    {0xe480e4, 1}, // _children
    {0xe480f0, 2}, // _/children
    {0xe480fc, 3}, // _define
    {0xe48104, 4}, // _name
    {0xe4810c, 5}, // _val
};
constexpr ScriptName kTagNames[] = {
    {0xdad360, 0},          // none
    {0xdd4168, 1},          // children
    {0xe48114, 2},          // /children
    {0xe48120, 3},          // define
    {0xdcac08, 4},          // name
    {0xe48128, 5},          // val
    {0xe4812c, 0xfa450242}, // LEGACY: the legacy control's class
    {0xe48134, 0x12},       // comic9
    {0xe4813c, 0x13},       // comic10
};

void ReleaseAndClear(cIGZUnknown *&p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

// The persist resource manager as the getter 0x20db69 fetches it (the service object's slot 5), released at scope end
// (0x1466e0).
struct ResourceManagerRef {
    void *p = nullptr;
    ResourceManagerRef()
    {
        if (void *const service = GetServiceObject()) RawSlot<5>(service, kResourceManagerId, kResourceManagerIid, &p);
    }
    ResourceManagerRef(const ResourceManagerRef &) = delete;
    ResourceManagerRef &operator=(const ResourceManagerRef &) = delete;
    ~ResourceManagerRef()
    {
        if (p) static_cast<cIGZUnknown *>(p)->Release();
    }
};

// A cRZString on the stack as the original builds it (the game's vtable, refcount 0), destroyed with cRZString's
// destructor body.
struct LocalString {
    union {
        cRZString mText;
    };
    LocalString()
    {
        ::new (&mText.mString) std::string();
        mText.mVTable = GameAddress<void>(cRZString::kVTable);
        mText.mRefCount = 0;
    }
    LocalString(const LocalString &) = delete;
    ~LocalString() { mText.Destruct(); }
};

} // namespace

// ---- construction ----------------------------------------------------------------------------------------------------

cGZUIScriptService::cGZUIScriptService() : rz::cRZSystemService(kServiceId, kPriority) {}

// 0x85b434: Shutdown first (non-virtually); the pending list is empty after it.
cGZUIScriptService::~cGZUIScriptService() noexcept { cGZUIScriptService::Shutdown(); }

cIGZUIScriptService *__cdecl cGZUIScriptService::Create()
{
    return new cGZUIScriptService;
}

// ---- cIGZUnknown -----------------------------------------------------------------------------------------------------

// 0x85b818: no null check of ppv; the faces by id, the rest cRZSystemService's.
bool cGZUIScriptService::QueryInterface(uint32_t riid, void **ppv)
{
    switch (riid) {
    case 1:
    case cIGZUIScriptService::kIID: *ppv = static_cast<cIGZUIScriptService *>(this); break;
    case cIGZScriptTarget::kIID: *ppv = static_cast<cIGZScriptTarget *>(this); break;
    case cIGZScriptInterpretingTarget::kIID: *ppv = static_cast<cIGZScriptInterpretingTarget *>(this); break;
    case cIGZScriptCompilingTarget::kIID: *ppv = static_cast<cIGZScriptCompilingTarget *>(this); break;
    case cIGZUILegacyScriptService::kIID: *ppv = static_cast<cIGZUILegacyScriptService *>(this); break;
    default: return rz::cRZSystemService::QueryInterface(riid, ppv);
    }
    AddRef();
    return true;
}

uint32_t cGZUIScriptService::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cGZUIScriptService::Release() { return rz::cRZSystemService::Release(); }

// ---- the pending stack -----------------------------------------------------------------------------------------------

// 0x85b7f1
bool cGZUIScriptService::PushCurrent()
{
    if (mCurrent) {
        mCurrent->AddRef();
        mPending.push_front(mCurrent);
        mCurrent = nullptr;
    }
    return true;
}

// 0x85b7c9
bool cGZUIScriptService::PopPending()
{
    if (mPending.empty()) return false;
    mPending.front()->Release();
    mPending.pop_front();
    return true;
}

// 0x85bf7f
cIGZUnknown *cGZUIScriptService::PeekPending() const { return mPending.empty() ? nullptr : mPending.front(); }

// ---- cIGZUIScriptService ---------------------------------------------------------------------------------------------

bool cGZUIScriptService::QueryScriptObject(const cGZUIScriptKey *key, uint32_t loadArg, uint32_t objectArg,
                                           uint32_t iid, void **out)
{
    cIGZUnknown *const object = GetScriptObject(key, loadArg, objectArg);
    if (!object) return false;
    const bool found = object->QueryInterface(iid, out);
    object->Release();
    return found;
}

// 0x85b581. The instance is the resource manager's for the entry the text "<word49><word50>" (the slot 3 object of the
// QueryWithIdsA service) names, when the manager's slot 15 accepts the key with it; else the caller's.
cIGZUnknown *cGZUIScriptService::GetScriptObject(const cGZUIScriptKey *key, uint32_t loadArg, uint32_t objectArg)
{
    cIGZUnknown *result = nullptr;
    cGZUIScriptKey local = *key;
    ResourceManagerRef manager;
    if (manager.p) {
        ServiceQueryHolder query;
        query.QueryWithIdsA();
        if (query.mObject) {
            void *const source = RawSlot<3, void *>(query.mObject);
            if (source) {
                const uint32_t high = RawSlot<49>(source);
                const uint32_t low = RawSlot<50>(source);
                char text[40];
                std::snprintf(text, sizeof(text), GameAddress<const char>(kInstanceFormat), high, low); // at most 22 characters
                local.mInstance = hexparse::ParseHex32(text);
                if (!RawSlot<15, bool>(manager.p, &local)) local.mInstance = key->mInstance;
            }
            static_cast<cIGZUnknown *>(query.mObject)->Release();
        }
    }
    static_cast<cIGZUILegacyScriptService *>(this)->PushNewScript(loadArg);
    cIGZUnknown *const script = CompileNode(&local);
    if (script) {
        auto *const object = RawSlot<5, cIGZUnknown *>(script, objectArg);
        if (object) {
            cIGZUnknown *scriptObject = nullptr;
            if (object->QueryInterface(kScriptObjectIid, reinterpret_cast<void **>(&scriptObject))) {
                result = RawSlot<3, cIGZUnknown *>(scriptObject);
                if (result) result->AddRef();
                scriptObject->Release();
            }
        }
        RawSlot<9>(script, 0u);
        script->Release();
    }
    return result;
}

// 0x85b776 / 0x85b704 / 0x85b73d: the node's RunText / RunFile / RunResource (slots 6 / 7 / 8) with this service's +8
// face as its target first.
cIGZUnknown *cGZUIScriptService::RunNode6(void *arg)
{
    if (ReadScriptNode()) {
        auto *const node = static_cast<cIGZScriptTerp *>(mScriptNode);
        node->SetTarget(static_cast<cIGZScriptTarget *>(this));
        node->RunText(*static_cast<const cIGZString *>(arg));
    }
    cIGZUnknown *const result = mResult;
    mResult = nullptr;
    return result;
}

cIGZUnknown *cGZUIScriptService::RunNode7(void *arg)
{
    if (ReadScriptNode()) {
        auto *const node = static_cast<cIGZScriptTerp *>(mScriptNode);
        node->SetTarget(static_cast<cIGZScriptTarget *>(this));
        node->RunFile(*static_cast<const cIGZString *>(arg));
    }
    cIGZUnknown *const result = mResult;
    mResult = nullptr;
    return result;
}

cIGZUnknown *cGZUIScriptService::CompileNode(void *arg)
{
    if (ReadScriptNode()) {
        auto *const node = static_cast<cIGZScriptTerp *>(mScriptNode);
        node->SetTarget(static_cast<cIGZScriptTarget *>(this));
        node->RunResource(arg);
    }
    cIGZUnknown *const result = mResult;
    mResult = nullptr;
    return result;
}

// 0x85b7af
cIGZUnknown *cGZUIScriptService::GetScriptNode()
{
    ReadScriptNode();
    return mScriptNode;
}

cIGZUnknown *cGZUIScriptService::GetContext() { return mContext; }

// 0x85bbaf: counts the new one before releasing the old one.
void cGZUIScriptService::SetContext(cIGZUnknown *context)
{
    if (context == mContext) return;
    if (context) context->AddRef();
    if (mContext) mContext->Release();
    mContext = context;
}

// 0x85bc39 (lane MC)
bool cGZUIScriptService::ReadScriptNode()
{
    if (!mScriptNode &&
        Byte(GZCOM()->GetClassObject(cGZScriptTerp::kClassId, cIGZScriptTerp::kIID, reinterpret_cast<void **>(&mScriptNode)))) {
        auto *const node = static_cast<cIGZScriptTerp *>(mScriptNode);
        for (const ScriptName &name : kMarkupNames) {
            LocalString text;
            text.mText.FromChar(GameAddress<const char>(name.text));
            node->AddId(*text.mText.AsInterface(), name.id);
        }
        mSharedInterpreter = true;
        node->SetTarget(static_cast<cIGZScriptTarget *>(this));
        for (const ScriptName &name : kTagNames) {
            LocalString text;
            text.mText.FromChar(GameAddress<const char>(name.text));
            node->AddId(*text.mText.AsInterface(), name.id);
        }
        mSharedInterpreter = false;
    }
    return mScriptNode != nullptr;
}

// ---- cIGZUILegacyScriptService ---------------------------------------------------------------------------------------

// 0x85bb0c
bool cGZUIScriptService::PushNewScript(uint32_t arg)
{
    cIGZUnknown *script = nullptr;
    if (Byte(GZCOM()->GetClassObject(kScriptObjectClass, kCompilerIid, reinterpret_cast<void **>(&script)))) {
        cIGZUnknown *object = nullptr;
        if (script->QueryInterface(kScriptObjectIid, reinterpret_cast<void **>(&object))) {
            RawSlot<5>(object, arg);
            object->Release();
        }
        mCurrent = script;
        PushCurrent();
        script->Release();
    }
    return true;
}

// 0x85bb82: releases the old one before counting the new one.
bool cGZUIScriptService::SetCompilerArgument(cIGZUnknown *object)
{
    cIGZUnknown *const old = mCompilerArgument;
    if (object != old) {
        if (old) old->Release();
        mCompilerArgument = object;
        if (object) object->AddRef();
    }
    return true;
}

// ---- cIGZScriptTarget ------------------------------------------------------------------------------------------------

// 0x85b8ff: the interpreter (unless shared) handed to the host's slot 13, and the result object made. The class objects
// are written over the fields without releasing what they held (as the original).
bool cGZUIScriptService::Attach(void *host)
{
    if (!mSharedInterpreter) {
        if (Byte(GZCOM()->GetClassObject(kInterpreterClass, kInterpreterIid, reinterpret_cast<void **>(&mInterpreter))))
            RawSlot<13>(host, mInterpreter);
    }
    return Byte(GZCOM()->GetClassObject(kResultClass, kResultIid, reinterpret_cast<void **>(&mResult)));
}

// 0x85b956. Unless `keep`: the result and the compiler dropped, a compile in progress ended (the host's slot 14). Unless
// the interpreter is shared: the host's slot 14, the interpreter released (unchecked), the pending stack emptied.
bool cGZUIScriptService::Detach(void *host, bool keep)
{
    if (!keep) {
        ReleaseAndClear(mResult);
        ReleaseAndClear(mCompiler);
        if (mCompiling) {
            RawSlot<14>(host);
            mCompiling = false;
        }
    }
    if (mSharedInterpreter) return true;
    RawSlot<14>(host);
    mInterpreter->Release(); // ORIGINAL-BUG: unchecked (a Detach without an Attach that made the interpreter)
    mInterpreter = nullptr;
    while (PopPending()) {
    }
    mCurrent = nullptr;
    return true;
}

bool cGZUIScriptService::OnText(void *, cIGZString *) { return true; }

// ---- cIGZScriptInterpretingTarget ------------------------------------------------------------------------------------

// 0x85b4e1: the compiler of `classId` made (the old one released), given the compiler argument through its script-object
// face's slot 6, the top pending object (slot 9), made current, and the target told its slot 4 (slot 13).
bool cGZUIScriptService::BeginCompile(void *target, uint32_t classId)
{
    mCompilerClass = classId;
    ReleaseAndClear(mCompiler);
    if (!Byte(GZCOM()->GetClassObject(classId, kCompilerIid, reinterpret_cast<void **>(&mCompiler)))) return false;
    cIGZUnknown *object = nullptr;
    if (mCompiler->QueryInterface(kScriptObjectIid, reinterpret_cast<void **>(&object))) {
        RawSlot<6>(object, mCompilerArgument);
        object->Release();
    }
    RawSlot<9>(mCompiler, PeekPending());
    mCurrent = mCompiler;
    RawSlot<13>(target, RawSlot<4>(mCompiler));
    mCompiling = true;
    return true;
}

// 0x85b6e8
bool cGZUIScriptService::EndCompile(void *target, uint32_t)
{
    if (mCompiling) {
        RawSlot<14>(target);
        mCompiling = false;
    }
    return true;
}

bool cGZUIScriptService::OnAttribute(void *, uint32_t, cIGZUnknown *) { return true; }

// ---- cIGZScriptCompilingTarget ---------------------------------------------------------------------------------------

// 0x85b9ed. With a compiler and a result: the compiler's slot 3 gets `data`, the result's slot 4 the compiler, which is
// dropped. Otherwise by `event`: 1 the current object pushed, 2 the top popped (the next one current), 3 `data` the
// argument list: the last source (kind 4) whose slot 3 is 0x800e writes its text (slot 56) for the compiler's slot 11,
// and the last value (kind 5) goes with it to slot 10. Other events answer false.
bool cGZUIScriptService::OnCompile(void *compiler, int32_t event, void *data)
{
    if (mCompiler && mResult) {
        RawSlot<3>(mCompiler, data);
        RawSlot<4>(mResult, mCompiler);
        ReleaseAndClear(mCompiler);
        return true;
    }
    switch (event) {
    case 1:
        if (mCurrent) {
            PushCurrent();
            mCurrent = nullptr;
        }
        break;
    case 2:
        PopPending();
        mCurrent = PeekPending();
        break;
    case 3: {
        const auto &args = *static_cast<const cGZUIScriptArgs *>(data);
        cIGZUnknown *source = nullptr;
        cIGZUnknown *value = nullptr;
        for (const auto &arg : args) {
            if (arg.first == 4)
                source = arg.second;
            else if (arg.first == 5)
                value = arg.second;
        }
        if (source && RawSlot<3>(source) == kTextSourceType) {
            LocalString text;
            RawSlot<56>(source, &text.mText, 0u, 0x20u);
            RawSlot<11>(compiler, &text.mText);
            if (value) RawSlot<10>(compiler, &text.mText, value);
        }
        break;
    }
    default: return false;
    }
    return true;
}

// ---- cIGZSystemService -----------------------------------------------------------------------------------------------

// 0x85b7bc
bool cGZUIScriptService::Init()
{
    if (!mInitialized) mInitialized = true;
    return true;
}

// 0x85bbdb: the node, the result, the compiler and the context released, the pending stack emptied.
bool cGZUIScriptService::Shutdown()
{
    if (mInitialized) {
        ReleaseAndClear(mScriptNode);
        ReleaseAndClear(mResult);
        ReleaseAndClear(mCompiler);
        ReleaseAndClear(mContext);
        while (PopPending()) {
        }
        mInitialized = false;
    }
    return true;
}
