#pragma once
#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator

class cIGZString;
class cRZString;

// The containers are MSVC 14.40 std::unordered_map / std::list, the same types and layout the game uses, so the
// game's tables can be worked on with ours. The hashers are not noexcept on purpose: the game's clear() is the
// plain one (free every node, reset every bucket), which MSVC uses only when the hasher may throw.

// A resource key as the directory segment stores it: type, group, instance, and the high instance ("resource").
struct DirResourceKey {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
    uint32_t resource;
};
struct DirResourceKeyHash {
    size_t operator()(const DirResourceKey &key) const { return key.instance; } // the instance is the hash
};
struct DirResourceKeyEqual {
    bool operator()(const DirResourceKey &a, const DirResourceKey &b) const
    {
        return a.type == b.type && a.group == b.group && a.instance == b.instance && a.resource == b.resource;
    }
};

// A cached answer to "which file has this instance with resource 0 ignored" (see FindEntry).
struct DirInstanceProbe {
    bool found;        // node +0x0c
    uint32_t resource; // node +0x10: the high instance of the file that matched
};
struct DirInstanceHash {
    size_t operator()(uint32_t instance) const { return instance; }
};

// A counted pointer to an open record (a cGZDBSDFRecord; its cIGZPersistDBRecord face is at +8). Destroying it
// releases the record through its slot 2, as the game's list node destructor does.
struct DirOpenRecord {
    void *record = nullptr;
    explicit DirOpenRecord(void *r) : record(r) {}
    DirOpenRecord(const DirOpenRecord &) = delete;
    DirOpenRecord &operator=(const DirOpenRecord &) = delete;
    ~DirOpenRecord();
    // The record's interface pointer as the game compares it: record + 8, or null for a null record.
    const void *Face() const { return record ? static_cast<const uint8_t *>(record) + 8 : nullptr; }
};

// A file name kept as a cRZString value (vtable, std::string, reference count; 0x20 bytes).
struct DirFileName {
    const void *vtable; // the game's cRZString vtable
    std::string text;
    uint32_t refCount;
};

// One file of the directory: its name (relative to the segment's path) and its open records.
struct DirFileEntry {
    DirFileName name;                 // node +0x18
    std::list<DirOpenRecord> open;    // node +0x38

    // 0x9a0d70 (__thiscall on the entry, one stack argument): removes the first open record whose face is `face`.
    void RemoveOpenRecord(const void *face);
};

using DirProbeMap = std::unordered_map<uint32_t, DirInstanceProbe, DirInstanceHash>;
using DirFileMap = std::unordered_map<DirResourceKey, DirFileEntry, DirResourceKeyHash, DirResourceKeyEqual>;
static_assert(sizeof(DirProbeMap) == 0x20 && sizeof(DirFileMap) == 0x20, "MSVC unordered_map layout");
static_assert(sizeof(DirFileEntry) == 0x28, "file entry: name and list");

// The key list GetResourceKeyList appends to: its memory comes from the allocator service.
using DirKeyVector = std::vector<DirResourceKey, GZServiceAllocator<DirResourceKey>>;

