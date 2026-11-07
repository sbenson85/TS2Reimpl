// The FSH back end's end-point search for colours that lie (nearly) on one point (lane MS; src/gz/GimexFshEncode.h, 0x8990a2).
//
// For each colour channel and each 8-bit value it keeps, in an image table filled on first use, the pair of end points (in the
// channel's 5- or 6-bit steps, scaled to eight bits) whose interpolation at one of three positions (a third, a half, two thirds of the
// way: 0x105deac) gives that value with the smallest gap between the two end points. The search then, for each position, takes the
// nearest value above and below the wanted one that the table has an entry for, adds up the squared error of each over the colours
// and keeps the better one per channel. The table, the flag and the error scratch are image globals the original's second-half code
// may read, so they stay there.
#include "gz/GimexFshEncode.h"

#include "game/game.h"

#include <cmath>

namespace gimex {

namespace fsh {

namespace {

constexpr uint32_t kTableRva = 0x10e9858;       // [value][channel][position] of {present, low end, high end}
constexpr uint32_t kBuiltRva = 0x10eb358;
constexpr uint32_t kErrorsRva = 0x10e9840;      // the total error of each position, 3 doubles
constexpr uint32_t kDirectionErrorsRva = 0x10eb360; // the error of the two directions, 2 doubles
constexpr uint32_t kBitsRva = 0x105dea0;        // bits of each channel: 5, 6, 5
constexpr uint32_t kPositionsRva = 0x105deac;   // 3 rows of {a, b, c, d}: (a * x + b * y + c) / d

constexpr int32_t kChannels = 3;
constexpr int32_t kPositions = 3;
constexpr int32_t kEntryBytes = 3;
constexpr int32_t kValues = 256;
constexpr int32_t kValueStride = kChannels * kPositions * kEntryBytes; // 27
constexpr double kByteScale = 255.0;
constexpr double kHalf = 0.5;

struct Position {
    int32_t a;
    int32_t b;
    int32_t c;
    int32_t d;
};

int32_t Clamp255(int32_t value)
{
    int32_t clamped = value < 256 ? value : 255;
    if (value < 0) clamped = 0;
    return clamped;
}

void BuildTable()
{
    uint8_t *table = GameAddress<uint8_t>(kTableRva);
    const int32_t *bits = GameAddress<const int32_t>(kBitsRva);
    const Position *positions = GameAddress<const Position>(kPositionsRva);
    *GameAddress<int32_t>(kBuiltRva) = 1;
    for (int32_t channel = 0; channel < kChannels; ++channel) {
        const int32_t channelBits = bits[channel];
        const int32_t top = (1 << channelBits) - 1;
        const int32_t widen = 2 * channelBits - 8;
        const int32_t spread = 8 - channelBits;
        for (int32_t low = top; low >= 0; --low) {
            const int32_t low8 = low >> widen | low << spread;
            for (int32_t high = top; high >= 0; --high) {
                const int32_t high8 = high >> widen | high << spread;
                for (int32_t position = 0; position < kPositions; ++position) {
                    const Position &row = positions[position];
                    const int32_t value = (row.b * high8 + row.a * low8 + row.c) / row.d;
                    uint8_t *entry = table + ((value * kChannels + channel) * kPositions + position) * kEntryBytes;
                    const int32_t oldGap = entry[0] == 0 ? 0 : (entry[2] > entry[1] ? entry[2] - entry[1] : entry[1] - entry[2]);
                    const int32_t newGap = high8 > low8 ? high8 - low8 : low8 - high8;
                    if (entry[0] == 0 || newGap < oldGap) {
                        entry[0] = 1;
                        entry[1] = static_cast<uint8_t>(low << (8 - channelBits));
                        entry[2] = static_cast<uint8_t>(high << (8 - channelBits));
                    }
                }
            }
        }
    }
}

} // namespace

// 0x8990a2
double __cdecl FitSingleColour(int32_t *levels, int32_t count, const double *colours, const double *weights, int32_t *endpoints,
                               int32_t *indices)
{
    uint8_t *table = GameAddress<uint8_t>(kTableRva);
    double *totals = GameAddress<double>(kErrorsRva);
    double *errors = GameAddress<double>(kDirectionErrorsRva);
    const int32_t *bits = GameAddress<const int32_t>(kBitsRva);
    if (*GameAddress<int32_t>(kBuiltRva) == 0) BuildTable();

    const int32_t levelCount = *levels;
    int32_t best = 0;
    int32_t chosen[kChannels * kPositions] = {};
    for (best = 0; best < levelCount - 1; ++best) {
        double total = 0.0;
        for (int32_t channel = 0; channel < kChannels; ++channel) {
            const double colour = colours[channel];
            const double weight = weights[channel];
            const double scaled = colour * kByteScale;
            const int32_t wanted = Clamp255(static_cast<int32_t>(std::floor(scaled / weight + kHalf)));
            const bool known = table[((wanted * kChannels + channel) * kPositions + best) * kEntryBytes] != 0;
            int32_t picks[2];
            int32_t step = 1;
            for (int32_t direction = 0; direction < 2; ++direction) {
                int32_t value = wanted;
                double sum = 0.0;
                bool search = true;
                if (known) {
                    const double beyond = (scaled / weight - static_cast<double>(wanted)) * static_cast<double>(step);
                    search = beyond > 0.0;
                }
                if (search) {
                    value = Clamp255(wanted + step);
                    const uint8_t *entry = table + ((value * kChannels + channel) * kPositions + best) * kEntryBytes;
                    while (*entry == 0) {
                        entry += step * kValueStride;
                        value += step;
                    }
                }
                errors[direction] = sum;
                if (count > 0) {
                    const double mapped = static_cast<double>(value) * weight;
                    for (int32_t i = 0; i < count; ++i) {
                        const double difference = mapped - colours[i * 3 + channel] * kByteScale;
                        sum += difference * difference;
                        errors[direction] = sum;
                    }
                }
                picks[direction] = value;
                step = -step;
            }
            int32_t pick;
            if (errors[1] > errors[0]) {
                total = total + errors[0];
                pick = picks[0];
            } else {
                total = total + errors[1];
                pick = picks[1];
                if (!(errors[0] > errors[1]) && (wanted & 1) != 0) pick = picks[0];
            }
            chosen[channel * kPositions + best] = pick;
            totals[best] = total;
        }
    }

    if (levelCount == 4) {
        if (totals[1] < totals[0]) {
            best = (totals[2] < totals[1] ? 1 : 0) + 1;
        } else {
            best = totals[2] >= totals[0] ? 0 : 2;
        }
    } else if (levelCount == 3) {
        best = totals[1] < totals[0] ? 1 : 0;
    }

    for (int32_t channel = 0; channel < kChannels; ++channel) {
        const uint8_t *entry = table + ((chosen[channel * kPositions + best] * kChannels + channel) * kPositions + best) * kEntryBytes;
        const int32_t shift = 8 - bits[channel];
        endpoints[channel] = entry[1] >> shift;
        endpoints[kChannels + channel] = entry[2] >> shift;
    }
    for (int32_t i = 0; i < count; ++i) indices[i] = best + 1;
    if (best != 0) *levels = best + 2;
    return totals[best];
}

} // namespace fsh

} // namespace gimex
