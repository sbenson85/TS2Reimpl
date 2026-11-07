// cDeviceStateDX::DumpInfo (0x17da90, lane RJ): the state cache's part of the device's info dump (cDeviceDX::DumpInfo).
#include "gz/cDeviceStateDX.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cDeviceDX.h" // ts2_rva_d9e644, the dump's newline
#include "gz/cStateBlockDX.h"
#include "ifaces/cIGZOStream.h"
#include "rz/cRZString.h"

namespace nGZGraphic4 {

void cDeviceStateDX::DumpInfo(cIGZUnknown *stream)
{
    // The original calls the stream's slots on the pointer it is given (the cIGZOStream face).
    ifaces::cIGZOStream *const out = As<ifaces::cIGZOStream>(stream);
    const uint32_t count = static_cast<uint32_t>(mList.size());
    out->SetRZCharStr("State block count: ");
    out->SetUint32(count);
    out->SetRZCharStr(ts2_rva_d9e644);
    uint32_t bytes = 0, changes = 0;
    for (const cStateBlockDX *block : mList) {
        const uint8_t *const record = block->mRecord.data(); // the record's size (16 bits), then its change count at +3
        bytes += *reinterpret_cast<const uint16_t *>(record);
        changes += record[3];
    }
    // Single-precision arithmetic, as the original: the counts converted to float, the quotients widened for the format.
    const float divisor = static_cast<float>(count != 0 ? count : 1u);
    const float total = static_cast<float>(bytes);
    cRZString line;
    line.Construct();
    line.Sprintf("State blocks alloc size: %.1fKB, avg: %.1fbytes, avg state changes: %.1f\n",
                 static_cast<double>(total * 0.0009765625f), static_cast<double>(total / divisor),
                 static_cast<double>(static_cast<float>(changes) / divisor));
    out->SetRZCharStr(line.ToChar());
    line.Destruct();
}

} // namespace nGZGraphic4
