// The lot imposter's image writer (lane 261111q; sg/cImposterImageWriter.h).
#include "sg/cImposterImageWriter.h"

#include "gz/cGZGimexReader.h"
#include "gz/cIGZCOM.h"
#include "gz/cIGZFile.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject
#include "rz/cRZFile.h"
#include "sg/cImageData.h"

#include <cstring>

namespace nTSSG {

using nGZSceneGraph::cIImageData;

cImposterImageWriter *cImposterImageWriter::Construct()
{
    mFile = nullptr;
    mCodec = nullptr;
    cIGZCOM *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    // The codec slot is taken empty before the framework fills it (it was just cleared).
    if (cIGZGimexWriter *const old = mCodec) {
        mCodec = nullptr;
        old->Release();
    }
    com->GetClassObject(cGZGimexWriter::kClassId, cIGZGimexWriter::kIid, reinterpret_cast<void **>(&mCodec));
    void *const memory = ::operator new(kFileSize);
    cIGZFile *const file =
        memory ? reinterpret_cast<cIGZFile *>(static_cast<cRZFile *>(memory)->Construct()) : nullptr;
    cIGZFile *const old = mFile;
    if (file != old) {
        if (file) file->AddRef();
        mFile = file;
        if (old) old->Release();
    }
    mReady = mCodec != nullptr && mFile != nullptr;
    return this;
}

void __cdecl cImposterImageWriter::SetCodecFormat(int32_t mode, gimex::Info *info)
{
    info->jpegFlag = 3;
    int32_t bits;
    switch (mode) {
    case 1: bits = 0x19; break;
    case 4:
    case 5: bits = 0x1b; break;
    default: bits = 0x1c; break;
    }
    info->sourceBits = bits;
    info->redBits = 8;
    info->greenBits = 8;
    info->blueBits = 8;
    info->alphaBits = static_cast<uint32_t>(bits - 0x18);
}

bool cImposterImageWriter::Write(cIImageData *image, uint32_t level, const char *path, uint32_t format)
{
    if (!mReady || !path) return false;
    if (image->GetFormat() < 1) return false;
    if (image->GetFormat() >= kFormatLimit) return false;
    mFile->SetPath(path);
    if (!mFile->Open(kOpenAccess, kOpenCreation, kOpenSharing)) return false;
    bool written = false;
    mCodec->SetStream(reinterpret_cast<ifaces::cIGZFile *>(mFile));
    if (mCodec->Open(format, kCodecOpenFlag)) {
        gimex::Info info;
        std::memset(&info, 0, sizeof(info));
        info.size = gimex::kWriteInfoSize;
        info.version = gimex::kWriteInfoVersion;
        info.unknownC = 0;
        switch (image->GetFormat()) {
        case 1:
            info.bitsPerPixel = 0x20;
            info.sourceBits = 0x20;
            info.alphaBits = 8;
            info.redBits = info.greenBits = info.blueBits = 8;
            break;
        case 2:
            info.bitsPerPixel = 0x18;
            info.sourceBits = 0x18;
            info.alphaBits = 0;
            info.redBits = info.greenBits = info.blueBits = 8;
            break;
        case 3:
            info.bitsPerPixel = 8;
            info.sourceBits = 8;
            info.alphaBits = 8;
            info.redBits = info.greenBits = info.blueBits = 0;
            break;
        case 4:
            SetCodecFormat(1, &info);
            info.bitsPerPixel = info.sourceBits;
            break;
        case 5:
            SetCodecFormat(3, &info);
            info.bitsPerPixel = info.sourceBits;
            break;
        default: break;
        }
        info.width = image->LevelWidth(level);
        info.height = image->LevelHeight(level);
        const int32_t pitch = image->LevelPitch(level);
        info.imageBytes = static_cast<uint32_t>(pitch * image->LevelHeight(level));
        info.paletteCount = 0;
        info.quality = kQuality;
        info.variant = 0;
        // The codec is taken before the image is asked for its rows; Close asks the writer again.
        cIGZGimexWriter *const codec = mCodec;
        const int32_t rowPitch = image->LevelPitch(level);
        const bool ok = codec->Write(&info, image->LevelData(level), rowPitch);
        written = mCodec->Close() && ok;
    }
    return mFile->Close() && written;
}

bool cImposterImageWriter::WriteTop(cIImageData *image, const char *path, uint32_t format)
{
    return Write(image, 0, path, format);
}

} // namespace nTSSG
