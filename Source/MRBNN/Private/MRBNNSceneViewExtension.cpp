#include "MRBNNSceneViewExtension.h"

#include "MRBNNProjectSettings.h"
#include "MRBNNVolumeActor.h"
#include "MRBNNVolumeComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
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

class FMRBNNCloudOverCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMRBNNCloudOverCS);
	SHADER_USE_PARAMETER_STRUCT(FMRBNNCloudOverCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, BackCloudTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FrontCloudTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, CompositeSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, CompositeOutputTexture)
		SHADER_PARAMETER(FIntPoint, CompositeOutputSize)
	END_SHADER_PARAMETER_STRUCT()

	static constexpr int32 GroupSize = 8;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMRBNNCloudOverCS, "/Plugin/MRBNN/Private/MRBNNComputeRender.usf", "MainCloudOverCS", SF_Compute);

class FMRBNNDebugViewCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMRBNNDebugViewCS);
	SHADER_USE_PARAMETER_STRUCT(FMRBNNDebugViewCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DebugTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, CompositeSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, CompositeOutputTexture)
		SHADER_PARAMETER(FIntPoint, CompositeOutputSize)
		SHADER_PARAMETER(int32, DebugDisplayMode)
		SHADER_PARAMETER(float, DebugPreviewOpacity)
		SHADER_PARAMETER(float, DebugOverlayScale)
	END_SHADER_PARAMETER_STRUCT()

	static constexpr int32 GroupSize = 8;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMRBNNDebugViewCS, "/Plugin/MRBNN/Private/MRBNNComputeRender.usf", "MainDebugViewCS", SF_Compute);

bool IsRenderableMRBNNActor(const AMRBNNVolumeActor* Actor)
{
	return Actor && Actor->bUseComputeGlobalShader && Actor->bUseSceneViewExtensionRenderPass;
}

