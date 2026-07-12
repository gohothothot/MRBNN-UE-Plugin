#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "MRBNNTypes.h"
#include "MRBNNProjectSettings.generated.h"

class UMRBNNBakedVolumeData;
class UMRBNNVolumeComponent;

UCLASS(Config = Engine, DefaultConfig, DisplayName = "MRBNN")
class MRBNN_API UMRBNNProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMRBNNProjectSettings();

	static const UMRBNNProjectSettings* Get();

	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Data")
	TSoftObjectPtr<UMRBNNBakedVolumeData> DefaultBakedDataAsset;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Data")
	FDirectoryPath DefaultRepositoryRoot;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Data")
	FDirectoryPath DefaultDataRoot;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Data")
	FString DefaultSceneName;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Data")
	FFilePath DefaultSkyboxHDRI;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Data")
	bool bEnableDefaultSkybox = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Data")
	bool bEnableDefaultSkyboxBaking = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Baking")
	FDirectoryPath BakeDestinationRepositoryRoot;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Realtime", meta = (ClampMin = "64", UIMin = "128", UIMax = "1024"))
	int32 RealtimeOutputWidth = 384;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Realtime", meta = (ClampMin = "64", UIMin = "128", UIMax = "1024"))
	int32 RealtimeOutputHeight = 384;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Realtime", meta = (ClampMin = "1", UIMin = "1", UIMax = "8"))
	int32 RealtimeSamplesPerRender = 1;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Realtime")
	bool bRealtimeAccumulateFrames = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Realtime", meta = (ClampMin = "1", UIMin = "1", UIMax = "64"))
	int32 RealtimeMaxAccumulatedFrames = 12;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Realtime", meta = (ClampMin = "0", ClampMax = "2", UIMin = "0", UIMax = "2"))
	int32 RealtimeSpatialDenoisePasses = 0;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Realtime")
	bool bRealtimeFastDirectIllumination = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Rendering")
	FMRBNNRenderSettings DefaultRenderSettings;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "1", UIMin = "4", UIMax = "32"))
	int32 VolumeSliceCount = 12;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "1.0", UIMin = "50.0", UIMax = "500.0"))
	FVector VolumeExtent = FVector(120.0f, 220.0f, 160.0f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume")
	bool bVolumeShowBillboard = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume")
	bool bVolumeUseRaymarchShader = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume")
	bool bVolumeShowDensityPreview = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume")
	bool bVolumeFitDensityPreviewToBounds = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.01", UIMax = "0.25"))
	float VolumeSliceOpacity = 0.075f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "0.0", UIMin = "0.1", UIMax = "8.0"))
	float VolumeBrightness = 1.8f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "16", ClampMax = "128", UIMin = "32", UIMax = "96"))
	int32 VolumeRaymarchTextureResolution = 80;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume")
	bool bVolumeRaymarchFitToDensityBounds = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (EditCondition = "bVolumeRaymarchFitToDensityBounds", ClampMin = "0.0", UIMin = "0.0", UIMax = "32.0"))
	float VolumeRaymarchBoundsThreshold = 4.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (EditCondition = "bVolumeRaymarchFitToDensityBounds", ClampMin = "0.0", ClampMax = "0.25", UIMin = "0.02", UIMax = "0.15"))
	float VolumeRaymarchBoundsPadding = 0.08f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "4", ClampMax = "96", UIMin = "16", UIMax = "64"))
	int32 VolumeRaymarchStepCount = 40;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "20.0"))
	float VolumeRaymarchInputThreshold = 4.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "1.0", UIMin = "20.0", UIMax = "180.0"))
	float VolumeRaymarchNormalizeDensity = 96.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "0.1", UIMin = "0.3", UIMax = "2.0"))
	float VolumeRaymarchDensityPower = 0.82f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "0.0", UIMin = "0.01", UIMax = "0.25"))
	float VolumeRaymarchOpacity = 0.055f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
	float VolumeRaymarchShadowStrength = 0.55f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "0.0", UIMin = "0.01", UIMax = "0.25"))
	float VolumeRaymarchLightStep = 0.075f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume")
	FLinearColor VolumeRaymarchCloudColor = FLinearColor(0.86f, 0.90f, 0.92f, 1.0f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "8", ClampMax = "96", UIMin = "16", UIMax = "64"))
	int32 VolumeDensitySampleResolution = 48;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "1", ClampMax = "20000", UIMin = "512", UIMax = "12000"))
	int32 VolumeMaxDensityVoxelInstances = 4200;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "80.0"))
	float VolumeDensityThreshold = 10.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "0.1", UIMin = "0.5", UIMax = "5.0"))
	float VolumeDensityVoxelScale = 2.85f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.02", UIMax = "0.5"))
	float VolumeDensityVoxelOpacity = 0.17f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "0.1", ClampMax = "1.0", UIMin = "0.5", UIMax = "1.0"))
	float VolumeDensityBoundsFill = 0.88f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
	float VolumeAmbientRelight = 0.55f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
	float VolumeDirectionalRelight = 0.45f;

	UFUNCTION(BlueprintCallable, Category = "MRBNN|Paths")
	static FString ResolveMRBNNPath(const FString& PathWithTokens);

	UFUNCTION(BlueprintCallable, Category = "MRBNN|Data")
	UMRBNNBakedVolumeData* CreateTransientDefaultBakedData(UObject* Outer, UPARAM(ref) FText& OutError) const;

	bool ResolveDefaultDataSet(FString& OutRepositoryRoot, FString& OutWorkingDirectory, FString& OutSkyboxPath, FString& OutSkyboxBakingDirectory, FText& OutError) const;
	void ApplyRealtimePreset(UMRBNNVolumeComponent& Component) const;
};
