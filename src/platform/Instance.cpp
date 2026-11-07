#include "platform/Instance.h"

#include <cstring>
#include <cwctype>
#include <initializer_list>

namespace instance {

namespace {

constexpr char kGamePrefix[] = "Global\\The Sims 2";

bool ContainsSwitch(const std::wstring &line, const wchar_t *name)
{
    std::wstring lower(line);
    for (wchar_t &c : lower) c = static_cast<wchar_t>(std::towlower(c));
    std::wstring key = std::wstring(name) + L":";
    for (wchar_t &c : key) c = static_cast<wchar_t>(std::towlower(c));
    // The game's switch prefixes are '-', '/' and '\' (cRZCmdLine).
    for (const wchar_t *prefix : {L"-", L"/", L"\\"})
        if (lower.find(prefix + key) != std::wstring::npos) return true;
    return false;
}

} // namespace

bool MutexNameForInstance(const char *name, int id, std::string &renamed)
{
    if (id <= 0 || !name) return false;
    if (std::strncmp(name, kGamePrefix, sizeof(kGamePrefix) - 1) != 0) return false;
    renamed = std::string(name) + " [instance " + std::to_string(id) + "]";
    return true;
}

bool IsUsableDataDir(const std::wstring &dir)
{
    if (dir.size() < 4) return false;
    if (!std::iswalpha(dir[0]) || dir[1] != L':' || dir[2] != L'\\') return false;
    if (dir.back() != L'\\') return false;
    return dir.find_first_of(L" \t\"") == std::wstring::npos;
}

std::wstring CommandLineForInstance(const std::wstring &original, const std::wstring &userData, const std::wstring &allUsers)
{
    std::wstring line = original;
    if (!userData.empty() && !ContainsSwitch(original, L"activeUserDataDir")) line += L" -activeUserDataDir:" + userData;
    if (!allUsers.empty() && !ContainsSwitch(original, L"activeAllUsersDataDir")) line += L" -activeAllUsersDataDir:" + allUsers;
    return line;
}

} // namespace instance
