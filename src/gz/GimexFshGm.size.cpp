// FSH's GM image decoder (lane MR; src/gz/GimexFshGm.h, 0x895c47..0x89694e): the code tables, the DC and block decoders, the
// macroblock reader, the inverse transform, the colour conversion and the stream driver. Everything is integer arithmetic.
#include "gz/GimexFshGm.h"

#include "game/game.h"

namespace gimex {

namespace fsh {

GmState gGmState = {nullptr, 0, {0, 0, 0}, {}, 6, 0, {}, {}, 0};

namespace {

// ---- tables in the original image (RVAs) ----
constexpr uint32_t kLumaDcShort = 0x105ddb0;       // 31 entries of (size, code length), indexed by the next 5 bits
constexpr uint32_t kLumaDcLong = 0x105da10;        // 512 entries, indexed by the next 9 bits
constexpr uint32_t kChromaDcShort = 0x105de10;
constexpr uint32_t kChromaDcLong = 0x105d690;      // 1024 entries, indexed by the next 10 bits
constexpr uint32_t kZigzag = 0x105dc40;
constexpr uint32_t kQuantMatrix = 0x105dc80;
// The run / level code tables: entries of (run, level, code length); run 0x40 ends the block, 0x41 is the escape. One table per
// class of the next 16 bits (see LookUpCode); the predicted blocks use the alternates when their flag is set.
constexpr uint32_t kCodesA = 0x105d74c;
constexpr uint32_t kCodesB = 0x105d770;
constexpr uint32_t kCodesBFlagged = 0x105d828;
constexpr uint32_t kCodesC = 0x105db1c;
constexpr uint32_t kCodesCFlagged = 0x105db34;
constexpr uint32_t kCodesD = 0x105db4c;
constexpr uint32_t kCodesE = 0x105db7c;
constexpr uint32_t kCodesF = 0x105dbac;
constexpr uint32_t kCodesG = 0x105dbdc;
constexpr uint32_t kCodesH = 0x105dc0c;
constexpr uint32_t kAddressShort = 0x105dcb0;      // (increment, code length) by the next 11 bits (24..127)
constexpr uint32_t kAddressLong = 0x105dcc0;       // by the next 11 bits >> 6 (2..15)

constexpr int32_t kCodeEntryBytes = 3;
constexpr int8_t kEndOfBlock = 0x40;
constexpr int8_t kEscape = 0x41;

// ---- bit patterns ----
constexpr int32_t kCodeBits = 16;
constexpr uint32_t kClassA = 0x4000;
constexpr uint32_t kClassB = 0x400;
constexpr uint32_t kClassC = 0x200;
constexpr uint32_t kClassD = 0x100;
constexpr uint32_t kClassE = 0x80;
constexpr uint32_t kClassF = 0x40;
constexpr uint32_t kClassG = 0x20;
constexpr uint32_t kClassH = 0x10;
constexpr int32_t kShiftA = 12;
constexpr int32_t kBaseA = 4;
constexpr int32_t kShiftB = 8;
constexpr int32_t kBaseB = 4;
constexpr int32_t kShiftC = 6;
constexpr int32_t kBaseC = 8;
constexpr int32_t kShiftD = 4;
constexpr int32_t kShiftE = 3;
constexpr int32_t kShiftF = 2;
constexpr int32_t kShiftG = 1;
constexpr int32_t kBaseSmall = 0x10;

constexpr int32_t kDcPrefixBits = 5;
constexpr uint32_t kDcPrefixLong = 0x1f;           // a prefix of all ones goes to the long table
constexpr int32_t kLumaDcLongBits = 9;
constexpr int32_t kChromaDcLongBits = 10;
constexpr int32_t kDcEntryBytes = 2;
constexpr int32_t kLumaBlocks = 4;
constexpr int32_t kChromaBlue = 4;
constexpr int32_t kDcScaleShift = 3;               // the stored DC is the predictor times 8

constexpr int32_t kRunBits = 6;
constexpr int32_t kIntraEscapeBits = 8;
constexpr uint32_t kIntraEscapeWide = 0x80;
constexpr int32_t kIntraEscapeWrap = 0x100;
constexpr int32_t kInterEscapeBits = 12;
constexpr uint32_t kInterEscapeLevelMask = 0x7ff;
constexpr uint32_t kInterEscapeSign = 0x800;
constexpr int32_t kInterEscapeWrap = 0x1000;
constexpr int32_t kBlockCoefficients = 64;
constexpr int32_t kLevelShift = 3;
constexpr int32_t kPositiveLimit = 0x7ff;
constexpr int32_t kNegativeLimit = 0x800;
constexpr uint16_t kNegativeClamp = 0xf800;

constexpr uint32_t kAddressStuffing = 0xf;
constexpr uint32_t kAddressEscape = 8;
constexpr int32_t kAddressEscapeSteps = 0x21;
constexpr int32_t kAddressBits = 11;
constexpr uint32_t kAddressSingle = 0x18;           // below this, only the special codes
constexpr uint32_t kAddressOne = 0x400;             // from here the code is a single 1 bit: increment 1
constexpr uint32_t kAddressMedium = 0x80;
constexpr int32_t kAddressMediumShift = 6;

constexpr int32_t kQuantiserBits = 5;

// ---- the inverse transform ----
constexpr int32_t kW1 = 2841;
constexpr int32_t kW2 = 2676;
constexpr int32_t kW3 = 2408;
constexpr int32_t kW5 = 1609;
constexpr int32_t kW6 = 1108;
constexpr int32_t kW7 = 565;
constexpr int32_t kRotate = 181;
constexpr int32_t kRound8 = 128;
constexpr int32_t kRowStart = 128;
constexpr int32_t kColumnStart = 8192;
constexpr int32_t kColumnRound = 4;
constexpr int32_t kColumnClipShift = 14;
constexpr int32_t kClipCenter = 512;
constexpr int32_t kClipEntries = 1024;
constexpr int32_t kClipLow = -256;
constexpr int32_t kClipHigh = 255;
constexpr int32_t kColumnStride = 8;

// ---- clipping tables ----
constexpr int32_t kByteClipEntries = 0x3ff;
constexpr int32_t kByteClipZero = 0x180;            // entries below this are 0
constexpr int32_t kByteClipFull = 0x27f;            // entries above this are 255
constexpr int32_t kByteClipCenter = 0x200;          // index of the value 0 + 0x80 offset when converting colours

// ---- colour conversion ----
constexpr uint32_t kLumaScale = 0x129fc;
constexpr uint32_t kLumaBias = 0x10;
constexpr uint32_t kRedFromCr = 0x19893;
constexpr uint32_t kBlueFromCb = 0x2049c;
constexpr uint32_t kGreenFromCb = 0x6419;
constexpr uint32_t kGreenFromCr = 0xd021;
constexpr uint32_t kHalf16 = 0x8000;
constexpr int32_t kFixedShift = 16;
constexpr int32_t kQuadrantPixels = 8;
constexpr int32_t kChromaRowSamples = 8;
constexpr int32_t kPixelBytes = 4;

// ---- the stream ----
constexpr int32_t kHeaderBytes = 8;
constexpr int32_t kMacroblock = 16;
constexpr uint32_t kFlagRows = 1;
constexpr uint32_t kFlagPredicted = 2;
constexpr uint32_t kFlagTables = 4;
constexpr uint16_t kMagic = 0x4d47;                // 'GM'
constexpr int32_t kBlocksPerMacroblock = 6;
constexpr int32_t kQuadrantStep = 8;
constexpr int32_t kChromaQuadrantStep = 4;
constexpr int32_t kRightQuadrantBytes = 0x20;
constexpr uint32_t kSizeMask = 0xfffff;
constexpr uint32_t kHeaderFlagPresent = 0x8000000;
constexpr uint32_t kHeaderFlagMask = 0x300000;
constexpr int32_t kHeaderUnitShift = 4;

template <typename T> const T *Table(uint32_t rva)
{
    return GameAddress<T>(rva);
}

// The code table entry for the next 16 bits, or null when they are below the shortest code (the end of the block's data).
const int8_t *LookUpCode(uint32_t bits, bool flagged)
{
    uint32_t table;
    int32_t index;
    if (bits >= kClassA && !flagged) {
        table = kCodesA;
        index = static_cast<int32_t>(bits >> kShiftA) - kBaseA;
    } else if (bits >= kClassB) {
        table = flagged ? kCodesBFlagged : kCodesB;
        index = static_cast<int32_t>(bits >> kShiftB) - kBaseB;
    } else if (bits >= kClassC) {
        table = flagged ? kCodesCFlagged : kCodesC;
        index = static_cast<int32_t>(bits >> kShiftC) - kBaseC;
    } else if (bits >= kClassD) {
        table = kCodesD;
        index = static_cast<int32_t>(bits >> kShiftD) - kBaseSmall;
    } else if (bits >= kClassE) {
        table = kCodesE;
        index = static_cast<int32_t>(bits >> kShiftE) - kBaseSmall;
    } else if (bits >= kClassF) {
        table = kCodesF;
        index = static_cast<int32_t>(bits >> kShiftF) - kBaseSmall;
    } else if (bits >= kClassG) {
        table = kCodesG;
        index = static_cast<int32_t>(bits >> kShiftG) - kBaseSmall;
    } else if (bits >= kClassH) {
        table = kCodesH;
        index = static_cast<int32_t>(bits) - kBaseSmall;
    } else {
        return nullptr;
    }
    return Table<int8_t>(table) + index * kCodeEntryBytes;
}

// One DC difference: a code (5 bits, or the long table when they are all ones) giving the size, then that many bits.
int32_t ReadDc(uint32_t shortTable, uint32_t longTable, int32_t longBits)
{
    const uint32_t prefix = PeekBits(kDcPrefixBits);
    const int8_t *entry = prefix < kDcPrefixLong ? Table<int8_t>(shortTable) + prefix * kDcEntryBytes
                                                 : Table<int8_t>(longTable) + PeekBits(longBits) * kDcEntryBytes;
    gGmState.bitPosition += entry[1];
    const int32_t size = entry[0];
    if (size == 0) return 0;
    int32_t value = static_cast<int32_t>(GetBits(size));
    if ((static_cast<uint32_t>(value) & (1u << (size - 1))) == 0) value += 1 - (1 << size);
    return value;
}

// Dequantises one level into the block (the order of the coefficients is the zigzag table's).
void StoreLevel(int16_t *block, int32_t position, int32_t level, bool negative)
{
    const int32_t slot = Table<uint8_t>(kZigzag)[position];
    const int32_t value = (Table<uint8_t>(kQuantMatrix)[slot] * gGmState.quantiser * level) >> kLevelShift;
    uint16_t stored = static_cast<uint16_t>(value);
    if (!negative) {
        if (value > kPositiveLimit) stored = kPositiveLimit;
    } else {
        stored = static_cast<uint16_t>(-static_cast<int32_t>(stored));
        if (value > kNegativeLimit) stored = kNegativeClamp;
    }
    block[slot] = static_cast<int16_t>(stored);
}

int32_t &Predictor(int32_t block)
{
    return gGmState.dcPredictor[block < kLumaBlocks ? 0 : block == kChromaBlue ? 1 : 2];
}

int32_t Left(int32_t value, int32_t bits)
{
    return static_cast<int32_t>(static_cast<uint32_t>(value) << bits);
}

int16_t ClipWord(int32_t index)
{
    int32_t slot = index + kClipCenter;
    if (slot < 0 || slot >= kClipEntries) {
        ++gGmState.clipOverruns;
        slot = slot < 0 ? 0 : kClipEntries - 1;
    }
    return gGmState.wordClip[slot];
}

uint8_t ClipByte(int32_t index)
{
    int32_t slot = index + kByteClipCenter;
    if (slot < 0 || slot >= kByteClipEntries) {
        ++gGmState.clipOverruns;
        slot = slot < 0 ? 0 : kByteClipEntries - 1;
    }
    return gGmState.byteClip[slot];
}

} // namespace

uint32_t __cdecl GetBits(int32_t count)
{
    const uint32_t value = PeekBits(count);
    gGmState.bitPosition += count;
    return value;
}

int32_t __cdecl ReadLumaDc()
{
    return ReadDc(kLumaDcShort, kLumaDcLong, kLumaDcLongBits);
}

int32_t __cdecl ReadChromaDc()
{
    return ReadDc(kChromaDcShort, kChromaDcLong, kChromaDcLongBits);
}

void __cdecl DecodeIntraBlock(int32_t blockIndex)
{
    int16_t *block = gGmState.blocks[blockIndex];
    int32_t &predictor = Predictor(blockIndex);
    predictor += blockIndex < kLumaBlocks ? ReadLumaDc() : ReadChromaDc();
    block[0] = static_cast<int16_t>(static_cast<uint16_t>(Left(predictor, kDcScaleShift)));
    int32_t position = 1;
    for (;;) {
        const int8_t *entry = LookUpCode(PeekBits(kCodeBits), false);
        if (entry == nullptr) return;
        gGmState.bitPosition += entry[2];
        if (entry[0] == kEndOfBlock) return;
        int32_t run;
        int32_t magnitude;
        bool negative;
        if (entry[0] == kEscape) {
            run = static_cast<int32_t>(GetBits(kRunBits));
            int32_t level = static_cast<int32_t>(GetBits(kIntraEscapeBits));
            if (level == 0) {
                level = static_cast<int32_t>(GetBits(kIntraEscapeBits));
            } else if (static_cast<uint32_t>(level) == kIntraEscapeWide) {
                level = static_cast<int32_t>(GetBits(kIntraEscapeBits)) - kIntraEscapeWrap;
            } else if (static_cast<uint32_t>(level) > kIntraEscapeWide) {
                level -= kIntraEscapeWrap;
            }
            negative = level < 0;
            magnitude = negative ? -level : level;
        } else {
            magnitude = entry[1];
            negative = GetBits(1) != 0;
            run = entry[0];
        }
        position += run;
        if (position >= kBlockCoefficients) return;
        StoreLevel(block, position, magnitude, negative);
        ++position;
    }
}

void __cdecl DecodeInterBlock(int32_t blockIndex, int32_t flag)
{
    int16_t *block = gGmState.blocks[blockIndex];
    int32_t &predictor = Predictor(blockIndex);
    predictor += blockIndex < kLumaBlocks ? ReadLumaDc() : ReadChromaDc();
    block[0] = static_cast<int16_t>(static_cast<uint16_t>(Left(predictor, kDcScaleShift)));
    int32_t position = 1;
    for (;;) {
        const int8_t *entry = LookUpCode(PeekBits(kCodeBits), flag != 0);
        if (entry == nullptr) return;
        gGmState.bitPosition += entry[2];
        if (entry[0] == kEndOfBlock) return;
        int32_t magnitude;
        bool negative;
        if (entry[0] == kEscape) {
            position += static_cast<int32_t>(GetBits(kRunBits));
            magnitude = static_cast<int32_t>(GetBits(kInterEscapeBits));
            if ((static_cast<uint32_t>(magnitude) & kInterEscapeLevelMask) == 0) return;
            negative = magnitude >= static_cast<int32_t>(kInterEscapeSign);
            if (negative) magnitude = kInterEscapeWrap - magnitude;
        } else {
            magnitude = entry[1];
            position += entry[0];
            negative = GetBits(1) != 0;
        }
        if (position >= kBlockCoefficients) return;
        StoreLevel(block, position, magnitude, negative);
        ++position;
    }
}

int32_t __cdecl ReadAddressIncrement()
{
    int32_t steps = 0;
    for (;;) {
        const uint32_t bits = PeekBits(kAddressBits);
        if (bits < kAddressSingle) {
            if (bits == kAddressStuffing) {
                gGmState.bitPosition += kAddressBits;
            } else if (bits == kAddressEscape) {
                steps += kAddressEscapeSteps;
                gGmState.bitPosition += kAddressBits;
            } else {
                return 1;
            }
            continue;
        }
        if (bits >= kAddressOne) {
            ++gGmState.bitPosition;
            return steps + 1;
        }
        const int8_t *entry;
        if (bits >= kAddressMedium) entry = Table<int8_t>(kAddressLong) + (bits >> kAddressMediumShift) * 2;
        else entry = Table<int8_t>(kAddressShort) + bits * 2;
        gGmState.bitPosition += entry[1];
        return entry[0] + steps;
    }
}

void __cdecl IdctRow(int16_t *row)
{
    int32_t x1 = Left(row[4], 11);
    int32_t x2 = row[6];
    int32_t x3 = row[2];
    int32_t x4 = row[1];
    int32_t x5 = row[7];
    int32_t x6 = row[5];
    int32_t x7 = row[3];
    if ((x1 | x2 | x3 | x4 | x5 | x6 | x7) == 0) {
        const int16_t value = static_cast<int16_t>(Left(row[0], 3));
        for (int32_t i = 0; i < 8; ++i) row[i] = value;
        return;
    }
    int32_t x0 = Left(row[0], 11) + kRowStart;
    int32_t x8 = kW7 * (x4 + x5);
    x4 = x8 + (kW1 - kW7) * x4;
    x5 = x8 - (kW1 + kW7) * x5;
    x8 = kW3 * (x6 + x7);
    x6 = x8 - (kW3 - kW5) * x6;
    x7 = x8 - (kW3 + kW5) * x7;
    x8 = x0 + x1;
    x0 -= x1;
    x1 = kW6 * (x3 + x2);
    x2 = x1 - (kW2 + kW6) * x2;
    x3 = x1 + (kW2 - kW6) * x3;
    x1 = x4 + x6;
    x4 -= x6;
    x6 = x5 + x7;
    x5 -= x7;
    x7 = x8 + x3;
    x8 -= x3;
    x3 = x0 + x2;
    x0 -= x2;
    x2 = (kRotate * (x4 + x5) + kRound8) >> 8;
    x4 = (kRotate * (x4 - x5) + kRound8) >> 8;
    row[0] = static_cast<int16_t>((x7 + x1) >> 8);
    row[1] = static_cast<int16_t>((x3 + x2) >> 8);
    row[2] = static_cast<int16_t>((x0 + x4) >> 8);
    row[3] = static_cast<int16_t>((x8 + x6) >> 8);
    row[4] = static_cast<int16_t>((x8 - x6) >> 8);
    row[5] = static_cast<int16_t>((x0 - x4) >> 8);
    row[6] = static_cast<int16_t>((x3 - x2) >> 8);
    row[7] = static_cast<int16_t>((x7 - x1) >> 8);
}

void __cdecl IdctColumn(int16_t *column)
{
    int32_t x1 = Left(column[4 * kColumnStride], 8);
    int32_t x2 = column[6 * kColumnStride];
    int32_t x3 = column[2 * kColumnStride];
    int32_t x4 = column[1 * kColumnStride];
    int32_t x5 = column[7 * kColumnStride];
    int32_t x6 = column[5 * kColumnStride];
    int32_t x7 = column[3 * kColumnStride];
    if ((x1 | x2 | x3 | x4 | x5 | x6 | x7) == 0) {
        const int16_t value = ClipWord((column[0] + 32) >> 6);
        for (int32_t i = 0; i < 8; ++i) column[i * kColumnStride] = value;
        return;
    }
    int32_t x0 = Left(column[0], 8) + kColumnStart;
    int32_t x8 = kW7 * (x4 + x5) + kColumnRound;
    x4 = (x8 + (kW1 - kW7) * x4) >> 3;
    x5 = (x8 - (kW1 + kW7) * x5) >> 3;
    x8 = kW3 * (x6 + x7) + kColumnRound;
    x6 = (x8 - (kW3 - kW5) * x6) >> 3;
    x7 = (x8 - (kW3 + kW5) * x7) >> 3;
    x8 = x0 + x1;
    x0 -= x1;
    x1 = kW6 * (x3 + x2) + kColumnRound;
    x2 = (x1 - (kW2 + kW6) * x2) >> 3;
    x3 = (x1 + (kW2 - kW6) * x3) >> 3;
    x1 = x4 + x6;
    x4 -= x6;
    x6 = x5 + x7;
    x5 -= x7;
    x7 = x8 + x3;
    x8 -= x3;
    x3 = x0 + x2;
    x0 -= x2;
    x2 = (kRotate * (x4 + x5) + kRound8) >> 8;
    x4 = (kRotate * (x4 - x5) + kRound8) >> 8;
    column[0 * kColumnStride] = ClipWord((x7 + x1) >> kColumnClipShift);
    column[1 * kColumnStride] = ClipWord((x3 + x2) >> kColumnClipShift);
    column[2 * kColumnStride] = ClipWord((x0 + x4) >> kColumnClipShift);
    column[3 * kColumnStride] = ClipWord((x8 + x6) >> kColumnClipShift);
    column[4 * kColumnStride] = ClipWord((x8 - x6) >> kColumnClipShift);
    column[5 * kColumnStride] = ClipWord((x0 - x4) >> kColumnClipShift);
    column[6 * kColumnStride] = ClipWord((x3 - x2) >> kColumnClipShift);
    column[7 * kColumnStride] = ClipWord((x7 - x1) >> kColumnClipShift);
}

void __cdecl Idct8x8(int16_t *block)
{
    for (int32_t row = 0; row < 8; ++row) IdctRow(block + row * 8);
    for (int32_t column = 0; column < 8; ++column) IdctColumn(block + column);
}

int32_t __cdecl DecodeMacroblock(int32_t isLast, int32_t inter, int32_t flag)
{
    if (GetBits(1) == 0 && GetBits(1) != 0) gGmState.quantiser = static_cast<int32_t>(GetBits(kQuantiserBits));
    for (int32_t index = 0; index < kBlocksPerMacroblock; ++index) {
        for (int16_t &coefficient : gGmState.blocks[index]) coefficient = 0;
        if (inter == 0) DecodeIntraBlock(index);
        else DecodeInterBlock(index, flag);
        Idct8x8(gGmState.blocks[index]);
    }
    return isLast == 0 ? ReadAddressIncrement() : 0;
}

void __cdecl InitClipTables()
{
    for (int32_t i = 0; i < kByteClipEntries; ++i) {
        gGmState.byteClip[i] = i < kByteClipZero ? 0 : i > kByteClipFull ? 255 : static_cast<uint8_t>(i + 0x80);
    }
    for (int32_t i = -kClipCenter; i < kClipCenter; ++i) {
        gGmState.wordClip[i + kClipCenter] = static_cast<int16_t>(i < kClipLow ? kClipLow : i > kClipHigh ? kClipHigh : i);
    }
}

namespace {

// One pixel: B, G, R, A from a luma sample and the chroma terms (16.16 fixed point).
void ConvertPixel(uint8_t *pixel, int16_t luma, uint32_t redTerm, uint32_t blueTerm, uint32_t greenTerm)
{
    const uint32_t base = static_cast<uint32_t>(static_cast<int32_t>(luma)) * kLumaScale - kLumaBias + kHalf16;
    pixel[3] = 0xff;
    pixel[2] = ClipByte(static_cast<int32_t>(base + redTerm) >> kFixedShift);
    pixel[0] = ClipByte(static_cast<int32_t>(base + blueTerm) >> kFixedShift);
    pixel[1] = ClipByte(static_cast<int32_t>(base - greenTerm) >> kFixedShift);
}

} // namespace

void __cdecl ConvertBlock(uint8_t *dest, int32_t stride, int32_t, int32_t block, int32_t chromaRow, int32_t chromaColumn)
{
    const int32_t rowBytes = (stride >> 2) * kPixelBytes;
    const int16_t *luma = gGmState.blocks[block];
    for (int32_t y = 0; y < kQuadrantPixels; y += 2) {
        uint8_t *upper = dest + y * rowBytes;
        uint8_t *lower = upper + rowBytes;
        for (int32_t x = 0; x < kQuadrantPixels; x += 2) {
            const int32_t sample = (x >> 1) + ((y >> 1) + chromaRow) * kChromaRowSamples + chromaColumn;
            const uint32_t cb = static_cast<uint32_t>(static_cast<int32_t>(gGmState.blocks[kChromaBlue][sample]));
            const uint32_t cr = static_cast<uint32_t>(static_cast<int32_t>(gGmState.blocks[kChromaBlue + 1][sample]));
            const uint32_t red = cr * kRedFromCr;
            const uint32_t blue = cb * kBlueFromCb;
            const uint32_t green = cb * kGreenFromCb + cr * kGreenFromCr;
            ConvertPixel(upper + x * kPixelBytes, luma[y * 8 + x], red, blue, green);
            ConvertPixel(upper + (x + 1) * kPixelBytes, luma[y * 8 + x + 1], red, blue, green);
            ConvertPixel(lower + x * kPixelBytes, luma[(y + 1) * 8 + x], red, blue, green);
            ConvertPixel(lower + (x + 1) * kPixelBytes, luma[(y + 1) * 8 + x + 1], red, blue, green);
        }
    }
}

int32_t __cdecl DecodeGm(uint8_t *dest, int32_t stride, const uint8_t *data, int32_t width, int32_t height, uint32_t flags)
{
    if (gGmState.tablesReady == 0) {
        InitClipTables();
        gGmState.tablesReady = 1;
    }
    if (data == nullptr) return 0;
    const int32_t alignedHeight = (height + kMacroblock - 1) & -kMacroblock;
    const int32_t alignedWidth = (width + kMacroblock - 1) & -kMacroblock;
    int32_t remaining = (alignedWidth / kMacroblock) * (alignedHeight / kMacroblock);
    gGmState.data = data + kHeaderBytes;
    gGmState.bitPosition = 0;
    gGmState.dcPredictor[0] = 0;
    gGmState.dcPredictor[1] = 0;
    gGmState.dcPredictor[2] = 0;
    // Only the row-order walk reads the predicted and table flags; the column-order walk always decodes plain intra macroblocks.
    const bool rows = (flags & kFlagRows) != 0;
    const int32_t inter = rows && (flags & kFlagPredicted) != 0;
    const int32_t flag = rows && (flags & kFlagTables) != 0;
    const int32_t rowBytes = (stride >> 2) * kPixelBytes;
    auto macroblock = [&](uint8_t *topLeft) {
        DecodeMacroblock(remaining == 1, inter, flag);
        ConvertBlock(topLeft, stride, 0, 0, 0, 0);
        ConvertBlock(topLeft + kRightQuadrantBytes, stride, 0, 1, 0, kChromaQuadrantStep);
        ConvertBlock(topLeft + kQuadrantStep * rowBytes, stride, 0, 2, kChromaQuadrantStep, 0);
        ConvertBlock(topLeft + kQuadrantStep * rowBytes + kRightQuadrantBytes, stride, 0, 3, kChromaQuadrantStep, kChromaQuadrantStep);
        --remaining;
    };
    if (rows) {
        for (int32_t y = 0; y < alignedHeight; y += kMacroblock) {
            for (int32_t x = 0; x < alignedWidth; x += kMacroblock) macroblock(dest + y * rowBytes + x * kPixelBytes);
        }
    } else {
        for (int32_t x = 0; x < alignedWidth; x += kMacroblock) {
            for (int32_t y = 0; y < alignedHeight; y += kMacroblock) macroblock(dest + y * rowBytes + x * kPixelBytes);
        }
    }
    return 1;
}

int32_t __cdecl ParseGmHeader(const uint8_t *header, int32_t *width, int32_t *height, int32_t *flags, int32_t *quality, int32_t *size)
{
    if ((header[0] | header[1] << 8) != kMagic) return 0;
    *width = header[2] << kHeaderUnitShift;
    *height = header[3] << kHeaderUnitShift;
    const uint32_t word = header[4] | header[5] << 8 | header[6] << 16 | static_cast<uint32_t>(header[7]) << 24;
    *size = static_cast<int32_t>(word & kSizeMask);
    *flags = (word & kHeaderFlagPresent) != 0 ? ((word & kHeaderFlagMask) != 0 ? 4 : 8) : 0;
    *quality = (header[8] >> 1) & 0x1f;
    return 1;
}

int32_t __cdecl IsGm(const uint8_t *header)
{
    return (header[0] | header[1] << 8) == kMagic;
}

} // namespace fsh

} // namespace gimex
