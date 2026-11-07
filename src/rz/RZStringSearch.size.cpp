// Lane NO. RVAs are 1.18.0.187.
#include "rz/RZStringSearch.h"

#include <cctype>

namespace rztext {

const char *__cdecl FindNoCase(const char *text, const char *part)
{
    if (*part == '\0') return text;
    for (; *text != '\0'; ++text) {
        // Compare from this position until `part` ends (found), the characters differ or `text` ends.
        const char *p = part;
        const char *t = text;
        while (*p != '\0' && std::tolower(*p) == std::tolower(*t)) {
            ++p;
            ++t;
            if (*t == '\0') break;
        }
        if (*p == '\0') return text;
    }
    return nullptr;
}

} // namespace rztext