// cGZDBSegmentDirectoryFiles: a database segment backed by a directory of loose files, next to the .package
// segments (cGZDBSegmentPackedFile). Primary vtable 0xe5ed64 (37 slots: cIGZPersistDBSegmentDirectoryFiles
// and cIGZUnknown at +0); the cGZPersistDBSegment subobject at +4 has vtable 0xe5edfc (50 slots). RVAs are
// 1.18.0.187.
//
// Field map, from the code of the slots below (primary offsets):
//   +0x00  primary vtable (37 slots)
//   +0x04  cGZPersistDBSegment subobject (0x18 bytes, see cGZPersistDBSegment.h); its vtable is 0xe5edfc
//   +0x08  reference count (int32) shared with the subobject: Release updates it with RZAtomicAdd
//   +0x0c..+0x1b  the subobject's lock, segment cache and namespace (not used by these slots)
//   +0x3c  byte flag, set to 1 by vf4 (only once)
//   +0x4c  embedded object (its vtable pointer is at +0x4c); vf9 calls its slot 16 with the argument
//   +0x78, +0x79, +0x7a  bytes stored by vf15 and vf16 (meaning not known)
//   +0x7c  embedded object (vf14 forwards to its slot 3; not mapped further)
//   +0xac  object pointer, reference-counted by vf23 (SetObjectAC)
//   +0xb0  byte stored by vf28 (meaning not known)
//   +0x44  state dword (the subobject reads it at sub +0x40): bits 0..1 nonzero means open; IsWritable
//          reads bit 1
//   +0x48  byte (sub +0x44), set by AutoOpen
//   +0x49  byte (sub +0x45), set by AutoOpen
//   +0x50  path: std::string (MSVC layout: buffer at +0x50, size at +0x60, capacity at +0x64). vf8 (GetPath)
//          passes it to the output string's FromChar(data, size)
//   +0x70  dword (sub +0x6c): SetSegmentID stores the ID here, but only while this is zero
//   +0x74  dword (sub +0x70): SetSegmentID stores the ID here every time
// Not mapped yet: +0x18..+0x3b beyond the subobject, +0x68..+0x6f, +0x7b..+0xab apart from +0x7c's embedded object,
// and the members after +0xb0.
//
// Calls through other objects' vtables (the primary's slot 1 for AddRef, the subobject's slot 34 for the
// deleting destructor, the output string's slot 3) are made as the original makes them.
class cGZDBSegmentDirectoryFiles {
public:
    static constexpr uint32_t kVTable = 0xe5ed64;
    static constexpr uint32_t kSubVTable = 0xe5edfc;
    static constexpr uint32_t kIdSelf = 0x856c8cf7;  // cIGZPersistDBSegmentDirectoryFiles: +0
    static constexpr int kDeleteSlot = 34;           // subobject's slot 0x88: deleting destructor

    // Slot 0, 0x9a2660. The id 0x856c8cf7 stores this in *ppvObj and calls AddRef through the vtable of this.
    // Any other id goes to the subobject's QueryInterface (cGZPersistDBSegment, 0x99cf70) with this + 4,
    // which accepts the ids 1 and 0x656b8f0c.
    bool QueryInterface(uint32_t riid, void **ppvObj);
    // Slot 2, 0x9a27b0: RZAtomicAdd on the count at +8 by -1. A nonzero result is returned as is. At zero,
    // the subobject's slot 34 is called with no arguments (the count is not reset) and 0 is returned.
    uint32_t Release();
    // Slot 4, 0x9a1de0: sets the flag at +0x3c to 1 if it is zero. Returns nothing (the original leaves
    // al = 1, which no caller reads).
    void SetFlag3C();
    // Slot 8, 0x9a1b70: out->FromChar(path data, path size), out's slot 3 (cIGZString, this = out).
    void GetPath(cIGZString *out);
    // Slot 9, 0x9a29c0 (one argument): false when the state has bits 0..1 set or when arg's slot 8 answers
    // nonzero. Otherwise calls the embedded object's slot 16 with arg, appends '\\' to the path unless its
    // last character is '\\' or '/', and returns true. ORIGINAL-BUG: an empty path reads the byte before its
    // buffer for that test (see the comment at the spot).
    bool Vf9(void *arg);
    // Slot 15, 0x9a28b0 (two byte arguments): stores them at +0x78 and +0x79. Returns nothing.
    void SetBytes78(uint8_t a, uint8_t b);
    // Slot 16, 0x9a28a0 (one byte argument): stores it at +0x7a.
    void SetByte7A(uint8_t a);
    // Slot 23, 0x9a2980 (one argument): the object pointer at +0xac becomes obj. AddRef on obj (when it is not
    // null and differs from the old one) is called before the Release of the old one. Returns true.
    bool SetObjectAC(void *obj);
    // Slot 28, 0x1d3ac0 (one byte argument): stores it at +0xb0.
    void SetByteB0(uint8_t a);
    // Slot 31, 0x9a0990 (two arguments): calls its own slot 32 with (a, b, 2) and returns that call's eax.
    uint32_t Vf31(uint32_t a, uint32_t b);
    // Slot 36, 0x9a2fa0 (three arguments): a tail jump to its own slot 35 with the same arguments.
    uint32_t Vf36(uint32_t a, uint32_t b, uint32_t c);

