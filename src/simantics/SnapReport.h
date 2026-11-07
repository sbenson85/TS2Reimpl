#pragma once
#include <cstddef>
#include <cstdint>

// The error report of the snap tests (opcodes 0x2e Snap and 0x71): what a failed snap leaves in its state, and the lookup of
// the message that goes with it. Lane AJ. RVAs are 1.18.0.187.

// One report (0x18 bytes): the error code, 12 bytes of detail, the object the error is about (a counted reference the owner
// releases) and the kind of error, which says where its message comes from.
struct SnapErrorReport {
    uint32_t mCode;      // +0x00
    uint8_t mDetail[12]; // +0x04
    void *mObject;       // +0x10 counted reference (may be null)
    uint32_t mKind;      // +0x14 0: the resource service knows the code, 1 and 2: the object does

    // 0x7f0c00 (__thiscall, ret 4): looks the message of the error up into *message (a counted reference to a text object),
    // through the service (kind 0: slot 32 of the cached singleton C with the code and the out pointer), through the object
    // (kind 1: its slot 194 with the code and the out pointer; kind 2: its slot 195 with the out pointer alone) and
    // not at all for other kinds or a null object. Returns the lookup's byte (0 when no lookup was made).
    uint8_t LookupMessage(void **message);
};

static_assert(sizeof(SnapErrorReport) == 0x18, "SnapErrorReport layout");
static_assert(offsetof(SnapErrorReport, mObject) == 0x10, "SnapErrorReport layout");

// The snap state (0x20 bytes) as far as the report needs it: four tree-shaped containers, each a head node pointer and a
// count. The first one holds the reports; its head node's left link is the first (smallest) node, whose value starts at +0x10.
struct SnapStateReports {
    void *mReportsHead;      // +0x00
    uint32_t mReportCount;   // +0x04
    uint32_t mOtherFields[6]; // +0x08..+0x1f (three more containers)

    // 0x7f0ca0 (__thiscall, ret 4): copies the first report into *out (the object is counted again), or makes an empty one
    // (code 0, no object, kind 0; the detail bytes are left as they were) when there are none. Returns `out`.
    SnapErrorReport *BuildReport(SnapErrorReport *out) const;
};

static_assert(sizeof(SnapStateReports) == 0x20, "SnapStateReports layout");
