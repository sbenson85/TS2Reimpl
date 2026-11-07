#include "sg/cTextureManager.h"
#include "misc/UtilitiesE.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "rz/ServiceGetters.h"
#include "rz/cRZFile.h"
#include "simantics/PrimAACommon.h"

using namespace primAA;

// SimAntics opcode 0x6d, MaterialChange (0x5827e0). The handler reads the target object (operand 1: scope byte +9, id
// word +10; error 0x17) and, for the flag byte at param +0x11, one of five jobs:
//   bit 1 or 5   a TV/paintings job through the file and UI services: the cached state singleton must not report 10,
//                then (frame slot 18 == 0) the dialog service's slot-7 object is told a mode from this's data words
//                2/3 and the primitive ends with 2; otherwise the object's file is opened and handed to the service,
//                bit 5 ending with data word 2 set from a lookup
//   bit 2        a dialog request: ids from the data words 0/1, a property set from word 2, the dialog service's slots
//                15/17/36/63 (the result lands in the output holder)
//   bit 3        a "painting_%08lx" name made from data words 4/5 through the key parser
//   bit 4        a property set from data words 0..3 handed to the dialog service (slot 63)
// Two string-table texts are loaded first (table 0x88 into D, 0x87 into E; entries and groups from the flags in the
// bytes +6 and +0x11), then the material is bound: a new texture/material name from a fresh id (data words 4/5), the
// effect's property list ("stdMatBaseTextureName" ...), or the TV station's screen. Every path ends with the target
// object's slot 104 (D, E, 1) and slot 296 (1), returning 1; some return 2 early.

namespace {

// The handle shape of the cached queried object (0xae8900; slots 19/20/21 = 0x4c/0x50/0x54): built from an id (it
// takes a reference, slot 20) or from a string (slot 19 gives the id, then slot 20); Assign takes a reference on the
// source, releases the old one (slot 21) and copies the id; the destructor releases (slot 21) unless invalid.
struct Tag2 {
    int32_t v;
    explicit Tag2(int32_t handle) : v(handle) { Call<uint32_t>(Service(), 20, handle); }
    explicit Tag2(const char *name)
    {
        v = static_cast<int32_t>(Call<uint32_t>(Service(), 19, name));
        Call<uint32_t>(Service(), 20, v);
    }
    Tag2() : v(-1) {} // 0x18bf80: the invalid handle
    Tag2(const Tag2 &) = delete;
    Tag2 &operator=(const Tag2 &) = delete;
    void Assign(const Tag2 &src) // 0xae88a0
    {
        Call<uint32_t>(Service(), 20, src.v);
        Call<uint32_t>(Service(), 21, v);
        v = src.v;
    }
    ~Tag2() noexcept(false)
    {
        if (v != -1) {
            Call<uint32_t>(Service(), 21, v);
            v = -1;
        }
    }
    static void *Service() { return GetCachedQueriedObject(); } // 0xae8900
};

inline void *DialogHolder() // 0x20e031: the cached dialog service holder (slot 4: the dialog service, slot 7: its object)
{
    return OriginalFunction<void *(__cdecl *)(), 0x20e031>()();
}
inline void *FileService() // 0x20df26: the cached service whose slot 5 hands out the texture/material tables
{
    return OriginalFunction<void *(__cdecl *)(), 0x20df26>()();
}
inline void *ResourceManager() // 0x20deb5
{
    return OriginalFunction<void *(__cdecl *)(), 0x20deb5>()();
}
inline void *PaintingService() // 0x211da3: slot 42 the wall-art object, slot 66 the station table
{
    return OriginalFunction<void *(__cdecl *)(), 0x211da3>()();
}
// 0xae7340 (__cdecl: output string, text, id, key): turns a "##" text or a keyed name into the final text of `out`.
inline void ResolveKey(cRZString *out, const char *text, void *id, const char *key)
{
    nGZSceneGraph::ResolveResourceName(out->AsInterface(), text, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(id)), key);
}
// 0xaece50 (__cdecl): builds the texture object of `material` named `name` into *result.
inline void MakeTexture(void *material, void **result, const char *name, uint32_t flag)
{
    OriginalFunction<void(__cdecl *)(void *, void **, const char *, uint32_t), 0xaece50>()(material, result, name, flag);
}
// 0x54a410 (__cdecl, one output slot): finds the one resource of the cached lists and stores it in *result.
inline void FindResource(void **result)
{
    OriginalFunction<uint8_t(__cdecl *)(void **), 0x54a410>()(result);
}
inline bool OptionSet(uint32_t index) // 0xb394b0: bit `index` of the option mask
{
    return OriginalFunction<uint8_t(__cdecl *)(uint32_t), 0xb394b0>()(index) != 0;
}
// 0x54a380: slot 11 of the cached slot-23 object (null without one).
inline void *Slot23Value()
{
    void *cached = GetCachedSlot23();
    return cached ? Call<void *>(cached, 11) : nullptr;
}

