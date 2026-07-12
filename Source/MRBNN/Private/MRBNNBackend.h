#pragma once

#include "CoreMinimal.h"
#include "Math/Float16Color.h"
#include "MRBNNTypes.h"

class IMRBNNBackend
{
public:
	virtual ~IMRBNNBackend() = default;

	virtual bool Initialize(const FString& WorkingDirectory, const FString& RepositoryRoot, const FString& SkyboxPath, float SkyboxExposure, const FString& SkyboxBakingDirectory, FText& OutError) = 0;
	virtual void Shutdown() = 0;
	virtual bool Render(int32 Width, int32 Height, int32 FrameIndex, const FMRBNNRenderSettings& Settings, TArray<FFloat16Color>& OutPixels, FText& OutError) = 0;
};

TUniquePtr<IMRBNNBackend> CreateMRBNNBackend();
