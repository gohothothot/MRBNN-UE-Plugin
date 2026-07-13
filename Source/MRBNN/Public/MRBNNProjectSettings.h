#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "MRBNNProjectSettings.generated.h"

class UMRBNNBakedVolumeData;
class UTextureRenderTarget2D;

UENUM(BlueprintType)
enum class EMRBNNSceneViewDebugRT : uint8
{
	Disabled UMETA(DisplayName = "Disabled"),
	ComputeCloud UMETA(DisplayName = "Compute Cloud")
};

UENUM(BlueprintType)
enum class EMRBNNSceneViewDebugDisplayMode : uint8
{
	Hidden UMETA(DisplayName = "Hidden"),
	Fullscreen UMETA(DisplayName = "Fullscreen"),
	Overlay UMETA(DisplayName = "Overlay")
};

UCLASS(Config = Engine, DefaultConfig, DisplayName = "MRBNN")
class MRBNN_API UMRBNNProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMRBNNProjectSettings();

	static const UMRBNNProjectSettings* Get();
	static UMRBNNProjectSettings* GetMutable();

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

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RT Debug")
	EMRBNNSceneViewDebugRT SceneViewDebugRenderTarget = EMRBNNSceneViewDebugRT::ComputeCloud;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RT Debug")
	EMRBNNSceneViewDebugDisplayMode SceneViewDebugDisplayMode = EMRBNNSceneViewDebugDisplayMode::Hidden;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RT Debug", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.15", UIMax = "1.0"))
	float SceneViewDebugPreviewOpacity = 1.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RT Debug", meta = (ClampMin = "0.1", ClampMax = "1.0", UIMin = "0.2", UIMax = "0.6", EditCondition = "SceneViewDebugDisplayMode == EMRBNNSceneViewDebugDisplayMode::Overlay"))
	float SceneViewDebugOverlayScale = 0.35f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RT Debug")
	bool bLogSceneViewDebugRenderTarget = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "RT Debug")
	TObjectPtr<UTextureRenderTarget2D> SceneViewDebugPreviewRenderTarget;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "RT Debug")
	FString LastSceneViewDebugRenderTargetName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "RT Debug")
	FIntPoint LastSceneViewDebugOutputSize = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "RT Debug")
	int32 LastSceneViewDebugFrameIndex = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "RT Debug")
	int32 LastSceneViewDebugCloudCount = 0;

	UFUNCTION(BlueprintCallable, Category = "MRBNN|Paths")
	static FString ResolveMRBNNPath(const FString& PathWithTokens);

	UFUNCTION(BlueprintCallable, Category = "MRBNN|Data")
	UMRBNNBakedVolumeData* CreateTransientDefaultBakedData(UObject* Outer, UPARAM(ref) FText& OutError) const;

	bool ResolveDefaultDataSet(FString& OutRepositoryRoot, FString& OutWorkingDirectory, FString& OutSkyboxPath, FString& OutSkyboxBakingDirectory, FText& OutError) const;

	void EnsureSceneViewDebugPreviewRenderTarget(FIntPoint OutputSize);
	void MarkSceneViewDebugPreviewUpdated(const FString& DebugName, FIntPoint OutputSize, int32 FrameIndex, int32 CloudCount);
};
