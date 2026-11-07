#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/GscContext.h"
#include "ifaces/GscEntryTable.h"
#include "ifaces/GscFrame.h"
#include "ifaces/GscGate.h"
#include "ifaces/GscObjectDefTable.h"
#include "ifaces/GscObjectTable.h"
#include "ifaces/GscPeopleTable.h"
#include "ifaces/cITSPersistSystem.h"
#include "rz/StringKeyParser.h"
#include "simantics/PrimGenericSimCall.h"
#include "simantics/PrimGenericSimCallMath.h"
#include "simantics/PrimGenericSimCallMessage.h"
#include "simantics/PrimGenericSimCallRefs.h"
#include "simantics/PrimXCommon.h"

#include <vector>

using gscmsg::MessageRef;
using gscmsg::MessageServer;
using gscref::PlainRef;
using gscref::VBaseRef;
using primX::Call;
using primX::Text;

// GenericSimCall (opcode 0x01) cases, group G: the resource-key cases (31, 32, 61, 29), the cases that read a GUID from a string
// table and hand it to an interface (46, 53), the gate-and-person cases (6, 50) and the store command case (39).

namespace {
constexpr uint32_t kDefaultResult = 1;
constexpr uint32_t kPropertyKeyType = 0xac598eac; // the first key word of the property-set resources cases 29, 32, 61 look up
constexpr uint32_t kStringTableType = 0x53545223; // 'STR#'
constexpr uint32_t kCresType = 0xe519c933;        // the type word the model names of case 31 get
constexpr uint32_t kRecordMissing = 0x7a;         // error codes the cases raise
constexpr uint32_t kNoPerson = 0x37;
constexpr uint32_t kNoEntry = 0x9a;
constexpr uint32_t kNoInterface = 0x1c;

inline ifaces::GscContext *Ctx(void *context)
{
    return As<ifaces::GscContext>(context);
}
} // namespace

// The frame the handler was called with, as its interface.
static inline ifaces::GscFrame *Frame(void *frame)
{
    return As<ifaces::GscFrame>(frame);
}

// ---- shared text loading of cases 46 and 53 -----------------------------------------------------------------------------

// Fills `text` from the string tables: the group of the object's type (object slot 90, then its slot 11), else the group of the
// object's slot 61 / slot 69 chain, else (unless argument 3 is 0 and there is an object) the default group. Returns false when the
// case must end with result 0 (no type, no text anywhere).
bool GenericSimCall::LoadObjectText(Text &text, uint32_t table, uint32_t entry, void *object)
{
    if (object) {
        void *const type = Call<void *>(object, 90);
        if (!type) return false;
        const uint32_t group = Call<uint32_t>(type, 11);
        {
            gscref::Loader loaded(table, entry, group);
            gscref::AssignText(text.mText, loaded.text());
        }
        if (text.mText.mString.empty()) {
            void *const chain = Call<void *>(object, 61);
            const uint32_t otherGroup = Call<uint32_t>(chain, 69);
            if (otherGroup != 0) {
                gscref::Loader loaded(table, entry, otherGroup);
                gscref::AssignText(text.mText, loaded.text());
            }
        }
    }
    if (text.mText.mString.empty()) {
        if (Arg(3) == 0 && object) return false;
        gscref::Loader loaded(table, entry, gscref::kDefaultTextGroup);
        gscref::AssignText(text.mText, loaded.text());
    }
    return !text.mText.mString.empty();
}

