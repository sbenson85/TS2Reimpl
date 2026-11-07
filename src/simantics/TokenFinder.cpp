#include "simantics/TokenFinder.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace {

// The digits that follow `position` in the text (up to the first non-digit); `position` moves past them.
std::string ReadDigits(const std::string &text, int32_t &position)
{
    std::string digits;
    const int32_t size = static_cast<int32_t>(text.size());
    while (position < size && std::isdigit(static_cast<unsigned char>(text[position]))) {
        digits.push_back(text[position]);
        ++position;
    }
    return digits;
}

// The original lowercases a copy with _strlwr, which stops at a NUL.
std::string Lowercased(const std::string &text)
{
    std::string copy = text;
    _strlwr(copy.data());
    return copy;
}

} // namespace

int32_t FindToken(const std::string &text, const std::string &token, uint32_t start, int32_t *length, int32_t *arg1, int32_t *arg2)
{
    const std::string lowerText = Lowercased(text);
    const std::string lowerToken = Lowercased(token);

    // Where the match starts: nowhere when the token is longer than the text or `start` is past the last place it fits.
    uint32_t position;
    if (lowerText.size() < lowerToken.size() || lowerText.size() - lowerToken.size() < start) {
        return -1;
    }
    if (lowerToken.empty()) {
        position = start;
    } else {
        const size_t found = lowerText.find(lowerToken, start);
        if (found == std::string::npos) return -1;
        position = static_cast<uint32_t>(found);
    }
    if (position == 0xffffffffu) return -1;

    // The length comes from the original (not lowercased) token and text.
    if (!arg1 && !arg2) {
        *length = static_cast<int32_t>(token.size());
        return static_cast<int32_t>(position);
    }
    int32_t end = static_cast<int32_t>(token.size()) + static_cast<int32_t>(position);
    if (arg1) *arg1 = -1;
    if (arg2) *arg2 = -1;
    if (end < static_cast<int32_t>(text.size()) && text[end] == ':') {
        ++end;
        const std::string first = ReadDigits(text, end);
        if (!first.empty() && arg1) *arg1 = std::atoi(first.c_str());
        if (end < static_cast<int32_t>(text.size()) && text[end] == ':') {
            ++end;
            const std::string second = ReadDigits(text, end);
            if (!second.empty() && arg2) *arg2 = std::atoi(second.c_str());
        }
    }
    *length = end - static_cast<int32_t>(position);
    return static_cast<int32_t>(position);
}
