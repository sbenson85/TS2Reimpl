#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/cTSObject.h"
#include "game/original.h"
#include "misc/HexParse.h"
#include "rz/cRZString.h"
#include "simantics/PrimS.h"
#include "simantics/PrimTCommon.h"
#include "ts/cTSGameTipService.h"
#include "ts/cTSString.h"

using namespace primT;

namespace {

constexpr uint32_t kTextKey = cTSString::kStringTableType; // the last argument of every string loader
constexpr uint32_t kTextTable = 0x12d;    // the string table the dialog texts come from

// A string-table loader object (vtable 0xda5128, 0x34 bytes): a cRZString that fills itself from the string tables
// when it is constructed. The destructor is the cRZString one, run only for an
// object that was constructed.
class Loader {
public:
    Loader() = default;
    Loader(const Loader &) = delete;
    Loader &operator=(const Loader &) = delete;
    ~Loader() noexcept
    {
        if (mAlive) text().Destruct();
    }

    // cTSString::Construct without flags (0x1d0e90) for (kTextTable, entry, group).
    void Construct4(uint32_t entry, uint32_t group)
    {
        loader().Construct(static_cast<uint16_t>(kTextTable), static_cast<uint16_t>(entry), group, kTextKey);
        mAlive = true;
    }
    // cTSString::Construct with a flag byte (0x1d0df0) for (kTextTable, entry, group).
    void Construct5(uint32_t entry, uint32_t group, uint32_t flag)
    {
        loader().Construct(static_cast<uint16_t>(kTextTable), static_cast<uint16_t>(entry), group, static_cast<uint8_t>(flag), kTextKey);
        mAlive = true;
    }

    cTSString &loader() { return *reinterpret_cast<cTSString *>(mBytes); }
    cRZString &text() { return loader().mString; }

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

    // The loader's text goes into this string (0x26be10, SString::AssignText).
    void Take(Loader &loader)
    {
        reinterpret_cast<SString *>(&mText)->AssignText(reinterpret_cast<const SString *>(&loader.text()));
    }
};

// The cached services (original code, __cdecl, no arguments): the dialog service (0x20e031) and the cinematic/state
// service (0x211da3). The game tip service comes from ours (GetGameTipService, 0xb39020).
inline void *DialogService()
{
    return OriginalFunction<void *(__cdecl *)(), 0x20e031>()();
}

inline void *StateService()
{
    return OriginalFunction<void *(__cdecl *)(), 0x211da3>()();
}

// hexparse::ParseHex32 (0x1b0e2c): the number a text holds (hex, with or without a 0x prefix).
inline int32_t ParseNumber(const char *text)
{
    return static_cast<int32_t>(hexparse::ParseHex32(text));
}

// The text of table 0x12d entry `entry` in the group `file` names, loaded into a fresh string (empty without a file).
void LoadText(Text &out, void *file, uint32_t entry)
{
    if (!file) return;
    const uint32_t group = Call<uint32_t>(file, 11);
    Loader loader;
    loader.Construct4(entry & 0xffff, group);
    out.Take(loader);
}

} // namespace

