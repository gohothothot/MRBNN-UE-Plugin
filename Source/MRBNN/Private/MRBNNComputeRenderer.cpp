#include "MRBNNComputeRenderer.h"

#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

namespace
{
FVector4f ToVector4f(const FLinearColor& Color)
{
	return FVector4f(Color.R, Color.G, Color.B, Color.A);
}

class FMRBNNComputeRenderCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMRBNNComputeRenderCS);
	SHADER_USE_PARAMETER_STRUCT(FMRBNNComputeRenderCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture3D, DensityTexture)
		SHADER_PARAMETER_TEXTURE(Texture3D, FeatureTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, VolumeSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(FVector3f, CameraPosition)
		SHADER_PARAMETER(FVector3f, CameraForward)
		SHADER_PARAMETER(FVector3f, CameraRight)
		SHADER_PARAMETER(FVector3f, CameraUp)
		SHADER_PARAMETER(FVector2f, TanHalfFov)
		SHADER_PARAMETER(FVector3f, LightDirection)
		SHADER_PARAMETER(FVector4f, LightColor)
		SHADER_PARAMETER(FVector4f, Albedo)
		SHADER_PARAMETER(FVector4f, CloudColor)
		SHADER_PARAMETER(FVector4f, BakedFeatureTint)
		SHADER_PARAMETER(int32, StepCount)
		SHADER_PARAMETER(int32, DirectShadowSteps)
		SHADER_PARAMETER(int32, bUseBakedFeatures)
		SHADER_PARAMETER(int32, bUseExplicitCamera)
		SHADER_PARAMETER(int32, bCompositeOutput)
		SHADER_PARAMETER(int32, FrameIndex)
		SHADER_PARAMETER(float, Opacity)
		SHADER_PARAMETER(float, Ambient)
		SHADER_PARAMETER(float, Directional)
		SHADER_PARAMETER(float, ShadowStrength)
		SHADER_PARAMETER(float, LightStep)
		SHADER_PARAMETER(float, Brightness)
		SHADER_PARAMETER(float, DirectLightIntensity)
		SHADER_PARAMETER(float, DirectShadowDensity)
		SHADER_PARAMETER(float, PhaseG)
		SHADER_PARAMETER(float, PhaseStrength)
		SHADER_PARAMETER(float, BakedFeatureContribution)
		SHADER_PARAMETER(float, MultiScatterContribution)
		SHADER_PARAMETER(float, FeatureAlbedoBlend)
	END_SHADER_PARAMETER_STRUCT()

	static constexpr int32 GroupSize = 8;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMRBNNComputeRenderCS, "/Plugin/MRBNN/Private/MRBNNComputeRender.usf", "MainCS", SF_Compute);

