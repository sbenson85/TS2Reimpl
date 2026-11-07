#include "app/AppInit.h"
#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/GscContext.h"
#include "ifaces/GscEntryTable.h"
#include "ifaces/GscFrame.h"
#include "ifaces/GscObjectTable.h"
#include "ifaces/GscPeopleTable.h"
#include "ifaces/cITSPersistSystem.h"
#include "rz/RZMemory.h"
#include "rz/cRZRandom.h"
#include "simantics/PrimGenericSimCall.h"
#include "simantics/PrimGenericSimCallMessage.h"
#include "simantics/PrimGenericSimCallMisc.h"
#include "simantics/PrimGenericSimCallOriginalsB.h"
#include "simantics/PrimGenericSimCallRefs.h"
#include "simantics/PrimXCommon.h"

#include <cstdlib>
#include <new>
#include <vector>

using gscmisc::HandlePair;
using gscmisc::RefRow;
using gscmsg::MessageRef;
using gscmsg::MessageServer;
using gscref::PlainRef;
using gscref::VBaseRef;
using primX::Call;
using primX::Text;

// GenericSimCall (opcode 0x01) cases, group H: the outfit cases (59 plastic surgery failure, 60 the outfit GUID, 4), which share
// the "property set of the owner's graph" lookup, and the appearance notification they end with.

namespace {
constexpr uint32_t kDefaultResult = 1;
constexpr uint32_t kPropertyKeyType = 0xac598eac;
constexpr uint32_t kNoPerson = 0x37;
constexpr uint32_t kNoInterface = 0x1c;

inline ifaces::GscContext *Ctx(void *context)
{
    return As<ifaces::GscContext>(context);
}

// 0xb38ea0: the cached service the outfit cases tell about the sim they change (slot 3 takes a flag word, slot 12 a tag, slot 5 a
// resource and a name, slot 48 a name, a float and a flag).
inline void *TagService()
{
    return CachedServiceGetter<0x10faf70, 0xeb7d6513, 0xcb7d64f8>();
}

// 0x259c20: the cached service of the text/outfit tables (slot 19 finds a table by name, slot 25 lists its rows, slot 36 prepares
// a property set, slot 8 posts the change).
inline void *TextService()
{
    return CachedServiceGetter<0x10cc8ec, 0x4cf22ab5, 0x2cf22a9b>();
}

// 0x54b290 (__cdecl, one argument, result in al): the object's owner's kind answers 0xd07b9ce9 to slot 3 (0xcb) and option bit 3 is set.
inline bool IsMarkedObject(void *object)
{
    if (!object) return false;
    void *const owner = Call<void *>(object, 8);
    void *const kind = Call<void *>(owner, 64);
    if (Call<uint32_t>(kind, 3, 0xcbu) != 0xd07b9ce9u) return false;
    return ((appinit::GetOptionMask() >> 3) & 1u) != 0;
}

// 0x504cb0: the GUID is the all-zero GUID.
inline bool IsNullGuid(const uint8_t *guid)
{
    return gscmisc::GuidEquals(guid, gscorig::NullGuid());
}
} // namespace

static inline ifaces::GscFrame *Frame(void *frame)
{
    return As<ifaces::GscFrame>(frame);
}

// The end of cases 59 and 60: tells the tag service (slot 3, flag 0x1f), then asks the sim's owner object (interface slot 8) for the
// component holder (0xfa1c39f7 / 0x1a34f3f4) and hands it the sim's three numbers (word 8, the packed words 0x77 / 0x78 and whether
// that pack is nonzero) together with the mode and 1 (its slot 8).
void GenericSimCall::NotifyAppearanceChange(void *iface, int32_t mode)
{
    Call<void>(TagService(), 3, 0x1fu);
    VBaseRef component;
    Call<void>(Call<void *>(iface, 8), 9, gscref::kComponentIidA, gscref::kVirtualBaseIid, component.Reset());
    if (!component.Get()) return;
    const int32_t word8 = Call<int16_t>(iface, 10, 8u, 1u);
    const int32_t low = Call<int16_t>(iface, 10, 0x77u, 1u);
    const int32_t high = Call<int16_t>(iface, 10, 0x78u, 1u);
    const uint32_t packed = (static_cast<uint32_t>(high) << 16) | (static_cast<uint32_t>(low) & 0xffffu);
    Call<void>(component.Get(), 8, word8, packed != 0 ? 1u : 0u, packed, mode, 1u);
}

