#pragma once
// gmdcfx: real GMDC resources from the game copy's packages, and a reader over their bytes (lane RA, for
// tests/sg_render_object.cpp; the same independent reader tests/gmdc_decode_acceptance.cpp has in its own file).
//
//   std::wstring game = gmdcfx::GameDir();            // local\game (above the test exe, or the main checkout's); "" without
//   gmdcfx::Package p; p.Load(game + L"\\...package");
//   std::vector<uint8_t> rcol; p.Find(type, group, instance, resource, rcol);
//   size_t root = gmdcfx::RootBlock(rcol);            // the GMDC block after the RCOL header (0: not one GMDC block)
//   gmdcfx::MakeTable();                              // once: our cGZPersistBinaryReader's slots
//   gmdcfx::Over(&block);                             // the bytes the reader reads next
//   face->Read(gmdcfx::Reader());                     // a container's Read over our reader and a memory stream
//
// The package index, the DIR list, the QFS decompression and the RCOL header are this file's own code (not the game's or
// ours), so the bytes a decoder reads come from an independent reader. Read only; nothing of the game copy is written or
// committed.
#include "code_patch.h"
#include "drawfx.h"

#include "gz/cGZPersistBinaryReader.h"
#include "hook/hooks.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace ifaces {
class cIGZPersistReader;
} // namespace ifaces

namespace gmdcfx {

constexpr uint32_t kGmdcType = 0xac4f8687, kDirType = 0xe86b1eef;

// The game copy: local\game above the test exe, or in the main checkout when the exe is in a worktree.
inline std::wstring GameDir()
{
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    for (int up = 0; up < 8; ++up) {
        const size_t slash = dir.find_last_of(L"\\/");
        if (slash == std::wstring::npos) break;
        dir.resize(slash);
        const std::wstring probe = dir + L"\\local\\game";
        if (GetFileAttributesW((probe + L"\\Base").c_str()) != INVALID_FILE_ATTRIBUTES) return probe;
    }
    const size_t at = std::wstring(exe).find(L"\\.claude\\worktrees\\");
    if (at != std::wstring::npos) {
        const std::wstring probe = std::wstring(exe).substr(0, at) + L"\\local\\game";
        if (GetFileAttributesW((probe + L"\\Base").c_str()) != INVALID_FILE_ATTRIBUTES) return probe;
    }
    return L"";
}

// `n` bytes of a file from `at`.
inline bool ReadRange(const std::wstring &path, uint32_t at, uint32_t n, std::vector<uint8_t> &out)
{
    FILE *f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
    out.resize(n);
    const bool ok = std::fseek(f, long(at), SEEK_SET) == 0 && std::fread(out.data(), 1, n, f) == n;
    std::fclose(f);
    return ok;
}

inline uint32_t Word(const std::vector<uint8_t> &b, size_t at)
{
    uint32_t v = 0;
    if (at + 4 <= b.size()) std::memcpy(&v, b.data() + at, 4);
    return v;
}

// QFS (RefPack): a 4-byte compressed size, 0x10 0xfb, the 3-byte big-endian decompressed size, then the codes.
inline bool Decompress(const uint8_t *in, size_t n, std::vector<uint8_t> &out)
{
    if (n < 9 || in[4] != 0x10 || in[5] != 0xfb) return false;
    const size_t size = size_t(in[6]) << 16 | size_t(in[7]) << 8 | in[8];
    out.clear();
    out.reserve(size);
    size_t at = 9;
    while (at < n) {
        const uint32_t b0 = in[at];
        uint32_t plain = 0, copy = 0, offset = 0;
        bool stop = false;
        if (b0 < 0x80) {
            if (at + 2 > n) return false;
            const uint32_t b1 = in[at + 1];
            plain = b0 & 3;
            copy = ((b0 >> 2) & 7) + 3;
            offset = ((b0 & 0x60) << 3) + b1 + 1;
            at += 2;
        } else if (b0 < 0xc0) {
            if (at + 3 > n) return false;
            const uint32_t b1 = in[at + 1], b2 = in[at + 2];
            plain = b1 >> 6;
            copy = (b0 & 0x3f) + 4;
            offset = ((b1 & 0x3f) << 8) + b2 + 1;
            at += 3;
        } else if (b0 < 0xe0) {
            if (at + 4 > n) return false;
            const uint32_t b1 = in[at + 1], b2 = in[at + 2], b3 = in[at + 3];
            plain = b0 & 3;
            copy = ((b0 & 0x0c) << 6) + b3 + 5;
            offset = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1;
            at += 4;
        } else if (b0 < 0xfc) {
            plain = ((b0 & 0x1f) << 2) + 4;
            at += 1;
        } else {
            plain = b0 & 3;
            stop = true;
            at += 1;
        }
        if (at + plain > n) return false;
        out.insert(out.end(), in + at, in + at + plain);
        at += plain;
        if (copy) {
            if (offset > out.size()) return false;
            const size_t from = out.size() - offset;
            for (uint32_t i = 0; i < copy; ++i) out.push_back(out[from + i]);
        }
        if (stop) break;
    }
    return out.size() == size;
}

// A package's resources by key, decompressed when its DIR entry lists them. Index entries are 24 bytes (type, group,
// instance, resource, offset, size) or 20 (no resource); DIR entries likewise 20 or 16, the last word the decompressed size.
struct Package {
    std::wstring path;
    size_t entrySize = 0;
    std::vector<uint32_t> index; // the entries' words
    std::vector<uint32_t> dir;   // the compressed keys: type, group, instance, resource