bool AddMRBNNComputeRenderPass(FRDGBuilder& GraphBuilder, const FMRBNNComputeRenderer::FRenderDesc& Desc, FRDGTextureRef OutputTexture, const TCHAR* EventName)
{
	if (!Desc.DensityTexture.IsValid() || !OutputTexture || Desc.OutputSize.X <= 0 || Desc.OutputSize.Y <= 0)
	{
		return false;
	}

	FMRBNNComputeRenderer::FRenderDesc RenderDesc = Desc;
	if (!RenderDesc.FeatureTexture.IsValid())
	{
		RenderDesc.FeatureTexture = RenderDesc.DensityTexture;
	}

	TShaderMapRef<FMRBNNComputeRenderCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FMRBNNComputeRenderCS::FParameters* Parameters = GraphBuilder.AllocParameters<FMRBNNComputeRenderCS::FParameters>();
	Parameters->DensityTexture = RenderDesc.DensityTexture.GetReference();
	Parameters->FeatureTexture = RenderDesc.FeatureTexture.GetReference();
	Parameters->VolumeSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);
	Parameters->OutputSize = RenderDesc.OutputSize;
	Parameters->CameraPosition = RenderDesc.bUseExplicitCamera ? RenderDesc.CameraPosition : FVector3f(RenderDesc.RenderSettings.CameraPosition);
	Parameters->CameraForward = RenderDesc.CameraForward;
	Parameters->CameraRight = RenderDesc.CameraRight;
	Parameters->CameraUp = RenderDesc.CameraUp;
	Parameters->TanHalfFov = RenderDesc.TanHalfFov;
	Parameters->LightDirection = FVector3f(RenderDesc.RenderSettings.LightDirection.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.35, 0.7, 0.62)));
	Parameters->LightColor = ToVector4f(RenderDesc.RenderSettings.LightColor);
	Parameters->Albedo = ToVector4f(RenderDesc.RenderSettings.Albedo);
	Parameters->CloudColor = ToVector4f(RenderDesc.ComputeSettings.CloudColor);
	Parameters->BakedFeatureTint = ToVector4f(RenderDesc.ComputeSettings.BakedFeatureTint);
	Parameters->StepCount = FMath::Clamp(RenderDesc.ComputeSettings.StepCount, 4, 192);
	Parameters->DirectShadowSteps = FMath::Clamp(RenderDesc.ComputeSettings.DirectShadowSteps, 0, 16);
	Parameters->bUseBakedFeatures = RenderDesc.ComputeSettings.bUseBakedFeatures ? 1 : 0;
	Parameters->bUseExplicitCamera = RenderDesc.bUseExplicitCamera ? 1 : 0;
	Parameters->bCompositeOutput = RenderDesc.bCompositeOutput ? 1 : 0;
	Parameters->FrameIndex = RenderDesc.FrameIndex;
	Parameters->Opacity = FMath::Max(RenderDesc.ComputeSettings.Opacity, 0.0f);
	Parameters->Ambient = FMath::Max(RenderDesc.ComputeSettings.Ambient, 0.0f);
	Parameters->Directional = FMath::Max(RenderDesc.ComputeSettings.Directional, 0.0f);
	Parameters->ShadowStrength = FMath::Max(RenderDesc.ComputeSettings.ShadowStrength, 0.0f);
	Parameters->LightStep = FMath::Max(RenderDesc.ComputeSettings.LightStep, 0.001f);
	Parameters->Brightness = FMath::Max(RenderDesc.ComputeSettings.Brightness, 0.0f);
	Parameters->DirectLightIntensity = FMath::Max(RenderDesc.ComputeSettings.DirectLightIntensity, 0.0f);
	Parameters->DirectShadowDensity = FMath::Max(RenderDesc.ComputeSettings.DirectShadowDensity, 0.0f);
	Parameters->PhaseG = FMath::Clamp(RenderDesc.ComputeSettings.PhaseG, -0.85f, 0.85f);
	Parameters->PhaseStrength = FMath::Clamp(RenderDesc.ComputeSettings.PhaseStrength, 0.0f, 1.0f);
	Parameters->BakedFeatureContribution = FMath::Clamp(RenderDesc.ComputeSettings.BakedFeatureContribution, 0.0f, 2.0f);
	Parameters->MultiScatterContribution = FMath::Clamp(RenderDesc.ComputeSettings.MultiScatterContribution, 0.0f, 2.0f);
	Parameters->FeatureAlbedoBlend = FMath::Clamp(RenderDesc.ComputeSettings.FeatureAlbedoBlend, 0.0f, 1.0f);

	const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(RenderDesc.OutputSize, FMRBNNComputeRenderCS::GroupSize);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("%s", EventName),
		ComputeShader,
		Parameters,
		GroupCount);

	return true;
}
}

bool FMRBNNComputeRenderer::AddRenderPass(FRDGBuilder& GraphBuilder, const FRenderDesc& Desc, const TCHAR* EventName)
{
	if (!Desc.OutputTexture.IsValid())
	{
		return false;
	}

	FRDGTextureRef OutputTexture = RegisterExternalTexture(GraphBuilder, Desc.OutputTexture.GetReference(), TEXT("MRBNN.ComputeOutput"));
	return AddMRBNNComputeRenderPass(GraphBuilder, Desc, OutputTexture, EventName);
}

bool FMRBNNComputeRenderer::AddRenderPassToRDGTexture(FRDGBuilder& GraphBuilder, const FRenderDesc& Desc, FRDGTextureRef OutputTexture, const TCHAR* EventName)
{
	return AddMRBNNComputeRenderPass(GraphBuilder, Desc, OutputTexture, EventName);
}

bool FMRBNNComputeRenderer::EnqueueRender(const FRenderDesc& Desc)
{
	if (!Desc.DensityTexture.IsValid() || !Desc.OutputTexture.IsValid() || Desc.OutputSize.X <= 0 || Desc.OutputSize.Y <= 0)
	{
		return false;
	}

	ENQUEUE_RENDER_COMMAND(MRBNNComputeRender)(
		[Desc](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			FMRBNNComputeRenderer::AddRenderPass(GraphBuilder, Desc, TEXT("MRBNN.ComputeVolumeRender.Manual"));
			GraphBuilder.Execute();
		});

	return true;
}