    // Slot 5, 0x9a2a60 (Shutdown): true at once when +0x3c is clear. Otherwise clears it, stops the timer (its
    // slot 7), closes through the subobject's slot 9 when open, resets +0x40 = 1, the path, +0x70/+0x74 = 0 and the
    // bytes +0x78/+0x79 = 1/0, destroys and frees the +0x6c object, clears the extension map (its slot 6) and the
    // files, releases +0xac (nulled first) and returns the base's Shutdown.
    bool Shutdown();
    // Slot 14, 0x99ed70: a tail jump to slot 3 of the extension map at +0x7c with the caller's arguments.
    // Two stack arguments are forwarded; the map's answer is returned.
    uint32_t Vf14(uint32_t a, uint32_t b);
    // Slot 17, 0x9a14a0 (key, out): EnsureOpen; false when FindEntry finds nothing, else out's slot 17 (Copy)
    // gets the file's name and true is returned.
    bool GetFileName(const DirResourceKey *key, cIGZString *out);
    // Slot 18, 0x9a1520 (key, out): this's slot 17 into a local cRZString; on success out = the path (out's
    // slot 17 with +0x4c) followed by the name (slot 20, Append(data, size)).
    bool GetFilePath(const DirResourceKey *key, cIGZString *out);
    // Slot 21, 0x9a1100 (type, out key): EnsureOpen; false unless +0x3c is set. out = {type, +0x70, n, (left)}
    // for n counting up from +0x40 (which is advanced past each n tried) until a key is not in the files table;
    // true then. False when +0x40 reaches 0xffffffff.
    bool NewKey(uint32_t type, DirResourceKey *out);
    // Slot 22, 0x9a0c80 (names, max): under the lock, EnsureOpen; without names returns the file count, else
    // writes file names (FromChar, slot 3) into names[0..] in table order until max or a null entry; returns n.
    uint32_t GetFileNames(cIGZString **names, uint32_t max);
    // Slot 26, 0x9a2920 (enable byte, seconds): when enable differs from +0xb2: off stops the timer (slot 7), on
    // starts it (StartRefreshTimer) if +0x3c is set; stores it. +0xb4 = max(seconds, 15). Returns true.
    bool SetAutoRefresh(uint8_t enable, uint32_t seconds);
    // Slot 35, 0x9a2fb0 (key, out name, extension): the key is copied and translated by the namespace (+0x18,
    // slot 3) when there is one; with an object at +0xac its slot 5 (key copy, out, extension, this) answers. Otherwise
    // the extension map's slot 5 (&type, extension) must accept the type (else this's slot 30 logs and false);
    // out's Sprintf (slot 31) then writes the instance in hex (format 0xe5f120), or instance and group (0xe5f128)
    // when the group is not +0x70. Returns true.
    bool GetFileNameFromKey(const DirResourceKey *key, cIGZString *out, uint32_t extension);