    bool Load(const std::wstring &file)
    {
        path = file;
        index.clear();
        dir.clear();
        std::vector<uint8_t> header, bytes;
        if (!ReadRange(path, 0, 0x60, header) || Word(header, 0) != 0x46504244) return false; // "DBPF"
        const uint32_t count = Word(header, 0x24), at = Word(header, 0x28), size = Word(header, 0x2c);
        if (count == 0 || size % count != 0 || !ReadRange(path, at, size, bytes)) return false;
        entrySize = size / count;
        if (entrySize != 24 && entrySize != 20) return false;
        const size_t words = entrySize / 4;
        index.resize(size_t(count) * words);
        std::memcpy(index.data(), bytes.data(), size);
        for (size_t e = 0; e < count; ++e) {
            const uint32_t *w = &index[e * words];
            if (w[0] != kDirType) continue;
            if (!ReadRange(path, w[words - 2], w[words - 1], bytes)) return false;
            const size_t dirWords = words - 1;
            for (size_t k = 0; k + dirWords * 4 <= bytes.size(); k += dirWords * 4) {
                uint32_t key[4] = {0, 0, 0, 0};
                std::memcpy(key, bytes.data() + k, (dirWords - 1) * 4);
                dir.insert(dir.end(), key, key + 4);
            }
        }
        return true;
    }

