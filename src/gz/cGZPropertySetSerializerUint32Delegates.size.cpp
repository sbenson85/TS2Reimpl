// The uint32-keyed property-set serializer's delegates, cGZPropertySetSerializerBinaryT / XmlT <cKeyUint32Traits>: the
// instantiation of the template in src/gz/cGZPropertySetSerializerDelegates.h with uint32 keys (binary magic 0x2bea4862, keys
// as u32 / "0x%08x" texts). /O1 code like the original's (0x236be6-0x242e4d). Lane EU.
#include "gz/cGZPropertySetSerializer.h"
#include "gz/cGZPropertySetSerializerDelegates.h"

template class cGZPropertySetSerializerBinaryT<cKeyUint32Traits>;
template struct cGZPropertySetSerializerBinaryAdapters<cKeyUint32Traits>;
template class cGZPropertySetSerializerXmlT<cKeyUint32Traits>;
template struct cGZPropertySetSerializerXmlAdapters<cKeyUint32Traits>;
