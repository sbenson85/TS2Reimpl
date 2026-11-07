#pragma once
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>

// The command-line object: the process command line split into arguments, plus the switch lookups
// ("/name:value", "-x", "\name") the startup code uses everywhere. Original vtable: RVA 0xd9b4fc
// (1.18.0.187), 17 slots: cIGZCmdLine's 15, the scalar deleting destructor (15) and a
// GetArgument-style accessor (16) that is not part of the interface. Methods are non-virtual here
// so they can be hooked onto the game's vtable entries; calls the original makes through a vtable
// (this's or another object's) are made the same way, in the same order.

// The interface (slot order is ABI; MSVC puts an overload group at the first declaration's position
// in reverse declaration order, so the three IsSwitchPresent and two GetIndexOfSwitch overloads are
// declared in the opposite order to their slots). Only used to call through other objects' vtables.
class cIGZCmdLine : public cIGZUnknown {
public:
    virtual int32_t ArgC() const = 0;                                       // slot 3
    virtual const char *ArgV(int32_t index) const = 0;                      // slot 4
    virtual bool GetCommandLineText(cIGZString &out) const = 0;             // slot 5
    virtual int32_t Find(const cIGZString &text, bool caseSensitive) const = 0;          // slot 6
    virtual int32_t FindSubString(const cIGZString &text, bool caseSensitive) const = 0; // slot 7
    virtual bool IsSwitchPresent(char sw, cIGZString &param, bool trimQuotes) const = 0;                 // slot 10
    virtual bool IsSwitchPresent(const cIGZString &sw) const = 0;                                        // slot 9
    virtual bool IsSwitchPresent(const cIGZString &sw, cIGZString &param, bool trimQuotes) const = 0;    // slot 8
    virtual int32_t GetIndexOfSwitch(char sw, int32_t start) const = 0;                                  // slot 12
    virtual int32_t GetIndexOfSwitch(const cIGZString &sw, int32_t start) const = 0;                     // slot 11
    virtual bool InsertArgument(const cIGZString &arg, int32_t index) = 0;  // slot 13
    virtual bool EraseArgument(int32_t index) = 0;                          // slot 14
};

// std::vector<cRZString> in the game's layout (MSVC 14.40, 32-byte elements: first, last, end of
// storage), with the instantiated helpers the command line and other classes (cTSStringSet, font
// tables) call. The game's elements are destroyed through their vtable (slot 33, flags 0) and
// copy-constructed with cRZString::Construct(other, 0, -1), so none of this is std::vector.
// Each member is the RVA noted; all are __thiscall on the vector (the first member of the object
// they belong to), the static ones __cdecl.
struct cRZStringVector {
    static constexpr uint32_t kMaxElements = 0x7ffffff;
    static constexpr int kSlotDeletingDestructor = 33; // cRZString's vtable slot used to destroy elements

    cRZString *mFirst;
    cRZString *mLast;
    cRZString *mEnd;

    int32_t Count() const { return static_cast<int32_t>(mLast - mFirst); }

    cRZString *PushBack(const cRZString &value);                  // 0x1ac271: capacity must be spare; returns the old end
    cRZString *EmplaceReallocate(cRZString *where, const cRZString &value); // 0x1ac291: reallocating insert; returns the new element
    cRZString *PushBackAny(const cRZString &value);               // the `if (last == end) realloc else push` pair at the call sites
    cRZString **Insert(cRZString **out, cRZString *where, const cRZString &value); // 0x1ae427 (returns out)
    cRZString **Erase(cRZString **out, cRZString *where);         // 0x1af56e (returns out)
    uint32_t CalculateGrowth(uint32_t newSize) const;             // 0x143994
    void ChangeArray(cRZString *newFirst, uint32_t newSize, uint32_t newCapacity); // 0x1ad8db
    void Tidy();                                                  // 0x1441eb: destroy, free, zero
    void Clear();                                                 // the clear() inlined in the constructor's parser (0x1adb0b has the same shape)
    // 0x14155c (ret 8): assign(first, first + count) (the vector's operator= 0x1420af, cTSStringSet::vf10 0x1eed90):
    // past the capacity, the old elements destroyed and freed and a new block of the geometric growth taken (length_error
    // past kMaxElements) before copy-constructing; past the size, the existing elements assigned and the rest
    // copy-constructed; otherwise the first `count` assigned and the surplus destroyed.
    void AssignCounted(const cRZString *first, uint32_t count);