// Case 46: loads a GUID text from the string table (table argument 0, entry argument 1 + 1; the object is the stack object) and
// gives its 16 parsed bytes to the object's component interface (slot 0 of the interface answered through the virtual base).
// Result 1, 0 when there is no text or no interface.
uint32_t GenericSimCall::Case46()
{
    const uint16_t table = static_cast<uint16_t>(Arg(0));
    const uint16_t entry = static_cast<uint16_t>(Arg(1) + 1);
    Text text;
    void *const object = As<ifaces::GscObjectTable>(mL1c)->FindObject(Frame(mFrame)->GetStackObjectId());
    if (!LoadObjectText(text, table, entry, object)) return 0;
    uint8_t guid[16] = {};
    gscorig::ParseGuid(guid, text.c_str());
    VBaseRef component;
    Call<void>(mSelf, 9, gscref::kComponentIidA, gscref::kComponentIidB, component.Reset());
    if (!component.Get()) return 0;
    VBaseRef sink;
    (void)gscref::QueryViaVirtualBase(component.Get(), gscref::kVirtualBaseIid, sink.Reset());
    if (!sink.Get()) Throw(kNoInterface);
    Call<void>(sink.Get(), 0, guid);
    return 1;
}

// Case 53: like 46, but the text object is the one argument 4 names and the GUID goes to the stack object's interface through
// slot 3 (guid, argument 2 == 1).
uint32_t GenericSimCall::Case53()
{
    const uint16_t table = static_cast<uint16_t>(Arg(0));
    const uint16_t entry = static_cast<uint16_t>(Arg(1) + 1);
    const int16_t objectId = Arg(4);
    Text text;
    void *const object = As<ifaces::GscObjectTable>(mL1c)->FindObject(objectId);
    if (!LoadObjectText(text, table, entry, object)) return 0;
    uint8_t guid[16] = {};
    gscorig::ParseGuid(guid, text.c_str());
    void *const target = As<ifaces::GscObjectTable>(mL1c)->FindObject(Frame(mFrame)->GetStackObjectId());
    if (!target) return 0;
    VBaseRef component;
    Call<void>(target, 9, gscref::kComponentIidA, gscref::kComponentIidB, component.Reset());
    if (!component.Get()) return 0;
    VBaseRef sink;
    (void)gscref::QueryViaVirtualBase(component.Get(), gscref::kVirtualBaseIid, sink.Reset());
    if (!sink.Get()) Throw(kNoInterface);
    const uint32_t flag = (Arg(2) == 1) ? 1u : 0u;
    Call<void>(sink.Get(), 3, guid, flag);
    return 1;
}

// ---- cases 29 and 31 -------------------------------------------------------------------------------------------------------

// Case 29: repeated on every visit (the frame's resume state keeps argument 1 + 100 between visits): the person for argument 0
// (none ends the case) has an object with a graph (object slot 10, its slot 67); when the service 0xb38e50 does not already know
// the object's tag (slot 44) the property resource {0xac598eac, <the graph's slot 11>, 1, 0} is looked up, and when it exists the
// service gets the tag together with it (slot 45). Result 2 (visit again) while argument 1 counts down to 0, then 1. (The key's
// last two words are the constructor's 3rd and 4th arguments: the pushes in front of the slot-11 call belong to the constructor,
// the slot takes no arguments.)
uint32_t GenericSimCall::Case29()
{
    uint32_t result = kDefaultResult;
    if (As<ifaces::GscGate>(Ctx(Context())->GetGate())->GetBusyCount() >= 4) return result;
    const int16_t personId = Arg(0);
    int16_t repeats = Arg(1);
    if (Frame(mFrame)->GetResumeState() == 0) {
        Frame(mFrame)->SetResumeState(static_cast<int32_t>(repeats) + 100);
    } else {
        repeats = static_cast<int16_t>(Frame(mFrame)->GetResumeState() - 100);
    }
    void *const person = Ctx(Context())->GetPeopleTable()->FindPerson(personId, 1);
    if (!person) return result;
    void *const object = Call<void *>(person, 10);
    if (!object) return result;
    if (Call<uint32_t>(object, 67) == 0) return result;
    void *const tagService = CachedServiceGetter<0x10faf78, 0x0c4f252f, 0x2c4f2954>(); // 0xb38e50
    void *const tagOwner = object;
    if ((Call<uint32_t>(tagService, 44, Call<uint32_t>(tagOwner, 28)) & 0xff) != 0) return 1;
    void *const keyService = gscref::KeyService();
    PlainRef found;
    void *const graph = Call<void *>(object, 67);
    const Key16 key{{kPropertyKeyType, Call<uint32_t>(graph, 11), 1u, 0u}};
    if ((As<ifaces::cITSPersistSystem>(keyService)->GetResource(&key, gscref::kKeyLookupType, found.Reset(), 0, 0) & 0xff) != 0) {
        Call<void>(tagService, 45, Call<uint32_t>(object, 28), found.Get(), 1u);
        if (repeats < 0) {
            result = 2;
        } else if (repeats != 0) {
            result = 2;
            --repeats;
            Frame(mFrame)->SetResumeState(static_cast<int32_t>(repeats) + 100);
        } else {
            result = 1;
        }
    }
    return result;
}

