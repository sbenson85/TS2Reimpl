// The event faces (cEventFaces.h): the image-free program's tables for nGZEvent::cNullEvent, cScalarEvent<unsigned long>
// and nGZEvent::cConnectionChangedEvent.
#include "gz/cEventFaces.h"

#include <cstring>
#include <new>

namespace rzface {

using nGZEvent::cIEventBody;

// ---- cNullEvent ----
// No fields: the copy has nothing to take (the game's slots 1 and 2 are a bare `ret 4`; ours gives the body).
cIEventBody *cNullEvent::Assign(const cIEventBody *) { return this; }
cIEventBody *cNullEvent::Slot2(const cIEventBody *other) { return cNullEvent::Assign(other); }
uint32_t cNullEvent::GetEventType() const { return kEventType; }
uint32_t cNullEvent::GetSize() const { return sizeof(cNullEvent); }
uint32_t cNullEvent::GetBodySize() const { return 0; }
cIEventBody *cNullEvent::CloneInto(void *memory) const { return ::new (memory) cNullEvent; }

// ---- cScalarEvent<T> ----
// The game's copy and clone are void (their result register holds the value); ours give the body.
template <typename T> cIEventBody *cScalarEvent<T>::Assign(const cIEventBody *other)
{
    mValue = static_cast<const cScalarEvent *>(other)->mValue;
    return this;
}
template <typename T> cIEventBody *cScalarEvent<T>::Slot2(const cIEventBody *other) { return cScalarEvent::Assign(other); }
template <> uint32_t cScalarEvent<unsigned long>::GetEventType() const { return 0x7b22dd0f; }
template <typename T> uint32_t cScalarEvent<T>::GetSize() const { return sizeof(cScalarEvent); }
template <typename T> uint32_t cScalarEvent<T>::GetBodySize() const { return sizeof(cScalarEvent) - 4; }
template <typename T> cIEventBody *cScalarEvent<T>::CloneInto(void *memory) const
{
    return ::new (memory) cScalarEvent(mValue);
}
template class cScalarEvent<unsigned long>;

// ---- cConnectionChangedEvent ----
cIEventBody *cConnectionChangedEvent::Assign(const cIEventBody *other)
{
    std::memcpy(mWords, static_cast<const cConnectionChangedEvent *>(other)->mWords, sizeof(mWords));
    return this;
}
cIEventBody *cConnectionChangedEvent::Slot2(const cIEventBody *other) { return cConnectionChangedEvent::Assign(other); }
uint32_t cConnectionChangedEvent::GetEventType() const { return kEventType; }
uint32_t cConnectionChangedEvent::GetSize() const { return sizeof(cConnectionChangedEvent); }
uint32_t cConnectionChangedEvent::GetBodySize() const { return sizeof(cConnectionChangedEvent) - 4; }
cIEventBody *cConnectionChangedEvent::CloneInto(void *memory) const
{
    auto *const copy = ::new (memory) cConnectionChangedEvent;
    std::memcpy(copy->mWords, mWords, sizeof(mWords));
    return copy;
}

} // namespace rzface
