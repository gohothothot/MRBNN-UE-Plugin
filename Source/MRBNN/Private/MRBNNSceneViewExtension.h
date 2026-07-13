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
	FScreenPassTexture PostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs);

	TWeakObjectPtr<AMRBNNVolumeActor> Actor;
	mutable FCriticalSection PendingRenderDescsCriticalSection;
	TArray<FMRBNNComputeRenderer::FRenderDesc> PendingRenderDescs;
};