// Case 31: preloads the models listed in an object's STR# table 0x85: the record for (argument 0 | argument 1 << 16) in the
// fourth prologue table gives, through its slot 18 / slot 11, the group; the STR# {0x53545223, group, 0x85, 0} comes from the
// persist system (an empty table made by the director when it is not found) and every non-empty name in it, with "_cres" added
// when missing, becomes a key (type 0xe519c933) queued for loading with a 60 second hold. Result 1.
uint32_t GenericSimCall::Case31()
{
    if (As<ifaces::GscGate>(Ctx(Context())->GetGate())->GetBusyCount() >= 4) return kDefaultResult;
    const int16_t low = Arg(0); // (separate statements: the order of C++ call arguments is unspecified; the original reads 0 then 1)
    const int16_t high = Arg(1);
    const uint32_t packed = gscref::PackWords(low, high);
    void *const record = As<ifaces::GscObjectDefTable>(mL18c)->FindRecord(packed);
    if (!record) return kDefaultResult;
    const Key16 key{{kStringTableType, Call<uint32_t>(Call<void *>(record, 18), 11), 0x85u, 0u}};
    void *const service = gscref::KeyService();
    PlainRef table;
    const bool found = (As<ifaces::cITSPersistSystem>(service)->GetResource(&key, 0xfb4cb0deu, table.Reset(), 0, 0) & 0xff) != 0;
    if (!found) {
        // No such table: the director makes an empty one (class 0xdb4cb0f9, interface 0xfb4cb0de).
        Call<void>(GetDirectorFrameworkObject(), 3, 0xdb4cb0f9u, 0xfb4cb0deu, table.Reset());
    }
    // ORIGINAL-BUG: when the director cannot make the table either, the calls below go through a null pointer.
    const int32_t count = Call<int32_t>(table.Get(), 5, 0u);
    Text name;
    static const char kSuffix[] = "_cres";
    for (int32_t i = 0; i < count; ++i) {
        void *const entryText = Call<void *>(table.Get(), 6, static_cast<uint32_t>(i), 0u);
        name.mText.Assign(*reinterpret_cast<const cIGZString *>(entryText));
        if (name.mText.mString.empty()) continue;
        const std::string &s = name.mText.mString;
        const bool endsWithSuffix = s.size() >= sizeof(kSuffix) - 1 && s.compare(s.size() - (sizeof(kSuffix) - 1), sizeof(kSuffix) - 1, kSuffix) == 0;
        if (!endsWithSuffix) name.mText.mString.append(kSuffix);
        Key16 parsed{};
        StringKeyParser::ParseKey(reinterpret_cast<StringKeyParser::Result *>(&parsed), name.c_str(), kCresType, nullptr);
        As<ifaces::cITSPersistSystem>(gscref::KeyService())->QueueResourceLoad(&parsed, 0, 60000, 0, 1);
    }
    return kDefaultResult;
}

// ---- case 32 ---------------------------------------------------------------------------------------------------------------