    static void DestroyRange(cRZString *first, cRZString *last, const void *allocator); // 0x141663 (cdecl)
    static cRZString *UninitializedCopy(const cRZString *first, const cRZString *last, cRZString *dest, const void *allocator); // 0x1ac987 (cdecl)
    static cRZString *CopyBackward(const cRZString *first, const cRZString *last, cRZString *destEnd); // 0x1ae350 (cdecl)
    static cRZString *CopyForward(const cRZString *first, const cRZString *last, cRZString *dest);     // 0x1ae37a (cdecl)
    static cRZString *CopyN(const cRZString *first, uint32_t count, cRZString *dest);                 // 0x141633 (cdecl)
    static cRZString *UninitializedCopyN(const cRZString *first, uint32_t count, cRZString *dest, const void *allocator); // 0x141a91 (cdecl)
};

static_assert(sizeof(cRZStringVector) == 12, "cRZStringVector layout");

class cRZCmdLine {
public:
    static constexpr uint32_t kVTable = 0xd9b4fc;
    static constexpr uint32_t kSize = 0x54;
    static constexpr uint32_t kGZIID_cIGZCmdLine = 0xa31e539c;
    static constexpr int kSlotAddRef = 1;
    static constexpr int kSlotDeletingDestructor = 15;
    // Slots of the cIGZString arguments the original calls through their vtables.
    static constexpr int kStringSlotStrlen = 7;
    static constexpr int kStringSlotCopy = 17;

    // Constructors (all leave the refcount 0) ------------------------------------------------
    cRZCmdLine *Construct();                          // 0x1ae5e5: empty command line
    cRZCmdLine *Construct(const char *text);          // 0x1ae5a3: copy of the text, parsed into arguments
    cRZCmdLine *Construct(cIGZCmdLine *other);        // 0x1ae4e8: text and arguments taken from another command line
    void Destruct();                                  // 0x14201d
    cRZCmdLine *DeletingDestructor(uint32_t flags);   // slot 15, 0x142229

    // cIGZCmdLine slots --------------------------------------------------------------------
    bool QueryInterface(uint32_t riid, void **ppvObj);   // slot 0, 0x1af4a0
    uint32_t AddRef();                                   // slot 1 (0x148fb0, shared with every refcounted class)
    uint32_t Release();                                  // slot 2, 0x1af4d8 (folded with two other classes' Release)
    int32_t ArgC() const;                                // slot 3, 0x1af537
    const char *ArgV(int32_t index) const;               // slot 4, 0x1af541
    bool GetCommandLineText(cIGZString &out) const;      // slot 5, 0x1aedba (folded with two other classes)
    int32_t Find(const cIGZString &text, bool caseSensitive) const;          // slot 6, 0x1ae9d3
    int32_t FindSubString(const cIGZString &text, bool caseSensitive) const; // slot 7, 0x1aea85
    bool IsSwitchPresent(const cIGZString &sw, cIGZString &param, bool trimQuotes) const; // slot 8, 0x1af0ee
    bool IsSwitchPresent(const cIGZString &sw) const;                                     // slot 9, 0x1af09e
    bool IsSwitchPresent(char sw, cIGZString &param, bool trimQuotes) const;              // slot 10, 0x1af321
    int32_t GetIndexOfSwitch(const cIGZString &sw, int32_t start) const;     // slot 11, 0x1aedd0
    int32_t GetIndexOfSwitch(char sw, int32_t start) const;                  // slot 12, 0x1aef39
    bool InsertArgument(const cIGZString &arg, int32_t index);               // slot 13, 0x1af02a
    bool EraseArgument(int32_t index);                                       // slot 14, 0x1ae992
    cRZString *GetArgument(int32_t index) const;                             // slot 16, 0x1ae610 (empty string when out of range)

    // Helpers (__thiscall, `this` unused) -----------------------------------------------------
    void Parse(const cRZString &text, cRZStringVector &out);     // 0x1ae6fe: split the text into arguments
    void RebuildText(cRZStringVector &args, cRZString &text);    // 0x1ae632: join the arguments back into text

    const void *mVTable;     // 0x00
    uint32_t mRefCount;      // 0x04
    cRZStringVector mArgs;   // 0x08: the arguments (each a cRZString)
    cRZString mText;         // 0x14: the whole command line as text
    cRZString mEmpty;        // 0x34: stays empty; what ArgV/GetArgument return for an index past the end
};

static_assert(sizeof(cRZCmdLine) == cRZCmdLine::kSize, "cRZCmdLine size");
static_assert(offsetof(cRZCmdLine, mRefCount) == 0x04, "cRZCmdLine layout");
static_assert(offsetof(cRZCmdLine, mArgs) == 0x08, "cRZCmdLine layout");
static_assert(offsetof(cRZCmdLine, mText) == 0x14, "cRZCmdLine layout");
static_assert(offsetof(cRZCmdLine, mEmpty) == 0x34, "cRZCmdLine layout");
