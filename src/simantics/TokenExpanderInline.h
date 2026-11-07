#pragma once
// Private to the TokenExpander*.cpp files: what the stage files share beyond the class declaration.
#include "app/AppInit.h"
#include "game/vtable.h"
#include "ifaces/ifaces.h"
#include "simantics/TokenExpander.h"

namespace tokens {

// The sign extension to 32 bits (cwde / movsx) the original applies to the 16-bit words it reads.
constexpr int32_t Widen(int16_t value) { return value; }

template <typename ValueFn> void StringTokenExpander::FormatNumber(ValueFn getValue, cRZString &out)
{
    // The original asks the cached language service three times and the language twice before it uses either.
    if (appinit::GetLanguageService() != nullptr &&
        As<ifaces::TokenLanguageService>(appinit::GetLanguageService())->GetLanguage(0) != nullptr) {
        ifaces::TokenLanguage *language = As<ifaces::TokenLanguageService>(appinit::GetLanguageService())->GetLanguage(0);
        const int32_t value = getValue();
        language->FormatInteger(value, value >> 31, &out);
    } else {
        out.Sprintf("%d", getValue());
    }
}

} // namespace tokens