    // Slot 6, 0x9a1fc0 (Open: read, write bytes): true at once when already open. Without +0x3c, with an empty
    // path or with neither access asked: slot 30 logs 0xe5ef20, the state is cleared and false returned. Else the
    // state becomes 1 (read) | 2 (write); for writing with +0x3d set the directory must exist (DirectoryExists, else
    // log 0xe5eec8, state cleared, false). A DirectoryScan of the path (not scanned) goes to +0x6c when there is none;
    // the framework's slot 5 (0x25076b9e, 0xa237613c) gives a listener whose slot 7 registers "CN_DBSDF" with the
    // callback 0x99fb10 and this (false: released, false returned). Then slot 33 (BuildKeyList) answers; on success
    // with +0xb2 the refresh timer is started. The listener is released (slot 2).
    bool Open(uint8_t read, uint8_t write);
    // Slot 13, 0x9a1bb0 (out): a new extension map (refcount 1) filled from the one at +0x7c (CopyFrom) into *out;
    // true.
    bool CopyExtensionMap(void **out);
    // Slot 19, 0x9a15f0 (name, extension, out key): the key a file name stands for. With +0xac: the name and extension
    // are joined (0x1c2732) and +0xac's slot 4 (path, out, this) answers. Else instance and group come from hex digits:
    // with +0x78, 8 leading hex digits are the instance (16: then the group); else with +0x79, 8 trailing ones the
    // instance (16: instance then group); else with +0x7a the instance is Crc24(name) | 0xff000000; the group is +0x70
    // unless read. The extension map's slot 4 gives the type (else false). out = {type, group, instance}
    // (resource untouched); the namespace (+0x18, slot 4) translates it in place. True.
    bool KeyFromFileName(const cIGZString *name, const cIGZString *extension, DirResourceKey *out);
    // Slot 20, 0x9a1e80 (file name): with +0xac its slot 3 (name, this) answers. Else the name without extension
    // (0x1c1ea9) and the extension (0x1c1e0e, its leading '.' removed) go to slot 19 with a scratch key.
    bool IsResourceFileName(const cIGZString *fileName);

    // Slot 34, 0x99ed80 (file name relative to the path, added): a file appeared in or left the directory. EnsureOpen.
    // Added: the name's key (slot 19 on the name without extension and the extension without its first character)
    // gets an entry named fileName unless the table has the key already; false. Removed: the first file in table
    // order with no open records whose lowercased name equals fileName exactly is erased; true then, else false.
    bool FileChanged(const cRZString *fileName, uint8_t added);
    // Slot 27, 0x9a00b0 (Refresh): false without +0x3c. Lists the directory's files (a DirectoryScan of the path) and
    // keeps those slot 20 accepts, lowercased; when some are kept and the table has files, the table's names (slot 22,
    // lowercased) are matched against them: each listed file that has a name is dropped, every unmatched name is
    // reported removed (slot 34 with 0) and every unmatched listed file added (slot 34 with 1); true when anything was
    // reported. With +0xb2 the timer is restarted with a fresh random delay (no IsRunning test).
    bool Refresh();
    // Slot 33, 0x99f360 (BuildKeyList): false (logged, 0xe5f3f0) when the directory is missing. With a segment cache
    // (+0x14) and a volume stamp (0x9a11c0) the key list is read from the cache record {0xa079ce1b, 0, Crc32(path)}
    // (stamp, count, then key, name size and name per file); when that fails the directory is listed (the +0x6c scan):
    // every file whose name gives a key (slot 19) is added, a key already present is logged (0xe5f378). Then the list
    // is written back to the cache when there is one. True.
    bool BuildKeyList();
    // Slot 32, 0x9a2110 (key, out record, access: 1 read, 2 write, 3 create): EnsureOpen; false (logged, 0xe5f310)
    // without +0x3c or with access 0. A key FindEntry doesn't find can't be read (logged, 0xe5f1a0); a file with open
    // records can't be written or created (logged, 0xe5f210), nor read when its first record isn't open for reading
    // (logged, 0xe5f288). The file name: slot 36 (name and extension, joined with '.') to create, else slot 17. A new
    // cGZDBSDFRecord (0x188 bytes: the cGZPersistDBSerialRecord base 0x99b840 with this + 4, its five vtables, two
    // cRZFiles, access +0x184 = 0) is AddRef'd and opened (its slot 14: key, segment path + name, access). On success
    // it joins the front of the file's open list (a new file gets an entry named by the name) and its QueryInterface (0xe56b8f03)
    // answers out; failing that it is removed from the open list of the entry at the exact key (inserted when missing:
    // ORIGINAL-BUG, see the .cpp). The record is released; true only when both succeeded.
    bool OpenRecordFile(const DirResourceKey *key, void **out, uint32_t access);

