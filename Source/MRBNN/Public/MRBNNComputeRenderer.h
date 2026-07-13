#pragma once

#include "CoreMinimal.h"
#include "MRBNNTypes.h"
#include "RenderGraphFwd.h"

class FRDGBuilder;

class FMRBNNComputeRenderer
{
public:
	struct FRenderDesc
	{
		FTextureRHIRef DensityTexture;
		FTextureRHIRef FeatureTexture;
		FTextureRHIRef OutputTexture;
		FIntPoint OutputSize = FIntPoint::ZeroValue;
		FMRBNNRenderSettings RenderSettings;
		FMRBNNComputeVolumeSettings ComputeSettings;
		FVector3f CameraPosition = FVector3f::ZeroVector;
		FVector3f CameraForward = FVector3f(1.0f, 0.0f, 0.0f);
		FVector3f CameraRight = FVector3f(0.0f, 1.0f, 0.0f);
		FVector3f CameraUp = FVector3f(0.0f, 0.0f, 1.0f);
		FVector2f TanHalfFov = FVector2f(0.48f, 0.48f);
		bool bUseExplicitCamera = false;
		bool bCompositeOutput = false;
		int32 FrameIndex = 0;
	};

	static bool AddRenderPass(FRDGBuilder& GraphBuilder, const FRenderDesc& Desc, const TCHAR* EventName = TEXT("MRBNN.ComputeVolumeRender"));
	static bool AddRenderPassToRDGTexture(FRDGBuilder& GraphBuilder, const FRenderDesc& Desc, FRDGTextureRef OutputTexture, const TCHAR* EventName = TEXT("MRBNN.ComputeVolumeRender"));
	static bool EnqueueRender(const FRenderDesc& Desc);
};