inline const char *GameText(uint32_t rva)
{
    return static_cast<const char *>(GameAddress<void>(rva));
}
inline const char *GameString(uint32_t rva) // a char * variable holding a text
{
    return *GameAddress<const char *>(rva);
}

constexpr uint32_t kFormatPainting = 0xdd7fb0;    // "painting_%08lx"
constexpr uint32_t kKeyType = 0xdd7a84;           // "type"
constexpr uint32_t kKeySuccess = 0xdd7a80;        // "successCase"
constexpr uint32_t kKeySimA = 0xdd7a88;           // "simA"
constexpr uint32_t kKeySimB = 0xdd7a8c;           // "simB"
constexpr uint32_t kSuffixTexture = 0xdd7a90;     // "_txtr"
constexpr uint32_t kSuffixMaterial = 0xdd7a94;    // "_txmt"
constexpr uint32_t kTextTrue = 0xda2d44;          // "true"
constexpr uint32_t kTextFalse = 0xda2d3c;         // "false"
constexpr uint32_t kPropBaseTexture = 0xdb7724;   // "stdMatBaseTextureName"
constexpr uint32_t kPropBaseTextureOn = 0xdd7fc0; // "stdMatBaseTextureEnabled"
constexpr uint32_t kPropRemove = 0xdd7fdc;        // "kRemoveFromSegmentWhenUnused"
constexpr uint32_t kTvScreen = 0xdd7ffc;          // "tvscreenanimations_screen_simscommercial"
constexpr uint32_t kPropEachFrame = 0xdd8028;     // "modifiedEachFrame"
constexpr uint32_t kPropNotify = 0xdd803c;        // "shaderNotifyString"
constexpr uint32_t kTextVideo = 0xdd8050;         // "video"
constexpr uint32_t kPropAnimMode = 0xdd8058;      // "stdMatTextureCoordAnimMode"
constexpr uint32_t kPropLighting = 0xdd8074;      // "stdMatLightingEnabled"
constexpr uint32_t kTvNone = 0xdb3688;            // "tvstation_none"

} // namespace

