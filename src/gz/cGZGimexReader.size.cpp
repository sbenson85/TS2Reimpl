// cGZGimexReader and cGZGimexWriter (lane LX, standalone shape; src/gz/cGZGimexReader.h).
#include "gz/cGZGimexReader.h"

#include "gz/cIGZCOM.h"
#include "gz/cIGZString.h"
#include "ifaces/cIGZDBSerialRecordAsIGZFile.h"
#include "ifaces/cIGZFile.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject
#include "rz/cRZFileName.h"
#include "rz/cRZString.h"

#include <new>

namespace {

constexpr uint32_t kRecordFileClass = 0x0a32c0da; // cGZDBSerialRecordAsIGZFile
constexpr uint32_t kRecordFileIid = 0x8a32c0ac;   // cIGZDBSerialRecordAsIGZFile
constexpr uint32_t kAccessRead = 1;
constexpr uint32_t kAccessWrite = 2;
constexpr int32_t kFirstFormat = 1; // the probe's rows, from the first after the null codec to gimex::kRows

cIGZCOM *GZCOM()
{
    return static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
}

// Holds the Gimex lock for the scope.
struct GimexLock {
    GimexLock() { gimex::Lock(); }
    GimexLock(const GimexLock &) = delete;
    GimexLock &operator=(const GimexLock &) = delete;
    ~GimexLock() { gimex::Unlock(); }
};

// A cRZString local built from a cIGZString (0x144f2a), destroyed at scope end.
struct LocalString {
    union {
        cRZString s;
    };
    explicit LocalString(const cIGZString &text) { s.Construct(text); }
    ~LocalString() { s.Destruct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
};

// A cRZFileName local (0x1c7ef4 / 0x1c0eb2).
struct LocalFileName {
    union {
        cRZFileName f;
    };
    explicit LocalFileName(const cRZString &path) { f.Construct(&path); }
    ~LocalFileName() { f.Destruct(); }
    LocalFileName(const LocalFileName &) = delete;
    LocalFileName &operator=(const LocalFileName &) = delete;
};

// Both classes' kByExtension step: the stream's path (cIGZFile slot 25) split, and its extension looked up under the
// lock. True and *format the matched (current) format when one matched; *format untouched otherwise.
bool FormatOfPath(ifaces::cIGZFile *stream, int32_t *format)
{
    LocalString path(*static_cast<const cIGZString *>(stream->GetPath()));
    if (path.s.Strlen() == 0) return false;
    LocalFileName name(path.s);
    GimexLock lock;
    if (gimex::FindFormatByExtension(name.f.GetExtension()->ToChar(), nullptr) == 0) return false;
    *format = gimex::GetFormat();
    return true;
}

} // namespace

// ---- cGZGimexStreamHolder ----

ifaces::cIGZFile *cGZGimexStreamHolder::GetStream()
{
    if (mStream) return mStream;
    return mRecordFile ? mRecordFile->AsFile() : nullptr;
}

bool cGZGimexStreamHolder::AttachRecord(cIGZUnknown *record)
{
    cIGZCOM *com = GZCOM();
    if (mRecordFile) {
        ifaces::cIGZDBSerialRecordAsIGZFile *old = mRecordFile;
        mRecordFile = nullptr;
        old->Release();
    }
    com->GetClassObject(kRecordFileClass, kRecordFileIid, reinterpret_cast<void **>(&mRecordFile));
    if (!mRecordFile) return false;
    mRecordFile->SetRecord(record); // its answer (always 1) is not looked at
    return true;
}

bool cGZGimexStreamHolder::TakeStream(ifaces::cIGZFile *stream, uint32_t mode)
{
    bool taken = false;
    if (stream) {
        if (stream->IsOpen()) {
            uint32_t access = 0;
            uint32_t creation = 0;
            uint32_t sharing = 0;
            stream->GetOpenModes(&access, &creation, &sharing);
            taken = (access & mode) != 0;
        }
        if (taken) {
            stream->AddRef();
        } else {
            stream = nullptr;
        }
    }
    if (mStream) mStream->Release();
    mStream = stream;
    return taken;
}

// ---- cGZGimexReader ----

cGZGimexReader::cGZGimexReader() = default;

cGZGimexReader::~cGZGimexReader()
{
    if (mRecordFile) mRecordFile->Release();
}

bool cGZGimexReader::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 1 && riid != kIid) return false;
    *ppv = static_cast<cIGZGimexReader *>(this);
    AddRef();
    return true;
}

