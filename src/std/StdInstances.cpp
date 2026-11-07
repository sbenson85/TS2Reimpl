// The speed-built (/O2) members of src/std/StdInstances.h.
#include "std/StdInstances.h"

#include <algorithm>
#include <new>

bool NameValueTable::ByLengthThenReverseText::operator()(const Entry &a, const Entry &b) const
{
    if (a.first.size() != b.first.size()) return a.first.size() < b.first.size();
    return std::lexicographical_compare(a.first.rbegin(), a.first.rend(), b.first.rbegin(), b.first.rend());
}

NameValueTable *NameValueTable::Construct(const NameValueEntry *entries)
{
    new (&mEntries) std::vector<Entry>();
    for (; entries->name; ++entries) mEntries.push_back(Entry(std::string(entries->name), entries->value));
    std::sort(mEntries.begin(), mEntries.end(), ByLengthThenReverseText());
    return this;
}

ServiceStringObject *ServiceStringObject::ConstructFromText(const char *text)
{
    new (&mText) GZServiceString(text);
    return this;
}

StaticObjectStub *StaticObjectStub::Construct()
{
    return this;
}
