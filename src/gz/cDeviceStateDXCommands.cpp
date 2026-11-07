// nGZGraphic4::cDeviceStateDX::ApplyStateCommand (slot 12, 0x17fd40) and the 33 per-command apply functions it
// dispatches to (RVAs 0x186530..0x18a180, called only from the slot). A render state command (src/gz/RenderStates.h:
// vtable at +0, id byte at +4, size at +6, the fields from +8) turns into one call on the cache: a state, a group of
// states (the lists of engine indices are the original's tables, 0xda0194..0xda0a7c, written out here), a light, the
// material, a texture transform, the viewport and so on. The commands' field names in RenderStates.h are tentative;
// the offsets below are what the apply functions read.
#include <bit>
#include <cstring>

#include "gz/RenderStates.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceStateDX.h"

namespace nGZGraphic4 {

namespace {

template <class T> T At(const void *command, size_t offset)
{
    T value;
    std::memcpy(&value, static_cast<const uint8_t *>(command) + offset, sizeof(T));
    return value;
}
uint32_t Byte(const void *command, size_t offset) { return At<uint8_t>(command, offset); }
uint32_t Word(const void *command, size_t offset) { return At<uint32_t>(command, offset); }
float Float(const void *command, size_t offset) { return At<float>(command, offset); }

// The engine's render state indices of each group (the original's tables).
constexpr uint32_t kColorWrite[] = {0x12, 0x13, 0x14, 0x15};
constexpr uint32_t kFog[] = {0x22, 0x29, 0x23, 0x24, 0x25, 0x28, 0x26, 0x27};
constexpr uint32_t kLighting[] = {0x17, 0x18, 0x19};
constexpr uint32_t kPerVertexColor[] = {0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
constexpr uint32_t kAlphaBlend[] = {0x33, 0x38, 0x3a, 0x36};
constexpr uint32_t kAlphaTest[] = {0x30, 0x31};
constexpr uint32_t kDepthTest[] = {0x2d, 0x2e};
constexpr uint32_t kStencil[] = {0x3d, 0x3f};
constexpr uint32_t kStencilFunction[] = {0x48, 0x40, 0x41};
constexpr uint32_t kStencilOperation[] = {0x42, 0x44, 0x46};
// Texture stage state indices of the blend (the first is the engine's index 8; then 0..7).
constexpr uint32_t kTextureBlend[] = {8, 0, 1, 2, 3, 4, 5, 6, 7};
// Sampler state indices.
constexpr uint32_t kAddressing[] = {0, 1, 2};
constexpr uint32_t kFilter[] = {4, 5, 9};
constexpr uint32_t kMipFilter[] = {6, 7};
// Light types by the command's kind (directional, point, spot).
constexpr uint32_t kLightType[] = {3, 1, 2};
// The blend's argument and operation maps (the argument's high nibble, the modifier bits, passes through).
constexpr uint32_t kBlendArgument[] = {1, 5, 2, 0, 4, 3};
constexpr uint32_t kBlendOperation[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0xb, 0xa, 0x10, 0xc, 0xd, 0xe, 0x12, 0x14, 0x13, 0x15, 0x16};

// A colour channel as 0..255: the float plus 49152.0f puts it in the mantissa (1/256 steps), the bits less those of
// 49152.0f are the value times 256, clamped to a byte by the sign masks.
uint32_t Channel(float f, float scale)
{
    const float shifted = f * scale + 49152.0f;
    const uint32_t bits = std::bit_cast<uint32_t>(shifted) + 0xb8c00000u;
    auto sign = [](uint32_t x) { return static_cast<uint32_t>(static_cast<int32_t>(x) >> 31); };
    return ((bits & ~sign(bits)) | sign(255u - bits)) & 0xff;
}
uint32_t Color(const void *command, size_t offset, bool scaled)
{
    const float r = Float(command, offset), g = Float(command, offset + 4), b = Float(command, offset + 8);
    // The fog colour is taken as it is, the ambient colour of the enabled lights scaled by a quarter.
    const float scale = scaled ? 0.25f : 1.0f;
    return 0xff000000u | Channel(r, scale) << 16 | Channel(g, scale) << 8 | Channel(b, scale);
}

// A table entry; the engine's enumerations stay inside their tables (the original reads past the end for a larger
// byte, which no command carries).
template <size_t N> uint32_t Entry(const uint32_t (&table)[N], uint32_t index) { return index < N ? table[index] : 0; }

} // namespace

void cDeviceStateDX::ApplyStateCommand(uint32_t address)
{
    const void *c = reinterpret_cast<const void *>(static_cast<uintptr_t>(address));
    switch (Byte(c, 4)) {
    case 0: // cRenderTargetsState: four targets and the depth surface
        SetRenderTargets(reinterpret_cast<cIGZUnknown *const *>(static_cast<const uint8_t *>(c) + 8),
                         At<cIGZUnknown *>(c, 0x18));
        break;
    case 1: // cViewportState: the four fractions
        SetViewportFractions(Float(c, 8), Float(c, 0xc), Float(c, 0x10), Float(c, 0x14));
        break;
    case 2: SetDepthRange(Float(c, 8), Float(c, 0xc)); break; // cDepthRangeState
    case 3: SetClipping(Byte(c, 8)); break;                   // cClippingState
    case 4: SetRenderState(0x2c, Byte(c, 8) + 1); break;      // cCullModeState
    case 5: SetRenderState(0x2a, Byte(c, 8) + 2); break;      // cFillModeState
    case 6: SetRenderState(0x2b, Byte(c, 8) != 0 ? 2 : 1); break; // cShadingState
    case 7: { // cColorWriteEnableState: one flag per channel
        const uint32_t values[] = {Byte(c, 8), Byte(c, 9), Byte(c, 0xa), Byte(c, 0xb)};
        SetRenderStates(4, kColorWrite, values);
        break;
    }
    case 8: { // cFogState: two bytes, the colour, the mode's byte in one of two slots, three floats
        uint32_t values[8];
        values[0] = Byte(c, 8);
        values[1] = Byte(c, 9);
        values[2] = Color(c, 0xc, false);
        if (Byte(c, 0xa) != 0) {
            values[3] = Byte(c, 0xb);
            values[4] = 0;
        } else {
            values[3] = 0;
            values[4] = Byte(c, 0xb);
        }
        values[5] = Word(c, 0x18);
        values[6] = Word(c, 0x1c);
        values[7] = Word(c, 0x20);
        SetRenderStates(8, kFog, values);
        break;
    }
    case 9: { // cFFLightingState
        const uint32_t values[] = {Byte(c, 8), Byte(c, 9), Byte(c, 0xa)};
        SetRenderStates(3, kLighting, values);
        break;
    }
    case 10: { // cFFLight: the light's index at +8, its kind at +0xc
        D3DLIGHT9 light;
        std::memset(&light, 0, sizeof(light));
        light.Type = static_cast<D3DLIGHTTYPE>(Entry(kLightType, Word(c, 0xc)));
        light.Diffuse.r = Float(c, 0x28);
        light.Diffuse.g = Float(c, 0x2c);
        light.Diffuse.b = Float(c, 0x30);
        light.Specular.r = Float(c, 0x34);
        light.Specular.g = Float(c, 0x38);
        light.Specular.b = Float(c, 0x3c);
        light.Position.x = Float(c, 0x10);
        light.Position.y = Float(c, 0x14);
        light.Position.z = Float(c, 0x18);
        light.Direction.x = Float(c, 0x1c);
        light.Direction.y = Float(c, 0x20);
        light.Direction.z = Float(c, 0x24);
        light.Range = Float(c, 0x54);
        light.Falloff = 1.0f;
        light.Attenuation0 = Float(c, 0x40);
        light.Attenuation1 = Float(c, 0x44);
        light.Attenuation2 = Float(c, 0x48);
        light.Theta = Float(c, 0x4c);
        light.Phi = Float(c, 0x50);
        SetLight(Word(c, 8), &light);
        break;
    }
    case 11: SetLightCount(Word(c, 8), Color(c, 0xc, true)); break; // cFFEnabledLights: the count and the ambient colour
    case 12: { // cFFMaterial: colours stored alpha first
        D3DMATERIAL9 m;
        m.Diffuse = {Float(c, 0x1c), Float(c, 0x20), Float(c, 0x24), Float(c, 0x18)};
        m.Ambient = {Float(c, 0xc) * 4.0f, Float(c, 0x10) * 4.0f, Float(c, 0x14) * 4.0f, Float(c, 8) * 4.0f};
        m.Specular = {Float(c, 0x2c), Float(c, 0x30), Float(c, 0x34), Float(c, 0x28)};
        m.Emissive = {Float(c, 0x3c), Float(c, 0x40), Float(c, 0x44), Float(c, 0x38)};
        m.Power = Float(c, 0x48);
        SetMaterial(&m);
        break;
    }
    case 13: { // cFFPerVertexColorState
        const uint32_t values[] = {Byte(c, 8), Byte(c, 9), Byte(c, 0xa), Byte(c, 0xb), Byte(c, 0xc)};
        SetRenderStates(5, kPerVertexColor, values);
        break;
    }
    case 14: { // cFFTextureCoordsSourceState: the three generated sources are mapped, others pass through
        uint32_t value = Word(c, 0xc);
        if (value == 0x100) value = 0x10000;
        else if (value == 0x101) value = 0x20000;
        else if (value == 0x102) value = 0x30000;
        SetTextureStageState(Word(c, 8), 10, value);
        break;
    }
    case 15: { // cFFTextureMatrixState
        SetTextureTransform(Word(c, 8), reinterpret_cast<const D3DMATRIX *>(static_cast<const uint8_t *>(c) + 0xc));
        break;
    }
    case 16: // cFFTextureTransformTypeState
        SetTextureStageState(Word(c, 8), 11, (Byte(c, 0xd) == 1 ? 0x100u : 0u) | Byte(c, 0xc));
        break;
    case 17: { // cAlphaBlendState
        const uint32_t values[] = {Byte(c, 8), Byte(c, 9) + 1, Byte(c, 0xa) + 1, Byte(c, 0xb) + 1};
        SetRenderStates(4, kAlphaBlend, values);
        break;
    }
    case 18: { // cAlphaTestState
        const uint32_t values[] = {Byte(c, 8), Byte(c, 9)};
        SetRenderStates(2, kAlphaTest, values);
        break;
    }
    case 19: SetRenderState(0x32, Byte(c, 8) + 1); break; // cAlphaTestFunction
    case 20: { // cDepthTestState
        const uint32_t values[] = {Byte(c, 8), Byte(c, 9)};
        SetRenderStates(2, kDepthTest, values);
        break;
    }
    case 21: SetRenderState(0x2f, Byte(c, 8) + 1); break; // cDepthTestFunction
    case 22: { // cStencilState
        const uint32_t values[] = {Byte(c, 8), Word(c, 0xc)};
        SetRenderStates(2, kStencil, values);
        break;
    }
    case 23: { // cStencilFunctionState
        const uint32_t values[] = {Byte(c, 8) + 1, Word(c, 0xc), Word(c, 0x10)};
        SetRenderStates(3, kStencilFunction, values);
        break;
    }
    case 24: { // cStencilOperationState
        const uint32_t values[] = {Byte(c, 8) + 1, Byte(c, 9) + 1, Byte(c, 0xa) + 1};
        SetRenderStates(3, kStencilOperation, values);
        break;
    }
    case 25: { // cTextureBlendState: arguments carry modifier bits in their high nibble, operations are mapped whole
        auto argument = [&](size_t offset) {
            const uint32_t v = Byte(c, offset);
            return (v & 0xf0) | Entry(kBlendArgument, v & 0x0f);
        };
        auto operation = [&](size_t offset) { return Entry(kBlendOperation, Byte(c, offset)); };
        const uint32_t values[] = {argument(0xc),  operation(0xd), argument(0x10), argument(0xe), argument(0xf),
                                   operation(0x11), argument(0x14), argument(0x12), argument(0x13)};
        SetTextureStageStates(Word(c, 8), 9, kTextureBlend, values);
        break;
    }
    case 26: SetRenderState(0x3c, Word(c, 8)); break; // cColorScalarState
    case 27: SetRenderState(0x1a, Byte(c, 8)); break; // cSpecularAddState
    case 28: { // cTextureAddressingState
        const uint32_t values[] = {Byte(c, 0xc) + 1, Byte(c, 0xd) + 1, Byte(c, 0xe) + 1};
        SetSamplerStates(Word(c, 8), 3, kAddressing, values);
        break;
    }
    case 29: SetRenderState(Byte(c, 8) + 1, Byte(c, 9)); break; // cTexCoordInterpolationState
    case 30: { // cTextureFilterState
        const uint32_t values[] = {Byte(c, 0xc) + 1, Byte(c, 0xd) + 1, Word(c, 0x10)};
        SetSamplerStates(Word(c, 8), 3, kFilter, values);
        break;
    }
    case 31: { // cTextureMipFilterState
        const uint32_t values[] = {Byte(c, 0xc), Word(c, 0x10)};
        SetSamplerStates(Word(c, 8), 2, kMipFilter, values);
        break;
    }
    case 32: SetScissor(Float(c, 8), Float(c, 0xc), Float(c, 0x10), Float(c, 0x14), static_cast<uint8_t>(Byte(c, 0x18))); break;
    default: break;
    }
}

// The default commands (0x186550..0x18a1a0): each builds the state command of its id holding the engine's default and
// applies it as above; the values here are what those commands come to. The colours they read (the fog colour, the
// lights' diffuse and specular) are globals nothing writes, so black.
void cDeviceStateDX::ApplyDefaultCommand(uint32_t id, const D3DCAPS9 *caps)
{
    static const D3DMATRIX kIdentity = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    switch (id) {
    case 0: { // 0x186550: the device's first back buffer and its target provider's surface (asked in that order: depth first)
        cDeviceDX *device = GetDeviceConst();
        cIGZUnknown *depth = device->GetTargetProviderSurface();
        cIGZUnknown *const targets[4] = {device->GetBackBuffer(0), nullptr, nullptr, nullptr};
        SetRenderTargets(targets, depth);
        break;
    }
    case 1: SetViewportFractions(0.0f, 0.0f, 1.0f, 1.0f); break; // 0x18a1a0
    case 2: SetDepthRange(0.0f, 1.0f); break;                    // 0x18a110
    case 3: SetClipping(1); break;                               // 0x18a080
    case 4: SetRenderState(0x2c, 2); break;                      // 0x186ad0: cull mode
    case 5: SetRenderState(0x2a, 3); break;                      // 0x186bd0: fill mode
    case 6: SetRenderState(0x2b, 2); break;                      // 0x186f80: shading
    case 7: { // 0x186900
        const uint32_t values[] = {0xf, 0xf, 0xf, 0xf};
        SetRenderStates(4, kColorWrite, values);
        break;
    }
    case 8: { // 0x186d60: fog off, the colour black, density 0, start 1, end 2
        const uint32_t values[] = {0, 1, 0xff000000u, 0, 0, 0, 0x3f800000u, 0x40000000u};
        SetRenderStates(8, kFog, values);
        break;
    }
    case 9: { // 0x187820
        const uint32_t values[] = {1, 1, 0};
        SetRenderStates(3, kLighting, values);
        break;
    }
    case 10: { // 0x187660: every light the device has, black and directional along -z
        D3DLIGHT9 light;
        std::memset(&light, 0, sizeof(light));
        light.Type = D3DLIGHT_DIRECTIONAL;
        light.Direction.z = -1.0f;
        light.Range = 1.0f;
        light.Falloff = 1.0f;
        // The caps are read without a check (the enumeration has them once a device exists).
        for (uint32_t i = 0; i < caps->MaxActiveLights; ++i) SetLight(i, &light);
        break;
    }
    case 11: SetLightCount(0, 0xff000000u); break; // 0x187410: no lights, black ambient
    case 12: { // 0x1879f0: white, the ambient colour scaled by 4 as the command does
        D3DMATERIAL9 m;
        m.Diffuse = {1.0f, 1.0f, 1.0f, 1.0f};
        m.Ambient = {4.0f, 4.0f, 4.0f, 4.0f};
        m.Specular = {0.0f, 0.0f, 0.0f, 1.0f};
        m.Emissive = {0.0f, 0.0f, 0.0f, 1.0f};
        m.Power = 0.0f;
        SetMaterial(&m);
        break;
    }
    case 13: { // 0x187b80
        const uint32_t values[] = {1, 0, 1, 2, 0};
        SetRenderStates(5, kPerVertexColor, values);
        break;
    }
    case 14: // 0x187c70: each stage its own texture coordinates
        for (uint32_t stage = 0; stage < 8; ++stage) SetTextureStageState(stage, 10, stage);
        break;
    case 15: // 0x187db0
        for (uint32_t stage = 0; stage < 8; ++stage) SetTextureTransform(stage, &kIdentity);
        break;
    case 16: // 0x187ec0
        for (uint32_t stage = 0; stage < 8; ++stage) SetTextureStageState(stage, 11, 0);
        break;
    case 17: { // 0x188060
        const uint32_t values[] = {0, 2, 1, 1};
        SetRenderStates(4, kAlphaBlend, values);
        break;
    }
    case 18: { // 0x1883f0
        const uint32_t values[] = {0, 0};
        SetRenderStates(2, kAlphaTest, values);
        break;
    }
    case 19: SetRenderState(0x32, 8); break; // 0x1882b0: alpha function always
    case 20: { // 0x188650
        const uint32_t values[] = {1, 1};
        SetRenderStates(2, kDepthTest, values);
        break;
    }
    case 21: SetRenderState(0x2f, 4); break; // 0x188510: depth function less-equal
    case 22: { // 0x188ae0
        const uint32_t values[] = {0, 0xffffffffu};
        SetRenderStates(2, kStencil, values);
        break;
    }
    case 23: { // 0x1887b0
        const uint32_t values[] = {8, 0, 0xffffffffu};
        SetRenderStates(3, kStencilFunction, values);
        break;
    }
    case 24: { // 0x188870
        const uint32_t values[] = {1, 1, 1};
        SetRenderStates(3, kStencilOperation, values);
        break;
    }
    case 25: { // 0x188df0
        const uint32_t values[] = {1, 1, 1, 1, 1, 1, 1, 1, 1};
        for (uint32_t stage = 0; stage < 8; ++stage) SetTextureStageStates(stage, 9, kTextureBlend, values);
        break;
    }
    case 26: SetRenderState(0x3c, 0); break; // 0x188b70
    case 27: SetRenderState(0x1a, 0); break; // 0x188c20
    case 28: { // 0x1893d0
        const uint32_t values[] = {1, 1, 1};
        for (uint32_t sampler = 0; sampler < 16; ++sampler) SetSamplerStates(sampler, 3, kAddressing, values);
        break;
    }
    case 29: // 0x1891c0: the texture coordinates' interpolation states
        for (uint32_t i = 0; i < 16; ++i) SetRenderState(i + 1, 0);
        break;
    case 30: { // 0x1898f0
        const uint32_t values[] = {1, 1, 1};
        for (uint32_t sampler = 0; sampler < 16; ++sampler) SetSamplerStates(sampler, 3, kFilter, values);
        break;
    }
    case 31: { // 0x189cd0
        const uint32_t values[] = {0, 0};
        for (uint32_t sampler = 0; sampler < 16; ++sampler) SetSamplerStates(sampler, 2, kMipFilter, values);
        break;
    }
    case 32: SetScissor(0.0f, 0.0f, 1.0f, 1.0f, 0); break; // 0x186740
    default: break;
    }
}

} // namespace nGZGraphic4
