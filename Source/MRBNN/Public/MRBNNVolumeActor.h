#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MRBNNComputeRenderer.h"
#include "MRBNNVolumeActor.generated.h"

class ADirectionalLight;
class FMRBNNSceneViewExtension;
class FSceneView;
class UBoxComponent;
class UMRBNNBakedVolumeData;
class UMRBNNVolumeComponent;
class USceneComponent;
class UVolumeTexture;

UCLASS(BlueprintType, Blueprintable)
class MRBNN_API AMRBNNVolumeActor : public AActor
{
	GENERATED_BODY()
	friend class FMRBNNSceneViewExtension;

public:
	AMRBNNVolumeActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRBNN|Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRBNN|Components")
	TObjectPtr<UBoxComponent> VolumeBounds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRBNN|Components")
	TObjectPtr<UMRBNNVolumeComponent> MRBNNVolume;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Data / Baking", meta = (DisplayName = "Baked Data"))
	TObjectPtr<UMRBNNBakedVolumeData> BakedData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Data / Baking", meta = (DisplayName = "Use Project Settings When Data Missing"))
	bool bUseProjectSettingsWhenBakedDataMissing = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Data / Baking", meta = (DisplayName = "Configure From Project Settings On Construction"))
	bool bAutoConfigureFromProjectSettings = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Data / Baking", meta = (DisplayName = "Auto Rebuild When Parameters Change"))
	bool bAutoRebuildOnParameterChange = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "MRBNN|Data / Baking", meta = (DisplayName = "Resolved Repository Root"))
	FString ResolvedRepositoryRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "MRBNN|Data / Baking", meta = (DisplayName = "Resolved Working Directory"))
	FString ResolvedWorkingDirectory;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "MRBNN|Data / Baking", meta = (DisplayName = "Resolved Density Volume"))
	FString ResolvedDensityVolumePath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Preview", meta = (DisplayName = "Render On Begin Play"))
	bool bAutoRenderOnBeginPlay = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Preview", meta = (DisplayName = "Render Once In Editor"))
	bool bAutoRenderEditorPreviewOnce = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Preview", meta = (AdvancedDisplay, DisplayName = "Allow Live Render In Editor"))
	bool bAllowLiveRenderInEditor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Preview", meta = (DisplayName = "Use Compute Volume Renderer"))
	bool bUseComputeGlobalShader = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Preview", meta = (EditCondition = "bUseComputeGlobalShader", DisplayName = "Composite In Scene View"))
	bool bUseSceneViewExtensionRenderPass = true;

	UPROPERTY(Transient)
	bool bUseRaymarchShader = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Cloud Shape", meta = (ClampMin = "1.0", UIMin = "50.0", UIMax = "500.0", DisplayName = "Cloud Box Extent"))
	FVector VolumeExtent = FVector(240.0f, 260.0f, 95.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Cloud Shape", meta = (DisplayName = "Fit Texture To Active Density Bounds"))
	bool bRaymarchFitToDensityBounds = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Cloud Shape", meta = (AdvancedDisplay, DisplayName = "Use MRBNN Cloud Axis Mapping"))
	bool bUseMRBNNCloudAxisMapping = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Cloud Shape", meta = (EditCondition = "bRaymarchFitToDensityBounds", ClampMin = "0.0", UIMin = "0.0", UIMax = "32.0", DisplayName = "Density Bounds Threshold"))
	float RaymarchBoundsThreshold = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Cloud Shape", meta = (EditCondition = "bRaymarchFitToDensityBounds", ClampMin = "0.0", ClampMax = "0.25", UIMin = "0.02", UIMax = "0.15", DisplayName = "Density Bounds Padding"))
	float RaymarchBoundsPadding = 0.14f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Density", meta = (ClampMin = "16", ClampMax = "128", UIMin = "32", UIMax = "96", DisplayName = "Volume Texture Resolution"))
	int32 RaymarchTextureResolution = 80;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Density", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "20.0", DisplayName = "Density Input Threshold"))
	float RaymarchInputThreshold = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Density", meta = (ClampMin = "1.0", UIMin = "20.0", UIMax = "180.0", DisplayName = "Density Normalize"))
	float RaymarchNormalizeDensity = 96.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Density", meta = (ClampMin = "0.1", UIMin = "0.3", UIMax = "2.0", DisplayName = "Density Power"))
	float RaymarchDensityPower = 0.82f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Density", meta = (ClampMin = "0.0", UIMin = "0.01", UIMax = "0.25", DisplayName = "Volume Opacity"))
	float RaymarchOpacity = 0.055f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Density", meta = (DisplayName = "Cloud Color"))
	FLinearColor RaymarchCloudColor = FLinearColor(0.86f, 0.90f, 0.92f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Quality", meta = (ClampMin = "4", ClampMax = "96", UIMin = "16", UIMax = "64", DisplayName = "Base Step Count"))
	int32 RaymarchStepCount = 40;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Quality", meta = (ClampMin = "1", UIMin = "128", UIMax = "2048", DisplayName = "Output Width"))
	int32 OutputWidth = 384;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Quality", meta = (ClampMin = "1", UIMin = "128", UIMax = "2048", DisplayName = "Output Height"))
	int32 OutputHeight = 384;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Quality", meta = (ClampMin = "1", UIMin = "1", UIMax = "64", DisplayName = "Samples Per Render"))
	int32 SamplesPerRender = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Quality", meta = (DisplayName = "Accumulate Frames"))
	bool bAccumulateFrames = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Quality", meta = (ClampMin = "1", UIMin = "1", UIMax = "512", EditCondition = "bAccumulateFrames", DisplayName = "Max Accumulated Frames"))
	int32 MaxAccumulatedFrames = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Quality", meta = (ClampMin = "0", ClampMax = "4", UIMin = "0", UIMax = "2", DisplayName = "Spatial Denoise Passes"))
	int32 SpatialDenoisePasses = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Lighting", meta = (DisplayName = "Use Directional Light"))
	bool bUseDirectionalLightForRelight = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Lighting", meta = (EditCondition = "bUseDirectionalLightForRelight", DisplayName = "Auto Find Directional Light"))
	bool bAutoFindDirectionalLight = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Lighting", meta = (EditCondition = "bUseDirectionalLightForRelight", DisplayName = "Directional Light Actor"))
	TObjectPtr<ADirectionalLight> DirectionalLightActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Lighting", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0", DisplayName = "Ambient Light"))
	float AmbientRelight = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Lighting", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "3.0", DisplayName = "Directional Light"))
	float DirectionalRelight = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Lighting", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "4.0", DisplayName = "Directional Intensity Scale"))
	float RaymarchDirectLightIntensityScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Lighting", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0", DisplayName = "Shadow Strength"))
	float RaymarchShadowStrength = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Lighting", meta = (ClampMin = "0", ClampMax = "8", UIMin = "0", UIMax = "6", DisplayName = "Direct Shadow Steps"))
	int32 RaymarchDirectShadowSteps = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Lighting", meta = (ClampMin = "0.0", UIMin = "0.1", UIMax = "4.0", DisplayName = "Direct Shadow Density"))
	float RaymarchDirectShadowDensity = 1.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Lighting", meta = (ClampMin = "0.0", UIMin = "0.01", UIMax = "0.25", DisplayName = "Light Step"))
	float RaymarchLightStep = 0.075f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Lighting", meta = (ClampMin = "-0.85", ClampMax = "0.85", UIMin = "-0.2", UIMax = "0.75", DisplayName = "Phase G"))
	float RaymarchPhaseG = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Lighting", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", DisplayName = "Phase Strength"))
	float RaymarchPhaseStrength = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Lighting", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "1.2", DisplayName = "Edge Silver Strength"))
	float RaymarchEdgeSilverStrength = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Lighting", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "1.2", DisplayName = "Deep Shadow Strength"))
	float RaymarchDeepShadowStrength = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Lighting", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "1.2", DisplayName = "Powder Strength"))
	float RaymarchPowderStrength = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Baked Feature Proxy", meta = (DisplayName = "Use Baked MRBNN Feature Lighting"))
	bool bUseBakedFeatureLighting = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Baked Feature Proxy", meta = (ClampMin = "0", ClampMax = "3", UIMin = "0", UIMax = "3", DisplayName = "Baked Feature Level"))
	int32 RaymarchBakedFeatureLevel = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Baked Feature Proxy", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "1.5", DisplayName = "Baked Feature Contribution"))
	float RaymarchBakedFeatureContribution = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Baked Feature Proxy", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "1.5", DisplayName = "Multi-Scatter Contribution"))
	float RaymarchMultiScatterContribution = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Baked Feature Proxy", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", DisplayName = "Feature Albedo Blend"))
	float RaymarchFeatureAlbedoBlend = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Baked Feature Proxy", meta = (DisplayName = "Baked Feature Tint"))
	FLinearColor RaymarchBakedFeatureTint = FLinearColor(1.0f, 0.965f, 0.88f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Density", meta = (ClampMin = "0.0", UIMin = "0.1", UIMax = "8.0", DisplayName = "Brightness"))
	float PreviewBrightness = 1.8f;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Data / Baking", meta = (DisplayName = "Use Project Settings Data"))
	void ConfigureFromProjectSettings();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Data / Baking", meta = (DisplayName = "Package Current Data To Plugin Data"))
	bool BakeCurrentDataToPluginData();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Data / Baking", meta = (DisplayName = "Bake/Rebuild Compute Volume"))
	bool RebuildVolumeShader();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Preview", meta = (DisplayName = "Render Preview Once"))
	bool RenderPreviewOnce();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Quality")
	void ApplyRealtimePreviewSettings();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Quality")
	void ApplyMobilePreviewSettings();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Quality")
	void ApplyPaperPreviewSettings();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Quality")
	void ResetAccumulation();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Data / Baking", meta = (DeprecatedFunction, DeprecationMessage = "Use Bake/Rebuild Compute Volume."))
	bool RebuildDensityVolumePreview();

protected:
	void SyncComponentSettingsFromActor();
	UMRBNNBakedVolumeData* ResolveActiveBakedData();
	void RefreshResolvedDataFields();
	void ResetRenderState();
	void UpdateVolumeProxy();
	bool BuildDensityVolumePreview();
	bool BuildRaymarchVolumeTexture();
	bool RenderComputeGlobalShaderPreview();
	bool BuildComputeRenderDescForView(const FSceneView& View, FMRBNNComputeRenderer::FRenderDesc& OutDesc);
	FMRBNNComputeVolumeSettings MakeComputeVolumeSettings() const;
	void EnsureComputeViewExtension();
	bool ShouldUseSceneViewExtensionRenderPass() const;
	void UpdateRelightFromDirectionalLight();
	void UpdateVolumeMaterial();
	void UpdateRaymarchMaterial();
	void UpdateDebugText();
	void MaybeRenderEditorPreviewOnce();
	bool ResolveDensityVolumeFile(FString& OutVolumePath, FIntVector& OutResolution, int32& OutSkipByteCount, FText& OutError);
	bool HasRenderedPreviewTexture() const;
	ADirectionalLight* FindDirectionalLight() const;

	UPROPERTY(Transient)
	TObjectPtr<UVolumeTexture> RaymarchDensityTexture;

	UPROPERTY(Transient)
	TObjectPtr<UVolumeTexture> RaymarchFeatureTexture;

	TSharedPtr<FMRBNNSceneViewExtension, ESPMode::ThreadSafe> ComputeViewExtension;
	FString RaymarchTextureBuildKey;
	double NextPresentationRefreshTimeSeconds = 0.0;
	FLinearColor CurrentRaymarchDirectLightColor = FLinearColor::White;
	float CurrentRaymarchDirectLightIntensity = 1.0f;
	FVector LastRelightDirection = FVector::ZeroVector;
	FLinearColor LastRelightColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
	float LastRelightIntensity = -1.0f;
	bool bHasLastRelightState = false;
	bool bEditorPreviewRenderAttempted = false;
	bool bDensityPreviewBuilt = false;
	bool bRaymarchTextureBuilt = false;
};