uint32_t PrimTObject::Prim24(void *frame, const PrimParamView *param)
{
    void *table = Call<void *>(mContext, 26);
    void *list = Call<void *>(mContext, 24);
    (void)Call<uint32_t>(mContext, 3); // the answer is not used
    bool frameBusy = true;
    void *entry = Call<void *>(table, 76, this);
    void *ctx10 = Call<void *>(mContext, 10);
    if (Call<uint32_t>(frame, 18) == 0) {
        frameBusy = false;
        Call<void>(frame, 19, 1u);
        if (entry) {
            const uint32_t kind = Call<uint32_t>(entry, 7);
            if (kind == 1) {
                Call<void>(entry, 9);
                entry = nullptr;
            } else if (kind == 5) {
                entry = nullptr;
            }
        }
    }

    const uint8_t op = Byte(param, 9);
    const uint8_t textFlags = Byte(param, 0xc);
    void *person = Call<void *>(table, 29, static_cast<int32_t>(Call<int16_t>(frame, 12)));
    void *stackObject = Call<void *>(table, 29, static_cast<int32_t>(Call<int16_t>(frame, 3)));
    if (!stackObject) stackObject = person;
    const uint32_t selector = (Byte(param, 0xb) >> 4) & 7;

    if (op == 9) {
        const int16_t value = As<ifaces::cTSObject>(this)->GetArgument(selector);
        uint32_t result = 0;
        void *service = DialogService();
        void *dialogs = Call<void *>(service, 3);
        if (dialogs) result = Call<uint8_t>(dialogs, 8, static_cast<uint32_t>(static_cast<uint16_t>(value))) != 0 ? 1u : 0u;
        return result;
    }

    if (op == 8 || op == 0x0a || op == 0x1c) {
        if (!stackObject) Raise(0x17);
        void *service = DialogService();
        void *dialogs = Call<void *>(service, 3);
        if (dialogs) {
            void *owner = Call<void *>(frame, 20);
            const uint32_t language = Call<uint32_t>(owner, 38);
            void *locale = Call<void *>(list, 14, language);

            uint32_t index;
            if ((textFlags & 2) != 0) index = static_cast<uint32_t>(Call<int16_t>(stackObject, 19, 0u) + 1);
            else index = Word(param, 0x11);

            Text text;
            void *source = Call<void *>(Call<void *>(locale, 18), 16);
            if ((textFlags & 1) != 0) source = Call<void *>(locale, 70);
            else if ((textFlags & 0x40) != 0) source = Call<void *>(list, 6);
            LoadText(text, source, index);

            void *extra = nullptr;      // the answer the text object gives through its slot 262
            void *reference = nullptr;  // the object value passed on to the dialog
            uint16_t id = 0;
            Call<void>(stackObject, 262, &text.mText, 0u, frame, 0u, &extra);

            const uint32_t mode = (Byte(param, 0xb) >> 1) & 7;
            if (mode == 0) {
                reference = extra;
                if (reference == nullptr && person != nullptr) {
                    void *kindObject = Call<void *>(person, 61);
                    reference = Call<void *>(kindObject, 27);
                    id = static_cast<uint16_t>(Call<int16_t>(person, 47));
                }
            } else if (mode == 2) {
                const int16_t number = Call<int16_t>(frame, 12);
                void *found = Call<void *>(ctx10, 16, static_cast<int32_t>(number), 1u);
                if (found) reference = Call<void *>(found, 10);
                else Raise(0x37);
            }

            if (op == 8) {
                // The original checks the selector against 0..7 here (error 2); its three bits can't leave that range.
                uint8_t kind = Byte(param, 0x10);
                if (kind == 4) {
                    kind = 0;
                    id = static_cast<uint16_t>(As<ifaces::cTSObject>(this)->GetArgument(1u));
                }
                if (kind == 5) {
                    kind = 0;
                    id = static_cast<uint16_t>(As<ifaces::cTSObject>(this)->GetArgument(1u));
                    const int16_t other = As<ifaces::cTSObject>(this)->GetArgument(2u);
                    void *found = Call<void *>(ctx10, 16, static_cast<int32_t>(other), 1u);
                    if (found) reference = Call<void *>(found, 10);
                }
                const uint16_t handle = Call<uint16_t>(dialogs, 3, static_cast<uint32_t>(kind), &text.mText, reference, static_cast<uint32_t>(id));
                if (handle == 0xffff) return 0;
                Call<void>(dialogs, 6, static_cast<uint32_t>(handle), static_cast<uint32_t>(Byte(param, 0xe)));
                Call<void>(dialogs, 7, static_cast<uint32_t>(handle), static_cast<uint32_t>(Byte(param, 0xd)));
                As<ifaces::cTSObject>(this)->SetArgument(selector, static_cast<uint32_t>(handle));
            } else {
                const uint32_t flag = op == 0x1c ? 1u : 0u;
                const uint16_t made = static_cast<uint16_t>(As<ifaces::cTSObject>(this)->GetArgument(selector));
                if (Call<uint8_t>(dialogs, 4, static_cast<uint32_t>(made), &text.mText, reference, static_cast<uint32_t>(id), flag) == 0) return 0;
                const uint16_t second = static_cast<uint16_t>(As<ifaces::cTSObject>(this)->GetArgument(selector));
                Call<void>(dialogs, 6, static_cast<uint32_t>(second), static_cast<uint32_t>(Byte(param, 0xe)));
                const uint16_t third = static_cast<uint16_t>(As<ifaces::cTSObject>(this)->GetArgument(selector));
                Call<void>(dialogs, 7, static_cast<uint32_t>(third), static_cast<uint32_t>(Byte(param, 0xd)));
            }
        }
        return 1;
    }

    // The other opcodes first ask the state service whether anything is running.
    void *state = StateService();
    if ((state != nullptr && Call<uint8_t>(state, 55) != 0) || Call<uint8_t>(state, 56) != 0) return 2;

    if (op == 0x39) {
        if (frameBusy) return 2;
        void *service = DialogService();
        void *ui = service ? Call<void *>(service, 12) : nullptr;
        cITSGameTipService *tips = GetGameTipService();
        if (!ui || !tips) return 1;
        const uint32_t word = Word(param, 0x11);
        Held holder;
        // (a "yes" is expected to come with the tip; the original uses it without a check)
        if (!tips->GetTip(word, reinterpret_cast<cITSGameTip **>(&holder.mRef.mObject))) return 1;
        Text first, second;
        Call<void>(holder.mRef.mObject, 10, &first.mText);
        Call<void>(holder.mRef.mObject, 11, &second.mText);
        void *token = Call<void *>(holder.mRef.mObject, 8);
        Call<void>(ui, 16, &first.mText, &second.mText, token, 1u, 1u, 1u);
        return 1;
    }

    if (op == 0x16 || op == 0x19) {
        // ORIGINAL-BUG: the stack object (the person when the frame has none) is used below without a null check;
        // a script that reaches here with neither object crashes.
        if (static_cast<int16_t>(static_cast<uint32_t>(As<ifaces::cTSObject>(this)->GetArgument(1u))) == 0) {
            void *service = DialogService();
            if (service) {
                void *ui = Call<void *>(service, 12);
                if (ui) Call<void>(ui, 17);
            }
            return 1;
        }
        if (frameBusy) return 2;
        void *owner = Call<void *>(frame, 20);
        const uint32_t language = Call<uint32_t>(owner, 38);
        void *locale = Call<void *>(list, 14, language);
        void *source = Call<void *>(Call<void *>(locale, 18), 16);
        if ((textFlags & 1) != 0) source = Call<void *>(locale, 70);
        else if ((textFlags & 0x40) != 0) source = Call<void *>(list, 6);

        uint32_t firstEntry;
        if ((textFlags & 0x10) != 0) firstEntry = static_cast<uint32_t>(Call<int16_t>(stackObject, 19, 3u));
        else firstEntry = Byte(param, 0xa);
        Text first;
        LoadText(first, source, firstEntry);

        uint32_t secondEntry;
        if ((textFlags & 2) != 0) secondEntry = static_cast<uint32_t>(Call<int16_t>(stackObject, 19, 0u));
        else secondEntry = Word(param, 0x11);
        Text second;
        LoadText(second, source, secondEntry);

        uint32_t thirdEntry;
        if ((textFlags & 0x20) != 0) thirdEntry = static_cast<uint32_t>(Call<int16_t>(stackObject, 19, 4u));
        else thirdEntry = static_cast<uint32_t>(Byte(param, 6)) << 8 | Byte(param, 4);
        Text third;
        if (source) {
            // The one text loaded through the five-argument loader (flag 1): the group call takes no arguments.
            const uint32_t group = Call<uint32_t>(source, 11);
            Loader loader;
            loader.Construct5(thirdEntry & 0xffff, group, 1);
            third.Take(loader);
        }
        const int32_t number = ParseNumber(third.mText.ToChar());
        void *service = DialogService();
        if (service) {
            void *ui = Call<void *>(service, 12);
            if (ui) {
                const uint32_t flag = op == 0x19 ? 1u : 0u;
                const int32_t second2 = static_cast<int16_t>(static_cast<uint32_t>(As<ifaces::cTSObject>(this)->GetArgument(2u)));
                const int32_t first2 = static_cast<int16_t>(static_cast<uint32_t>(As<ifaces::cTSObject>(this)->GetArgument(1u)));
                Call<void>(ui, 16, &first.mText, &second.mText, number, first2, second2, flag);
            }
        }
        return 1;
    }

    // Everything else is handed to the table's runner (slot 78) with this, the frame, the param and the opcode.
    if ((Byte(param, 0xb) & 1) != 0) {
        Call<void>(table, 78, this, frame, param, 0xffffffffu);
        return 1;
    }
    if (entry == nullptr) {
        if (op == 0x12) return 1;
        const bool needsObject = op == 5 || op == 6 || op == 7 || op == 0xf || op == 0x21 || op == 0xb || op == 0xc || op == 0xd || op == 0x33 ||
                                 op == 0x34 || op == 0x46;
        if (needsObject && person != nullptr && Call<int16_t>(person, 266) < 1) Raise(0x6c);
        Call<void>(table, 78, this, frame, param, static_cast<uint32_t>(op));
        return 2;
    }
    switch (Call<uint32_t>(entry, 7)) {
    case 0:
    case 1:
        return 2;
    case 2: {
        Call<void>(entry, 10);
        if (op == 3) {
            void *name = Call<void *>(entry, 11);
            cRZString copy;
            copy.Construct(*reinterpret_cast<const cIGZString *>(name));
            struct Destroy {
                cRZString *s;
                ~Destroy() { s->Destruct(); }
            } destroy{&copy};
            Call<void>(mOwner, 26, &copy);
        }
        return 1;
    }
    case 3:
        Call<void>(entry, 10);
        // The original checks the selector against 0..7 here (error 2); its three bits can't leave that range.
        if (op == 2) As<ifaces::cTSObject>(this)->SetArgument(selector, 0u);
        return 0;
    case 4:
        Call<void>(entry, 10);
        if (op == 2) As<ifaces::cTSObject>(this)->SetArgument(selector, 1u);
        return 0;
    default:
        return 1;
    }
}
