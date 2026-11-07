// The size-optimised part of lane HP's statics (0x90a915 sits in a /O1 module).
#include "app/StaticInitParser.h"

namespace staticinit {

StaticFlag *StaticFlag::Construct()
{
    mSet = 1;
    return this;
}

} // namespace staticinit
