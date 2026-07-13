#include "MRBNNSceneViewExtension.h"

#include "MRBNNVolumeActor.h"
#include "MRBNNVolumeComponent.h"
#include "GlobalShader.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"

namespace
{
class FMRBNNCompositeCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMRBNNCompositeCS);
	SHADER_USE_PARAMETER_STRUCT(FMRBNNCompositeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CloudTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, CompositeSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, CompositeOutputTexture)
		SHADER_PARAMETER(FIntPoint, CompositeOutputSize)
		SHADER_PARAMETER(float, CompositeStrength)
	END_SHADER_PARAMETER_STRUCT()

	static constexpr int32 GroupSize = 8;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMRBNNCompositeCS, "/Plugin/MRBNN/Private/MRBNNComputeRender.usf", "MainCompositeCS", SF_Compute);
}

FMRBNNSceneViewExtension::FMRBNNSceneViewExtension(const FAutoRegister& AutoRegister, TWeakObjectPtr<AMRBNNVolumeActor> InActor)
	: FSceneViewExtensionBase(AutoRegister)
	, Actor(InActor)
{
}

void FMRBNNSceneViewExtension::SetActor(TWeakObjectPtr<AMRBNNVolumeActor> InActor)
{
	Actor = InActor;
}

void FMRBNNSceneViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	AMRBNNVolumeActor* VolumeActor = Actor.Get();
	if (!VolumeActor || !VolumeActor->ShouldUseSceneViewExtensionRenderPass() || InViewFamily.Views.IsEmpty())
	{
		return;
	}

	TArray<FMRBNNComputeRenderer::FRenderDesc> RenderDescs;
	RenderDescs.Reserve(InViewFamily.Views.Num());
	for (const FSceneView* View : InViewFamily.Views)
	{
		if (!View)
		{
			continue;
		}

		FMRBNNComputeRenderer::FRenderDesc RenderDesc;
		if (VolumeActor->BuildComputeRenderDescForView(*View, RenderDesc))
		{
			RenderDesc.bCompositeOutput = true;
			RenderDescs.Add(RenderDesc);
		}
	}

	if (RenderDescs.IsEmpty())
	{
		return;
	}

	if (VolumeActor->MRBNNVolume)
	{
		VolumeActor->MRBNNVolume->MarkComputeVolumeRenderDispatched(FPlatformTime::Seconds());
	}

	FScopeLock Lock(&PendingRenderDescsCriticalSection);
	PendingRenderDescs = MoveTemp(RenderDescs);
}

void FMRBNNSceneViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	if (PassId == EPostProcessingPass::Tonemap)
	{
		InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(this, &FMRBNNSceneViewExtension::PostProcessPass_RenderThread));
	}
}

FScreenPassTexture FMRBNNSceneViewExtension::PostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	FMRBNNComputeRenderer::FRenderDesc RenderDesc;
	{
		FScopeLock Lock(&PendingRenderDescsCriticalSection);
		if (!PendingRenderDescs.IsEmpty())
		{
			RenderDesc = PendingRenderDescs[0];
			PendingRenderDescs.RemoveAt(0, 1, EAllowShrinking::No);
		}
	}

	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	if (!SceneColor.IsValid() || !RenderDesc.DensityTexture.IsValid())
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	const FIntPoint OutputExtent = SceneColor.Texture->Desc.Extent;
	FRDGTextureDesc CloudTextureDesc = FRDGTextureDesc::Create2D(
		OutputExtent,
		PF_FloatRGBA,
		FClearValueBinding::Transparent,
		TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef CloudTexture = GraphBuilder.CreateTexture(CloudTextureDesc, TEXT("MRBNN.SceneCloud"));

	FRDGTextureDesc CompositeTextureDesc = FRDGTextureDesc::Create2D(
		OutputExtent,
		PF_FloatRGBA,
		FClearValueBinding::Black,
		TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef CompositeTexture = GraphBuilder.CreateTexture(CompositeTextureDesc, TEXT("MRBNN.SceneComposite"));

	RenderDesc.OutputSize = OutputExtent;
	RenderDesc.bCompositeOutput = true;
	FMRBNNComputeRenderer::AddRenderPassToRDGTexture(GraphBuilder, RenderDesc, CloudTexture, TEXT("MRBNN.ComputeVolumeRender.PostProcess"));

	TShaderMapRef<FMRBNNCompositeCS> CompositeShader(GetGlobalShaderMap(View.GetFeatureLevel()));
	FMRBNNCompositeCS::FParameters* Parameters = GraphBuilder.AllocParameters<FMRBNNCompositeCS::FParameters>();
	Parameters->SceneColorTexture = SceneColor.Texture;
	Parameters->CloudTexture = CloudTexture;
	Parameters->CompositeSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->CompositeOutputTexture = GraphBuilder.CreateUAV(CompositeTexture);
	Parameters->CompositeOutputSize = OutputExtent;
	Parameters->CompositeStrength = 1.0f;

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("MRBNN.CompositeSceneColor"),
		CompositeShader,
		Parameters,
		FComputeShaderUtils::GetGroupCount(OutputExtent, FMRBNNCompositeCS::GroupSize));

	const FScreenPassTexture CompositeOutput(CompositeTexture, SceneColor.ViewRect);
	if (Inputs.OverrideOutput.IsValid())
	{
		AddDrawTexturePass(GraphBuilder, FScreenPassViewInfo(View), CompositeOutput, Inputs.OverrideOutput);
		return Inputs.OverrideOutput;
	}

	return CompositeOutput;
}

bool FMRBNNSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	const AMRBNNVolumeActor* VolumeActor = Actor.Get();
	if (!VolumeActor || !VolumeActor->ShouldUseSceneViewExtensionRenderPass())
	{
		return false;
	}

	const UWorld* ActorWorld = VolumeActor->GetWorld();
	const UWorld* ContextWorld = Context.GetWorld();
	return !ContextWorld || ContextWorld == ActorWorld;
}
