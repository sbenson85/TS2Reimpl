#pragma once
#include "gz/cGZLegacyGridMatrix.h"
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>

// cRZUnknownAutoPtr<cRZSparseMatrix<WinGrid*Data>> (RVAs 1.18.0.187; lane FC, the standalone shape): a reference-counted
// holder of a sparse matrix, used by the window grid (cells, columns, rows). One face (cIGZUnknown), three slots:
// QueryInterface (the folded 0x1b945c: id 1 only), AddRef (the folded 0x145260: ++count at +8) and Release (0x86de32 /
// 0x86de56 / 0x86de7a), and an out-of-line destructor (0x85d7c8 / 0x85d7e9 / 0x85d80a: when the byte at +0xc says it owns
// the matrix, it clears +4 and deletes the matrix it held).
//
// One class per original instantiation, so each has its own vtable (the cell, column and row holders have three in the
// game; the column and row matrices are the same C++ type, so a tag tells them apart). Release: a count above 1 is
// decremented and returned; otherwise (1, or 0) the holder destroys itself and returns 0; the 0x10-byte block is freed with
// the sized operator delete. The grid handler (cGZLegacyHandlers5.size.cpp) builds them; nothing else in the image does.
template <class Matrix, class Tag> class cRZUnknownAutoPtrMatrix : public cIGZUnknown {
public:
    static constexpr uint32_t kSize = 0x10;

    explicit cRZUnknownAutoPtrMatrix(Matrix *matrix = nullptr) : mMatrix(matrix), mRefCount(0), mOwnsMatrix(matrix != nullptr) {}
    cRZUnknownAutoPtrMatrix(const cRZUnknownAutoPtrMatrix &) = delete;
    cRZUnknownAutoPtrMatrix &operator=(const cRZUnknownAutoPtrMatrix &) = delete;
    // 0x85d7c8 (cells), 0x85d7e9 (columns), 0x85d80a (rows): not virtual in the original (no slot for it).
    ~cRZUnknownAutoPtrMatrix()
    {
        if (mOwnsMatrix) {
            Matrix *const matrix = mMatrix;
            mMatrix = nullptr;
            delete matrix;
        }
    }

    bool QueryInterface(uint32_t riid, void **ppv) override // the folded 0x1b945c
    {
        if (riid == 1) {
            *ppv = this;
            AddRef();
        }
        return riid == 1;
    }
    uint32_t AddRef() override { return ++mRefCount; } // the folded 0x145260
    uint32_t Release() override                        // 0x86de32 / 0x86de56 / 0x86de7a
    {
        if (mRefCount > 1) return --mRefCount;
        delete this;
        return 0;
    }

    Matrix *mMatrix;     // +4: the matrix held
    uint32_t mRefCount;  // +8
    bool mOwnsMatrix;    // +0xc
};

struct WinGridCellTag;
struct WinGridColTag;
struct WinGridRowTag;
using cRZUnknownAutoPtrWinGridCellData = cRZUnknownAutoPtrMatrix<legacygrid::CellMatrix, WinGridCellTag>; // vtable 0xe49b9c
using cRZUnknownAutoPtrWinGridColData = cRZUnknownAutoPtrMatrix<legacygrid::LineMatrix, WinGridColTag>;   // vtable 0xe49bac
using cRZUnknownAutoPtrWinGridRowData = cRZUnknownAutoPtrMatrix<legacygrid::LineMatrix, WinGridRowTag>;   // vtable 0xe49bbc

static_assert(sizeof(cRZUnknownAutoPtrWinGridCellData) == 0x10, "holder size");
static_assert(offsetof(cRZUnknownAutoPtrWinGridCellData, mMatrix) == 4, "holder layout");
static_assert(offsetof(cRZUnknownAutoPtrWinGridCellData, mRefCount) == 8, "holder layout");
static_assert(offsetof(cRZUnknownAutoPtrWinGridCellData, mOwnsMatrix) == 0xc, "holder layout");
