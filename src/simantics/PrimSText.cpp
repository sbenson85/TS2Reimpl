#include "simantics/PrimS.h"
#include "game/vtable.h"
#include "ifaces/cTSObject.h"

#include "game/game.h"
#include "game/original.h"
#include "misc/UtilitiesC.h"
#include "rz/cRZString.h"
#include "simantics/AutonomyHelpers.h"
#include "simantics/PrimSCommon.h"
#include "ts/cTSString.h"

#include <cstdlib>

using namespace primS;

// Handlers that load their text from the string tables: Cinematic (0x585590), UIEffect (0x5a7c60) and
// MakeActionString (0x59d740), and the small copy helper they share.

namespace {

constexpr uint32_t kTextKey = cTSString::kStringTableType; // the last argument of every loader
constexpr uint32_t kDefaultGroup = 0x7fd46cd0; // the group the loaders use when the locale does not give one
constexpr uint32_t kUiInterface = 0x5a69d3d5;

// A string-table loader object (cTSString: vtable 0xda5128, 0x34 bytes): a cRZString that fills itself from the string
// tables when it is constructed. The destructor is the cRZString one (the
// original runs 0x141193 on the loader), run only for an object that was constructed.
class Loader {
public:
    Loader() = default;
    Loader(const Loader &) = delete;
    Loader &operator=(const Loader &) = delete;
    ~Loader() noexcept
    {
        if (mAlive) text().Destruct();
    }

    // An empty loader (cTSString::ConstructEmpty, 0x1d0fb0).
    void ConstructEmpty()
    {
        loader().ConstructEmpty();
        mAlive = true;
    }
    // A loader that loads (table, entry, group) with the given flag byte (cTSString::Construct, 0x1d0df0).
    void ConstructLoaded(uint32_t table, uint32_t entry, uint32_t group, uint32_t flag)
    {
        loader().Construct(static_cast<uint16_t>(table), static_cast<uint16_t>(entry), group, static_cast<uint8_t>(flag), kTextKey);
        mAlive = true;
    }
    // The same without the flag (0x1d0e90).
    void ConstructLoaded4(uint32_t table, uint32_t entry, uint32_t group)
    {
        loader().Construct(static_cast<uint16_t>(table), static_cast<uint16_t>(entry), group, kTextKey);
        mAlive = true;
    }
    // Copies the text and the key fields of another loader (cTSString::Assign, 0x1d1000).
    void Assign(const Loader &other) { loader().Assign(&other.loader()); }

    cTSString &loader() { return *reinterpret_cast<cTSString *>(mBytes); }
    const cTSString &loader() const { return *reinterpret_cast<const cTSString *>(mBytes); }
    cRZString &text() { return loader().mString; }
    const char *c_str() { return text().mString.c_str(); }

private:
    alignas(4) uint8_t mBytes[0x34] = {};
    bool mAlive = false;
};

// A plain cRZString local, constructed empty and destroyed when the handler leaves.
struct Text {
    cRZString mText;
    Text() { mText.Construct(); }
    Text(const Text &) = delete;
    Text &operator=(const Text &) = delete;
    ~Text() noexcept { mText.Destruct(); }
    bool empty() const { return mText.mString.empty(); }
};

// Whether the UI manager exists (0x452680 reads the game global 0x10da968).
inline bool UiManagerPresent()
{
    return *GameAddress<void *>(0x10da968) != nullptr;
}

inline void *CachedUiService()
{
    return OriginalFunction<void *(__cdecl *)(), 0x20e031>()();
}

inline void *LocaleOf(void *list, void *frame)
{
    void *owner = Call<void *>(frame, 20);
    uint32_t language = Call<uint32_t>(owner, 38);
    return Call<void *>(list, 14, language);
}

} // namespace

SString *SString::AssignText(const SString *other)
{
    if (&mString != &other->mString) mString.assign(other->mString.c_str(), other->mString.size());
    return this;
}

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x7b, Cinematic (0x585590)
// ---------------------------------------------------------------------------------------------------------