uint32_t cGZGimexReader::AddRef()
{
    return ++mRefCount;
}

uint32_t cGZGimexReader::Release()
{
    if (mRefCount > 1) return --mRefCount;
    FinalRelease();
    return 0;
}

bool cGZGimexReader::SetRecord(cIGZUnknown *record)
{
    return AttachRecord(record);
}

bool cGZGimexReader::SetStream(ifaces::cIGZFile *stream)
{
    if (mOpen) Close();
    return TakeStream(stream, kAccessRead);
}

bool cGZGimexReader::OpenCurrent(int32_t *frames)
{
    *frames = gimex::Open(&mInstance, GetStream(), nullptr, 1);
    mOpen = 1;
    return true;
}

bool cGZGimexReader::OpenByExtension(const char *extension, int32_t *frames)
{
    if (mOpen || !GetStream()) return false;
    GimexLock lock;
    const int32_t format = gimex::FindFormatByExtension(extension, nullptr);
    if (format == 0) return false;
    mFormat = format;
    return OpenCurrent(frames);
}

bool cGZGimexReader::Open(uint32_t format, int32_t *frames)
{
    if (mOpen || !GetStream()) return false;
    if (format != gimex::kProbe && format != gimex::kByExtension && format != gimex::kByExtensionOrProbe) {
        mFormat = static_cast<int32_t>(format);
    } else {
        bool found = false;
        if (format == gimex::kByExtension || format == gimex::kByExtensionOrProbe) {
            found = FormatOfPath(GetStream(), &mFormat);
        }
        if (!found) {
            if (format != gimex::kProbe && format != gimex::kByExtensionOrProbe) return false;
            // The highest score above 0 wins, the first row of equals.
            int32_t best = 0;
            int32_t bestScore = 0;
            for (int32_t row = kFirstFormat; row < static_cast<int32_t>(gimex::kProbe); ++row) {
                int32_t score;
                {
                    GimexLock lock;
                    gimex::SetFormat(row);
                    score = gimex::Is(GetStream());
                }
                if (score > bestScore) {
                    bestScore = score;
                    best = row;
                }
            }
            if (best == 0) return false;
            mFormat = best;
        }
    }
    GimexLock lock;
    gimex::SetFormat(mFormat);
    return OpenCurrent(frames);
}

gimex::Info *cGZGimexReader::GetInfo(int32_t frame)
{
    GimexLock lock;
    gimex::SetFormat(mFormat);
    return gimex::GetInfo(mInstance, frame);
}

bool cGZGimexReader::Read(gimex::Info *info, void *pixels, int32_t pitch)
{
    if (!mOpen) return false;
    GimexLock lock;
    gimex::SetFormat(mFormat);
    return gimex::Read(mInstance, info, pixels, pitch);
}

int32_t cGZGimexReader::FreeInfo(gimex::Info *info)
{
    return gimex::FreeMemory(info);
}

bool cGZGimexReader::Close()
{
    if (!mOpen) return false;
    bool result = false;
    if (mInstance) {
        GimexLock lock;
        gimex::SetFormat(mFormat);
        result = gimex::Close(mInstance);
    }
    mInstance = nullptr;
    mFormat = 0;
    mOpen = 0;
    return result;
}

void cGZGimexReader::FinalRelease()
{
    if (mStream) {
        if (mStream->IsOpen()) mStream->Close();
        SetStream(nullptr);
    }
    delete this;
}

