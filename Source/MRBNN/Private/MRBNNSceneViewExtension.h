#pragma once

#include "CoreMinimal.h"
#include "MRBNNComputeRenderer.h"
#include "SceneViewExtension.h"

class AMRBNNVolumeActor;

class FMRBNNSceneViewExtension final : public FSceneViewExtensionBase
{
public:
	FMRBNNSceneViewExtension(const FAutoRegister& AutoRegister, TWeakObjectPtr<AMRBNNVolumeActor> InActor);

	void SetActor(TWeakObjectPtr<AMRBNNVolumeActor> InActor);

	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;

protected:
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

private:
	struct FPendingCloudRenderDesc
	{
		FMRBNNComputeRenderer::FRenderDesc RenderDesc;
		float SortDepth = 0.0f;
	};

	struct FPendingViewRenderDescs
	{
		TArray<FPendingCloudRenderDesc> CloudDescs;
		FTextureRHIRef DebugOutputTexture;
		FIntPoint DebugOutputSize = FIntPoint::ZeroValue;
		int32 DebugFrameIndex = 0;
		int32 DebugDisplayMode = 0;
		float DebugPreviewOpacity = 1.0f;
		float DebugOverlayScale = 0.35f;
	};

	FScreenPassTexture PostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs);

	TWeakObjectPtr<AMRBNNVolumeActor> Actor;
	mutable FCriticalSection PendingRenderDescsCriticalSection;
	TArray<FPendingViewRenderDescs> PendingRenderDescs;
};
