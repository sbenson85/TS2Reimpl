// The text streams' own vtables (lane RU): what an image-free program's cRZTextOStream and cRZTextODSStream store in
// place of the image's 0xda2b4c and 0xda2bb0 (standalone/owned_statics.cpp aliases ts2_rva_da2b4c and ts2_rva_da2bb0
// to them). The image's 24 slots in order: an entry whose image body is hooked is the function the hook installs, the
// image's _purecall is ours, a folded body (shared with other classes, never hooked) is the member doing its work
// (tests/rz_file_streams_tables.cpp). RVAs are 1.18.0.187.
#include "rz/RZFileStreams.h"
#include "rz/cDebugStream.h"

#include <cstring>

extern "C" int __cdecl _purecall();

uint32_t cRZTextOStream::AddRef() { return ++mRefCount; }
void cRZTextOStream::Flush() {}
bool cRZTextOStream::ReturnsTrue() { return true; }
void *cRZTextOStream::GetOwner() { return mOwner; }

// Filled by a dynamic initialiser (a member function's address is no constant expression) before any code of the
// program runs, as the image's tables are there before its code.
#pragma warning(disable : 4073) // initializers put in library initialization area: intended
#pragma init_seg(lib)

namespace {

template <typename T> const void *Member(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance members only");
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

const void *Pure() { return reinterpret_cast<const void *>(&_purecall); }

} // namespace

#define TS2_TEXT_STREAM_SLOTS(slot3, slot14, slot21)                                                                    \
    Member(&cRZOStream::QueryInterface),     /*  0 0x1b5f6c (hooked) */                                                \
    Member(&cRZTextOStream::AddRef),         /*  1 0x148fb0 (folded) */                                                \
    Member(&cRZOStream::Release),            /*  2 0x1b5fc4 (folded) */                                                \
    slot3,                                   /*  3 */                                                                  \
    Member(&cRZTextOStream::SetSint8),       /*  4 0x1b63f6 (hooked) */                                                \
    Member(&cRZTextOStream::SetUint8),       /*  5 0x1b65c3 */                                                         \
    Member(&cRZTextOStream::SetSint16),      /*  6 0x1b624d */                                                         \
    Member(&cRZTextOStream::SetUint16),      /*  7 0x1b648d */                                                         \
    Member(&cRZTextOStream::SetSint32),      /*  8 0x1b62dd */                                                         \
    Member(&cRZTextOStream::SetUint32),      /*  9 0x1b64f0 */                                                         \
    Member(&cRZTextOStream::SetSint64),      /* 10 0x1b6383 */                                                         \
    Member(&cRZTextOStream::SetUint64),      /* 11 0x1b655f */                                                         \
    Member(&cRZTextOStream::SetFloat32),     /* 12 0x1b600b */                                                         \
    Member(&cRZTextOStream::SetFloat64),     /* 13 0x1b609a */                                                         \
    slot14,                                  /* 14 */                                                                  \
    Member(&cRZTextOStream::SetGZStr),       /* 15 0x1b6153 (hooked) */                                                \
    Member(&cRZOStream::SetGZSerializable),  /* 16 0x1b60fc (hooked) */                                                \
    Member(&cRZTextOStream::SetVoid),        /* 17 0x1b6648 (hooked) */                                                \
    Member(&cRZTextOStream::ReturnsTrue),    /* 18 0x144910 (folded) */                                                \
    Member(&cRZOStream::SetOwner),           /* 19 0x1b6617 (folded) */                                                \
    Member(&cRZTextOStream::GetOwner),       /* 20 0x145770 (folded) */                                                \
    slot21,                                  /* 21 */                                                                  \
    Member(&cRZOStream::SetByteOrder),       /* 22 0x1b5f3c (folded) */                                                \
    Member(&cRZTextOStream::ReturnsTrue),    /* 23 0x144910 (folded) */

// cRZTextOStream: slots 3 and 14 pure, 21 its deleting destructor (0x1b5848, hooked).
extern "C" const void *ts2_own_vt_da2b4c[24] = {
    TS2_TEXT_STREAM_SLOTS(Pure(), Pure(), Member(&cRZTextOStream::DeletingDestructor))
};

// cRZTextODSStream: slot 3 does nothing (0x140fe0, folded), 14 is OutputDebugStringA (0x1b61fe, hooked), 21 its
// deleting destructor (0x1b5829, hooked).
extern "C" const void *ts2_own_vt_da2bb0[24] = {
    TS2_TEXT_STREAM_SLOTS(Member(&cRZTextOStream::Flush), Member(&cRZTextODSStream::SetRZCharStr),
                          Member(&cRZTextODSStream::DeletingDestructor))
};

// cRZOFStream (lane FB): its own members in the slots they hook (0x1b5939 .. 0x1b661f, 0x1b57eb), the base's
// where the image's are the base's (0x1b5f6c, 0x1b60fc, folded 0x148fb0 0x1b5fc4 0x1b6617 0x145770 0x1b5f3c
// 0x144910), GetError 0x1b5941 (a shared body, never hooked; the member is ours).
extern "C" const void *ts2_own_vt_da2c14[24] = {
    Member(&cRZOStream::QueryInterface),     /*  0 0x1b5f6c */
    Member(&cRZTextOStream::AddRef),         /*  1 0x148fb0 (folded) */
    Member(&cRZOStream::Release),            /*  2 0x1b5fc4 (folded) */
    Member(&cRZOFStream::Flush),             /*  3 0x1b5939 */
    Member(&cRZOFStream::SetSint8),          /*  4 0x1b63d8 */
    Member(&cRZOFStream::SetUint8),          /*  5 0x1b65b4 */
    Member(&cRZOFStream::SetSint16),         /*  6 0x1b620d */
    Member(&cRZOFStream::SetUint16),         /*  7 0x1b644a */
    Member(&cRZOFStream::SetSint32),         /*  8 0x1b62a1 */
    Member(&cRZOFStream::SetUint32),         /*  9 0x1b64e1 */
    Member(&cRZOFStream::SetSint64),         /* 10 0x1b632f */
    Member(&cRZOFStream::SetUint64),         /* 11 0x1b6542 */
    Member(&cRZOFStream::SetFloat32),        /* 12 0x1b5fdb */
    Member(&cRZOFStream::SetFloat64),        /* 13 0x1b6069 */
    Member(&cRZOFStream::SetRZCharStr),      /* 14 0x1b616d */
    Member(&cRZOFStream::SetGZStr),          /* 15 0x1b6109 */
    Member(&cRZOStream::SetGZSerializable),  /* 16 0x1b60fc */
    Member(&cRZOFStream::SetVoid),           /* 17 0x1b661f */
    Member(&cRZOFStream::GetError),          /* 18 0x1b5941 (shared with cRZIFStream and cDebugStream) */
    Member(&cRZOStream::SetOwner),           /* 19 0x1b6617 (folded) */
    Member(&cRZTextOStream::GetOwner),       /* 20 0x145770 (folded) */
    Member(&cRZOFStream::DeletingDestructor),/* 21 0x1b57eb */
    Member(&cRZOStream::SetByteOrder),       /* 22 0x1b5f3c (folded) */
    Member(&cRZTextOStream::ReturnsTrue),    /* 23 0x144910 (folded) */
};

// cRZIFStream: slots 1, 2, 19, 20, 22 and 23 are the folded bodies shared with the output streams (the same
// refcount, owner and byte-order cells at the same offsets in both bases), so the output family's members do their
// work here too.
extern "C" const void *ts2_own_vt_da2c78[24] = {
    Member(&cRZIStream::QueryInterface),     /*  0 0x1b5f48 */
    Member(&cRZTextOStream::AddRef),         /*  1 0x148fb0 (folded) */
    Member(&cRZOStream::Release),            /*  2 0x1b5fc4 (folded) */
    Member(&cRZIFStream::Skip),              /*  3 0x1b6704 */
    Member(&cRZIFStream::GetSint8),          /*  4 0x1b5ddd */
    Member(&cRZIFStream::GetSint8),          /*  5 0x1b5ddd */
    Member(&cRZIFStream::GetSint16),         /*  6 0x1b5c2b */
    Member(&cRZIFStream::GetUint16),         /*  7 0x1b5e1d */
    Member(&cRZIFStream::GetSint32),         /*  8 0x1b5c9a */
    Member(&cRZIFStream::GetUint32),         /*  9 0x1b5e4d */
    Member(&cRZIFStream::GetSint64),         /* 10 0x1b5d04 */
    Member(&cRZIFStream::GetUint64),         /* 11 0x1b5e7d */
    Member(&cRZIFStream::GetFloat32),        /* 12 0x1b5952 */
    Member(&cRZIFStream::GetFloat64),        /* 13 0x1b5990 */
    Member(&cRZIFStream::GetRZCharStr),      /* 14 0x1b5b46 */
    Member(&cRZIFStream::GetGZStr),          /* 15 0x1b59db */
    Member(&cRZIStream::GetGZSerializable),  /* 16 0x1b59ce */
    Member(&cRZIFStream::GetVoid),           /* 17 0x1b5f0a */
    Member(&cRZOFStream::GetError),          /* 18 0x1b5941 */
    Member(&cRZOStream::SetOwner),           /* 19 0x1b6617 (folded) */
    Member(&cRZTextOStream::GetOwner),       /* 20 0x145770 (folded) */
    Member(&cRZIFStream::DeletingDestructor),/* 21 0x1b576f */
    Member(&cRZOStream::SetByteOrder),       /* 22 0x1b5f3c (folded) */
    Member(&cRZTextOStream::ReturnsTrue),    /* 23 0x144910 (folded) */
};

// cDebugStream (vtable 0xdcf704): cRZOFStream's table with its own numeric and text setters (4-9, 12, 14, 15) and
// deleting destructor (21).
extern "C" const void *ts2_own_vt_dcf704[24] = {
    Member(&cRZOStream::QueryInterface),     /*  0 0x1b5f6c */
    Member(&cRZTextOStream::AddRef),         /*  1 0x148fb0 (folded) */
    Member(&cRZOStream::Release),            /*  2 0x1b5fc4 (folded) */
    Member(&cRZOFStream::Flush),             /*  3 0x1b5939 */
    Member(&cDebugStream::SetSint8),         /*  4 0x520d70 */
    Member(&cDebugStream::SetUint8),         /*  5 0x5211f0 */
    Member(&cDebugStream::SetSint16),        /*  6 0x520bd0 */
    Member(&cDebugStream::SetUint16),        /*  7 0x521050 */
    Member(&cDebugStream::SetSint32),        /*  8 0x520ca0 */
    Member(&cDebugStream::SetUint32),        /*  9 0x521120 */
    Member(&cRZOFStream::SetSint64),         /* 10 0x1b632f */
    Member(&cRZOFStream::SetUint64),         /* 11 0x1b6542 */
    Member(&cDebugStream::SetFloat32),       /* 12 0x520720 */
    Member(&cRZOFStream::SetFloat64),        /* 13 0x1b6069 */
    Member(&cDebugStream::SetRZCharStr),     /* 14 0x520ab0 */
    Member(&cDebugStream::SetGZStr),         /* 15 0x520800 */
    Member(&cRZOStream::SetGZSerializable),  /* 16 0x1b60fc */
    Member(&cRZOFStream::SetVoid),           /* 17 0x1b661f */
    Member(&cRZOFStream::GetError),          /* 18 0x1b5941 */
    Member(&cRZOStream::SetOwner),           /* 19 0x1b6617 (folded) */
    Member(&cRZTextOStream::GetOwner),       /* 20 0x145770 (folded) */
    Member(&cDebugStream::DeletingDestructor),/* 21 0x512070 */
    Member(&cRZOStream::SetByteOrder),       /* 22 0x1b5f3c (folded) */
    Member(&cRZTextOStream::ReturnsTrue),    /* 23 0x144910 (folded) */
};
