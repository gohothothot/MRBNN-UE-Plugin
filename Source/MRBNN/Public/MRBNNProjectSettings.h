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

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Debug")
	bool bEnableVerboseLogging = false;

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

	UFUNCTION(BlueprintCallable, Category = "MRBNN|Paths")
	static FString ResolveMRBNNPath(const FString& PathWithTokens);

	UFUNCTION(BlueprintCallable, Category = "MRBNN|Data")
	UMRBNNBakedVolumeData* CreateTransientDefaultBakedData(UObject* Outer, UPARAM(ref) FText& OutError) const;

	bool ResolveDefaultDataSet(FString& OutRepositoryRoot, FString& OutWorkingDirectory, FString& OutSkyboxPath, FString& OutSkyboxBakingDirectory, FText& OutError) const;
	void ApplyRealtimePreset(UMRBNNVolumeComponent& Component) const;
};