uint32_t PrimSObject::Prim7B(void *frame, const PrimParamView *param)
{
    void *list = Call<void *>(mContext, 24);
    void *owner = Call<void *>(frame, 20);
    uint32_t language = Call<uint32_t>(owner, 38);
    void *locale = Call<void *>(list, 14, language);
    void *table = Call<void *>(mContext, 26);

    const int16_t id = Read(this, Byte(param, 0xd), Word(param, 0xe));
    void *object = Call<void *>(table, 29, static_cast<int32_t>(id));
    if (!object) Raise(0x17);
    if (Call<int16_t>(object, 266) < 1) Raise(0x6c);

    const uint8_t flags = Byte(param, 9);
    int32_t entry = Word(param, 4);
    if ((flags >> 4) & 1) entry = Read(this, Byte(param, 0xa), Word(param, 0xb));

    Loader text;
    text.ConstructEmpty();
    if ((flags >> 5) & 1) {
        Loader loaded;
        loaded.ConstructLoaded(0x97, static_cast<uint32_t>(entry + 1), kDefaultGroup, 1);
        text.Assign(loaded);
    } else if ((flags >> 6) & 1) {
        if (Call<void *>(locale, 70) != nullptr) {
            void *group = Call<void *>(locale, 70);
            const uint32_t key = Call<uint32_t>(group, 11);
            Loader loaded;
            loaded.ConstructLoaded(0x97, static_cast<uint32_t>(entry + 1), key, 1);
            text.Assign(loaded);
        }
    } else {
        void *group = Call<void *>(locale, 18);
        const uint32_t key = Call<uint32_t>(group, 11);
        Loader loaded;
        loaded.ConstructLoaded(0x97, static_cast<uint32_t>(entry + 1), key, 1);
        text.Assign(loaded);
    }

    bool flagD;
    if ((flags >> 1) & 1) flagD = As<ifaces::cTSObject>(this)->GetArgument(0) != 0;
    else flagD = (flags & 1) != 0;
    const uint32_t flag2 = (flags >> 2) & 1;
    const uint32_t flag3 = (flags >> 3) & 1;
    const uint32_t flag7 = flags >> 7;

    void *service = OriginalFunction<void *(__cdecl *)(), 0x211da3>()();
    void *target = Call<void *>(object, 265, 0);
    bool answer = false;
    if (target) {
        // The service reports an error code through the last argument; the original does not set it first, so a
        // service that leaves it alone gets whatever the frame held. -1 (no error) stands in for that.
        int32_t error = -1;
        answer = Call<uint8_t>(service, 27, text.c_str(), target, flagD ? 1u : 0u, flag2, flag3, flag7, &error) != 0;
        if (error >= 0) Raise(static_cast<int16_t>(error));
    }
    return answer ? 1u : 0u;
}

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x22, UIEffect (0x5a7c60)
// ---------------------------------------------------------------------------------------------------------

uint32_t PrimSObject::Prim22(void *frame, const PrimParamView *param)
{
    void *list = Call<void *>(mContext, 24);
    void *locale = LocaleOf(list, frame);
    const uint32_t kind = Byte(param, 4);
    const uint32_t word = Word(param, 0xc);

    if (kind == 8) {
        const int16_t first = Read(this, Byte(param, 0x11), Word(param, 0xf));
        const int16_t second = Call<int16_t>(this, 47);
        if (CachedUiService() != nullptr) {
            void *ui = CachedUiService();
            if (Call<uint32_t>(ui, 3) != 0) {
                void *ui2 = CachedUiService();
                void *target = Call<void *>(ui2, 3);
                Call<void>(target, 5, static_cast<uint32_t>(static_cast<uint16_t>(first)), word, static_cast<uint32_t>(static_cast<uint16_t>(second)));
            }
        }
        return 1;
    }

    const uint32_t entryA = Word(param, 7) + 1u;
    const uint32_t entryB = Word(param, 5) + 1u;
    Text textA, textB;
    const uint8_t flags = Byte(param, 9);

    auto load = [&](Text &into, uint32_t entry, bool literal, bool localeGroup) {
        Loader loaded;
        if (literal) {
            loaded.ConstructLoaded(0x96, entry & 0xffff, kDefaultGroup, 1);
        } else if (localeGroup) {
            if (Call<void *>(locale, 70) == nullptr) return;
            void *group = Call<void *>(locale, 70);
            const uint32_t key = Call<uint32_t>(group, 11);
            loaded.ConstructLoaded(0x96, entry & 0xffff, key, 1);
        } else {
            void *group = Call<void *>(locale, 18);
            const uint32_t key = Call<uint32_t>(group, 11);
            loaded.ConstructLoaded(0x96, entry & 0xffff, key, 1);
        }
        reinterpret_cast<SString *>(&into.mText)->AssignText(reinterpret_cast<const SString *>(&loaded.text()));
    };
    load(textA, entryA, (flags >> 2) & 1, (flags >> 3) & 1);
    if (textA.empty()) Raise(0x7d);
    load(textB, entryB, flags & 1, (flags >> 1) & 1);
    if (textB.empty()) Raise(0x7d);

    const uint32_t numberA = std::strtoul(textA.mText.mString.c_str(), nullptr, 0);
    const uint32_t numberB = std::strtoul(textB.mText.mString.c_str(), nullptr, 0);

    if (numberA == 0 && numberB == 0) {
        void *ui = CachedUiService();
        void *target = Call<void *>(ui, 12);
        switch (kind) {
        case 3:
            Call<void>(target, 15, 0);
            return 1;
        case 4:
            Call<void>(target, 13, 0, static_cast<uint32_t>(Call<uint16_t>(this, 47)), word);
            return 1;
        case 7:
            Call<void>(target, 14);
            return 1;
        default:
            break;
        }
    }
    if (numberA == 0 || numberB == 0) Raise(0x7d);

    void *director = GetCachedServiceObjectD();
    void *registry = Call<void *>(director, 3);
    void *outer = Call<void *>(registry, 39, numberB);
    if (!outer) Raise(0x7a);
    void *inner = Call<void *>(outer, 39, numberA);
    if (!inner) Raise(0x7a);

    void *ui = CachedUiService();
    void *target = Call<void *>(ui, 12);
    switch (kind) {
    case 0: Call<void>(target, 12, inner); break;
    case 1:
        Call<void>(target, 8, inner);
        Call<void>(target, 10, inner, 0);
        break;
    case 2:
        Call<void>(target, 8, inner);
        Call<void>(target, 10, inner, 1);
        break;
    case 3: Call<void>(target, 15, inner); break;
    case 4: Call<void>(target, 13, inner, static_cast<uint32_t>(Call<uint16_t>(this, 47)), word); break;
    case 5: Call<void>(target, 9, inner); break;
    case 7: Call<void>(target, 14); break;
    case 9: Call<void>(target, 11, inner, 1); break;
    case 10: Call<void>(target, 11, inner, 0); break;
    default: break; // 6 and 8 (and anything above 10) do nothing
    }
    return 1;
}

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x32, MakeActionString (0x59d740)
// ---------------------------------------------------------------------------------------------------------