// Case 32 (counts, written to argument 1): argument 3 selects what is counted. 0: the entry for the gate's word 9 (none throws
// 0x9a) answers slot 15 with two masks (argument 1 bands, argument 2 as in case 76) and the category bit of argument 0.
// -1: how many of the property keys the persist system lists (for the cached slot-23 group) are family keys. Otherwise: for the
// object that argument 3 names, whether the property resource of its graph says "range 8" or both "range 4" and "range 0x10".
uint32_t GenericSimCall::Case32()
{
    const int32_t category = Arg(0);
    const int32_t bands = Arg(1);
    const int32_t polarity = Arg(2);
    const int32_t which = Arg(3);
    uint32_t result = 0;
    (void)CachedServiceGetter<0x10cc8ec, 0x4cf22ab5, 0x2cf22a9b>(); // 0x259c20: only fills its cache
    void *const gate = Ctx(Context())->GetGate();
    void *const entries = Ctx(Context())->GetEntryTable();
    void *const service = gscref::KeyService();
    const uint32_t categoryBit = gscref::CategoryBit(static_cast<uint32_t>(category));
    int32_t bandMask = -1;
    int32_t polarityMask = -1;
    if (which == 0) {
        const int16_t entryId = As<ifaces::GscGate>(gate)->GetWord(9);
        void *const entry = As<ifaces::GscEntryTable>(entries)->FindEntry(entryId);
        if (!entry) Throw(kNoEntry);
        if (bands > 0x32) bandMask = 0x10;
        else if (bands > 0x12) bandMask = 8;
        else if (bands > 0xf) bandMask = 4;
        else if (bands > 2) bandMask = 2;
        else if (bands > 1) bandMask = 1;
        else if (bands > 0) bandMask = 0x20;
        else if (bands == -1) bandMask = 0x40;
        if (polarity == 1) polarityMask = 1;
        else if (polarity == 0) polarityMask = 2;
        result = Call<uint32_t>(entry, 15, bandMask, polarityMask, categoryBit, 0u);
    } else if (which == -1) {
        std::vector<Key16> keys;
        As<ifaces::cITSPersistSystem>(service)->ListKeys(&keys, gscref::CachedSlot23Key(), kPropertyKeyType);
        for (const Key16 &k : keys) {
            if (gscref::IsFamilyKey(&k)) ++result;
        }
    } else {
        void *const object = As<ifaces::GscObjectTable>(mL1c)->FindObject(which);
        if (object && Call<uint32_t>(object, 61) != 0) {
            void *const chain = Call<void *>(object, 61);
            void *const graph = Call<void *>(chain, 67);
            const Key16 key{{kPropertyKeyType, Call<uint32_t>(graph, 11), 1u, 0u}};
            PlainRef resource;
            if ((As<ifaces::cITSPersistSystem>(service)->GetResource(&key, gscref::kKeyLookupType, resource.Reset(), 0, 0) & 0xff) != 0) {
                bool range4 = false, range16 = false, range8 = false;
                const uint32_t *span = Call<const uint32_t *>(resource.Get(), 42, 4u, categoryBit);
                if (span && span[0] != span[1]) range4 = (Call<uint32_t>(resource.Get(), 101, span) & 0xff) != 0;
                span = Call<const uint32_t *>(resource.Get(), 42, 0x10u, categoryBit);
                if (span && span[0] != span[1]) range16 = (Call<uint32_t>(resource.Get(), 101, span) & 0xff) != 0;
                span = Call<const uint32_t *>(resource.Get(), 42, 8u, categoryBit);
                if (span && span[0] != span[1]) range8 = (Call<uint32_t>(resource.Get(), 101, span) & 0xff) != 0;
                if (range8 || (range4 && range16)) result = 1;
            }
        }
    }
    SetArg(1, static_cast<uint16_t>(result));
    return kDefaultResult;
}

// ---- case 6 ----------------------------------------------------------------------------------------------------------------

