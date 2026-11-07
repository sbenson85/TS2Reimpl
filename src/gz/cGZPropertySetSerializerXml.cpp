// The XML delegate of the string-keyed property-set serializer (cGZPropertySetSerializerXmlT<cKeyStringTraitsT>): reading a
// set from XML text through the framework's expat parser object, whose content handler is this object's +4 face, and
// writing one through the framework's XML writer. Lane DV (Write lane DY); layout and record format in
// src/gz/cGZPropertySetSerializer.h. The bodies are one template with the uint32 instantiation
// (src/gz/cGZPropertySetSerializerDelegates.h, lane EU); this file is the string instantiation, /O2 code like the original's
// (0x551000-0x557400).
#include "gz/cGZPropertySetSerializer.h"
#include "gz/cGZPropertySetSerializerDelegates.h"

template class cGZPropertySetSerializerXmlT<cKeyStringTraits>;
template struct cGZPropertySetSerializerXmlAdapters<cKeyStringTraits>;