    // 0x99e4c0 (constructor, returns this): the cGZPersistDBSegment base (ours), the two vtables, empty probe and file
    // tables, +0x3c/+0x3d = 0, +0x3e = 1, +0x40 = 1, state 0, +0x48/+0x49 = 0, an empty path (the game's cRZString
    // vtable, count 0), +0x6c..+0x77 = 0, +0x78 = 1, +0x79/+0x7a = 0, an empty extension map (the game's vtable
    // 0xe5ed38, count 0), +0xac = 0, +0xb0..+0xb2 = 0, +0xb4 = 120 seconds, the timer (cRZCallbackTimer::Construct) and
    // the cRZRandom seeded with 0xffffffff (from the clock).
    cGZDBSegmentDirectoryFiles *Construct();
    // 0x99ebf0 (deleting destructor, flags; returns this): the two vtables, Shutdown, the timer's destructor, a Release
    // of +0xac when set, the file table, the extension map (the game's vtable, then its tree), the path, the probe
    // table and the base's destructor; the 0xf8 bytes are freed when flags has bit 0.
    cGZDBSegmentDirectoryFiles *DeletingDestructor(uint32_t flags);

    // Non-virtual helpers.
    // 0x9a0f00 (__thiscall, out iterator, key): the files table's node for key, else the end (the list head).
    // See the .cpp for the instance-only matching through the +0x1c probes.
    DirFileMap::iterator FindEntry(const DirResourceKey &key);
    // 0x9a1df0 (__thiscall, one stack argument, the key): erases probe key->instance.
    void EraseProbe(const DirResourceKey *key);
    // 0x9a2f30 (__thiscall on the files table, no stack arguments; __fastcall takes it in ecx the same way).
    static void __fastcall ClearFiles(DirFileMap *files);
    // 0x9a2b30: unless the timer runs (its slot 8), a random delay in [+0xb4, 2 * +0xb4] seconds is drawn from the
    // cRZRandom at +0xe0; the timer gets the callback 0x9a2b90 with this (slot 9) and is started with the delay in
    // ms and this (slot 6).
    void StartRefreshTimer();
    // The prologue the record functions share: when the state has no open bits and +0x48 or +0x49 is set, Open
    // (slot 6) with (+0x48, +0x49); if that fails and both are set, Open again with (+0x48, 0).
    void EnsureOpen();