// Case 6 (result 0 unless it gets past the entry checks): the person for the frame's stack id (none throws 0x37) has a nonzero
// word 0x3d naming an entry of the entry table that does not refuse the person (slot 18). The object the gate's word 3 names then
// answers the attribute interface; when its word 0x1f is the stack id, the first element of the object's vf63 / slot 45 list whose
// word 0x3d is that entry and whose slot 31 says yes is handed to the object table (slot 40), else the interface object itself.
uint32_t GenericSimCall::Case06()
{
    const int32_t stackId = Frame(mFrame)->GetStackObjectId();
    void *const person = As<ifaces::GscPeopleTable>(mL24)->FindPerson(stackId, 1);
    if (!person) Throw(kNoPerson);
    const int32_t entryId = Call<int16_t>(person, 16, 0x3du);
    if (entryId == 0) return 0;
    void *const entry = As<ifaces::GscEntryTable>(mL2c)->FindEntry(entryId);
    if (!entry) return 0;
    if (Call<int32_t>(mL2c, 18, person, entry) != 0) return 0;
    void *const gate = Ctx(Context())->GetGate();
    const int32_t targetId = As<ifaces::GscGate>(gate)->GetWord(3);
    void *const target = As<ifaces::GscObjectTable>(mL1c)->FindObject(targetId);
    if (target) {
        PlainRef iface;
        if ((Call<uint32_t>(target, 0, gscref::kAttributeIid, iface.Reset()) & 0xff) != 0) {
            const int32_t word = Call<int16_t>(iface.Get(), 10, 0x1fu, 1u);
            if (stackId == word) {
                PlainRef chosen;
                void *const holder = Call<void *>(mSelf, 63);
                void **const list = Call<void **>(holder, 45); // {first, last} of a vector of object pointers
                for (void **it = reinterpret_cast<void **>(list[0]); it != reinterpret_cast<void **>(list[1]); ++it) {
                    void *const element = *it;
                    if (Call<int16_t>(element, 10, 0x3du, 1u) == entryId && (Call<uint32_t>(element, 31) & 0xff) != 0) {
                        chosen.Assign(*it);
                        break;
                    }
                }
                Call<void>(mL1c, 40, chosen.Get());
            } else {
                Call<void>(mL1c, 40, iface.Get());
            }
        }
    }
    return 1;
}

// ---- case 61 ---------------------------------------------------------------------------------------------------------------

// Case 61 (the community name is in the game's own message: kAddExternalOutfitToFamilyClothingBin): the entry of the person for
// argument 2 (throws 0x37 / 0x9a as in 82), the record of argument 0 | argument 1 << 16 and the person's own record (slot 15)
// both exist (else 0x7a); the property resource of the person's graph is looked up, then every key the persist system lists for
// the first record's graph group (type 0xac598eac) is looked up too; a resource whose slot-23 mask intersects the first one's (or
// that has none) is given to the entry (slot 11) and the result becomes 1. No such resource: logs a message and throws 0xa1.
uint32_t GenericSimCall::Case61()
{
    uint32_t result = 0;
    const int16_t low = Arg(0);
    const int16_t high = Arg(1);
    const uint32_t packed = gscref::PackWords(low, high);
    const int16_t personId = Arg(2);
    void *const person = As<ifaces::GscPeopleTable>(mL24)->FindPerson(personId, 1);
    if (!person) Throw(kNoPerson);
    void *entry = nullptr;
    const int32_t entryId = Call<int16_t>(person, 16, 0x3du);
    if (static_cast<uint32_t>(entryId) >= 1 && static_cast<uint32_t>(entryId) <= 0x7cff) entry = As<ifaces::GscEntryTable>(mL2c)->FindEntry(entryId);
    if (!entry) Throw(kNoEntry);
    auto *const records = As<ifaces::GscObjectDefTable>(mL18c);
    void *const recordA = records->FindRecord(packed);
    void *const recordB = records->FindRecord(Call<uint32_t>(person, 15));
    if (!recordA || !recordB) Throw(kRecordMissing);
    void *const service = gscref::KeyService();
    PlainRef current;
    const Key16 ownKey{{kPropertyKeyType, Call<uint32_t>(Call<void *>(recordB, 67), 11), 1u, 0u}};
    PlainRef own;
    (void)As<ifaces::cITSPersistSystem>(service)->GetResource(&ownKey, gscref::kKeyLookupType, own.Reset(), 0, 0);
    if (own.Get()) {
        std::vector<Key16> keys;
        As<ifaces::cITSPersistSystem>(service)->ListKeys(&keys, Call<uint32_t>(Call<void *>(recordA, 67), 11), kPropertyKeyType);
        for (const Key16 &k : keys) {
            if (k.w[0] != kPropertyKeyType) continue;
            if ((As<ifaces::cITSPersistSystem>(service)->GetResource(&k, gscref::kKeyLookupType, current.Reset(), 0, 0) & 0xff) == 0) continue;
            if (Call<uint32_t>(current.Get(), 23) != 0) {
                const uint32_t ownMask = Call<uint32_t>(own.Get(), 23);
                const uint32_t mask = Call<uint32_t>(current.Get(), 23);
                if ((mask & ownMask) == 0) {
                    current.Assign(nullptr);
                    continue;
                }
            }
            Call<void>(entry, 11, current.Get());
            result = 1;
        }
        if (result != 1) {
            // The owner's log object (this+0x48) gets the text, then the case throws.
            Call<void>(mSelf->mField48, 28, GameAddress<const char>(0xdd7be0));
            Throw(0xa1);
        }
    }
    return result;
}

