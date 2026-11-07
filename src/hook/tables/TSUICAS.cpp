#include "hook/hooks.h"
#include "ui/TSUICAS.h"

namespace {

using PetCoatEntryQI = bool (nTSUICAS::cTSUICASComponentPetCoatLayerEntry::*)(uint32_t, void **);
using PetCoatThumbQI = bool (nTSUICAS::cTSUICASComponentPetCoatLayerThumbnail::*)(uint32_t, void **);
using FragmentQI = bool (nTSUICAS::cTSFragmentFilterBase::*)(uint32_t, void **);
using PetCoatThumbVf7 = bool (nTSUICAS::cTSUICASComponentPetCoatLayerThumbnail::*)(uint32_t, uint32_t);

const HookSpec kHooks[] = {
    {"nTSUICAS_cTSUICASComponentPetCoatLayerEntry_QueryInterface", 0x602f64, 32, 0xf104e168,
     MemberAddress(static_cast<PetCoatEntryQI>(&nTSUICAS::cTSUICASComponentPetCoatLayerEntry::QueryInterface))},
    {"nTSUICAS_cTSUICASComponentPetCoatLayerThumbnail_QueryInterface", 0x602f92, 32, 0xcc17aaa6,
     MemberAddress(static_cast<PetCoatThumbQI>(&nTSUICAS::cTSUICASComponentPetCoatLayerThumbnail::QueryInterface))},
    {"nTSUICAS_cTSFragmentFilterBase_QueryInterface", 0x759042, 32, 0x41638cb0,
     MemberAddress(static_cast<FragmentQI>(&nTSUICAS::cTSFragmentFilterBase::QueryInterface))},
    {"nTSUICAS_cTSUICASComponentPetCoatLayerThumbnail_vf7", 0x603b9f, 32, 0xa1c7fb8c,
     MemberAddress(static_cast<PetCoatThumbVf7>(&nTSUICAS::cTSUICASComponentPetCoatLayerThumbnail::Vf7))},
    {"nTSUICAS_cTSUICASComponentPetBin_vf184", 0x6049a5, 32, 0xd61283b5,
     MemberAddress(&nTSUICAS::cTSUICASComponentPetBin::Vf184)},
    {"nTSUICAS_cTSUICASComponentModifiers_vf656_4", 0x5f8e0a, 32, 0xf9a56aea,
     MemberAddress(&nTSUICAS::cTSUICASComponentModifiers::Vf656_4)},

    // Component chain (QueryInterface, ret 8). 0x755d3c and 0x75907f are the chain's base links.
    {"cTSUIWinHandler_QueryInterface", 0x755d3c, 32, 0x8c80b6fb, MemberAddress(&cTSUIWinHandler::QueryInterface)},
    {"nTSUICAS_cTSUICASComponent_QueryInterface", 0x75907f, 32, 0x9d92a4c2,
     reinterpret_cast<void *>(&nTSUICAS::cTSUICASComponentQueryInterface)},
    {"nTSUICAS_cTSUICASComponentSimBin_QueryInterface", 0x5f1f44, 32, 0x4847596c,
     MemberAddress(&nTSUICAS::cTSUICASComponentSimBin::QueryInterface)},
    {"nTSUICAS_cTSUICASComponentBabyMaker_QueryInterface", 0x5f3570, 32, 0x2ed654ed,
     MemberAddress(&nTSUICAS::cTSUICASComponentBabyMaker::QueryInterface)},
    {"nTSUICAS_cTSUICASComponentArchetypes_QueryInterface", 0x5f67bc, 32, 0xa852e3d2,
     MemberAddress(&nTSUICAS::cTSUICASComponentArchetypes::QueryInterface)},
    {"nTSUICAS_cTSHairFilter_QueryInterface", 0x5fe25b, 32, 0xef90a875,
     MemberAddress(&nTSUICAS::cTSHairFilter::QueryInterface)},
    {"nTSUICAS_cTSUICASComponentModifiers_QueryInterface", 0x5f8da6, 32, 0xa4c51e5a,
     MemberAddress(&nTSUICAS::cTSUICASComponentModifiers::QueryInterface)},
    {"nTSUICAS_cTSUICASComponentOverlays_QueryInterface", 0x5fb48e, 32, 0xe9fa5fd4,
     MemberAddress(&nTSUICAS::cTSUICASComponentOverlays::QueryInterface)},
    {"nTSUICAS_cTSUICASComponentBin_QueryInterface", 0x5fcc7a, 32, 0x4c8442fe,
     MemberAddress(&nTSUICAS::cTSUICASComponentBin::QueryInterface)},
    {"nTSUICAS_cTSUICASComponentSkinTone_QueryInterface", 0x5ff548, 32, 0x2ecc5547,
     MemberAddress(&nTSUICAS::cTSUICASComponentSkinTone::QueryInterface)},
    {"nTSUICAS_cTSUICASComponentPetCoatColors_QueryInterface", 0x602f00, 32, 0x5af14147,
     MemberAddress(&nTSUICAS::cTSUICASComponentPetCoatColors::QueryInterface)},
    {"nTSUICAS_cTSUICASComponentPetBin_QueryInterface", 0x6051a0, 32, 0x84a7fe02,
     MemberAddress(&nTSUICAS::cTSUICASComponentPetBin::QueryInterface)},
    {"nTSUICAS_cTSUICASComponentBodyOptions_QueryInterface", 0x60610f, 32, 0x7b5958cf,
     MemberAddress(&nTSUICAS::cTSUICASComponentBodyOptions::QueryInterface)},
    {"nTSUICAS_cTSUICASComponentClothing_QueryInterface", 0x5ecf40, 32, 0x313e3823,
     MemberAddress(&nTSUICAS::cTSUICASComponentClothing::QueryInterface)},
    {"nTSUICAS_cTSUICASComponentPersonality_QueryInterface", 0x5ee467, 32, 0x5406a951,
     MemberAddress(&nTSUICAS::cTSUICASComponentPersonality::QueryInterface)},
    {"nTSUICAS_cTSUICASComponentGenetics_QueryInterface", 0x5efa4c, 32, 0x0e3b794b,
     MemberAddress(&nTSUICAS::cTSUICASComponentGenetics::QueryInterface)},
    {"nTSUICAS_cTSUICASComponentAccessories_QueryInterface", 0x5f1471, 32, 0xadeb6fd8,
     MemberAddress(&nTSUICAS::cTSUICASComponentAccessories::QueryInterface)},
    {"nTSUICAS_cTSUICASComponentHair_QueryInterface", 0x5fe291, 32, 0x336c868c,
     MemberAddress(&nTSUICAS::cTSUICASComponentHair::QueryInterface)},
    {"nTSUICAS_cTSUICASComponentPetGenetics_QueryInterface", 0x600f33, 32, 0x20baddec,
     MemberAddress(&nTSUICAS::cTSUICASComponentPetGenetics::QueryInterface)},
};
REGISTER_HOOKS(kHooks);

} // namespace