// Case 59 (plastic surgery that failed): when the own interface and the owner's slot 65 allow it, picks a random row of the
// "plasticsurgery_fail" outfit table (the text service lists the rows that match the sim's property set), names it with the key
// words ("%d:%d:%d:"), finds its resource (type 0xcccef852, instance 2 first, then 1) and has the tag service apply it to the sim
// (slots 5, 48, 12) and the text service post the change (slot 8, kind 0x13). Result 1 when posted (or when the owner refuses).
uint32_t GenericSimCall::Case59()
{
    uint32_t result = 0;
    void *const textService = TextService();
    void *const keyService = gscref::KeyService();
    void *const tagService = TagService();
    PlainRef iface;
    if (!textService || !keyService) return result;
    if ((Call<uint32_t>(mSelf, 0, gscref::kAttributeIid, iface.Reset()) & 0xff) == 0) return result;
    if ((Call<uint32_t>(mSelf->mField240, 65) & 0xff) == 0) return 1;
    PlainRef table;
    Call<void>(textService, 19, "plasticsurgery_fail", 0x6c4f3599u, table.Reset());
    PlainRef property;
    void *const graph = Call<void *>(Call<void *>(Call<void *>(iface.Get(), 8), 61), 67);
    const Key16 key{{kPropertyKeyType, Call<uint32_t>(graph, 11), 1u, 0u}};
    auto *const persist = As<ifaces::cITSPersistSystem>(keyService);
    if ((persist->GetResource(&key, gscref::kKeyLookupType, property.Reset(), 0, 0) & 0xff) == 0) return result;
    if (Call<int32_t>(property.Get(), 24) != 1) return result;
    Call<void>(textService, 36, property.Get(), 0u);
    std::vector<RefRow> rows;
    const uint32_t mask = Call<uint32_t>(property.Get(), 23, static_cast<uint32_t>(-1), 0u, 0u, 1u);
    const uint32_t group = Call<uint32_t>(property.Get(), 22, mask);
    Call<void>(textService, 25, table.Get(), &rows, group);
    if (rows.empty()) return result;
    const uint32_t count = static_cast<uint32_t>(rows.size());
    auto *const random = reinterpret_cast<cRZRandom *>(Call<void *>(mSelf->mField48, 55));
    const uint32_t pick = random->RandomUint32Uniform(count);
    void *const outfit = rows.data()[pick].mRefs[0];
    const Key16 outfitKey = *Call<const Key16 *>(outfit, 21);
    Text name;
    name.mText.Sprintf("%d:%d:%d:", outfitKey.w[0], outfitKey.w[1], outfitKey.w[2]);
    const Key16 byFirst{{0xcccef852u, outfitKey.w[1], 1u, 0u}};
    const Key16 bySecond{{0xcccef852u, outfitKey.w[1], 2u, 0u}};
    PlainRef match;
    bool found = (persist->GetResource(&bySecond, 0xaccef84cu, match.Reset(), 0, 0) & 0xff) != 0;
    if (!found) found = (persist->GetResource(&byFirst, 0xaccef84cu, match.Reset(), 0, 0) & 0xff) != 0;
    if (!found) return result;
    Call<void>(tagService, 5, match.Get(), name.c_str());
    Call<void>(tagService, 48, name.c_str(), *GameAddress<float>(0xd9c698), 1u);
    Call<void>(tagService, 12, Call<void *>(Call<void *>(Call<void *>(iface.Get(), 8), 61), 28));
    void *const tag = Call<void *>(Call<void *>(Call<void *>(iface.Get(), 8), 61), 28);
    if ((Call<uint32_t>(textService, 8, tag, 0x13u, 0u, 0u, 0u) & 0xff) != 0) {
        result = 1;
        NotifyAppearanceChange(iface.Get(), 2);
    }
    return result;
}

