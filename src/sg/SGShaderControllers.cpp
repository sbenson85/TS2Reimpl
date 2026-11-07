#include "sg/SGShaderControllers.h"

#include "misc/UtilitiesE.h"
#include "sg/cMaterialManager.h"

namespace nGZSceneGraph {

void UnregisterShaderPart(cIGZUnknown *part)
{
    static_cast<cIMaterialManager *>(GetCachedQueriedObject())->Unregister(part);
}

// Every instantiation, and the names its constructor and destructor hooks reach them by.
#pragma comment(linker, "/alternatename:_ShaderControllerT_AnimColorScalarAlpha_Construct=??0?$cShaderControllerT@VcIAnimColorScalarAlpha@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_ShaderControllerT_AnimColorScalarAlpha_Destruct=??1?$cShaderControllerT@VcIAnimColorScalarAlpha@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
template class cShaderControllerT<cIAnimColorScalarAlpha>;
#pragma comment(linker, "/alternatename:_ShaderControllerT_AnimatedTextureMatrix_Construct=??0?$cShaderControllerT@VcIAnimatedTextureMatrix@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_ShaderControllerT_AnimatedTextureMatrix_Destruct=??1?$cShaderControllerT@VcIAnimatedTextureMatrix@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
template class cShaderControllerT<cIAnimatedTextureMatrix>;
#pragma comment(linker, "/alternatename:_ShaderControllerT_AnimatedTiledTexture_Construct=??0?$cShaderControllerT@VcIAnimatedTiledTexture@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_ShaderControllerT_AnimatedTiledTexture_Destruct=??1?$cShaderControllerT@VcIAnimatedTiledTexture@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
template class cShaderControllerT<cIAnimatedTiledTexture>;
#pragma comment(linker, "/alternatename:_ShaderControllerT_ApplyColorScalarRadiance_Construct=??0?$cShaderControllerT@VcIApplyColorScalarRadiance@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_ShaderControllerT_ApplyColorScalarRadiance_Destruct=??1?$cShaderControllerT@VcIApplyColorScalarRadiance@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
template class cShaderControllerT<cIApplyColorScalarRadiance>;
#pragma comment(linker, "/alternatename:_ShaderControllerT_ApplyFFLightStates_Construct=??0?$cShaderControllerT@VcIApplyFFLightStates@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_ShaderControllerT_ApplyFFLightStates_Destruct=??1?$cShaderControllerT@VcIApplyFFLightStates@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
template class cShaderControllerT<cIApplyFFLightStates>;
#pragma comment(linker, "/alternatename:_ShaderControllerT_ApplyForceDepthTestFailCondition_Construct=??0?$cShaderControllerT@VcIApplyForceDepthTestFailCondition@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_ShaderControllerT_ApplyForceDepthTestFailCondition_Destruct=??1?$cShaderControllerT@VcIApplyForceDepthTestFailCondition@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
template class cShaderControllerT<cIApplyForceDepthTestFailCondition>;
#pragma comment(linker, "/alternatename:_ShaderControllerT_ApplyNonStandardLightState_Construct=??0?$cShaderControllerT@VcIApplyNonStandardLightState@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_ShaderControllerT_ApplyNonStandardLightState_Destruct=??1?$cShaderControllerT@VcIApplyNonStandardLightState@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
template class cShaderControllerT<cIApplyNonStandardLightState>;
#pragma comment(linker, "/alternatename:_ShaderControllerT_ApplyRenderTarget_Construct=??0?$cShaderControllerT@VcIApplyRenderTarget@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_ShaderControllerT_ApplyRenderTarget_Destruct=??1?$cShaderControllerT@VcIApplyRenderTarget@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
template class cShaderControllerT<cIApplyRenderTarget>;
#pragma comment(linker, "/alternatename:_ShaderControllerT_ApplyShaderProgramConstants_Construct=??0?$cShaderControllerT@VcIApplyShaderProgramConstants@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_ShaderControllerT_ApplyShaderProgramConstants_Destruct=??1?$cShaderControllerT@VcIApplyShaderProgramConstants@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
template class cShaderControllerT<cIApplyShaderProgramConstants>;
#pragma comment(linker, "/alternatename:_ShaderControllerT_ApplyStencilStateForOverlappingReflections_Construct=??0?$cShaderControllerT@VcIApplyStencilStateForOverlappingReflections@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_ShaderControllerT_ApplyStencilStateForOverlappingReflections_Destruct=??1?$cShaderControllerT@VcIApplyStencilStateForOverlappingReflections@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
template class cShaderControllerT<cIApplyStencilStateForOverlappingReflections>;
#pragma comment(linker, "/alternatename:_ShaderControllerT_AttributeColorScalar_Construct=??0?$cShaderControllerT@VcIAttributeColorScalar@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_ShaderControllerT_AttributeColorScalar_Destruct=??1?$cShaderControllerT@VcIAttributeColorScalar@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
template class cShaderControllerT<cIAttributeColorScalar>;
#pragma comment(linker, "/alternatename:_ShaderControllerT_AttributeFFMaterial_Construct=??0?$cShaderControllerT@VcIAttributeFFMaterial@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_ShaderControllerT_AttributeFFMaterial_Destruct=??1?$cShaderControllerT@VcIAttributeFFMaterial@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
template class cShaderControllerT<cIAttributeFFMaterial>;
#pragma comment(linker, "/alternatename:_ShaderControllerT_AttributeFFScaleModelToViewTransform_Construct=??0?$cShaderControllerT@VcIAttributeFFScaleModelToViewTransform@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_ShaderControllerT_AttributeFFScaleModelToViewTransform_Destruct=??1?$cShaderControllerT@VcIAttributeFFScaleModelToViewTransform@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
template class cShaderControllerT<cIAttributeFFScaleModelToViewTransform>;
#pragma comment(linker, "/alternatename:_ShaderControllerT_AttributeFFTextureMatrix_Construct=??0?$cShaderControllerT@VcIAttributeFFTextureMatrix@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_ShaderControllerT_AttributeFFTextureMatrix_Destruct=??1?$cShaderControllerT@VcIAttributeFFTextureMatrix@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
template class cShaderControllerT<cIAttributeFFTextureMatrix>;
#pragma comment(linker, "/alternatename:_ShaderControllerT_FFApplyTransforms_Construct=??0?$cShaderControllerT@VcIFFApplyTransforms@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_ShaderControllerT_FFApplyTransforms_Destruct=??1?$cShaderControllerT@VcIFFApplyTransforms@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
template class cShaderControllerT<cIFFApplyTransforms>;
#pragma comment(linker, "/alternatename:_ShaderControllerT_FFDepthOffset_Construct=??0?$cShaderControllerT@VcIFFDepthOffset@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_ShaderControllerT_FFDepthOffset_Destruct=??1?$cShaderControllerT@VcIFFDepthOffset@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
template class cShaderControllerT<cIFFDepthOffset>;
#pragma comment(linker, "/alternatename:_ShaderControllerT_ShaderControllerNotify_Construct=??0?$cShaderControllerT@VcIShaderControllerNotify@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_ShaderControllerT_ShaderControllerNotify_Destruct=??1?$cShaderControllerT@VcIShaderControllerNotify@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
template class cShaderControllerT<cIShaderControllerNotify>;
#pragma comment(linker, "/alternatename:_ShaderControllerT_ValidateRenderShaderContext_Construct=??0?$cShaderControllerT@VcIValidateRenderShaderContext@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_ShaderControllerT_ValidateRenderShaderContext_Destruct=??1?$cShaderControllerT@VcIValidateRenderShaderContext@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
template class cShaderControllerT<cIValidateRenderShaderContext>;

// The component level (its constructor and destructor reached through the controller's, and named by the tests).
template class cShaderComponentT<cIAnimColorScalarAlpha>;
template class cShaderComponentT<cIAnimatedTextureMatrix>;
template class cShaderComponentT<cIAnimatedTiledTexture>;
template class cShaderComponentT<cIApplyColorScalarRadiance>;
template class cShaderComponentT<cIApplyFFLightStates>;
template class cShaderComponentT<cIApplyForceDepthTestFailCondition>;
template class cShaderComponentT<cIApplyNonStandardLightState>;
template class cShaderComponentT<cIApplyRenderTarget>;
template class cShaderComponentT<cIApplyShaderProgramConstants>;
template class cShaderComponentT<cIApplyStencilStateForOverlappingReflections>;
template class cShaderComponentT<cIAttributeColorScalar>;
template class cShaderComponentT<cIAttributeFFMaterial>;
template class cShaderComponentT<cIAttributeFFScaleModelToViewTransform>;
template class cShaderComponentT<cIAttributeFFTextureMatrix>;
template class cShaderComponentT<cIFFApplyTransforms>;
template class cShaderComponentT<cIFFDepthOffset>;
template class cShaderComponentT<cIShaderControllerNotify>;
template class cShaderComponentT<cIValidateRenderShaderContext>;
} // namespace nGZSceneGraph