// ---- case 50 ---------------------------------------------------------------------------------------------------------------

// Case 50 (always 1): the person for argument 1 (none throws 0x37) has an object (person slot 10) with a tag (its slot 28); the
// first entry of the entry table's id list whose slot 29 accepts the tag gives two numbers (slot 60, slot 42). The context's
// slot 7 list turns the second into a thing (slot 18); two interfaces are made by the director ('SIMI' ... and 0xb54e37d ...);
// when the second one accepts the thing's slot 8 (slot 6 answers 0) and the persist system's slot 73 refuses to block, the
// first interface's slot 29 value (or the entry's first number when argument 5 is nonzero), scaled by argument 2 / 100 and
// rounded, is added to this+0x2c8's value when argument 0 is nonzero and written to arguments 3 and 4 as quotient and remainder
// by 10000.
uint32_t GenericSimCall::Case50()
{
    void *const person = As<ifaces::GscPeopleTable>(mL24)->FindPerson(Arg(1), 1);
    if (!person) Throw(kNoPerson);
    void *const tag = Call<void *>(Call<void *>(person, 10), 28);
    void *const entries = Ctx(Context())->GetEntryTable();
    std::vector<int16_t> ids;
    Call<void>(entries, 19, &ids);
    int32_t amount = 0;
    int32_t numberTwo = 0;
    for (int16_t id : ids) {
        void *const entry = As<ifaces::GscEntryTable>(entries)->FindEntry(id);
        if (entry && (Call<uint32_t>(entry, 29, tag) & 0xff) != 0) {
            amount = Call<int32_t>(entry, 60);
            numberTwo = Call<int32_t>(entry, 42);
            break;
        }
    }
    void *const list = Call<void *>(Context(), 7);
    void *const thing = Call<void *>(list, 18, numberTwo);
    if (thing) {
        PlainRef first;
        Call<void>(GetDirectorFrameworkObject(), 3, 0x53494d49u, 0x7a3e555du, first.Reset());
        PlainRef second;
        Call<void>(GetDirectorFrameworkObject(), 3, 0xb54e37du, 0xeb54e473u, second.Reset());
        if (Call<uint32_t>(second.Get(), 6, Call<uint32_t>(thing, 8, 0u)) == 0) {
            if (Call<uint32_t>(gscref::KeyService(), 73, first.Get(), second.Get(), 1u, 0u) == 0) {
                float base = static_cast<float>(Call<int32_t>(first.Get(), 29));
                if (Arg(5) != 0) base = static_cast<float>(amount);
                const int32_t scale = Arg(2);
                if (scale != 0) {
                    const float scaled = (base / 100.0f) * static_cast<float>(scale);
                    const int32_t rounded = gscmath::RoundViaMagic(scaled);
                    if (Arg(0) != 0) {
                        void *const owner = mSelf->mField2c8;
                        Call<void>(owner, 30, Call<int32_t>(owner, 29) + rounded);
                    }
                    SetArg(3, static_cast<uint32_t>(rounded / 10000));
                    SetArg(4, static_cast<uint32_t>(rounded % 10000));
                }
            }
        }
    }
    return 1;
}