// Case 60: gets a GUID text (table argument 0 and entry argument 1 + 1 from the string tables of the stack object, as in 46/53,
// or, with table 0, from the entry the context's slot 27 object picks for the own interface's person) and applies it to the sim:
// unless the GUID is the null GUID, the sim is a marked object, or the sim's property set already has that GUID (result 1),
// the property set is told the GUID (slot 55), the text service prepares it (slot 36) and posts the change (slot 8, kind 0xb),
// and the sim is notified (mode 0). Result 1 when posted (or when the owner refuses at the start).
uint32_t GenericSimCall::Case60()
{
    uint32_t result = 0;
    const uint16_t table = static_cast<uint16_t>(Arg(0));
    const uint16_t entry = static_cast<uint16_t>(Arg(1) + 1);
    Text text;
    PlainRef iface;
    if ((Call<uint32_t>(mSelf, 0, gscref::kAttributeIid, iface.Reset()) & 0xff) == 0) return 0;
    if ((Call<uint32_t>(mSelf->mField240, 65) & 0xff) == 0) return 1;
    if (static_cast<int16_t>(table) != 0) {
        void *const object = As<ifaces::GscObjectTable>(mL1c)->FindObject(Frame(mFrame)->GetStackObjectId());
        if (!LoadObjectText(text, table, entry, object)) return 0;
    } else {
        void *const picker = Call<void *>(Context(), 27);
        if (picker) {
            PlainRef source;
            // The picker's slot 4 takes (selector, address of the holder it fills); the interface's slot 57 takes nothing. The holder's
            // address is computed before the selector is asked for (the original evaluates the picker's arguments right to left).
            void **const sourceOut = source.Reset();
            const uint16_t selector = static_cast<uint16_t>(Call<uint32_t>(iface.Get(), 57));
            if ((Call<uint32_t>(picker, 4, static_cast<uint32_t>(selector), sourceOut) & 0xff) != 0) {
                HandlePair first;
                Call<void>(source.Get(), 6, &first, 6u);
                if (Call<uint32_t>(first.mFirst, 7) != 0) {
                    text.mText.mString.assign(Call<const char *>(first.mFirst, 5));
                } else {
                    {
                        // The slot fills `second` and returns the pair it filled; the first pair takes over what the returned pair
                        // holds, and the temporary is gone before the text is read (its references go back here).
                        HandlePair second;
                        auto *const filled = Call<HandlePair *>(source.Get(), 6, &second, 2u);
                        first.Assign(*filled);
                    }
                    text.mText.mString.assign(Call<const char *>(first.mFirst, 5));
                }
            }
        }
    }
    uint8_t guid[16] = {};
    gscorig::ParseGuid(guid, text.c_str());
    if (IsNullGuid(guid)) return 0;
    if (IsMarkedObject(iface.Get())) return 0;
    void *const textService = TextService();
    void *const keyService = gscref::KeyService();
    void *const tagService = TagService();
    if (!textService || !keyService) return result;
    PlainRef property;
    void *const graph = Call<void *>(Call<void *>(Call<void *>(iface.Get(), 8), 61), 67);
    const Key16 key{{kPropertyKeyType, Call<uint32_t>(graph, 11), 1u, 0u}};
    if ((As<ifaces::cITSPersistSystem>(keyService)->GetResource(&key, gscref::kKeyLookupType, property.Reset(), 0, 0) & 0xff) == 0) {
        return result;
    }
    if (gscmisc::GuidEquals(Call<const uint8_t *>(property.Get(), 28), guid)) return 1;
    Call<void>(property.Get(), 55, guid);
    (void)Call<uint32_t>(property.Get(), 96);
    Call<void>(textService, 36, property.Get(), 1u);
    Call<void>(tagService, 12, Call<void *>(Call<void *>(Call<void *>(iface.Get(), 8), 61), 28));
    void *const tag = Call<void *>(Call<void *>(Call<void *>(iface.Get(), 8), 61), 28);
    if ((Call<uint32_t>(textService, 8, tag, 0xbu, 0u, guid, 0u) & 0xff) != 0) {
        result = 1;
        NotifyAppearanceChange(iface.Get(), 0);
    }
    return result;
}