    const void *mVTable;            // +0x00
    const void *mSubVTable;         // +0x04
    int32_t mRefCount;              // +0x08
    const void *mLock;              // +0x0c: the subobject's lock (embedded; slot 1 Lock, slot 2 Unlock)
    uint8_t mUnmapped10[0x14 - 0x10];
    void *mSegmentCache;            // +0x14: the subobject's segment cache (BuildKeyList keeps the key list there)
    void *mNamespace;               // +0x18: the subobject's namespace (key translation, its slot 3)
    DirProbeMap mProbes;            // +0x1c: instance -> cached FindEntry answer
    uint8_t mFlag3C;                // +0x3c: initialised (vf4 sets it, Shutdown clears it)
    uint8_t mWriteNeedsDirectory;   // +0x3d: Open for writing fails unless the directory exists
    uint8_t mByte3E;                // +0x3e: 1 after construction
    uint8_t mUnmapped3F;
    uint32_t mNextInstance;         // +0x40: next instance vf21 hands out (1 after construction and Shutdown)
    uint32_t mState;                // +0x44
    uint8_t mAutoOpen48;            // +0x48
    uint8_t mAutoOpen49;            // +0x49
    uint8_t mUnmapped4A[0x4c - 0x4a];
    const void *mEmbedded4C;        // +0x4c: vtable pointer of the path's cRZString (the path is +0x4c..+0x6b)
    std::string mPath;              // +0x50, 0x18 bytes
    uint32_t mPathRefCount;         // +0x68
    void *mScan;                    // +0x6c: 0x5c-byte object (destroyed by 0x1c49eb, then freed) or null
    uint32_t mSegmentIDOnce;        // +0x70
    uint32_t mSegmentID;            // +0x74
    uint8_t mByte78;                // +0x78
    uint8_t mByte79;                // +0x79
    uint8_t mByte7A;                // +0x7a
    uint8_t mUnmapped7B;
    const void *mExtensionMap;      // +0x7c: cGZRESIDtoExtensionMap embedded here (its vtable pointer); 0x10 bytes
    uint8_t mUnmapped80[0x8c - 0x80];
    DirFileMap mFiles;              // +0x8c: resource key -> file
    void *mObjectAC;                // +0xac
    uint8_t mByteB0;                // +0xb0: FindEntry may match a key with resource 0 by instance
    uint8_t mByteB1;                // +0xb1: FindEntry may match a key with a resource by its resource-0 file
    uint8_t mAutoRefresh;           // +0xb2: the refresh timer is wanted (vf26)
    uint8_t mUnmappedB3;
    uint32_t mRefreshSeconds;       // +0xb4: at least 15 (vf26); the timer runs between this and twice this
    const void *mTimer;             // +0xb8: timer object embedded here (its vtable pointer)
    uint8_t mUnmappedBC[0xe0 - 0xbc];
    uint8_t mRandom[12];            // +0xe0: cRZRandom
};

static_assert(offsetof(cGZDBSegmentDirectoryFiles, mVTable) == 0, "cGZDBSegmentDirectoryFiles: vtable at +0");
static_assert(offsetof(cGZDBSegmentDirectoryFiles, mSubVTable) == 4, "cGZDBSegmentDirectoryFiles: subobject at +4");
static_assert(offsetof(cGZDBSegmentDirectoryFiles, mRefCount) == 8, "cGZDBSegmentDirectoryFiles: refcount at +8");
static_assert(offsetof(cGZDBSegmentDirectoryFiles, mFlag3C) == 0x3c, "cGZDBSegmentDirectoryFiles: flag at +0x3c");
static_assert(offsetof(cGZDBSegmentDirectoryFiles, mState) == 0x44, "cGZDBSegmentDirectoryFiles: state at +0x44");
static_assert(offsetof(cGZDBSegmentDirectoryFiles, mAutoOpen48) == 0x48, "cGZDBSegmentDirectoryFiles: +0x48");
static_assert(offsetof(cGZDBSegmentDirectoryFiles, mAutoOpen49) == 0x49, "cGZDBSegmentDirectoryFiles: +0x49");
static_assert(offsetof(cGZDBSegmentDirectoryFiles, mPath) == 0x50, "cGZDBSegmentDirectoryFiles: path at +0x50");
static_assert(offsetof(cGZDBSegmentDirectoryFiles, mSegmentIDOnce) == 0x70, "cGZDBSegmentDirectoryFiles: +0x70");
static_assert(offsetof(cGZDBSegmentDirectoryFiles, mSegmentID) == 0x74, "cGZDBSegmentDirectoryFiles: +0x74");
static_assert(offsetof(cGZDBSegmentDirectoryFiles, mEmbedded4C) == 0x4c, "cGZDBSegmentDirectoryFiles: +0x4c");
static_assert(offsetof(cGZDBSegmentDirectoryFiles, mByte78) == 0x78, "cGZDBSegmentDirectoryFiles: +0x78");
static_assert(offsetof(cGZDBSegmentDirectoryFiles, mByte7A) == 0x7a, "cGZDBSegmentDirectoryFiles: +0x7a");
static_assert(offsetof(cGZDBSegmentDirectoryFiles, mObjectAC) == 0xac, "cGZDBSegmentDirectoryFiles: +0xac");
static_assert(offsetof(cGZDBSegmentDirectoryFiles, mByteB0) == 0xb0, "cGZDBSegmentDirectoryFiles: +0xb0");
static_assert(offsetof(cGZDBSegmentDirectoryFiles, mProbes) == 0x1c, "cGZDBSegmentDirectoryFiles: probes at +0x1c");
static_assert(offsetof(cGZDBSegmentDirectoryFiles, mFiles) == 0x8c, "cGZDBSegmentDirectoryFiles: files at +0x8c");
static_assert(offsetof(cGZDBSegmentDirectoryFiles, mScan) == 0x6c, "cGZDBSegmentDirectoryFiles: +0x6c");
static_assert(offsetof(cGZDBSegmentDirectoryFiles, mExtensionMap) == 0x7c, "cGZDBSegmentDirectoryFiles: +0x7c");
static_assert(offsetof(cGZDBSegmentDirectoryFiles, mTimer) == 0xb8, "cGZDBSegmentDirectoryFiles: +0xb8");
static_assert(offsetof(cGZDBSegmentDirectoryFiles, mRandom) == 0xe0, "cGZDBSegmentDirectoryFiles: +0xe0");