// ---- case 39 ---------------------------------------------------------------------------------------------------------------

// Case 39: a command for the context's store (context slot 4). Argument 0 picks the command (0..7, and 10..17 which are the
// same commands on a different target; 8 posts a message unless the store's slot 57 says it is already done; 9 and everything
// else do nothing). With a selector below 10 the target ids come from this object's own attribute interface (its slot 57 is the
// person id, argument 1 the value); from 10 on argument 1 is the person id and argument 2 the value, and the interface object
// is the one the person's slot 15 value names. The commands call the store's slots 35 / 45 / 39+40 / 36 / 47 / 48 / 37.
uint32_t GenericSimCall::Case39()
{
    const int32_t selector = Arg(0);
    const int16_t argument1 = Arg(1);
    const int16_t argument2 = Arg(2);
    PlainRef iface;
    PlainRef person;
    uint16_t word = 0;
    int32_t value = 0;
    if (selector < 10) {
        Call<void>(mSelf, 0, gscref::kAttributeIid, iface.Reset());
        if (!iface.Get()) Throw(kNoInterface);
        word = static_cast<uint16_t>(Call<int16_t>(iface.Get(), 57));
        person.Assign(Ctx(Context())->GetPeopleTable()->FindPerson(static_cast<int16_t>(word), 1));
        value = argument1;
    } else {
        word = static_cast<uint16_t>(argument1);
        person.Assign(Ctx(Context())->GetPeopleTable()->FindPerson(static_cast<int16_t>(word), 1));
        if (!person.Get()) Throw(kNoPerson);
        auto *const objects = Ctx(Context())->GetObjectTable();
        iface.Assign(objects->FindObjectByHandle(Call<uint32_t>(person.Get(), 15)));
        value = argument2;
    }
    void *const store = Ctx(Context())->GetStore();
    if (static_cast<uint32_t>(selector) > 0x11) return kDefaultResult;
    // The jump table maps 0..7 and 10..17 to the same eight commands, 8 to the message and 9 to nothing.
    const int32_t command = selector >= 10 ? selector - 10 : selector;
    switch (command) {
    case 0: {
        int32_t level = 0;
        if (value >= 0 && value <= 3) level = value;
        Call<void>(store, 35, static_cast<uint32_t>(word), level);
        break;
    }
    case 1:
        Call<void>(store, 45, static_cast<uint32_t>(word));
        break;
    case 2:
        if (iface.Get()) Call<void>(store, 40, iface.Get(), value, 1u);
        if (person.Get()) Call<void>(store, 39, person.Get(), value, 1u);
        break;
    case 3:
        if (iface.Get()) Call<void>(store, 40, iface.Get(), value * 10, 0u);
        if (person.Get()) Call<void>(store, 39, person.Get(), value * 10, 0u);
        break;
    case 4:
        Call<void>(store, 36, static_cast<uint32_t>(word));
        break;
    case 5:
        if (value > 0) Call<void>(store, 47, static_cast<uint32_t>(word), value * 0xe10);
        break;
    case 6:
        Call<void>(store, 48, static_cast<uint32_t>(word), value * 0xe10);
        break;
    case 7: {
        uint32_t first = 1;
        uint32_t second = 1;
        if (value == 1) {
            second = 0;
        } else if (value == 2) {
            first = 0;
            second = 0;
        }
        Call<void>(store, 37, static_cast<uint32_t>(word), first, second);
        break;
    }
    case 8:
        if ((Call<uint32_t>(store, 57) & 0xff) == 0) {
            Call<void>(store, 56);
            MessageRef msg;
            msg.SetId(0x71465adcu);
            Call<void>(MessageServer(), 4, msg.mMessage, 0u);
        }
        break;
    default:
        break;
    }
    return kDefaultResult;
}