// Case 4 (visited again while it yields: the frame's resume state keeps the record id): asks for the person of the stack id (none
// throws 0x37); the record is the entry of argument 0 (or the id kept in the frame), made by the entry table (slot 13) when it
// does not exist yet and configured (argument 1 as its slot 58, the person's name through the owner's slot 51, the name of the
// holder's record), or the holder's own record (slot 16) when argument 0 is 0. A first visit stores the record's slot 41 in the
// frame, takes the entry of the person's word 0x3d and, when that entry is in its first stage with a name, posts a message with
// the name (id 0xec1aac03); otherwise (or on a later visit) the case finishes: argument 0 gets a status word, the entry table
// gets the record assigned to the person (slot 17) and a finished entry is retired (slots 43, 14). Result 2 while it waits,
// 1 when the assignment failed, else 0.
uint32_t GenericSimCall::Case04()
{
    uint32_t result = 0;
    bool finished = false;
    uint32_t statusWord = 0;
    PlainRef person(As<ifaces::GscPeopleTable>(mL24)->FindPerson(Frame(mFrame)->GetStackObjectId(), 1));
    if (!person.Get()) Throw(kNoPerson);
    int32_t id = Arg(0);
    if (Frame(mFrame)->GetResumeState() != 0) id = Frame(mFrame)->GetResumeState();
    void *const holder = Call<void *>(Context(), 23);
    PlainRef record;
    PlainRef entry;
    auto *const entries = As<ifaces::GscEntryTable>(mL2c);
    if (id != 0) {
        record.Assign(entries->FindEntry(id));
        if (!record.Get()) {
            Call<void>(mL2c, 13, record.Reset(), id);
            void *const known = Call<void *>(holder, 14);
            if (record.Get() && known) {
                const int32_t extra = Arg(1);
                if (extra != 0) Call<void>(record.Get(), 58, extra);
                Text name;
                if (Call<void *>(person.Get(), 10) != nullptr) {
                    if ((Call<uint32_t>(Call<void *>(person.Get(), 10), 51, &name.mText) & 0xff) != 0) {
                        Call<void>(record.Get(), 38, &name.mText);
                    }
                }
                if (Call<uint32_t>(Call<void *>(record.Get(), 37), 7) == 0) {
                    Call<void>(record.Get(), 38, Call<void *>(known, 37));
                }
            }
        }
    } else {
        record.Assign(Call<void *>(holder, 16));
    }
    if (Frame(mFrame)->GetResumeState() == 0) {
        if (record.Get()) {
            Frame(mFrame)->SetResumeState(Call<int32_t>(record.Get(), 41));
            if (Call<int16_t>(person.Get(), 16, 0x3du) != 0) {
                entry.Assign(entries->FindEntry(Call<int16_t>(person.Get(), 16, 0x3du)));
                if (entry.Get()) {
                    if (Call<int32_t>(entry.Get(), 24, 3u) > 1 || Call<uint32_t>(entry.Get(), 42) == 0) {
                        finished = true;
                    } else {
                        void *const lookup = Call<void *>(Context(), 6);
                        if (lookup) {
                            void *const named = Call<void *>(lookup, 57, Call<uint32_t>(entry.Get(), 42));
                            if (named) {
                                MessageRef msg;
                                msg.SetId(0xec1aac03u);
                                Call<void>(msg.mMessage, 11, 8u);
                                auto *const copy = static_cast<cRZString *>(::operator new(0x20));
                                copy->Construct(*reinterpret_cast<const cIGZString *>(Call<void *>(named, 19)));
                                Call<void>(msg.mMessage, 30, copy);
                                Call<void>(MessageServer(), 4, msg.mMessage, 0u);
                            } else {
                                finished = true;
                                statusWord = Call<uint32_t>(entry.Get(), 53);
                            }
                        }
                    }
                }
            } else {
                finished = true;
            }
        }
    } else {
        // ORIGINAL-BUG: on a later visit the original asks the person's entry without checking that the table found one (a null entry crashes both).
        entry.Assign(entries->FindEntry(Call<int16_t>(person.Get(), 16, 0x3du)));
        if (Call<uint32_t>(entry.Get(), 42) == 0) {
            finished = true;
            statusWord = Call<uint32_t>(entry.Get(), 53);
        }
    }
    if (!finished) return 2;
    SetArg(0, static_cast<uint16_t>(statusWord));
    const bool assign = Call<int16_t>(person.Get(), 16, 0x3du) == 0 || Call<int32_t>(mL2c, 18, person.Get(), 0u) == 0;
    if (assign) {
        if (Call<int32_t>(mL2c, 17, person.Get(), record.Get(), entry.Get()) == 0) result = 1;
    }
    if (entry.Get() && Call<int32_t>(entry.Get(), 24, 3u) == 0) {
        const int32_t stage = Call<int32_t>(entry.Get(), 41);
        if (stage >= 1 && Call<int32_t>(entry.Get(), 41) <= 0x7cff) {
            Call<void>(entry.Get(), 43, 0u);
            Call<void>(mL2c, 14, entry.Get());
        }
    }
    return result;
}