// The cGZPersistDBSegment subobject at +4 of cGZDBSegmentDirectoryFiles, entered by the original with
// ecx = object + 4. `this` here is the subobject; offsets in the comments are relative to it (primary offset
// = subobject offset + 4). The class has no data members: fields are reached through the pointer.
class DirectorySegmentView {
public:
    // Sub slot 7, 0x1d3df0: true when the state byte (sub +0x40) has bits 0..1 set.
    bool IsOpen() const;
    // Sub slot 8, 0x1d3e00: bit 1 of the dword at sub +0x40.
    bool IsWritable() const;
    // Sub slot 6, 0x99f340 (two stack arguments, bytes used): sub +0x44 = state, sub +0x45 = flag. Returns true.
    bool AutoOpen(uint8_t state, uint8_t flag);
    // Sub slot 16, 0x9a2a20: sub +0x70 = *id; sub +0x6c = *id only when sub +0x6c was zero. Returns true.
    bool SetSegmentID(const uint32_t *id);
    // Sub slot 3, 0x1d3db4: a tail jump to the primary's slot 4 (SetFlag3C), with this - 4.
    void Init();
    // Sub slot 11, 0x1d3b56: a tail jump to the primary's slot 8 (GetPath), with this - 4.
    void GetPath(cIGZString *out);
    // Sub slot 5, 0x1d3f3b: a tail jump to the primary's slot 6 (Open), with this - 4.
    bool OpenThunk(uint8_t read, uint8_t write);
    // Sub slot 44, 0x9a0630 (one argument): calls its own slot 45 with (0, arg) and returns that call's eax.
    uint32_t Vf44(uint32_t arg);

