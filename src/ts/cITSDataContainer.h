#pragma once
#include "gz/cIGZUnknown.h"

namespace ifaces {
class cITSDataImportExportContext;
} // namespace ifaces

// cITSDataContainer (interface id 0x8db5bf48): export to / import from a data import-export context, for classes in the
// standalone shape (docs/standalone-shape.md) to derive from. The original's name and slot order (global, as its RTTI).
// Implemented by cTSFamilyRelationshipArray (+4) and cTSNeighborhood (+8); hoisted from the FamilyRelationshipArray header
// by lane EM. Callers of other objects keep using the generated ifaces:: views (same ABI).
class cITSDataContainer : public cIGZUnknown {
public:
    virtual bool CollectReferenced(ifaces::cITSDataImportExportContext *context) = 0; // slot 3
    virtual bool Export(ifaces::cITSDataImportExportContext *context) = 0;            // slot 4
    virtual bool Import(ifaces::cITSDataImportExportContext *context) = 0;            // slot 5
};