    bool Find(uint32_t type, uint32_t group, uint32_t instance, uint32_t resource, std::vector<uint8_t> &out) const
    {
        const size_t words = entrySize / 4;
        const bool hasResource = entrySize == 24;
        for (size_t e = 0; e * words < index.size(); ++e) {
            const uint32_t *w = &index[e * words];
            if (w[0] != type || w[1] != group || w[2] != instance || (hasResource && w[3] != resource)) continue;
            std::vector<uint8_t> bytes;
            if (!ReadRange(path, w[words - 2], w[words - 1], bytes)) return false;
            bool compressed = false;
            for (size_t k = 0; k < dir.size(); k += 4)
                if (dir[k] == type && dir[k + 1] == group && dir[k + 2] == instance &&
                    (!hasResource || dir[k + 3] == resource))
                    compressed = true;
            if (compressed) return Decompress(bytes.data(), bytes.size(), out);
            out = std::move(bytes);
            return true;
        }
        return false;
    }
};

// The RCOL header ReadRoot reads: an optional 0xffff0000 | version word, the link count, the links (group, instance,
// resource from version 1, type), the class id count and ids. The root block's offset, or 0 if it is not one GMDC block.
inline size_t RootBlock(const std::vector<uint8_t> &b)
{
    size_t at = 0;
    uint32_t version = 0, links = Word(b, at);
    at += 4;
    if ((links & 0xffff0000u) == 0xffff0000u) {
        version = links & 0xffff;
        links = Word(b, at);
        at += 4;
    }
    at += size_t(links) * (version >= 1 ? 16 : 12);
    const uint32_t blocks = Word(b, at);
    at += 4;
    if (blocks != 1 || Word(b, at) != kGmdcType || at + 4 > b.size()) return 0;
    return at + 4;
}

// ---- the memory stream our reader reads ----
inline const std::vector<uint8_t> *gBytes = nullptr;
inline size_t gAt = 0;
inline void Over(const std::vector<uint8_t> *bytes)
{
    gBytes = bytes;
    gAt = 0;
}

inline bool TakeBytes(void *p, size_t n)
{
    if (gBytes->size() - gAt < n) {
        drawfx::Note("short read", uint32_t(gAt));
        return false;
    }
    std::memcpy(p, gBytes->data() + gAt, n);
    gAt += n;
    return true;
}

#define GMDCFX_BAD(n) \
    virtual void Bad##n() { drawfx::Note("bad slot " #n, n); }

// A cIGZIStream over the package bytes: the slots the reader calls (cGZPersistBinaryReader.h), numbers little-endian as
// in the files. Texts go into the string through its FromChar (slot 3: text, length).
struct MemIStream {
    GMDCFX_BAD(0) GMDCFX_BAD(1) GMDCFX_BAD(2) GMDCFX_BAD(3) GMDCFX_BAD(4)
    virtual bool GetUint8(uint8_t *d) { return TakeBytes(d, 1); } // 5
    GMDCFX_BAD(6)
    virtual bool GetUint16(uint16_t *d) { return TakeBytes(d, 2); } // 7
    virtual bool GetSint32(int32_t *d) { return TakeBytes(d, 4); }  // 8
    virtual bool GetUint32(uint32_t *d) { return TakeBytes(d, 4); } // 9
    GMDCFX_BAD(10) GMDCFX_BAD(11)
    virtual bool GetFloat32(float *d) { return TakeBytes(d, 4); } // 12
    GMDCFX_BAD(13) GMDCFX_BAD(14)
    virtual bool GetGZStr(void *str) // 15
    {
        // The package format: the length as a packed uint32, 7 bits a byte, low first.
        uint32_t n = 0;
        for (int shift = 0;; shift += 7) {
            uint8_t b = 0;
            if (!TakeBytes(&b, 1)) return false;
            if (shift < 32) n |= uint32_t(b & 0x7f) << shift;
            if (!(b & 0x80)) break;
        }
        if (gBytes->size() - gAt < n) {
            drawfx::Note("short text", uint32_t(gAt));
            return false;
        }
        const char *text = reinterpret_cast<const char *>(gBytes->data() + gAt);
        gAt += n;
        using Fn = void(__thiscall *)(void *, const char *, uint32_t);
        reinterpret_cast<Fn>((*reinterpret_cast<void ***>(str))[3])(str, text, n);
        return true;
    }
    GMDCFX_BAD(16)
    virtual bool GetVoid(void *d, uint32_t n) { return TakeBytes(d, n); } // 17
};
inline MemIStream gIStream;

#undef GMDCFX_BAD

// ---- our reader, with a vtable of its own members (a slot nobody calls is null: a call faults) ----
struct NoCount {
    uint32_t AddRef() { return 1; }
    uint32_t Release() { return 1; }
};
inline void *gReaderSlots[30];

inline void MakeTable()
{
    using R = cGZPersistBinaryReader;
    void **r = gReaderSlots;
    r[0] = MemberAddress(&R::QueryInterface);
    r[1] = MemberAddress(&NoCount::AddRef);
    r[2] = MemberAddress(&NoCount::Release);
    r[3] = MemberAddress(&R::ReadRoot);
    r[4] = MemberAddress(&R::GetBool);
    r[5] = MemberAddress(&R::GetSint8);
    r[6] = MemberAddress(&R::GetUint8);
    r[7] = MemberAddress(&R::GetSint16);
    r[8] = MemberAddress(&R::GetUint16);
    r[9] = MemberAddress(&R::GetSint32);
    r[10] = MemberAddress(&R::GetUint32);
    r[11] = MemberAddress(&R::GetFloat32);
    r[12] = MemberAddress(&R::GetFloat64);
    r[13] = MemberAddress(&R::GetBoolArray);
    r[14] = MemberAddress(&R::GetVoid);
    r[15] = MemberAddress(&R::GetVoid);
    r[16] = MemberAddress(&R::GetSint16Array);
    r[17] = MemberAddress(&R::GetUint16Array);
    r[18] = MemberAddress(&R::GetSint32Array);
    r[19] = MemberAddress(&R::GetUint32Array);
    r[20] = MemberAddress(&R::GetFloat32Array);
    r[21] = MemberAddress(&R::GetFloat64Array);
    r[22] = MemberAddress(&R::GetGZStr);
    r[23] = MemberAddress(&R::Skip);
    r[24] = MemberAddress(&R::ReadObject);
    r[25] = MemberAddress(&R::GetObjectRef);
    r[26] = MemberAddress(&R::ReadStringRecord);
    r[29] = MemberAddress(&R::DeletingDestructor);
}

inline cGZPersistBinaryReader gReaderObject;

inline ifaces::cIGZPersistReader *Reader()
{
    gReaderObject = cGZPersistBinaryReader{};
    gReaderObject.mVTable = gReaderSlots;
    gReaderObject.mRefCount = 1;
    gReaderObject.mStream = &gIStream;
    return reinterpret_cast<ifaces::cIGZPersistReader *>(&gReaderObject);
}

} // namespace gmdcfx
