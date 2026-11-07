#pragma once
// Fakes for the cTSStringSet loaders and writer (lane DG): the framework (file references and records), a file reference,
// a record (data, name, position calls, key), an input stream, an output stream, the persist system with a resource manager,
// a database record (ITS or serial) and the language service. The original and our code reach them through the same game
// globals; every call is logged as a few words, so both sides can be compared call for call.
#include "difftest.h"
#include "game/game.h"
#include "rz/cRZString.h"
#include "ts/cTSStringSet.h"

#include <climits>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace tsload {

using Bytes = std::vector<uint8_t>;

enum Event : uint32_t {
    evCreate = 1, evFileOpen, evFileOpenRecord, evFileAddRecord, evFileClose, evFileRelease,
    evRecName, evRecData, evRecRelease, evGetIStream, evGetOStream, evIn4, evIn5, evIn6, evIn15,
    evLang, evPersistManager, evPersistFind, evMgrOpen, evDbQuery, evDbKey, evDbClose, evDbRelease,
    evSerialSize, evSerialField, evSerialRelease, evRec10, evRec12, evRec13, evRec16, evRec21, evRecSetKey,
    evSegRelease, evOutU8, evOutS16, evOutVoid, evPersistWritable, evFileGetGroup, evOutRelease,
};

inline std::vector<uint32_t> gEvents;
inline void Ev(uint32_t a, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0)
{
    gEvents.push_back(a);
    gEvents.push_back(b);
    gEvents.push_back(c);
    gEvents.push_back(d);
}
inline uint32_t Hash(const void *data, size_t size)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; ++i) h = (h ^ static_cast<const uint8_t *>(data)[i]) * 16777619u;
    return h;
}

// ---- the configuration of one run ----
struct Config {
    bool createFile = true;      // the framework makes a file reference
    bool createRecord = true;    // the framework makes a record (Commit, serial copy)
    uint32_t openResult = 0;     // file->Open result
    bool fileHasRecord = true;   // file->OpenRecord finds the record
    bool managerOpens = true;    // manager->OpenRecord succeeds
    bool dbAnswersIts = true;    // the database record answers the record interface
    bool dbAnswersSerial = true; // ... and the serial one
    bool dbAnswersStream = true; // ... and the output stream (the factory's Write)
    bool segmentFound = true;    // persist->FindSegment succeeds
    bool writable = true;        // persist->IsSegmentWritable answer
    int languageId = 1;          // what the language service answers
    uint32_t position = 0x10, bytesLeft = 0x20; // record slots 12 and 10
    Bytes record;                // name (0x40 bytes) + payload of the record the file reference or the database hands out
    Bytes stream;                // what the record's input stream reads (0xFFF6)
    Bytes serialPayload;         // what the serial record copies into the new record
};
inline Config gConfig;

struct Rec {
    const void *vt;
    Bytes data;
    uint32_t position;
};
struct Plain {
    const void *vt;
};
inline Rec gRecord, gNewRecord;
inline Plain gFile, gFramework, gIn, gOut, gPersist, gManager, gDb, gSerial, gSegment, gLangService;
inline size_t gInCursor;
inline Bytes gOutBytes;

