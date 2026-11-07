#pragma once
// A persist stream for the viewer-reference tests: one ordered tape of words (GetBool, GetUint8, GetUint32, GetFloat32
// and the float arrays read the same items: a float is its bits), texts and object references, long enough for the
// viewer's entries (sgfx::Reader's queues hold 32 words and 8 texts). The writer's slots record onto the same tape, so a
// Write can be read back. Each kind is read in its own order (as sgfx::Reader's queues: the base parts interleave
// kinds); a read past a kind's last item is noted and answers zero. The records'
// versions stay sgfx::gReader's queue (WriteTypeInfo adds to it).
#include "sg_viewer_ref_fx.h"
#include "sgfx.h"

#include <bit>
#include <cstring>
#include <string>
#include <vector>

namespace vtape {

using drawfx::Fake;

enum Kind : uint32_t { kWord = 1, kText = 2, kRef = 3 };

struct Item {
    Kind kind;
    uint32_t word;
    std::string text;
    void *ref;
    Fake *counted; // a reference's count to raise when it is read (else its AddRef is called)
};

struct Tape {
    std::vector<Item> items;
    size_t at[4] = {}; // each kind's next item: the kinds are read in their own orders

    void Clear()
    {
        difftest::NotCounted bookkeeping;
        items.clear();
        for (size_t &a : at) a = 0;
    }
    void Add(Item item)
    {
        difftest::NotCounted bookkeeping;
        items.push_back(std::move(item));
    }
    void Word(uint32_t w) { Add({kWord, w, {}, nullptr, nullptr}); }
    void Float(float f) { Word(std::bit_cast<uint32_t>(f)); }
    void Text(const char *t) { Add({kText, 0, t ? t : "", nullptr, nullptr}); }
    void Ref(void *p, Fake *counted = nullptr) { Add({kRef, 0, {}, p, counted}); }
    const Item *Next(Kind kind)
    {
        size_t &a = at[kind];
        while (a < items.size() && items[a].kind != kind) ++a;
        if (a >= items.size()) {
            drawfx::Note("tape end", uint32_t(kind));
            return nullptr;
        }
        return &items[a++];
    }
    // How far each kind was read (its next item's index), and the tape's length.
    void NotePosition(const char *label) const
    {
        drawfx::Note(label, {uint32_t(at[kWord]), uint32_t(at[kText]), uint32_t(at[kRef]), uint32_t(items.size())});
    }
    // Every item: its kind and word, text or reference (normalised).
    void NoteItems(const char *label) const
    {
        drawfx::Call c;
        c.note = label;
        for (const Item &item : items) {
            c.words.push_back(item.kind);
            if (item.kind == kWord) c.words.push_back(item.word);
            if (item.kind == kRef) c.words.push_back(drawfx::Norm(drawfx::P(item.ref)));
            if (item.kind == kText) c.text += item.text + "|";
        }
        drawfx::gLog.push_back(std::move(c));
    }
};
inline Tape gTape;

namespace detail {
inline uint32_t ReadWord(Fake &, const uint32_t *)
{
    const Item *item = gTape.Next(kWord);
    return item ? item->word : 0;
}
inline uint32_t ReadArray(Fake &self, const uint32_t *a)
{
    uint32_t *out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    for (uint32_t i = 0; i < a[1]; ++i) out[i] = ReadWord(self, a);
    return 0;
}
// GetGZStr(string): the text through the string's FromChar(text, length) (cIGZString slot 3).
inline uint32_t ReadText(Fake &, const uint32_t *a)
{
    const Item *item = gTape.Next(kText);
    const char *text = item ? item->text.c_str() : "";
    void *s = reinterpret_cast<void *>(uintptr_t(a[0]));
    using FromCharFn = void(__thiscall *)(void *, const char *, uint32_t);
    reinterpret_cast<FromCharFn>((*static_cast<void ***>(s))[3])(s, text, uint32_t(std::strlen(text)));
    return 1;
}
// GetObjectRef(iid, out): the object, counted; true when there is one.
inline uint32_t ReadRef(Fake &, const uint32_t *a)
{
    const Item *item = gTape.Next(kRef);
    void *p = item ? item->ref : nullptr;
    if (p) {
        if (item->counted)
            ++item->counted->refs;
        else
            static_cast<cIGZUnknown *>(p)->AddRef();
    }
    *reinterpret_cast<void **>(uintptr_t(a[1])) = p;
    return p != nullptr;
}
// Slot 28: the stream's group (the fake's value[0]).
inline uint32_t ReadGroup(Fake &self, const uint32_t *) { return self.value[0]; }

inline uint32_t WriteByte(Fake &, const uint32_t *a)
{
    gTape.Word(a[0] & 0xff);
    return 1;
}
inline uint32_t WriteWord(Fake &, const uint32_t *a)
{
    gTape.Word(a[0]);
    return 1;
}
inline uint32_t WriteArray(Fake &, const uint32_t *a)
{
    const uint32_t *p = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    for (uint32_t i = 0; i < a[1]; ++i) gTape.Word(p[i]);
    return 1;
}
inline uint32_t WriteText(Fake &, const uint32_t *a)
{
    gTape.Text(reinterpret_cast<const char *>(uintptr_t(a[0])));
    return 1;
}
inline uint32_t WriteRef(Fake &, const uint32_t *a)
{
    gTape.Ref(reinterpret_cast<void *>(uintptr_t(a[0])));
    return 1;
}
inline uint32_t WriteTypeInfo(Fake &self, const uint32_t *a)
{
    const auto *info = reinterpret_cast<const ::cPersistTypeInfo *>(uintptr_t(a[0]));
    sgfx::detail::WriterTypeInfo(self, a);
    sgfx::gReader.Version(info ? info->mVersion : 0);
    return 1;
}
} // namespace detail

// The reader's and writer's slots on the tape (after the world's Build: it resets both fakes).
inline void Install()
{
    gTape.Clear();
    Fake &r = sgfx::gReader.fake;
    for (unsigned slot : {4u, 6u, 10u, 11u}) r.On(slot, &detail::ReadWord);
    r.Settle<20, 2>("au"); // (out, count)
    r.On(20, &detail::ReadArray);
    r.On(22, &detail::ReadText);
    r.On(25, &detail::ReadRef);
    r.Settle<28, 0>();
    r.On(28, &detail::ReadGroup);
    r.value[0] = 0;
    Fake &w = sgfx::gWriter.fake;
    w.On(4, &detail::WriteByte);
    w.On(6, &detail::WriteByte);
    w.On(10, &detail::WriteWord);
    w.On(11, &detail::WriteWord);
    w.Settle<20, 2>("@1x1u"); // (floats, count): the floats logged
    w.On(20, &detail::WriteArray);
    w.On(23, &detail::WriteText);
    w.On(26, &detail::WriteRef);
    w.On(28, &detail::WriteTypeInfo);
}

// ---- records the viewer-reference tests share ----

// The records of the base parts before the viewer's entries (sg_viewer_ref_node_base_impl.cpp's Read): the viewer
// node's version first.
inline void TapeBase(uint32_t version, std::mt19937 &rng)
{
    sgfx::Reader &r = sgfx::gReader;
    Tape &t = gTape;
    r.Version(version);
    const uint32_t baseVersion = rng() % 7;
    r.Version(baseVersion);
    for (int i = 0; i < 4; ++i) r.Version(rng() % 16);
    r.Version(4 + rng() % 2);
    t.Word(0); // no extensions
    t.Text(rng() % 2 ? "alpha" : "beta");
    t.Word(0); // no children
    for (int i = 0; i < 7; ++i) t.Float(sgtx::RandomFloat(rng));
    t.Word(rng() % 2 ? 0x7fffffffu : uint32_t(rng()));
    t.Word(rng() % 2);
    t.Word(rng() % 3 == 0);
    t.Word(0); // no names
    t.Word(uint32_t(rng()));
    t.Word(rng() % 2);
    // (Each flag says whether what follows it is there.)
    if (baseVersion >= 2) {
        const uint32_t texture = rng() % 2;
        t.Word(texture);
        if (texture) t.Text(rng() % 2 ? "viewer_tex" : "xmissing_tex");
    }
    if (baseVersion >= 3) {
        const uint32_t bound = rng() % 2;
        t.Word(bound);
        if (bound) {
            t.Ref(rng() % 3 == 0 ? nullptr : &svx::gVw.bound);
            for (int i = 0; i < 4; ++i) t.Float(sgtx::RandomFloat(rng));
            t.Word(rng() % 2 ? uint32_t(rng()) : uint32_t(rng() % 4));
        }
    }
    t.Word(uint32_t(rng() % 256));
    t.Word(uint32_t(rng()));
    if (baseVersion >= 4) {
        t.Word(uint32_t(rng()));
        for (int i = 0; i < 5; ++i) t.Float(sgtx::RandomFloat(rng));
        t.Word(uint32_t(rng()));
    }
}

// A word GetBool reads: true, false, or a word whose low byte decides.
inline uint32_t BoolWord(std::mt19937 &rng)
{
    switch (rng() % 4) {
    case 0: return 0x100;
    case 1: return 0x101;
    default: return rng() % 2;
    }
}

// The stream's group (reader slot 28): none, the group that counts as none (0x1c0532fa), or a real one.
inline uint32_t StreamGroup(std::mt19937 &rng)
{
    static const uint32_t kGroups[] = {0, 0x1c0532fau, 0x7f00a5c3u};
    return kGroups[rng() % 3];
}

} // namespace vtape
