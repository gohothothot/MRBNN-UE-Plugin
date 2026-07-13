#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Math/Float16Color.h"
#include "MRBNNBakedVolumeData.h"
#include "MRBNNComputeRenderer.h"
#include "MRBNNTypes.h"
#include "MRBNNVolumeComponent.generated.h"

class UTexture2D;
class UTextureRenderTarget2D;
class UVolumeTexture;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class IMRBNNBackend;

UCLASS(ClassGroup = (Rendering), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class MRBNN_API UMRBNNVolumeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMRBNNVolumeComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MRBNN")
	TObjectPtr<UMRBNNBakedVolumeData> BakedData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN")
	bool bUseProjectSettingsWhenBakedDataMissing = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRBNN", meta = (ClampMin = "1", UIMin = "64", UIMax = "2048"))
	int32 OutputWidth = 1024;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRBNN", meta = (ClampMin = "1", UIMin = "64", UIMax = "2048"))
	int32 OutputHeight = 1024;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN")
	bool bAutoInitialize = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN", meta = (EditCondition = "bAutoInitialize", AdvancedDisplay))
	bool bRetryFailedAutoInitialize = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN")
	bool bRenderEveryTick = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRBNN|Quality", meta = (ClampMin = "1", UIMin = "1", UIMax = "64"))
	int32 SamplesPerRender = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRBNN|Quality")
	bool bAccumulateFrames = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRBNN|Quality", meta = (ClampMin = "1", UIMin = "1", UIMax = "512"))
	int32 MaxAccumulatedFrames = 64;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRBNN|Quality", meta = (ClampMin = "0", ClampMax = "4", UIMin = "0", UIMax = "2"))
	int32 SpatialDenoisePasses = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN", meta = (AdvancedDisplay))
	bool bAllowAutomaticRenderInEditor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Camera")
	bool bUsePlayerCamera = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Camera", meta = (ClampMin = "0.001", UIMin = "1.0", UIMax = "1000.0"))
	float WorldUnitsPerMRBNNUnit = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN")
	FMRBNNRenderSettings RenderSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Material")
	bool bApplyOutputToMaterials = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Material", meta = (EditCondition = "bApplyOutputToMaterials"))
	FName OutputTextureParameterName = TEXT("MRBNNTexture");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Material", meta = (EditCondition = "bApplyOutputToMaterials"))
	TArray<TObjectPtr<UPrimitiveComponent>> TargetMaterialComponents;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Material", meta = (EditCondition = "bApplyOutputToMaterials"))
	bool bUseOwnerPrimitiveComponentsWhenTargetsEmpty = true;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MRBNN")
	TObjectPtr<UTexture2D> OutputTexture;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MRBNN")
	TObjectPtr<UTextureRenderTarget2D> OutputRenderTarget;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MRBNN|Runtime")
	int32 LastFrameIndex = 0;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MRBNN|Runtime")
	int32 LastRenderedSampleCount = 0;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MRBNN|Runtime")
	int32 LastAccumulatedFrameCount = 0;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MRBNN|Runtime")
	float LastBackendRenderTimeMs = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MRBNN|Runtime")
	float LastDenoiseTimeMs = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MRBNN|Runtime")
	float LastTextureUploadTimeMs = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MRBNN|Runtime")
	float LastMaterialApplyTimeMs = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MRBNN|Runtime")
	float LastTotalRenderTimeMs = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MRBNN|Runtime")
	float LastEstimatedSamplesPerSecond = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MRBNN|Runtime")
	FString LastResolvedWorkingDirectory;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MRBNN|Runtime")
	FString LastResolvedRepositoryRoot;

	UFUNCTION(BlueprintCallable, Category = "MRBNN")
	bool InitializeRenderer();

	UFUNCTION(BlueprintCallable, Category = "MRBNN")
	void ReleaseRenderer();

	UFUNCTION(BlueprintCallable, Category = "MRBNN")
	bool RenderOnce();

	UFUNCTION(BlueprintCallable, Category = "MRBNN|Compute")
	bool RenderComputeVolumeOnce(UVolumeTexture* DensityTexture, UVolumeTexture* FeatureTexture, const FMRBNNComputeVolumeSettings& ComputeSettings);

	bool BuildComputeVolumeRenderDesc(UVolumeTexture* DensityTexture, UVolumeTexture* FeatureTexture, const FMRBNNComputeVolumeSettings& ComputeSettings, FMRBNNComputeRenderer::FRenderDesc& OutDesc);
	void MarkComputeVolumeRenderDispatched(double DispatchStartSeconds);

	UFUNCTION(BlueprintCallable, Category = "MRBNN|Quality")
	void ResetProgressiveAccumulation();

	UFUNCTION(BlueprintCallable, Category = "MRBNN|Quality")
	void ApplyGamePreviewSettings();

	UFUNCTION(BlueprintCallable, Category = "MRBNN|Quality")
	void ApplyRealtimePreviewSettings();

	UFUNCTION(BlueprintCallable, Category = "MRBNN|Quality")
	void ApplyMobilePreviewSettings();

	UFUNCTION(BlueprintCallable, Category = "MRBNN|Quality")
	void ApplyBalancedPreviewSettings();

	UFUNCTION(BlueprintCallable, Category = "MRBNN|Quality")
	void ApplyHighQualityPreviewSettings();

	UFUNCTION(BlueprintCallable, Category = "MRBNN|Quality")
	void ApplyPaperPreviewSettings();

	UFUNCTION(BlueprintCallable, Category = "MRBNN")
	int32 GetAccumulatedFrameCount() const { return AccumulatedFrameCount; }

	UFUNCTION(BlueprintCallable, Category = "MRBNN")
	FString GetDebugSummary() const;

	UFUNCTION(BlueprintCallable, Category = "MRBNN")
	void SetCameraFromWorldLocation(const FVector& WorldLocation);

	UFUNCTION(BlueprintCallable, Category = "MRBNN")
	UTexture2D* GetOutputTexture() const { return OutputTexture; }

	UFUNCTION(BlueprintCallable, Category = "MRBNN")
	UTextureRenderTarget2D* GetOutputRenderTarget() const { return OutputRenderTarget; }

	UFUNCTION(BlueprintCallable, Category = "MRBNN")
	void ApplyOutputToMaterialTargets();

	UFUNCTION(BlueprintCallable, Category = "MRBNN")
	bool IsRendererInitialized() const { return Backend != nullptr; }

	UFUNCTION(BlueprintCallable, Category = "MRBNN")
	FText GetLastError() const { return LastError; }

protected:
	virtual void BeginDestroy() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	bool CanRunAutomaticRenderingInCurrentWorld() const;
	void EnsureOutputTexture();
	void EnsureOutputRenderTarget();
	void RefreshCameraFromPlayerView();
	void AccumulateSample(TConstArrayView<FFloat16Color> SamplePixels, TArray<FLinearColor>& InOutAveragePixels, int32& InOutFrameCount, int32 MaxFrameCount) const;
	void ApplySpatialDenoise(TArray<FLinearColor>& InOutPixels) const;
	void ConvertAverageToFloat16(TConstArrayView<FLinearColor> AveragePixels, TArray<FFloat16Color>& OutPixels) const;
	void UploadPixelsToOutputTexture(TConstArrayView<FFloat16Color> Pixels);
	void UploadPixelsToOutputRenderTarget(TConstArrayView<FFloat16Color> Pixels);
	void SetLastError(const FText& Error);

	IMRBNNBackend* Backend = nullptr;
	FString ResolvedWorkingDirectory;
	int32 FrameIndex = 0;
	int32 AccumulatedFrameCount = 0;
	bool bAutoInitializeAttempted = false;
	FText LastError;

	TArray<FLinearColor> AccumulatedAveragePixels;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DynamicMaterialInstances;

	UPROPERTY(Transient)
	TObjectPtr<UMRBNNBakedVolumeData> RuntimeProjectSettingsBakedData;
};