// ---- stream fakes ----
inline uint8_t NextByte()
{
    return gInCursor < gConfig.stream.size() ? gConfig.stream[gInCursor++] : 0;
}
inline bool __fastcall In4(Plain *, void *, void *out)
{
    Ev(evIn4);
    *static_cast<uint8_t *>(out) = NextByte();
    return true;
}
inline uint8_t __fastcall In5(Plain *, void *, uint8_t *out)
{
    Ev(evIn5);
    *out = NextByte();
    return 1;
}
inline uint8_t __fastcall In6(Plain *, void *, int16_t *out)
{
    Ev(evIn6);
    const uint8_t lo = NextByte();
    const uint8_t hi = NextByte();
    *out = static_cast<int16_t>(lo | hi << 8);
    return 1;
}
inline bool __fastcall In15(Plain *, void *, cRZString *out)
{
    Ev(evIn15);
    uint32_t length = 0;
    for (int i = 0; i < 4; ++i) length |= uint32_t(NextByte()) << (8 * i);
    std::string text;
    for (uint32_t i = 0; i < length; ++i) text.push_back(static_cast<char>(NextByte()));
    out->mString = text;
    return true;
}
inline uint32_t __fastcall OutRelease(Plain *, void *)
{
    Ev(evOutRelease);
    return 0;
}
inline uint8_t __fastcall OutU8(Plain *, void *, uint32_t value)
{
    Ev(evOutU8, value & 0xff);
    gOutBytes.push_back(static_cast<uint8_t>(value));
    return 1;
}
inline uint8_t __fastcall OutS16(Plain *, void *, uint32_t value)
{
    Ev(evOutS16, value & 0xffff);
    gOutBytes.push_back(static_cast<uint8_t>(value));
    gOutBytes.push_back(static_cast<uint8_t>(value >> 8));
    return 1;
}
inline uint32_t __fastcall OutVoid(Plain *, void *, const void *data, uint32_t size)
{
    Ev(evOutVoid, size, Hash(data, size));
    gOutBytes.insert(gOutBytes.end(), static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
    return 1;
}

// ---- the record (cITSPersistDBRecord face at offset 0) ----
inline uint32_t __fastcall RecRelease(Rec *, void *)
{
    Ev(evRecRelease);
    return 0;
}
inline void *__fastcall RecGetOStream(Rec *, void *)
{
    Ev(evGetOStream);
    return &gOut;
}
inline void *__fastcall RecGetIStream(Rec *, void *)
{
    Ev(evGetIStream);
    gInCursor = 0;
    return &gIn;
}
inline void *__fastcall RecGetData(Rec *self, void *)
{
    Ev(evRecData);
    return self->data.data() + self->position;
}
inline uint32_t __fastcall RecSlot10(Rec *, void *)
{
    Ev(evRec10);
    return gConfig.bytesLeft;
}
inline uint32_t __fastcall RecSlot12(Rec *, void *)
{
    Ev(evRec12);
    return gConfig.position;
}
inline uint32_t __fastcall RecSlot13(Rec *self, void *, uint32_t value)
{
    Ev(evRec13, value, self == &gNewRecord);
    return 0;
}
inline uint32_t __fastcall RecSlot16(Rec *self, void *, uint32_t value)
{
    Ev(evRec16, value, self == &gNewRecord);
    if (self == &gNewRecord) self->position = 0x40 + value; // the seek past the name that the record copy ends with
    return 0;
}
inline uint32_t __fastcall RecSlot21(Rec *self, void *, uint32_t value)
{
    Ev(evRec21, value, self == &gNewRecord);
    self->data.assign(value + 0x100, 0);
    self->position = 0;
    return 1;
}
inline void __fastcall RecSetKey(Rec *, void *, const uint32_t *key)
{
    Ev(evRecSetKey, key[0], key[1], key[2] ^ key[3] * 31);
}
inline bool __fastcall RecGetName(Rec *self, void *, cRZString *out)
{
    Ev(evRecName);
    size_t length = 0;
    while (length < 0x40 && length < self->data.size() && self->data[length] != 0) ++length;
    out->mString.assign(reinterpret_cast<const char *>(self->data.data()), length);
    return true;
}

// ---- the file reference ----
inline uint32_t __fastcall FileRelease(Plain *, void *)
{
    Ev(evFileRelease);
    return 0;
}
inline uint32_t __fastcall FileOpen(Plain *, void *, uint32_t group, uint32_t mode)
{
    Ev(evFileOpen, group, mode);
    return gConfig.openResult;
}
inline uint32_t __fastcall FileClose(Plain *, void *, uint32_t flags)
{
    Ev(evFileClose, flags);
    return 0;
}
inline uint8_t __fastcall FileOpenRecord(Plain *, void *, uint32_t type, uint32_t instance, void **out)
{
    Ev(evFileOpenRecord, type, instance);
    if (!gConfig.fileHasRecord) return 0;
    gRecord.data = gConfig.record;
    gRecord.position = 0x40;
    *out = &gRecord;
    return 1;
}
inline uint32_t __fastcall FileAddRecord(Plain *, void *, void *record, uint32_t type, uint32_t instance, cRZString *name, uint32_t flag)
{
    Ev(evFileAddRecord, type, instance, flag);
    Ev(evFileAddRecord, record == &gNewRecord ? 1 : record == nullptr ? 2 : 3, Hash(name->mString.data(), name->mString.size()));
    return 0;
}

// ---- the framework ----
inline uint8_t __fastcall FrameworkCreate(Plain *, void *, uint32_t classId, uint32_t iid, void **out)
{
    Ev(evCreate, classId, iid);
    if (classId == 0xb54e37d) {
        if (!gConfig.createFile) return 0;
        *out = &gFile;
        return 1;
    }
    if (classId == 0x2b70a71b) {
        if (!gConfig.createRecord) return 0;
        gNewRecord.data.assign(0x100, 0);
        gNewRecord.position = 0;
        *out = &gNewRecord;
        return 1;
    }
    return 0;
}
inline void *__fastcall DirectorSlot12(void *, void *) { return &gFramework; }

// ---- the persist system, its resource manager, the database record ----
inline void *__fastcall PersistGetManager(Plain *, void *)
{
    Ev(evPersistManager);
    return &gManager;
}
inline uint8_t __fastcall PersistFind(Plain *, void *, const uint32_t *key, uint32_t kind, void **out)
{
    Ev(evPersistFind, key[0], key[1], kind);
    Ev(evPersistFind, key[2], key[3]);
    if (!gConfig.segmentFound) return 0;
    *out = &gSegment;
    return 1;
}
inline uint8_t __fastcall PersistIsWritable(Plain *, void *, void *segment)
{
    Ev(evPersistWritable, segment == &gSegment);
    return gConfig.writable ? 1 : 0;
}
inline uint32_t __fastcall FileGetGroup(Plain *, void *)
{
    Ev(evFileGetGroup);
    return 0x4242;
}
inline uint32_t __fastcall SegRelease(Plain *, void *)
{
    Ev(evSegRelease);
    return 0;
}
inline uint8_t __fastcall MgrOpen(Plain *, void *, const uint32_t *key, void **out, uint32_t mode)
{
    Ev(evMgrOpen, key[0], key[1], key[2] ^ mode);
    if (!gConfig.managerOpens) return 0;
    *out = &gDb;
    return 1;
}
inline uint8_t __fastcall DbQuery(Plain *, void *, uint32_t iid, void **out)
{
    Ev(evDbQuery, iid);
    if (iid == 0x3b2271c0 && gConfig.dbAnswersIts) {
        gRecord.data = gConfig.record;
        gRecord.position = 0x40;
        *out = &gRecord;
        return 1;
    }
    if (iid == 0xc51b11cb && gConfig.dbAnswersStream) { // the factory Write asks its record for the output stream
        *out = &gOut;
        return 1;
    }
    if (iid == 0xc56b8f08 && gConfig.dbAnswersSerial) {
        *out = &gSerial;
        return 1;
    }
    return 0;
}
inline const uint32_t *__fastcall DbKey(Plain *, void *)
{
    static const uint32_t key[4] = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
    Ev(evDbKey);
    return key;
}
inline uint8_t __fastcall DbClose(Plain *, void *)
{
    Ev(evDbClose);
    return 1;
}
inline uint32_t __fastcall DbRelease(Plain *, void *)
{
    Ev(evDbRelease);
    return 0;
}
inline uint32_t __fastcall SerialSize(Plain *, void *)
{
    Ev(evSerialSize);
    return static_cast<uint32_t>(gConfig.serialPayload.size());
}
inline bool __fastcall SerialField(Plain *, void *, void *dst, uint32_t n)
{
    Ev(evSerialField, n, dst == gNewRecord.data.data());
    std::memcpy(dst, gConfig.serialPayload.data(), n < gConfig.serialPayload.size() ? n : gConfig.serialPayload.size());
    return true;
}
inline uint32_t __fastcall SerialRelease(Plain *, void *)
{
    Ev(evSerialRelease);
    return 0;
}
inline int8_t __fastcall LangId(Plain *, void *)
{
    Ev(evLang);
    return static_cast<int8_t>(gConfig.languageId);
}

inline void *gRecVT[64], *gFileVT[64], *gFrameworkVT[64], *gDirectorVT[64], *gInVT[64], *gOutVT[64], *gPersistVT[64],
    *gManagerVT[64], *gDbVT[64], *gSerialVT[64], *gSegmentVT[64], *gLangVT[64];

template <typename T> void *P(T fn) { return reinterpret_cast<void *>(fn); }

// Installs the fakes in the game image for the life of the object and restores everything afterwards.
class Fixture {
public:
    explicit Fixture(difftest::Context &ctx) : mCtx(ctx)
    {
        gRecVT[2] = P(&RecRelease);
        gRecVT[3] = P(&RecGetOStream);
        gRecVT[4] = P(&RecGetIStream);
        gRecVT[9] = P(&RecGetData);
        gRecVT[10] = P(&RecSlot10);
        gRecVT[12] = P(&RecSlot12);
        gRecVT[13] = P(&RecSlot13);
        gRecVT[16] = P(&RecSlot16);
        gRecVT[21] = P(&RecSlot21);
        gRecVT[23] = P(&RecGetName);
        gRecVT[25] = P(&RecSetKey);
        gFileVT[2] = P(&FileRelease);
        gFileVT[5] = P(&FileOpen);
        gFileVT[8] = P(&FileClose);
        gFileVT[19] = P(&FileOpenRecord);
        gFileVT[27] = P(&FileAddRecord);
        gFrameworkVT[3] = P(&FrameworkCreate);
        gDirectorVT[12] = P(&DirectorSlot12);
        gInVT[4] = P(&In4);
        gInVT[5] = P(&In5);
        gInVT[6] = P(&In6);
        gInVT[15] = P(&In15);
        gOutVT[2] = P(&OutRelease);
        gOutVT[5] = P(&OutU8);
        gOutVT[6] = P(&OutS16);
        gOutVT[17] = P(&OutVoid);
        gPersistVT[3] = P(&PersistGetManager);
        gPersistVT[34] = P(&PersistFind);
        gPersistVT[28] = P(&PersistIsWritable);
        gFileVT[11] = P(&FileGetGroup);
        gSegmentVT[2] = P(&SegRelease);
        gManagerVT[39] = P(&MgrOpen);
        gDbVT[0] = P(&DbQuery);
        gDbVT[2] = P(&DbRelease);
        gDbVT[3] = P(&DbKey);
        gDbVT[6] = P(&DbClose);
        gSerialVT[2] = P(&SerialRelease);
        gSerialVT[27] = P(&SerialField);
        gSerialVT[52] = P(&SerialSize);
        gLangVT[7] = P(&LangId);
        gRecord.vt = gNewRecord.vt = gRecVT;
        gFile.vt = gFileVT;
        gFramework.vt = gFrameworkVT;
        gIn.vt = gInVT;
        gOut.vt = gOutVT;
        gPersist.vt = gPersistVT;
        gManager.vt = gManagerVT;
        gDb.vt = gDbVT;
        gSerial.vt = gSerialVT;
        gSegment.vt = gSegmentVT;
        gLangService.vt = gLangVT;

        mDirector = *ctx.image.At<void *>(0x10c9390);
        mDirectorGuard = *ctx.image.At<int32_t>(0x10c93f4);
        mPersist = *ctx.image.At<void *>(0x10faf6c);
        mLang = *ctx.image.At<void *>(0x10f51c8);
        mCurrentLanguage = *ctx.image.At<int8_t>(0xff275c);
        *ctx.image.At<void *>(0x10c9390) = gDirectorVT;
        *ctx.image.At<int32_t>(0x10c93f4) = INT32_MIN;
        *ctx.image.At<void *>(0x10faf6c) = &gPersist;
        *ctx.image.At<void *>(0x10f51c8) = &gLangService;

        // The bullet replacement of language 0x12 uses function-local statics of the original (needle, replacement and their
        // thread-safe-init guards); the harness does not run the game's static initialisation, so the strings are built here and
        // the guards read as initialised: FFFD at 0xff261c/0xff263c (guards 0x10cc114/118), FFF6 at 0xff26dc/0xff26fc (0x10cc12c/130).
        static const uint32_t statics[5][3] = {{0xff25fc, 0x10cc110, 2},
            {0xff261c, 0x10cc114, 0}, {0xff263c, 0x10cc118, 1}, {0xff26dc, 0x10cc12c, 0}, {0xff26fc, 0x10cc130, 1}};
        for (int i = 0; i < 5; ++i) {
            mGuards[i] = *ctx.image.At<int32_t>(statics[i][1]);
            std::memcpy(mStatics[i], ctx.image.At<uint8_t>(statics[i][0]), sizeof(mStatics[i]));
            *ctx.image.At<int32_t>(statics[i][1]) = INT32_MIN;
            auto *string = reinterpret_cast<cRZString *>(ctx.image.At<uint8_t>(statics[i][0]));
            if (statics[i][2] == 2) string->Construct(); // GetString's empty text
            else string->Construct(statics[i][2] ? "\xe2\x80\x8b" : "\xe2\x80\xa2");
            mStaticAddress[i] = statics[i][0];
            mGuardAddress[i] = statics[i][1];
        }
    }
    Fixture(const Fixture &) = delete;
    Fixture &operator=(const Fixture &) = delete;
    ~Fixture()
    {
        for (int i = 0; i < 5; ++i) {
            reinterpret_cast<cRZString *>(mCtx.image.At<uint8_t>(mStaticAddress[i]))->Destruct();
            std::memcpy(mCtx.image.At<uint8_t>(mStaticAddress[i]), mStatics[i], sizeof(mStatics[i]));
            *mCtx.image.At<int32_t>(mGuardAddress[i]) = mGuards[i];
        }
        *mCtx.image.At<void *>(0x10c9390) = mDirector;
        *mCtx.image.At<int32_t>(0x10c93f4) = mDirectorGuard;
        *mCtx.image.At<void *>(0x10faf6c) = mPersist;
        *mCtx.image.At<void *>(0x10f51c8) = mLang;
        *mCtx.image.At<int8_t>(0xff275c) = mCurrentLanguage;
    }
    // The state before one side's run: a fresh log, the record data, the current-language cache unset.
    void Reset(const Config &config)
    {
        gConfig = config;
        gEvents.clear();
        gEvents.reserve(1024); // the fakes log during recorded calls: the log's growth must not count as a free
        gOutBytes.clear();
        gInCursor = 0;
        gRecord.data = config.record;
        gRecord.position = 0x40;
        gNewRecord.data.assign(0x100, 0);
        gNewRecord.position = 0;
        *mCtx.image.At<int8_t>(0xff275c) = -1;
    }

private:
    difftest::Context &mCtx;
    void *mDirector, *mPersist, *mLang;
    int32_t mDirectorGuard;
    int8_t mCurrentLanguage;
    int32_t mGuards[5];
    uint8_t mStatics[5][0x20];
    uint32_t mStaticAddress[5], mGuardAddress[5];
};

// ---- record builders ----
struct Writer {
    Bytes bytes;
    Writer &u8(uint8_t v) { bytes.push_back(v); return *this; }
    Writer &u16(uint16_t v) { bytes.push_back(uint8_t(v)); bytes.push_back(uint8_t(v >> 8)); return *this; }
    Writer &u32(uint32_t v) { u16(uint16_t(v)); u16(uint16_t(v >> 16)); return *this; }
    Writer &cstr(const std::string &s) { bytes.insert(bytes.end(), s.begin(), s.end()); bytes.push_back(0); return *this; }
    Writer &raw(const std::string &s) { bytes.insert(bytes.end(), s.begin(), s.end()); return *this; }
    Writer &gzstr(const std::string &s) { u32(uint32_t(s.size())); return raw(s); }
};

// A record: the 64-byte name field, the payload, then zeros (a count that claims more entries than there are reads empties).
inline Bytes MakeRecord(const std::string &name, const Bytes &payload)
{
    Bytes record(0x40, 0);
    std::memcpy(record.data(), name.data(), name.size() < 0x40 ? name.size() : 0x40);
    record.insert(record.end(), payload.begin(), payload.end());
    record.insert(record.end(), 0x200, 0);
    return record;
}

// ---- a dump of a set's loaded state, independent of heap addresses ----
inline void DumpMap(std::string &out, const cTSStringSetMap &map)
{
    for (const auto &entry : map) {
        out += "[" + std::to_string(int(entry.first)) + ":";
        for (cRZString *s = entry.second.mFirst; s != entry.second.mLast; ++s) {
            out += std::to_string(s->mString.size()) + "'" + s->mString + "' ";
        }
        out += "cap" + std::to_string(entry.second.mEnd - entry.second.mFirst) + "]";
    }
}
inline std::string DumpSet(cTSStringSet *set)
{
    std::string out = "S";
    DumpMap(out, set->mStrings);
    out += " C";
    DumpMap(out, set->mComments);
    out += " N'" + set->mName.mString + "'";
    return out;
}
// Which map entry a returned list is (the key), or "null".
inline std::string WhichList(cTSStringSet *set, const cRZStringVector *list)
{
    if (!list) return "null";
    for (const auto &entry : set->mStrings)
        if (static_cast<const cRZStringVector *>(&entry.second) == list) return "S" + std::to_string(int(entry.first));
    for (const auto &entry : set->mComments)
        if (static_cast<const cRZStringVector *>(&entry.second) == list) return "C" + std::to_string(int(entry.first));
    return "unknown";
}
inline std::string DumpEvents()
{
    std::string out = "E";
    for (uint32_t word : gEvents) out += std::to_string(word) + ",";
    return out;
}

} // namespace tsload