uint32_t PrimAAObject::Prim6D(void *frame, const PrimParamView *param)
{
    const uint8_t *p = reinterpret_cast<const uint8_t *>(param);

    void *byId = Call<void *>(mContext, 26);
    const int16_t targetId = Read(this, Byte(p, 9), Word(p, 0xa));
    void *target = Call<void *>(byId, 29, static_cast<int32_t>(targetId));
    if (!target) Raise(0x17);
    void *lists = Call<void *>(mContext, 24);

    const bool fA = Bit(p, 0x11, 1) || Bit(p, 0x11, 5);
    const bool fB = Bit(p, 0x11, 2);
    Text text; // D: the main text

    if (!fA && !fB) {
        const int32_t entryIndex = Bit(p, 6, 4) ? static_cast<int32_t>(DataWord(this, 0)) : static_cast<int32_t>(Word(p, 4));
        void *locale = nullptr;
        UnkRef holder(this);
        if (Bit(p, 6, 3)) {
            const int16_t id = Read(this, Byte(p, 0xc), Word(p, 0xd));
            void *object = Call<void *>(byId, 29, static_cast<int32_t>(id));
            if (!object) Raise(0x17);
            locale = Call<void *>(object, 61);
            holder.Assign(object);
        } else {
            void *owner = Call<void *>(frame, 20);
            locale = Call<void *>(lists, 14, Call<uint32_t>(owner, 38));
            if (target) holder.Assign(target);
        }
        const uint32_t entry = static_cast<uint32_t>(entryIndex + 1);
        if (Bit(p, 6, 1)) {
            LoadInto(text, 0x88, entry, kDefaultGroup);
        } else if (Bit(p, 6, 2)) {
            if (Call<void *>(locale, 70) != nullptr) {
                void *group = Call<void *>(locale, 70);
                LoadInto(text, 0x88, entry, Call<uint32_t>(group, 11));
            }
        } else {
            void *source = *reinterpret_cast<void **>(static_cast<uint8_t *>(holder.p) + 0x18c);
            void *found = Call<void *>(source, 6, entry, 0u);
            Call<uint32_t>(&text.mText, 16, found); // D.Assign
            if (text.str().empty()) {
                void *lang = Call<void *>(locale, 18);
                LoadInto(text, 0x88, entry, Call<uint32_t>(lang, 11));
            }
        }
        if (text.str().empty()) Raise(0x46);
    }

    const int32_t groupIndex = Bit(p, 6, 5) ? static_cast<int32_t>(DataWord(this, 1)) : static_cast<int32_t>(Word(p, 7));
    void *locale2 = nullptr;
    if (Bit(p, 6, 0)) {
        const int16_t id = Read(this, Byte(p, 0xc), Word(p, 0xd));
        void *object = Call<void *>(byId, 29, static_cast<int32_t>(id));
        if (!object) Raise(0x17);
        locale2 = Call<void *>(object, 61);
    } else {
        void *owner = Call<void *>(frame, 20);
        locale2 = Call<void *>(lists, 14, Call<uint32_t>(owner, 38));
    }
    Text extra; // E: the second text
    if (groupIndex != -1) {
        const uint32_t entry = static_cast<uint32_t>(groupIndex + 1);
        if (Bit(p, 6, 6)) {
            LoadInto(extra, 0x87, entry, kDefaultGroup);
        } else if (Bit(p, 6, 7)) {
            if (Call<void *>(locale2, 70) != nullptr) {
                void *group = Call<void *>(locale2, 70);
                LoadInto(extra, 0x87, entry, Call<uint32_t>(group, 11));
            }
        } else {
            void *lang = Call<void *>(locale2, 18);
            LoadInto(extra, 0x87, entry, Call<uint32_t>(lang, 11));
        }
    }

    VBRef component;
    component.r.ResetVirtualBase();
    Call<uint8_t>(target, 9, kIidA, kIidB, component.out());
    if (component.get() == nullptr) Raise(0x45);

    void *service = FileService();
    if (service == nullptr) return 1;

    const bool f6a = Bit(p, 0x11, 3);
    const bool f3a = Bit(p, 0x11, 4);
    const bool f69 = Bit(p, 0x11, 5);
    OutRef current; // the object the dialog/file services fill in

    if (fA) {
        void *state = GetCachedSingletonB();
        if (Call<int32_t>(state, 23) == 10) return 2;
        if (Call<int32_t>(frame, 18) == 0) {
            UnkRef holder(Call<void *>(DialogHolder(), 7));
            const int32_t selector = DataWord(this, 2);
            if (selector == 1) {
                Call<uint32_t>(holder.p, 18, 0u, static_cast<int32_t>(DataWord(this, 3)));
            } else if (selector == 2) {
                Call<uint32_t>(holder.p, 18, 1u, static_cast<int32_t>(DataWord(this, 3)));
            } else if (selector == 3) {
                Call<uint32_t>(holder.p, 18, 2u, static_cast<int32_t>(DataWord(this, 3)));
            } else {
                Call<uint32_t>(holder.p, 18, 0u, static_cast<int32_t>(DataWord(this, 3)));
            }
            Call<uint32_t>(frame, 19, 1u);
            return 2;
        }
        UnkRef panel(Call<void *>(DialogHolder(), 7));
        if (Call<uint8_t>(panel.p, 20) == 0) {
            Call<uint32_t>(panel.p, 17); // the answer is not used
            if (Call<uint8_t>(panel.p, 22) == 0) {
                Call<uint32_t>(this, 20, 2u, 0u);
                return 1;
            }
            return 2;
        }
        OutRef factory;
        void *director = GetDirectorFrameworkObject();
        if (Call<uint8_t>(director, 3, 0xa2ffb5d3u, 0x02ffbb60u, &factory.p) == 0) {
            Call<uint32_t>(this, 20, 2u, 0u);
            return 1;
        }
        cRZFile *file = static_cast<cRZFile *>(::operator new(sizeof(cRZFile)));
        file->Construct();
        UnkRef fileRef(file);
        Text path;
        Call<uint32_t>(panel.p, 21, &path.mText);
        Call<uint32_t>(file, 29, &path.mText);
        if (Call<uint8_t>(file, 3, 1u, 2u, 1u) == 0) {
            Call<uint32_t>(this, 20, 2u, 0u);
            return 1;
        }
        const bool loaded = Call<uint8_t>(factory.p, 12, file, 5u, current.Reset(), 8u) != 0;
        if (loaded && f69) {
            void *named = Call<void *>(current.p, 12, 0x5au);
            if (Call<uint8_t>(panel.p, 23, path.c_str(), named) == 0) {
                Call<uint32_t>(this, 20, 2u, 0u);
            } else {
                Call<uint32_t>(this, 20, 2u, 1u);
            }
            return 1;
        }
    } else if (fB) {
        void *dialogs = Call<void *>(DialogHolder(), 4);
        if (dialogs != nullptr) {
            uint32_t id = 0;
            UnkRef person;
            if (DataWord(this, 1) == 0) {
                const int16_t word = DataWord(this, 0);
                void *registry = Call<void *>(mContext, 10);
                void *found = Call<void *>(registry, 16, static_cast<int32_t>(word), 1u);
                if (found != nullptr) id = Call<uint32_t>(found, 15);
            } else {
                const uint32_t low = static_cast<uint16_t>(DataWord(this, 0));
                const int32_t high = DataWord(this, 1);
                id = low | static_cast<uint32_t>(high << 16);
            }
            if (id != 0) {
                person.Assign(Call<void *>(lists, 14, id));
                if (person.p != nullptr) {
                    if (Call<uint8_t>(person.p, 38) != 0) {
                        if (DataWord(this, 2) == 1) {
                            UnkRef properties(NewPropertySet());
                            {
                                std::string key(GameString(kKeyType));
                                PropInt value(DataWord(this, 2));
                                Call<uint32_t>(properties.p, 3, &key, &value);
                            }
                            current.Reset();
                            Call<uint32_t>(dialogs, 17, id, 0x100u, 0x100u, kIidDialog, &current.p, 0u, properties.p);
                        } else {
                            current.Reset();
                            Call<uint32_t>(dialogs, 15, id, kIidDialog, &current.p);
                        }
                    } else {
                        void *object = Call<void *>(byId, 42, id);
                        if (object != nullptr) {
                            current.Reset();
                            Call<uint32_t>(dialogs, 36, object, kIidDialog, &current.p);
                        }
                    }
                }
            }
        }
    } else if (f6a) {
        void *names = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
        void *chain = Call<void *>(Call<void *>(Call<void *>(mContext, 23), 10), 8);
        void *group = Call<void *>(names, 11, chain);
        const uint32_t low = static_cast<uint16_t>(DataWord(this, 4));
        const int32_t high = DataWord(this, 5);
        text.mText.Sprintf(GameText(kFormatPainting), low | static_cast<uint32_t>(high << 16));
        Text resolved;
        ResolveKey(&resolved.mText, text.c_str(), group, nullptr);
        AssignText(text.mText, resolved.mText);
    } else if (f3a) {
        const int16_t typeWord = DataWord(this, 0);
        const bool success = DataWord(this, 1) == 1;
        const int16_t simA = DataWord(this, 2);
        const int16_t simB = DataWord(this, 3);
        UnkRef properties(NewPropertySet());
        {
            std::string key(GameString(kKeyType));
            PropInt value(typeWord);
            Call<uint32_t>(properties.p, 3, &key, &value);
        }
        {
            std::string key(GameString(kKeySuccess));
            PropBool value(success);
            Call<uint32_t>(properties.p, 3, &key, &value);
        }
        {
            std::string key(GameString(kKeySimA));
            PropInt value(simA);
            Call<uint32_t>(properties.p, 3, &key, &value);
        }
        {
            std::string key(GameString(kKeySimB));
            PropInt value(simB);
            Call<uint32_t>(properties.p, 3, &key, &value);
        }
        void *dialogs = Call<void *>(DialogHolder(), 4);
        if (dialogs != nullptr) {
            current.Reset();
            Call<uint32_t>(dialogs, 63, properties.p, kIidDialog, &current.p);
        }
    }

    Tag2 materialTag; // the material the code below registers (starts invalid)
    if (current.p != nullptr && (fA || fB || f3a)) {
        const int16_t titleObject = Read(this, Byte(p, 0xc), Word(p, 0xd));
        void *object = Call<void *>(byId, 29, static_cast<int32_t>(titleObject));
        if (!object) Raise(0x17);
        void *lang = Call<void *>(Call<void *>(object, 61), 18);
        Loader title;
        title.Construct(0x88, 1, Call<uint32_t>(lang, 11), 1);
        if (title.empty()) Raise(0x46);

        OutRef resource;
        resource.Reset();
        FindResource(&resource.p);
        void *slot23 = Slot23Value();
        uint32_t newId = 0;
        Call<uint32_t>(GetDirectorFrameworkObject(), 7, &newId);
        Text name;
        name.mText.Sprintf(GameText(kFormatPainting), newId);
        Text label;
        ResolveKey(&label.mText, name.c_str(), slot23, nullptr);
        Call<uint32_t>(this, 20, 5u, newId >> 16);
        Call<uint32_t>(this, 20, 4u, newId & 0xffffu);
        OutRef texture;
        MakeTexture(current.p, texture.Reset(), label.c_str(), 1);
        UnkRef shader;
        OutRef iface;
        // ORIGINAL-BUG: when the texture object has no such interface the shader stays empty and the calls below
        // dereference it (the original faults the same way).
        if (Call<uint8_t>(texture.p, 0, 0x2a956da8u, &iface.p) != 0) {
            shader.Assign(Call<void *>(iface.p, 11));
        }
        Text bound;
        {
            std::string joined = name.str() + GameString(kSuffixTexture);
            bound.str().assign(joined);
        }
        ResolveKey(&bound.mText, bound.c_str(), slot23, nullptr);
        {
            void *textures = Call<void *>(shader.p, 5);
            Call<uint32_t>(textures, 12, bound.c_str(), resource.p);
        }
        void *manager = ResourceManager();
        {
            void *textures = Call<void *>(shader.p, 5);
            void *table = Call<void *>(textures, 8);
            uint8_t scratch[0x14] = {};
            void *entry = Call<void *>(table, 3, scratch, resource.p);
            Call<uint32_t>(manager, 14, table, entry);
        }
        Tag2 titleTag(title.c_str());
        void *registry = Call<void *>(service, 5);
        uint8_t reply[0x14] = {};
        Call<uint32_t>(registry, 33, reply, titleTag.v);
        UnkRef material(Call<void *>(*reinterpret_cast<void **>(reply + 4), 7));
        Call<uint32_t>(material.p, 13, GameText(kPropBaseTexture), label.c_str());
        Call<uint32_t>(material.p, 13, GameText(kPropBaseTextureOn), GameText(kTextTrue));
        Call<uint32_t>(material.p, 13, GameText(kPropRemove), GameText(kTextTrue));
        Call<uint32_t>(material.p, 10, name.c_str());
        {
            Tag2 registered(static_cast<int32_t>(Call<uint32_t>(registry, 23, material.p, 2u)));
            materialTag.Assign(registered);
        }
        {
            std::string joined = name.str() + GameString(kSuffixMaterial);
            bound.str().assign(joined);
        }
        ResolveKey(&bound.mText, bound.c_str(), slot23, nullptr);
        {
            void *lists2 = Call<void *>(material.p, 9);
            Call<uint32_t>(lists2, 12, bound.c_str(), resource.p);
        }
        {
            void *lists2 = Call<void *>(material.p, 9);
            void *table = Call<void *>(lists2, 8);
            uint8_t scratch[0x14] = {};
            void *entry = Call<void *>(table, 3, scratch, resource.p);
            Call<uint32_t>(manager, 14, table, entry);
        }
        AssignText(text.mText, label.mText);
        Call<uint32_t>(this, 20, 2u, 1u);
    } else {
        if (Bit(p, 0x11, 0)) {
            void *panel = Call<void *>(PaintingService(), 42);
            if (panel != nullptr) {
                Call<uint32_t>(panel, 7, static_cast<int32_t>(static_cast<int16_t>(Call<uint32_t>(frame, 12))));
                if (!text.mText.IsEqual(GameText(kTvNone), 0xffffffffu, false)) {
                    void *tables = Call<void *>(service, 5);
                    // (the original builds and drops a text here when slot 32 says yes)
                    if (Call<uint8_t>(tables, 32, text.c_str()) == 0 && Call<uint8_t>(panel, 8, text.c_str()) != 0) {
                        Tag2 screenTag(GameText(kTvScreen));
                        uint8_t reply[0x14] = {};
                        Call<uint32_t>(tables, 33, reply, screenTag.v);
                        UnkRef screen(Call<void *>(*reinterpret_cast<void **>(reply + 4), 7));
                        Call<uint32_t>(screen.p, 13, GameText(kPropEachFrame), GameText(kTextTrue));
                        Call<uint32_t>(screen.p, 13, GameText(kPropNotify), text.c_str());
                        Call<uint32_t>(screen.p, 13, GameText(kPropAnimMode), GameText(kTextVideo));
                        Call<uint32_t>(screen.p, 13, GameText(kPropLighting), GameText(kTextFalse));
                        Call<uint32_t>(screen.p, 13, GameText(kPropBaseTexture), text.c_str());
                        Call<uint32_t>(screen.p, 13, GameText(kPropBaseTextureOn), GameText(kTextTrue));
                        Call<uint32_t>(screen.p, 10, text.c_str());
                        Tag2 registered(static_cast<int32_t>(Call<uint32_t>(tables, 23, screen.p, 2u)));
                        materialTag.Assign(registered);
                    }
                    Call<uint32_t>(panel, 6, static_cast<int32_t>(static_cast<int16_t>(Call<uint32_t>(frame, 12))), text.c_str());
                }
            }
        } else {
            void *panel = Call<void *>(PaintingService(), 42);
            if (panel != nullptr) Call<uint32_t>(panel, 7, static_cast<int32_t>(static_cast<int16_t>(Call<uint32_t>(frame, 12))));
        }
    }

    if (OptionSet(7)) {
        void *stations = Call<void *>(PaintingService(), 66);
        Text replaced;
        if (stations != nullptr && Call<uint8_t>(stations, 8, &text.mText, &replaced.mText, 0u) != 0) AssignText(text.mText, replaced.mText);
    }
    Call<uint32_t>(component.get(), 104, text.c_str(), extra.c_str(), 1u);
    Call<uint32_t>(target, 296, 1u);
    return 1;
}