namespace {

// The object the interface-0x5a69d3d5 holder gives to the display calls: slot 133 when the flag asks for it and
// it has one, else slot 132.
inline void *DisplayTarget(void *holderObject, bool preferAlternate)
{
    if (preferAlternate && holderObject != nullptr && Call<void *>(holderObject, 133) != nullptr) return Call<void *>(holderObject, 133);
    return Call<void *>(holderObject, 132);
}

} // namespace

uint32_t PrimSObject::Prim32(void *frame, const PrimParamView *param)
{
    void *list = Call<void *>(mContext, 24);
    void *table = Call<void *>(mContext, 26);
    void *secondary = &mSecondary;
    const int32_t flagWord = static_cast<int16_t>(Word(param, 6)); // tested bit by bit, sign-extended

    if (Byte(param, 0xd) != 0) {
        uint32_t id32 = 0;
        void *object = nullptr;
        if ((flagWord >> 5) & 1) {
            if ((flagWord >> 6) & 1) {
                const int32_t high = As<ifaces::cTSObject>(this)->GetArgument(3);
                const uint32_t low = static_cast<uint16_t>(As<ifaces::cTSObject>(this)->GetArgument(2));
                id32 = static_cast<uint32_t>(high << 16) | low;
            } else {
                id32 = Dword(param, 9);
            }
            if (id32 == 0) Raise(0x7a);
        } else {
            const uint16_t id = static_cast<uint16_t>(Call<int16_t>(secondary, 7, static_cast<uint32_t>(Byte(param, 0xf)),
                                                                    static_cast<uint32_t>(Word(param, 0x10)), 0u, 0u, 0u, 0u, 0u));
            object = Call<void *>(table, 29, static_cast<uint32_t>(id));
            if (!object) Raise(0x17);
        }

        int32_t amount;
        if ((flagWord >> 7) & 1) amount = As<ifaces::cTSObject>(this)->GetArgument(1);
        else amount = Byte(param, 0xe);

        if (UiManagerPresent()) {
            OriginalFunction<void(__cdecl *)(uint32_t, int32_t, void *), 0x4529d0>()(id32, amount, object);
            return 1;
        }
        if (!Call<uint8_t>(secondary, 33)) return 1;

        Held holder;
        if (Call<uint8_t>(this, 0, kUiInterface, &holder.mRef.mObject)) {
            void *ui = DisplayTarget(holder.mRef.mObject, ((flagWord >> 13) & 1) != 0);
            if (Call<uint8_t>(ui, 29)) {
                if (id32 != 0) {
                    Call<void>(ui, 65, 0);
                    Call<void>(ui, 72, id32, amount);
                } else {
                    Call<void>(ui, 65, object);
                    Call<void>(ui, 72, 0, amount);
                }
            }
        }
        return 1;
    }

    Text text;
    Call<void *>(table, 29, static_cast<int32_t>(Call<int16_t>(frame, 12))); // asked, the answer is not used
    void *locale = LocaleOf(list, frame);
    int32_t entry = static_cast<int16_t>(Word(param, 0x12));
    if ((flagWord >> 4) & 1) entry = As<ifaces::cTSObject>(this)->GetArgument(0);

    // The original's frame slot that held `table` is reused for the zero-extended entry in the last branch below; its
    // upper bytes then end up in an argument of the UI call at the end (see there).
    uint32_t slot1c = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(table));

    auto assign = [&](Loader &loaded) {
        reinterpret_cast<SString *>(&text.mText)->AssignText(reinterpret_cast<const SString *>(&loaded.text()));
    };
    if ((flagWord >> 2) & 1) {
        Loader loaded;
        loaded.ConstructLoaded4(0x12e, static_cast<uint32_t>(entry), kDefaultGroup);
        assign(loaded);
    } else if ((flagWord >> 3) & 1) {
        if (Call<void *>(locale, 70) != nullptr) {
            void *group = Call<void *>(locale, 70);
            const uint32_t key = Call<uint32_t>(group, 11);
            Loader loaded;
            loaded.ConstructLoaded4(0x12e, static_cast<uint32_t>(entry), key);
            assign(loaded);
        }
    } else {
        void *group = Call<void *>(locale, 18);
        slot1c = static_cast<uint16_t>(entry);
        const uint32_t key = Call<uint32_t>(group, 11);
        {
            Loader loaded;
            loaded.ConstructLoaded4(0x12e, slot1c, key);
            assign(loaded);
        }
        if (text.empty()) {
            if (Call<void *>(locale, 70) != nullptr) {
                void *other = Call<void *>(locale, 70);
                const uint32_t otherKey = Call<uint32_t>(other, 11);
                Loader loaded;
                loaded.ConstructLoaded4(0x12e, slot1c, otherKey);
                assign(loaded);
            }
            if (text.empty()) {
                Loader loaded;
                loaded.ConstructLoaded4(0x12e, slot1c, kDefaultGroup);
                assign(loaded);
            }
        }
    }
    if (text.empty()) Raise(0x5e);

    // The block this's slot 262 fills: four words, then a dword (contiguous in the frame).
    struct {
        uint16_t mWord[4];
        uint32_t mExtra;
    } block = {{0, 0, 0, 0}, 0};
    if (UiManagerPresent()) {
        block.mWord[1] = static_cast<uint16_t>(As<ifaces::cTSObject>(this)->GetArgument(0));
        if ((flagWord >> 10) & 1) block.mWord[2] = static_cast<uint16_t>(As<ifaces::cTSObject>(this)->GetArgument(4));
        if ((flagWord >> 11) & 1) block.mWord[3] = static_cast<uint16_t>(As<ifaces::cTSObject>(this)->GetArgument(5));
    }
    Call<uint32_t>(this, 262, static_cast<void *>(&text.mText), 0, frame, static_cast<void *>(&block), static_cast<void *>(&block.mExtra));

    if (UiManagerPresent()) {
        if (block.mWord[0] == 0) {
            block.mWord[0] = static_cast<uint16_t>(Call<int16_t>(frame, 12));
            block.mWord[1] = static_cast<uint16_t>(As<ifaces::cTSObject>(this)->GetArgument(0));
        }
        Held holder;
        if (Call<uint8_t>(this, 0, kUiInterface, &holder.mRef.mObject)) {
            // The two flags are pushed as the dwords the compiler kept them in: their frame slots (the table
            // pointer's or entry's slot, and the param argument's slot) with the low byte replaced by the flag.
            const uint32_t flagA = (slot1c & ~0xffu) | static_cast<uint32_t>((flagWord >> 8) & 1);
            const uint32_t flagB = (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(param)) & ~0xffu) | static_cast<uint32_t>((flagWord >> 9) & 1);
            OriginalFunction<void(__cdecl *)(const char *, void *, void *, uint32_t, uint32_t), 0x4526a0>()(
                text.mText.mString.c_str(), &block, holder.mRef.mObject, flagA, flagB);
            SetActionStringShown(1);
        }
    } else if (Call<uint8_t>(secondary, 33)) {
        Held holder;
        if (Call<uint8_t>(this, 0, kUiInterface, &holder.mRef.mObject)) {
            void *ui = DisplayTarget(holder.mRef.mObject, ((flagWord >> 12) & 1) != 0);
            Call<void>(ui, 26, static_cast<void *>(&text.mText));
        }
    }
    return 1;
}