    // Sub slots 0 (0x9a2687) and 2 (0x9a27d5): tail jumps to the primary's QueryInterface and Release, with this - 4.
    bool QueryInterfaceThunk(uint32_t riid, void **ppvObj);
    uint32_t ReleaseThunk();
    // Sub slot 4, 0x9a2b22: a tail jump to the primary's slot 5 (Shutdown), with this - 4.
    bool ShutdownThunk();
    // Sub slot 12, 0x1d4505: a tail jump to the primary's slot 9 (Vf9, sets the path), with this - 4.
    bool SetPathThunk(void *path);
    // Sub slot 34, 0x9a0ee0: the primary's slot 5 (Shutdown) through its vtable, then the base's slot 34 helper
    // (cGZPersistDBSegment::vf34, which calls slot 33 with 1: the deleting destructor).
    void DeleteSelf();
    // Sub slot 9, 0x99f980: true at once unless open. Calls slot 15 of every open record (no null check) and
    // empties each file's open list (releasing the records), clears the state, the extension map (slot 6) and the
    // files, destroys and frees the +0x6c object. Returns true.
    bool Close();
    // Sub slot 35, 0x9a0bf0: EnsureOpen, then whether FindEntry finds the key.
    bool TestForRecord(const DirResourceKey *key);
    // Sub slot 37, 0x9a08e0 (key, out, mode): EnsureOpen; mode bit 1 asks the primary's slot 32 for (key, out, 2),
    // else bit 0 for (key, out, 1), else false.
    bool OpenRecord(const DirResourceKey *key, void **out, uint32_t mode);
    // Sub slot 38, 0x99ff20 (key, out): the primary's slot 32 with (key, out, 3); on success the instance's probe is
    // erased. Returns slot 32's answer.
    bool CreateNewRecord(const DirResourceKey *key, void **out);
    // Sub slots 39 (0x99fe80) and 40 (0x99fde0), one argument, a record's face (record + 8): the face's slot 3 gives
    // its key; in the FindEntry file's open list the first record with that face gets its slot 15 (Close) or 16
    // (Abort) and is removed (RemoveOpenRecord). False when the file or the record is not found, or the record is null.
    bool CloseRecord(void *face);
    bool AbortRecord(void *face);
    // Sub slot 45, 0x9a0810 (out vector, filter): EnsureOpen; counts the files, in table order, that the filter's
    // slot 3 accepts (all of them without a filter), appending their keys to out when out is not null.
    uint32_t GetResourceKeyList(DirKeyVector *out, void *filter);

    // The record functions on the file system (lane NP). "Open for writing" below means a record whose +0x184 (the
    // access it was opened with) is 2 or 3. The file's path is the segment's path followed by its name.
    // Sub slot 36, 0x9a0650 (key): EnsureOpen; -1 without +0x3c, or (logged, 0xe5f030) when FindEntry finds nothing.
    // Every open record open for writing answers its slot 3 (the last answer counts); that answer when it is not -1,
    // else the file's length (cRZFile::LengthOfPath, -1 when there is no file).
    uint32_t GetRecordSize(const DirResourceKey *key);
    // Sub slot 41, 0x99ff60 (key): EnsureOpen; false without +0x3c, when FindEntry finds nothing (logged, 0xe5efd8)
    // or when the file has open records (logged, 0xe5ef70). Else the entry is erased, the file deleted (0x1c2e25,
    // its answer ignored) and the instance's probe erased; true.
    bool DeleteRecord(const DirResourceKey *key);
    // Sub slot 42, 0x9a09b0 (key, buffer, in/out size): EnsureOpen; -1 and *size = 0 without +0x3c, when FindEntry
    // finds nothing (logged, 0xe5f0e0) or when the file's only open record is open for writing (logged, 0xe5f078).
    // Without a buffer: *size = 0 and the file's length (LengthOfPath). Else the file is opened (cRZFile, read,
    // open-existing, share-read): failing, *size = 0 and -1; else min(length, *size) bytes are read (ReadWithCount,
    // which stores the count read in *size), the file is closed and *size returned.
    int32_t ReadRecord(const DirResourceKey *key, void *buffer, uint32_t *size);
    // Sub slot 47, 0x9a0520 (key, out stamp), no EnsureOpen: false without +0x3c or when FindEntry finds nothing.
    // Else *out = 0 and, unless the file's only open record is open for writing, when the file has a length and a
    // modification time (0x1c2068 with 2) that is not 0, *out = time ^ length. True.
    bool GetRecordStamp(const DirResourceKey *key, uint32_t *out);
    // Sub slot 33, 0x99eaeb: a tail jump to the primary's deleting destructor (0x99ebf0) with this - 4.
    cGZDBSegmentDirectoryFiles *DeletingDestructorThunk(uint32_t flags);

private:
    cGZDBSegmentDirectoryFiles *Primary();
    bool FinishRecord(void *face, int slot);
};
