#pragma once
// The scene graph's behaviour-type event attribute records, owned (lane SQ). The original keeps each as a 24-byte static in
// its data section (RVAs in the comments, 1.18.0.187: id, a pointer to the event's name text, then four words); the registries
// copy them by value into their maps, and nothing compares the name pointer (the maps are read for the id, the text and the
// words), so the text is our own literal. Records the registries store with their own last word (the "20-byte" ones) keep the
// original's value here and the registry overrides it, as the original does.
#include "gz/cGZEventReflector.h"

namespace sgevents {

using nGZEvent::cEventAttribute;

// object-graph node
inline constexpr cEventAttribute kGraphConnChanged{0x7b54c99c, "GraphConnChanged", 1, 0x3b5cb427, 2, 0xc};                 // 0xff832c
inline constexpr cEventAttribute kSubordinateListChanged{0x1d05597b, "SubordinateListChanged", 1, 0x9d054e5e, 2, 4};        // 0xff8344
// composition tree node
inline constexpr cEventAttribute kPartitioningUpdateReqStateChanged{0x0a884a15, "PartitioningUpdateReqStateChanged", 1,
                                                                    0x7b256782, 2, 0xc};                                    // 0xff835c
inline constexpr cEventAttribute kIncludeInPartitionStateChanged{0xcc6c9992, "IncludeInPartitionStateChanged", 1, 0x7b256782,
                                                                 2, 0xc};                                                   // 0xff8374
// transform node
inline constexpr cEventAttribute kBoneID{0xc9de8b96, "BoneID", 1, 0x7b22dd0f, 3, 0xc};                                      // 0xff838c
inline constexpr cEventAttribute kBoneTransform{0x9b59fb00, "BoneTransform", 1, 0x9b5645f2, 3, 0xc};                        // 0xff83d4
inline constexpr cEventAttribute kBoneTranslation{0xfb5b3383, "BoneTranslation", 1, 0x7b5b330c, 3, 0xc};                    // 0xff83a4
inline constexpr cEventAttribute kBoneOrientation{0x9b5b3506, "BoneOrientation", 1, 0x7b5b34d5, 3, 0xc};                    // 0xff83bc
// simple lights
inline constexpr cEventAttribute kBoundsUpdated{0xdd041f30, "BoundsUpdated", 1, 0x7b256782, 2, 8};  // 0x100b6e8, 0x106abb0, 0x106ba08
inline constexpr cEventAttribute kLightDependentAttributesChanged{0x69f75eba, "LightDependentAttributesChanged", 1,
                                                                  0x7b256782, 2, 8};                                        // 0x106acf8
// geometry builder, geometry node, shape
inline constexpr cEventAttribute kBuildGeometry{0xbbb112f6, "BuildGeometry", 1, 0x7b256782, 2, 8};                          // 0x106ab4c, 0x106a558
inline constexpr cEventAttribute kGeometryNodeUpdated{0x9bb24282, "GeometryNodeUpdated", 1, 0x7b256782, 2, 8};              // 0x106a528, 0x106ba50
inline constexpr cEventAttribute kHandleGeometryNodeUpdated{0x2a7e3a48, "HandleGeometryNodeUpdated", 1, 0x7b256782, 1, 8};  // 0x106a540, 0x106ba68
inline constexpr cEventAttribute kMaterialBindingsUpdated{0x3d44b140, "MaterialBindingsUpdated", 1, 0x2c27f3de, 2, 8};      // 0x106ba20
inline constexpr cEventAttribute kPartCountUpdated{0xedf79966, "PartCountUpdated", 1, 0x7b256782, 2, 8};                    // 0x106ba38
// device events (also cTextureManager's and cMaterialManager's own kDeviceEventRecord)
inline constexpr cEventAttribute kCurrentDeviceEvent{0xca7ccbbc, "CurrentDeviceEvent", 1, 0x3b6c3e0a, 2, 8};                // 0xff7890, 0x106a5a0
inline constexpr cEventAttribute kCurrentDeviceRenderSceneEvent{0x2b5745f8, "CurrentDeviceRenderSceneEvent", 1, 0xcb573da1, 2, 8}; // 0xff78a8

} // namespace sgevents