// ---- cGZGimexWriter ----

cGZGimexWriter::cGZGimexWriter() = default;

cGZGimexWriter::~cGZGimexWriter()
{
    if (mRecordFile) mRecordFile->Release();
}

bool cGZGimexWriter::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 1 && riid != kIid) return false;
    *ppv = static_cast<cIGZGimexWriter *>(this);
    AddRef();
    return true;
}

uint32_t cGZGimexWriter::AddRef()
{
    return ++mRefCount;
}

uint32_t cGZGimexWriter::Release()
{
    if (mRefCount > 1) return --mRefCount;
    FinalRelease();
    return 0;
}

bool cGZGimexWriter::SetRecord(cIGZUnknown *record)
{
    return AttachRecord(record);
}

bool cGZGimexWriter::SetStream(ifaces::cIGZFile *stream)
{
    if (mOpen) Close();
    return TakeStream(stream, kAccessWrite);
}

uint8_t cGZGimexWriter::OpenCurrent(int32_t flag)
{
    GimexLock lock;
    gimex::SetFormat(mFormat);
    mOpen = static_cast<uint8_t>(gimex::WOpen(&mInstance, GetStream(), nullptr, flag));
    return mOpen;
}

uint8_t cGZGimexWriter::OpenByExtension(const char *extension, int32_t flag)
{
    if (mOpen || !GetStream()) return false;
    gimex::Lock();
    mFormat = gimex::FindFormatByExtension(extension, nullptr);
    // ORIGINAL-BUG: no format matched: the lock taken above is never released.
    if (mFormat == 0) return false;
    const uint8_t result = OpenCurrent(flag);
    gimex::Unlock();
    return result;
}

uint8_t cGZGimexWriter::Open(uint32_t format, int32_t flag)
{
    if (mOpen || !GetStream()) return false;
    if (format != gimex::kProbe && format != gimex::kByExtension && format != gimex::kByExtensionOrProbe) {
        mFormat = static_cast<int32_t>(format);
    } else {
        if (format == gimex::kProbe) return false;
        if (!FormatOfPath(GetStream(), &mFormat)) return false;
    }
    GimexLock lock;
    gimex::SetFormat(mFormat);
    return OpenCurrent(flag);
}

bool cGZGimexWriter::Write(gimex::Info *info, const void *pixels, int32_t pitch)
{
    if (!mOpen) return false;
    GimexLock lock;
    if (mFormat == static_cast<int32_t>(gimex::kFormatLibJpeg)) info->jpegFlag = 1;
    gimex::SetFormat(mFormat);
    return gimex::Write(mInstance, info, pixels, pitch);
}

bool cGZGimexWriter::Close()
{
    if (!mOpen) return false;
    bool result = false;
    if (mInstance) {
        GimexLock lock;
        gimex::SetFormat(mFormat);
        result = gimex::WClose(mInstance);
    }
    mFormat = 0;
    mInstance = nullptr;
    mOpen = 0;
    return result;
}

void cGZGimexWriter::FinalRelease()
{
    if (mStream) {
        if (mStream->IsOpen()) mStream->Close();
        SetStream(nullptr);
    }
    delete this;
}

// ---- adapters ----

cGZGimexReader *__cdecl cGZGimexReaderAdapters::CreateReader()
{
    return new (std::nothrow) cGZGimexReader;
}

cGZGimexWriter *__cdecl cGZGimexReaderAdapters::CreateWriter()
{
    return new (std::nothrow) cGZGimexWriter;
}

cGZGimexReader *__fastcall cGZGimexReaderAdapters::ConstructReader(void *memory, void *)
{
    return ::new (memory) cGZGimexReader;
}

cGZGimexWriter *__fastcall cGZGimexReaderAdapters::ConstructWriter(void *memory, void *)
{
    return ::new (memory) cGZGimexWriter;
}