AMRBNNVolumeActor* FindSceneViewExtensionLeader(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AMRBNNVolumeActor> It(World); It; ++It)
	{
		AMRBNNVolumeActor* Candidate = *It;
		if (IsRenderableMRBNNActor(Candidate))
		{
			return Candidate;
		}
	}

	return nullptr;
}
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
	UWorld* ActorWorld = VolumeActor ? VolumeActor->GetWorld() : nullptr;
	if (!VolumeActor || !ActorWorld || !IsRenderableMRBNNActor(VolumeActor) || FindSceneViewExtensionLeader(ActorWorld) != VolumeActor || InViewFamily.Views.IsEmpty())
	{
		return;
	}

	TArray<AMRBNNVolumeActor*> CloudActors;
	for (TActorIterator<AMRBNNVolumeActor> It(ActorWorld); It; ++It)
	{
		AMRBNNVolumeActor* CloudActor = *It;
		if (IsRenderableMRBNNActor(CloudActor))
		{
			CloudActors.Add(CloudActor);
		}
	}

	if (CloudActors.IsEmpty())
	{
		return;
	}

	UMRBNNProjectSettings* Settings = UMRBNNProjectSettings::GetMutable();
	const bool bCaptureComputeCloud =
		Settings &&
		Settings->SceneViewDebugRenderTarget == EMRBNNSceneViewDebugRT::ComputeCloud &&
		Settings->SceneViewDebugDisplayMode != EMRBNNSceneViewDebugDisplayMode::Hidden;
	const bool bCopyDebugTexture =
		Settings &&
		Settings->SceneViewDebugRenderTarget == EMRBNNSceneViewDebugRT::ComputeCloud;
	const FIntPoint FamilyRenderTargetSize = InViewFamily.RenderTarget
		? InViewFamily.RenderTarget->GetSizeXY()
		: FIntPoint::ZeroValue;

	TArray<FPendingViewRenderDescs> RenderDescs;
	RenderDescs.Reserve(InViewFamily.Views.Num());
	TSet<AMRBNNVolumeActor*> DispatchedActors;
	for (const FSceneView* View : InViewFamily.Views)
	{
		if (!View)
		{
			continue;
		}

		FPendingViewRenderDescs ViewRenderDescs;
		ViewRenderDescs.DebugDisplayMode = Settings ? static_cast<int32>(Settings->SceneViewDebugDisplayMode) : 0;
		ViewRenderDescs.DebugPreviewOpacity = Settings ? Settings->SceneViewDebugPreviewOpacity : 1.0f;
		ViewRenderDescs.DebugOverlayScale = Settings ? Settings->SceneViewDebugOverlayScale : 0.35f;

		for (AMRBNNVolumeActor* CloudActor : CloudActors)
		{
			FMRBNNComputeRenderer::FRenderDesc RenderDesc;
			if (CloudActor && CloudActor->BuildComputeRenderDescForView(*View, RenderDesc))
			{
				RenderDesc.bCompositeOutput = true;
				FPendingCloudRenderDesc CloudRenderDesc;
				CloudRenderDesc.RenderDesc = RenderDesc;
				CloudRenderDesc.SortDepth = FVector::DotProduct(
					CloudActor->GetActorLocation() - View->ViewMatrices.GetViewOrigin(),
					View->GetViewDirection());
				ViewRenderDescs.CloudDescs.Add(CloudRenderDesc);
				DispatchedActors.Add(CloudActor);
			}
		}

		if (ViewRenderDescs.CloudDescs.IsEmpty())
		{
			continue;
		}

		ViewRenderDescs.CloudDescs.Sort(
			[](const FPendingCloudRenderDesc& Left, const FPendingCloudRenderDesc& Right)
			{
				return Left.SortDepth > Right.SortDepth;
			});

		if (bCopyDebugTexture || bCaptureComputeCloud)
		{
			FIntPoint DebugOutputSize = FamilyRenderTargetSize;
			if (DebugOutputSize.X <= 0 || DebugOutputSize.Y <= 0)
			{
				DebugOutputSize = View->UnscaledViewRect.Size();
			}
			DebugOutputSize.X = FMath::Max(DebugOutputSize.X, 1);
			DebugOutputSize.Y = FMath::Max(DebugOutputSize.Y, 1);
			ViewRenderDescs.DebugOutputSize = DebugOutputSize;
			ViewRenderDescs.DebugFrameIndex = ViewRenderDescs.CloudDescs[0].RenderDesc.FrameIndex;

			if (Settings && bCopyDebugTexture)
			{
				Settings->EnsureSceneViewDebugPreviewRenderTarget(DebugOutputSize);
				if (UTextureRenderTarget2D* DebugRenderTarget = Settings->SceneViewDebugPreviewRenderTarget)
				{
					if (FTextureRenderTargetResource* DebugResource = DebugRenderTarget->GameThread_GetRenderTargetResource())
					{
						const FTextureRHIRef DebugTexture = DebugResource->GetRenderTargetTexture();
						if (DebugTexture.IsValid())
						{
							ViewRenderDescs.DebugOutputTexture = DebugTexture;
							Settings->MarkSceneViewDebugPreviewUpdated(TEXT("MRBNN.CombinedSceneCloud"), DebugOutputSize, ViewRenderDescs.DebugFrameIndex, ViewRenderDescs.CloudDescs.Num());
							if (Settings->bLogSceneViewDebugRenderTarget)
							{
								UE_LOG(LogTemp, Log, TEXT("MRBNN SceneView debug RT queued: %dx%d clouds=%d frame=%d"),
									DebugOutputSize.X,
									DebugOutputSize.Y,
									ViewRenderDescs.CloudDescs.Num(),
									ViewRenderDescs.DebugFrameIndex);
							}
						}
					}
				}
			}
		}

		RenderDescs.Add(MoveTemp(ViewRenderDescs));
	}

	if (RenderDescs.IsEmpty())
	{
		return;
	}

	const double DispatchStartSeconds = FPlatformTime::Seconds();
	for (AMRBNNVolumeActor* DispatchedActor : DispatchedActors)
	{
		if (DispatchedActor && DispatchedActor->MRBNNVolume)
		{
			DispatchedActor->MRBNNVolume->MarkComputeVolumeRenderDispatched(DispatchStartSeconds);
		}
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
	FPendingViewRenderDescs RenderDescs;
	{
		FScopeLock Lock(&PendingRenderDescsCriticalSection);
		if (!PendingRenderDescs.IsEmpty())
		{
			RenderDescs = MoveTemp(PendingRenderDescs[0]);
			PendingRenderDescs.RemoveAt(0, 1, EAllowShrinking::No);
		}
	}

	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	if (!SceneColor.IsValid() || RenderDescs.CloudDescs.IsEmpty())
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	const FIntPoint OutputExtent = SceneColor.Texture->Desc.Extent;
	const FRDGTextureDesc CloudTextureDesc = FRDGTextureDesc::Create2D(
		OutputExtent,
		PF_FloatRGBA,
		FClearValueBinding::Transparent,
		TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef CombinedCloudTexture = nullptr;

	for (int32 CloudIndex = 0; CloudIndex < RenderDescs.CloudDescs.Num(); ++CloudIndex)
	{
		FMRBNNComputeRenderer::FRenderDesc& RenderDesc = RenderDescs.CloudDescs[CloudIndex].RenderDesc;
		if (!RenderDesc.DensityTexture.IsValid())
		{
			continue;
		}

		FRDGTextureRef CloudTexture = GraphBuilder.CreateTexture(CloudTextureDesc, TEXT("MRBNN.SceneCloud"));
		RenderDesc.OutputSize = OutputExtent;
		RenderDesc.bCompositeOutput = true;
		FMRBNNComputeRenderer::AddRenderPassToRDGTexture(GraphBuilder, RenderDesc, CloudTexture, TEXT("MRBNN.ComputeVolumeRender.PostProcess"));

		if (!CombinedCloudTexture)
		{
			CombinedCloudTexture = CloudTexture;
			continue;
		}

		FRDGTextureRef NewCombinedCloudTexture = GraphBuilder.CreateTexture(CloudTextureDesc, TEXT("MRBNN.CombinedSceneCloud"));
		TShaderMapRef<FMRBNNCloudOverCS> CloudOverShader(GetGlobalShaderMap(View.GetFeatureLevel()));
		FMRBNNCloudOverCS::FParameters* CloudOverParameters = GraphBuilder.AllocParameters<FMRBNNCloudOverCS::FParameters>();
		CloudOverParameters->BackCloudTexture = CombinedCloudTexture;
		CloudOverParameters->FrontCloudTexture = CloudTexture;
		CloudOverParameters->CompositeSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		CloudOverParameters->CompositeOutputTexture = GraphBuilder.CreateUAV(NewCombinedCloudTexture);
		CloudOverParameters->CompositeOutputSize = OutputExtent;

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("MRBNN.CloudOver"),
			CloudOverShader,
			CloudOverParameters,
			FComputeShaderUtils::GetGroupCount(OutputExtent, FMRBNNCloudOverCS::GroupSize));

		CombinedCloudTexture = NewCombinedCloudTexture;
	}

	if (!CombinedCloudTexture)
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	if (RenderDescs.DebugOutputTexture.IsValid())
	{
		FRDGTextureRef DebugTexture = RegisterExternalTexture(GraphBuilder, RenderDescs.DebugOutputTexture.GetReference(), TEXT("MRBNN.ProjectSceneViewDebugRT"));
		if (DebugTexture)
		{
			FRHICopyTextureInfo CopyInfo;
			CopyInfo.Size = FIntVector(
				FMath::Min(OutputExtent.X, FMath::Max(RenderDescs.DebugOutputSize.X, 1)),
				FMath::Min(OutputExtent.Y, FMath::Max(RenderDescs.DebugOutputSize.Y, 1)),
				1);
			AddCopyTexturePass(GraphBuilder, CombinedCloudTexture, DebugTexture, CopyInfo);
		}
	}

	const FRDGTextureDesc CompositeTextureDesc = FRDGTextureDesc::Create2D(
		OutputExtent,
		PF_FloatRGBA,
		FClearValueBinding::Black,
		TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef CompositeTexture = GraphBuilder.CreateTexture(CompositeTextureDesc, TEXT("MRBNN.SceneComposite"));

	TShaderMapRef<FMRBNNCompositeCS> CompositeShader(GetGlobalShaderMap(View.GetFeatureLevel()));
	FMRBNNCompositeCS::FParameters* Parameters = GraphBuilder.AllocParameters<FMRBNNCompositeCS::FParameters>();
	Parameters->SceneColorTexture = SceneColor.Texture;
	Parameters->CloudTexture = CombinedCloudTexture;
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

	FRDGTextureRef FinalTexture = CompositeTexture;
	if (RenderDescs.DebugDisplayMode != static_cast<int32>(EMRBNNSceneViewDebugDisplayMode::Hidden))
	{
		FRDGTextureRef DebugViewTexture = GraphBuilder.CreateTexture(CompositeTextureDesc, TEXT("MRBNN.DebugViewComposite"));
		TShaderMapRef<FMRBNNDebugViewCS> DebugViewShader(GetGlobalShaderMap(View.GetFeatureLevel()));
		FMRBNNDebugViewCS::FParameters* DebugViewParameters = GraphBuilder.AllocParameters<FMRBNNDebugViewCS::FParameters>();
		DebugViewParameters->SceneColorTexture = CompositeTexture;
		DebugViewParameters->DebugTexture = CombinedCloudTexture;
		DebugViewParameters->CompositeSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		DebugViewParameters->CompositeOutputTexture = GraphBuilder.CreateUAV(DebugViewTexture);
		DebugViewParameters->CompositeOutputSize = OutputExtent;
		DebugViewParameters->DebugDisplayMode = RenderDescs.DebugDisplayMode;
		DebugViewParameters->DebugPreviewOpacity = FMath::Clamp(RenderDescs.DebugPreviewOpacity, 0.0f, 1.0f);
		DebugViewParameters->DebugOverlayScale = FMath::Clamp(RenderDescs.DebugOverlayScale, 0.1f, 1.0f);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("MRBNN.DebugView"),
			DebugViewShader,
			DebugViewParameters,
			FComputeShaderUtils::GetGroupCount(OutputExtent, FMRBNNDebugViewCS::GroupSize));

		FinalTexture = DebugViewTexture;
	}

	const FScreenPassTexture CompositeOutput(FinalTexture, SceneColor.ViewRect);
	if (Inputs.OverrideOutput.IsValid())
	{
		AddDrawTexturePass(GraphBuilder, FScreenPassViewInfo(View), CompositeOutput, Inputs.OverrideOutput);
		return Inputs.OverrideOutput;
	}

	return CompositeOutput;
}

bool FMRBNNSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	AMRBNNVolumeActor* VolumeActor = Actor.Get();
	if (!VolumeActor || !IsRenderableMRBNNActor(VolumeActor))
	{
		return false;
	}

	UWorld* ActorWorld = VolumeActor->GetWorld();
	UWorld* ContextWorld = Context.GetWorld();
	if (ContextWorld && ContextWorld != ActorWorld)
	{
		return false;
	}

	return FindSceneViewExtensionLeader(ActorWorld) == VolumeActor;
}
