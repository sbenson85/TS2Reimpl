#include "sg/cTextureManager.h"
#include "rz/Singleton_20ddfb.h"
#include "simantics/PrimAACommon.h"

using namespace primAA;

// SimAntics opcode 0x70, Effect (0x5805c0). The handler picks a target object (operand 1: scope byte +5, id word +6;
// error 0x17), the object's scene component (slot 9 with the identifiers 0xfa1c39f7 / 0xfa1c39ed; error 0x50), loads the
// effect name from string table 0x8f (param word +8, or this's data word 0 with flag bit 3; entry + 1, +1 more for the
// locale's group chosen by flag bits 0 and 1), finds the effect manager of the component's owner (0xb3c1d0) and acts by
// the mode in param byte +4:
//   0, 0xf, 1, 6   add an effect (manager slot 1: kinds 0, 2, 4 with the name and the group text) or, with flag bit 7,
//                  a counted one (slot 2, then the target's slot 281); 0xf marks the "looping" flag
//   2, 3           add an effect of kind 1 / 3
//   4, 5, 14       manager slot 7 with (0,2)+(0,5) / (1,7) / (0,0)
//   7              ask the manager for the shorts of the name (slot 3) and append them to the target's list
//   8, 9           slot 7 (1,2) + target slot 282 / slot 8
//   10..13         slot 12 with 0xf and the mode - 10 as a float
// Modes 0 and 1 then fetch the effect object back (manager slot 6) and attach what the flags name (bits 12, 2, 5, 4,
// 13 and the final record, bit 11/9), ending with slot 14 (3, target).

namespace {

// A 32-bit handle: the cached service 0xae8750 takes a reference on it when it is built (slot 18) and gives it back
// (slot 19) unless it is -1.
struct Tag {
    int32_t v;
    explicit Tag(int32_t handle) : v(handle) { Call<uint32_t>(Service(), 18, handle); }
    Tag(const Tag &) = delete;
    Tag &operator=(const Tag &) = delete;
    ~Tag() noexcept(false)
    {
        if (v != -1) {
            Call<uint32_t>(Service(), 19, v);
            v = -1;
        }
    }
    bool Invalid() const { return v == -1; }
    static void *Service() { return nGZSceneGraph::TextureManager(); }
};

inline void *UiService() // 0x20e006 (the cached service whose slots 6 and 7 hand out handles)
{
    return OriginalFunction<void *(__cdecl *)(), 0x20e006>()();
}

inline void *DialogHolder() // 0x20e031 (the cached dialog service holder; slot 4 gives the dialog service)
{
    return OriginalFunction<void *(__cdecl *)(), 0x20e031>()();
}

inline void *EffectManagerOf(void *component) // 0xb3c1d0: the effect manager the component owns (created on demand)
{
    return OriginalFunction<void *(__cdecl *)(void *), 0xb3c1d0>()(component);
}

constexpr uint32_t kFormatBalloon = 0xdd808c;  // "BalloonEffectThumbnail_WantFear_0x%08x"
constexpr uint32_t kFormatBalloonKey = 0xdd80b4; // the same with a trailing "_0"
constexpr uint32_t kFormatPair = 0xdd80e4;     // "_0x%08x_0x%08x"
constexpr uint32_t kHeadlinesName = 0xdd7a7c;  // a char * variable holding "ShowHeadlines"
constexpr uint32_t kBoolHolderVTable = 0xd9c670;

inline const char *GameText(uint32_t rva)
{
    return static_cast<const char *>(GameAddress<void>(rva));
}

// The boolean the settings lookup fills (57c620: vtable 0xd9c670, byte at +4).
struct BoolHolder {
    const void *vtable = GameAddress<void>(kBoolHolderVTable);
    uint8_t value = 1;
};

// The reference 0x1cbaf3 fills from the framework service (identifiers 0x0ac1887e / 0x4bb0d269); released through its
// slot 2 when the frame leaves.
struct HeadlineSettings {
    void *p = nullptr;
    HeadlineSettings()
    {
        if (void *service = GetServiceObject()) {
            using Query = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
            VirtualCall<Query, 5>(service, 0x0ac1887eu, 0x4bb0d269u, &p);
        }
    }
    HeadlineSettings(const HeadlineSettings &) = delete;
    HeadlineSettings &operator=(const HeadlineSettings &) = delete;
    ~HeadlineSettings() noexcept(false)
    {
        if (p) Call<uint32_t>(p, 2);
    }
};

// Reads a 16-byte key a slot filled: the dwords at +4 and +8 are the ones the texts print.
struct Key16 {
    uint8_t bytes[16] = {};
    uint32_t at4() const { return Dword(bytes, 4); }
    uint32_t at8() const { return Dword(bytes, 8); }
};

} // namespace

