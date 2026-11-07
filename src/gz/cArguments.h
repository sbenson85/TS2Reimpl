#pragma once
// nGZCommandParser::cArguments, the parsed arguments of one command line (0x30 bytes), in the standalone shape
// (docs/standalone-shape.md): the compiler builds the vtable; the game's constructor, copy constructor and destructor
// RVAs are hooked to adapters that build or destroy our class, so every object the game makes carries our vtable. RVAs
// are 1.18.0.187.
//
// RTTI: cArguments derives from cIArguments (one face at +0, 17 slots, no cIGZUnknown). Parse splits a line into
// NUL-terminated copies in a byte buffer (quotes and parentheses group, a word starting with '-' and a letter is a
// switch); the argument vector points into the buffer, with one more pointer at the end (so that slot 6 can measure the
// last argument), and the switches are sorted by name (stable). Commands then take their positional arguments (slots
// 10 / 11) and switches (slots 1 / 2 / 3; a switch taken is marked used), and slot 15 complains about the switches left.
#include "rz/GameAllocator.h" // the vectors: GameAllocator (RZMemory::Malloc / Free, as the original's); GZServiceAllocator

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nGZCommandParser {

// The names slot 14 hands out: a vector on the allocator service (its growth is 0x38c800).
using NameVector = std::vector<const char *, GZServiceAllocator<const char *>>;

// The interface (vtable 0xe7f84c's slots, in order). Every slot is __thiscall; `ret N` is in the comments.
class cIArguments {
public:
    // 0: splits `text` into the arguments (ret 0xc). `skipFirst`: the first word is the command's name (not counted
    //    among the positional arguments, not handed out by slot 10); `caseSensitive`: how switch names compare.
    virtual void Parse(const char *text, uint8_t skipFirst, uint8_t caseSensitive) = 0;
    // 1: takes the switch `name` that has no arguments (ret 4): false when it is not given (or every one given is
    //    used); throws when it has arguments.
    virtual bool TakeSwitch(const char *name) = 0;
    // 2: takes the switch `name` with between `least` and `most` arguments (ret 0x10): its arguments (their number to
    //    *count when count is not null), or null when it is not given; throws when the number is outside the range.
    virtual const char *const *TakeSwitchArguments(const char *name, uint32_t *count, uint32_t least, uint32_t most) = 0;
    // 3: slot 2 with exactly `count` arguments (ret 8).
    virtual const char *const *TakeSwitchArgumentsExactly(const char *name, uint32_t count) = 0;
    virtual const char *Argument(uint32_t index) = 0;                     // 4 (ret 4)
    virtual void GetAll(int32_t *count, const char *const **arguments) = 0; // 5 (ret 8): the count of slot 7, the vector
    virtual int32_t ArgumentLength(uint32_t index) = 0;                   // 6 (ret 4)
    virtual int32_t Count() = 0;                                          // 7: every word (switches too)
    virtual uint32_t PositionalCount() = 0;                               // 8: the words before the first switch
    virtual int32_t SwitchCount() = 0;                                    // 9
    // 10: the positional arguments (ret 0xc): null when there are none; their number to *count when count is not
    //     null; throws when the number is outside [least, most].
    virtual const char *const *Positional(uint32_t *count, uint32_t least, uint32_t most) = 0;
    virtual const char *const *PositionalExactly(uint32_t count) = 0;     // 11 (ret 4): slot 10 with exactly `count`
    virtual bool HasSwitch(const char *name) = 0;                         // 12 (ret 4): an unused switch `name` given?
    virtual int32_t UnusedSwitchCount() = 0;                              // 13
    virtual void GetUnusedSwitches(NameVector *names) = 0; // 14 (ret 4): their names, in order
    virtual void CheckSwitchesUsed() = 0;                                 // 15: throws "Unhandled switches: ..."
    virtual ~cIArguments() = default;                                     // 16: the deleting destructor (ret 4)
};

class cArguments : public cIArguments {
public:
    static constexpr uint32_t kVTable = 0xe7f84c; // the game's (17 slots)

    // A switch: its name (after the '-', in the buffer), the index of its first argument in the argument vector, how
    // many arguments follow it (up to the next switch) and whether a command took it.
    struct Switch {
        const char *name;   // +0
        uint32_t first;     // +4
        uint32_t count;     // +8
        uint8_t used;       // +0xc
        uint8_t pad[3];
    };
    static_assert(sizeof(Switch) == 0x10, "Switch layout");

    // How switches compare: by name (strcmp, or _stricmp when not case sensitive); against a name alone, an equal
    // switch that is used sorts first, so a lookup lands on the first unused one.
    struct SwitchOrder {
        bool caseSensitive;
        int Compare(const char *a, const char *b) const;
        bool operator()(const Switch &a, const Switch &b) const { return Compare(a.name, b.name) < 0; }
        bool operator()(const Switch &a, const char *name) const
        {
            int order = Compare(a.name, name);
            return order == 0 ? a.used != 0 : order < 0;
        }
        bool operator()(const char *name, const Switch &a) const { return Compare(name, a.name) < 0; }
    };

    cArguments();                                  // 0xb0be60
    cArguments(const cArguments &other);           // 0xb0bdd0
    cArguments &operator=(const cArguments &other); // 0xb0c000
    ~cArguments() override;                        // 0xb0bf40 (slot 16: 0xb0c320)

    void Parse(const char *text, uint8_t skipFirst, uint8_t caseSensitive) override;  // 0xb0c720
    bool TakeSwitch(const char *name) override;                                 // 0xb0cc70
    const char *const *TakeSwitchArguments(const char *name, uint32_t *count, uint32_t least, uint32_t most) override; // 0xb0cbb0
    const char *const *TakeSwitchArgumentsExactly(const char *name, uint32_t count) override; // 0xb0cb90
    const char *Argument(uint32_t index) override;                              // 0xb0c2a0
    void GetAll(int32_t *count, const char *const **arguments) override;        // 0xb0c410
    int32_t ArgumentLength(uint32_t index) override;                            // 0xb0c3f0
    int32_t Count() override;                                                   // 0xb0c6d0
    uint32_t PositionalCount() override;                                        // 0x149cf0 (folded)
    int32_t SwitchCount() override;                                             // 0xb0c6e0
    const char *const *Positional(uint32_t *count, uint32_t least, uint32_t most) override; // 0xb0c650
    const char *const *PositionalExactly(uint32_t count) override;              // 0xb0c630
    bool HasSwitch(const char *name) override;                                  // 0xb0c550
    int32_t UnusedSwitchCount() override;                                       // 0xb0c6f0
    void GetUnusedSwitches(NameVector *names) override;          // 0xb0cce0
    void CheckSwitchesUsed() override;                                          // 0xb0c430

    std::vector<char, GameAllocator<char>> mBuffer;            // +0x04 the words, each NUL-terminated
    uint32_t mPositional;                                           // +0x10
    uint8_t mSkipFirst;                                             // +0x14
    uint8_t mCaseSensitive;                                         // +0x15
    std::vector<const char *, GameAllocator<const char *>> mArguments; // +0x18 into mBuffer, plus the end
    std::vector<Switch, GameAllocator<Switch>> mSwitches;      // +0x24 sorted by name
};

static_assert(offsetof(cArguments, mBuffer) == 4 && offsetof(cArguments, mPositional) == 0x10 &&
                  offsetof(cArguments, mSkipFirst) == 0x14 && offsetof(cArguments, mCaseSensitive) == 0x15 &&
                  offsetof(cArguments, mArguments) == 0x18 && offsetof(cArguments, mSwitches) == 0x24,
              "cArguments layout");
static_assert(sizeof(cArguments) == 0x30, "cArguments size");

// The game's entry points (`this` in ecx; __fastcall with the dummy edx = __thiscall): the constructors and the
// destructor build or destroy our class in place; the slots forward to our members (objects the game built before our
// constructor hook ran, if any).
struct cArgumentsAdapters {
    static cArguments *__fastcall Construct(void *memory, void *);
    static cArguments *__fastcall ConstructCopy(void *memory, void *, const cArguments *other);
    static cArguments *__fastcall Assign(cArguments *self, void *, const cArguments *other);
    static void __fastcall Destruct(cArguments *self, void *);
    static cArguments *__fastcall DeletingDestructor(cArguments *self, void *, uint32_t flags);
    static void __fastcall Parse(cArguments *self, void *, const char *text, uint8_t skipFirst, uint8_t caseSensitive);
    static bool __fastcall TakeSwitch(cArguments *self, void *, const char *name);
    static const char *const *__fastcall TakeSwitchArguments(cArguments *self, void *, const char *name, uint32_t *count,
                                                              uint32_t least, uint32_t most);
    static const char *const *__fastcall TakeSwitchArgumentsExactly(cArguments *self, void *, const char *name, uint32_t count);
    static const char *__fastcall Argument(cArguments *self, void *, uint32_t index);
    static void __fastcall GetAll(cArguments *self, void *, int32_t *count, const char *const **arguments);
    static int32_t __fastcall ArgumentLength(cArguments *self, void *, uint32_t index);
    static int32_t __fastcall Count(cArguments *self, void *);
    static int32_t __fastcall SwitchCount(cArguments *self, void *);
    static const char *const *__fastcall Positional(cArguments *self, void *, uint32_t *count, uint32_t least, uint32_t most);
    static const char *const *__fastcall PositionalExactly(cArguments *self, void *, uint32_t count);
    static bool __fastcall HasSwitch(cArguments *self, void *, const char *name);
    static int32_t __fastcall UnusedSwitchCount(cArguments *self, void *);
    static void __fastcall GetUnusedSwitches(cArguments *self, void *, NameVector *names);
    static void __fastcall CheckSwitchesUsed(cArguments *self, void *);
};

} // namespace nGZCommandParser