uint32_t PrimAAObject::Prim70(void *frame, const PrimParamView *param)
{
    const uint8_t *p = reinterpret_cast<const uint8_t *>(param);

    Text nameA; // the effect name
    const int16_t objectId = Read(this, Byte(p, 5), Word(p, 6));
    void *byId = Call<void *>(mContext, 26);
    void *target = Call<void *>(byId, 29, static_cast<int32_t>(objectId));
    if (!target) Raise(0x17);

    const uint32_t mode = Byte(p, 4);
    const uint32_t kind = Byte(p, 0xd);
    const uint32_t slotIndex = Byte(p, 0xa);
    void *settings = Call<void *>(mContext, 10);
    void *lists = Call<void *>(mContext, 24);
    void *frameOwner = Call<void *>(frame, 20);
    const uint32_t language = Call<uint32_t>(frameOwner, 38);
    void *entryOwner = Call<void *>(lists, 14, language);

    int32_t nameIndex = static_cast<int16_t>(Word(p, 8));
    if (Bit(p, 0xe, 3)) nameIndex = DataWord(this, 0);
    if (nameIndex != -1 && (mode < 10 || mode > 13)) {
        nameIndex += 1;
        const uint32_t entry = static_cast<uint16_t>(nameIndex);
        if (Bit(p, 0xe, 0)) {
            LoadInto(nameA, 0x8f, entry, kDefaultGroup);
        } else if (Bit(p, 0xe, 1)) {
            if (Call<void *>(entryOwner, 70) != nullptr) {
                void *group = Call<void *>(entryOwner, 70);
                LoadInto(nameA, 0x8f, entry, Call<uint32_t>(group, 11));
            }
        } else {
            void *group = Call<void *>(entryOwner, 18);
            LoadInto(nameA, 0x8f, entry, Call<uint32_t>(group, 11));
        }
        if (nameA.str().empty()) Raise(0x4f);
    }

    // The scene component of the target (or, for a target that wraps another object, of the object behind it).
    VBRef component;
    component.r.ResetVirtualBase();
    Call<uint8_t>(target, 9, kIidA, kIidB, component.out());
    void *base = target;
    if (Call<uint8_t>(target, 104) != 0) {
        OutRef iface; // released when the block ends, also when an error leaves it
        Call<uint8_t>(target, 0, 0x5a6216f6u, &iface.p);
        // The query's answer is not checked (a missing interface faults here in the original).
        base = Call<void *>(Call<void *>(iface.p, 6), 11);
        component.r.ResetVirtualBase();
        Call<uint8_t>(base, 9, kIidA, kIidB, component.out());
    }
    if (component.get() == nullptr) Raise(0x50);

    Text nameB; // the group text the manager calls fill in
    // The original also builds a 5-character default string ("Edith") here and never reads it: omitted.
    const bool plainModes = mode == 0 || mode == 1 || mode == 2 || mode == 3 || mode == 6 || mode == 7 || mode == 15;

    VBRef component2;
    if (base != nullptr) {
        component2.r.ResetVirtualBase();
        const bool found = Call<uint8_t>(base, 9, kIidA, kIidB, component2.out()) != 0;
        if (found && plainModes) {
            void *chosen = nullptr;
            switch (kind) {
            case 0:
                if (Call<void *>(target, 254, slotIndex) != nullptr) {
                    void *entryObject = Call<void *>(target, 254, slotIndex);
                    chosen = Call<void *>(entryObject, 3);
                    Call<uint32_t>(target, 82, chosen, 0u, &nameB.mText, 1u);
                }
                break;
            case 1:
                if (Call<void *>(target, 251, slotIndex) != nullptr) {
                    void *entryObject = Call<void *>(target, 251, slotIndex);
                    chosen = Call<void *>(entryObject, 3);
                    Call<uint32_t>(target, 82, chosen, 0u, &nameB.mText, 1u);
                }
                break;
            case 2:
                if (Call<void *>(target, 68, slotIndex) != nullptr) {
                    void *entryObject = Call<void *>(target, 68, slotIndex);
                    chosen = Call<void *>(entryObject, 3);
                    Call<uint32_t>(target, 82, chosen, 0u, &nameB.mText, 1u);
                }
                break;
            default:
                break;
            }
            if (chosen == nullptr) Raise(0x1b);
        }
    }

    // Headlines: the "ShowHeadlines" setting can switch the whole primitive off.
    if (base != nullptr && Call<int32_t>(base, 98) == 2) {
        if (kind == 2 && slotIndex == 4) {
            BoolHolder holder;
            HeadlineSettings headlines;
            if (headlines.p != nullptr) {
                bool answered;
                {
                    std::string name(*GameAddress<const char *>(kHeadlinesName));
                    answered = Call<uint8_t>(headlines.p, 9, &name, &holder) != 0;
                }
                if (answered && holder.value == 0) return 1;
            }
        }
    }

    int16_t groupWord = 0;
    if (Bit(p, 0xe, 6)) groupWord = DataWord(this, 1);
    if (nameIndex == -1 && mode != 5 && mode != 9 && mode != 8 && mode != 4 && mode != 14) Raise(0x83);
    if ((mode == 0 || mode == 1 || mode == 6) && Call<uint8_t>(target, 188) == 0) return 1;

    // ORIGINAL-BUG: when the base object has no scene component (its slot 9 answers no) the getter gets a null
    // component and faults (the original does the same).
    void *effects = EffectManagerOf(component2.get());
    uint32_t counted = 0;
    bool looping = false;
    const bool countedForm = Bit(p, 0xe, 7);

    // The counted form of modes 0, 1 and 6: manager slot 2, then the target's slot 281 with two data words.
    auto addCounted = [&](uint32_t flag) {
        counted = Call<uint32_t>(effects, 2, 5u, nameA.c_str(), nameB.c_str(), static_cast<int32_t>(groupWord), flag);
        const int32_t d3 = DataWord(this, 3);
        const int32_t d2 = DataWord(this, 2);
        Call<uint32_t>(target, 281, 0u, counted, d2, d3, effects);
    };

    switch (mode) {
    case 9:
        Call<uint32_t>(effects, 8, 0u);
        break;
    case 8:
        Call<uint32_t>(effects, 7, 1u, 2u);
        Call<uint32_t>(target, 282);
        break;
    case 15:
        looping = true;
        [[fallthrough]];
    case 0:
        if (countedForm) {
            addCounted(looping ? 1u : 0u);
        } else {
            Call<uint32_t>(effects, 1, 0u, nameA.c_str(), nameB.c_str(), 2u, static_cast<int32_t>(groupWord), looping ? 1u : 0u);
        }
        // Slot 13 of this with 0x1d: -1 asks the manager (slot 10) for its default.
        if (static_cast<int16_t>(Call<uint32_t>(this, 13, 0x1du)) == -1) Call<uint32_t>(effects, 10, 1u);
        break;
    case 1:
        if (countedForm) {
            addCounted(0u);
        } else {
            Call<uint32_t>(effects, 1, 2u, nameA.c_str(), nameB.c_str(), 2u, static_cast<int32_t>(groupWord), 0u);
        }
        break;
    case 2:
        Call<uint32_t>(effects, 1, 1u, nameA.c_str(), nameB.c_str(), 2u, 0u, 0u);
        break;
    case 3:
        Call<uint32_t>(effects, 1, 3u, nameA.c_str(), nameB.c_str(), 2u, 0u, 0u);
        break;
    case 4:
        Call<uint32_t>(effects, 7, 0u, 2u);
        Call<uint32_t>(effects, 7, 0u, 5u);
        break;
    case 14:
        Call<uint32_t>(effects, 7, 0u, 0u);
        break;
    case 5:
        Call<uint32_t>(effects, 7, 1u, 7u);
        break;
    case 6:
        if (countedForm) {
            addCounted(0u);
        } else {
            Call<uint32_t>(effects, 1, 4u, nameA.c_str(), nameB.c_str(), 2u, static_cast<int32_t>(groupWord), 0u);
        }
        break;
    case 7: {
        if (static_cast<int16_t>(Call<uint32_t>(this, 266)) < 1) Raise(0x6c);
        ShortVec *list = Call<ShortVec *>(this, 265, 0u);
        if (list != nullptr) {
            list->Clear();
            ShortVec shorts;
            if (Call<uint8_t>(effects, 3, nameB.c_str(), &shorts) != 0) {
                for (size_t i = 0; i < shorts.size(); ++i) list->PushBack(shorts.first[i]);
            }
        }
        break;
    }
    case 10:
    case 11:
    case 12:
    case 13: {
        float amount = static_cast<float>(static_cast<int32_t>(mode) - 10);
        Call<uint32_t>(effects, 12, 0xfu, &amount, 1u);
        break;
    }
    default:
        break;
    }

    if (mode == 0 || mode == 1) {
        void *effect = Call<void *>(effects, 6, nameA.c_str(), nameB.c_str(), static_cast<int32_t>(groupWord));
        if (effect != nullptr) {
            void *record = nullptr; // the object whose record the final text describes
            if (Bit(p, 0xe, 12)) {
                Text text;
                void *service = UiService();
                const uint32_t handle = Call<uint32_t>(service, 7, static_cast<int32_t>(DataWord(this, 6)), &text.mText);
                Tag tag(static_cast<int32_t>(handle));
                if (!tag.Invalid()) Call<uint32_t>(effect, 14, 0u, NewStringCopy(text.mText));
            } else if (Bit(p, 0xe, 2)) {
                if (Bit(p, 0xe, 8)) {
                    void *cursor = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(this) + 0x1c0);
                    const uint32_t id = Call<uint32_t>(cursor, 6);
                    record = Call<void *>(lists, 14, id);
                } else if (Bit(p, 0xe, 10)) {
                    const uint32_t low = static_cast<uint16_t>(DataWord(this, 4));
                    const int32_t high = DataWord(this, 5);
                    record = Call<void *>(lists, 14, low | static_cast<uint32_t>(high << 16));
                } else {
                    const int16_t id = Read(this, Byte(p, 0x10), Word(p, 0x11));
                    void *object = Call<void *>(byId, 29, static_cast<int32_t>(id));
                    if (!object) Raise(0x17);
                    record = Call<void *>(Call<void *>(object, 61), 27);
                }
            } else if (Bit(p, 0xe, 5)) {
                const int16_t value = Read(this, Byte(p, 0x10), Word(p, 0x11));
                float scaled = static_cast<float>(static_cast<int32_t>(value));
                Call<uint32_t>(effect, 13, 0xdu, &scaled, 1u);
                const uint16_t entry = static_cast<uint16_t>(Byte(p, 0x13) + 1);
                void *lang = Call<void *>(entryOwner, 18);
                Loader text;
                text.Construct(0x95, entry, Call<uint32_t>(lang, 11), 1);
                if (text.empty()) {
                    if (Call<void *>(entryOwner, 70) != nullptr) {
                        void *group = Call<void *>(entryOwner, 70);
                        Loader fallback;
                        fallback.Construct(0x95, entry, Call<uint32_t>(group, 11), 1);
                        text.Assign(fallback);
                    }
                    if (text.empty()) {
                        Loader fallback;
                        fallback.Construct(0x95, entry, kDefaultGroup, 1);
                        text.Assign(fallback);
                    }
                }
                if (text.text().Strlen() != 0) Call<uint32_t>(effect, 14, 0u, NewStringCopy(text.text()));
            } else if (Bit(p, 0xe, 4)) {
                const int16_t value = Read(this, Byte(p, 0x10), Word(p, 0x11));
                void *found = Call<void *>(settings, 16, static_cast<int32_t>(value), 1u);
                if (found != nullptr) record = Call<void *>(found, 10);
            } else if (Bit(p, 0xe, 13)) {
                const int16_t value = Read(this, Byte(p, 0x10), Word(p, 0x11));
                void *object = Call<void *>(byId, 29, static_cast<int32_t>(value));
                OutRef iface;
                if (!object) Raise(9);
                if (Call<uint8_t>(object, 0, 0x5a69d3d5u, &iface.p) == 0) Raise(9);
                const bool first = DataWord(this, 4) == 0;
                const int32_t index = DataWord(this, 5);
                void *shared = GetCachedSingleton();
                void *registry = Call<void *>(shared, 4);
                if (registry != nullptr) {
                    const uint32_t kindWord = static_cast<uint16_t>(Call<uint32_t>(iface.p, 57));
                    void **list = first ? Call<void **>(registry, 21, kindWord) : Call<void **>(registry, 22, kindWord);
                    const uint32_t count = static_cast<uint32_t>(reinterpret_cast<intptr_t>(list[1]) - reinterpret_cast<intptr_t>(list[0])) >> 2;
                    // (the original shifts the signed difference; the lists are far below 2 GiB)
                    if (!(static_cast<uint32_t>(index) < count)) Raise(2);
                    void *item = reinterpret_cast<void **>(list[0])[index];
                    if (item != nullptr) {
                        uint8_t found = 0;
                        bool claimed = false;
                        OutRef g1;
                        OutRef g2;
                        Text h;
                        void *dialogs = Call<void *>(DialogHolder(), 4);
                        if (dialogs != nullptr) {
                            void *owner = Call<void *>(item, 17);
                            if (owner != nullptr) {
                                if (Call<uint8_t>(owner, 38) != 0) {
                                    claimed = true;
                                    g1.Reset();
                                    Call<uint32_t>(dialogs, 14, owner, kIidDialog, &g1.p);
                                } else {
                                    g1.Reset();
                                    Call<uint32_t>(dialogs, 31, &found, owner, kIidDialog, &g1.p, 0u);
                                }
                                h.mText.Sprintf(GameText(kFormatBalloon), Call<uint32_t>(owner, 28));
                            } else {
                                Key16 key;
                                Call<void *>(item, 8, key.bytes);
                                h.mText.Sprintf(GameText(kFormatBalloonKey), key.at4(), key.at8());
                                g1.Reset();
                                Key16 lookup;
                                void *keyPtr = Call<void *>(item, 8, lookup.bytes);
                                found = Call<uint8_t>(dialogs, 60, keyPtr, kIidDialog, &g1.p);
                            }
                            g2.Reset();
                            Key16 second;
                            void *secondPtr = Call<void *>(item, 9, second.bytes);
                            Call<uint8_t>(dialogs, 60, secondPtr, kIidDialog, &g2.p);
                            if (g2.p != nullptr) {
                                Text pair;
                                Key16 key;
                                Call<void *>(item, 9, key.bytes);
                                pair.mText.Sprintf(GameText(kFormatPair), key.at4(), key.at8());
                                h.AppendText(pair);
                            }
                            AttrVec requests;
                            requests.PushBack(Attr{g1.p, static_cast<uint8_t>(claimed ? 1 : 0), 1.0f, 1, 4});
                            requests.PushBack(Attr{g2.p, 0, 0.61f, 2, 5});
                            void *service = UiService();
                            // The vector goes to the callee by value, which takes the buffer over.
                            AttrVecWords words;
                            {
                                AttrVec handoff;
                                handoff.CopyFrom(requests);
                                words = AttrVecWords{handoff.first, handoff.last, handoff.end};
                                handoff.first = handoff.last = handoff.end = nullptr;
                            }
                            const uint32_t handle = Call<uint32_t>(service, 4, words, &h.mText, static_cast<uint32_t>(claimed ? 1 : 0));
                            Tag tag(static_cast<int32_t>(handle));
                            if (!tag.Invalid()) Call<uint32_t>(effect, 14, 0u, NewStringCopy(h.mText));
                        }
                    }
                }
            }
            if (record != nullptr) {
                Text text;
                void *info = Call<void *>(record, 4);
                int32_t value = static_cast<int32_t>(Call<uint32_t>(info, 3, 0xccu));
                if (Bit(p, 0xe, 11)) value = DataWord(this, 6);
                void *service = UiService();
                const uint32_t handle = Call<uint32_t>(service, 6, &text.mText, record, static_cast<uint32_t>(Bit(p, 0xe, 9)), value);
                Tag tag(static_cast<int32_t>(handle));
                if (!tag.Invalid()) Call<uint32_t>(effect, 14, 0u, NewStringCopy(text.mText));
            }
            Call<uint32_t>(effect, 14, 3u, target);
        }
    }
    return 1;
}
