#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MRBNNComputeRenderer.h"
#include "MRBNNVolumeActor.generated.h"

class ADirectionalLight;
class FMRBNNSceneViewExtension;
class FSceneView;
class UBoxComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMRBNNVolumeComponent;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UTexture;
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRBNN")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRBNN")
	TObjectPtr<UBoxComponent> VolumeBounds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRBNN")
	TObjectPtr<UStaticMeshComponent> VolumeRaymarchMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRBNN")
	TObjectPtr<UStaticMeshComponent> VolumeBillboard;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRBNN")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> VolumeDensityVoxels;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRBNN")
	TObjectPtr<UInstancedStaticMeshComponent> VolumeSlices;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRBNN|Debug")
	TObjectPtr<UTextRenderComponent> DebugText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRBNN")
	TObjectPtr<UMRBNNVolumeComponent> MRBNNVolume;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Setup")
	bool bAutoConfigureFromProjectSettings = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Setup")
	bool bAutoRenderOnBeginPlay = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Setup")
	bool bAutoRenderEditorPreviewOnce = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Setup")
	bool bHideSlicesUntilFirstRender = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Setup")
	bool bUseFallbackPreviewBeforeRender = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Setup", meta = (AdvancedDisplay))
	bool bAllowLiveRenderInEditor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume", meta = (ClampMin = "1.0", UIMin = "50.0", UIMax = "500.0"))
	FVector VolumeExtent = FVector(120.0f, 220.0f, 160.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume")
	bool bShowVolumeBillboard = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume")
	bool bUseComputeGlobalShader = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume", meta = (EditCondition = "bUseComputeGlobalShader"))
	bool bUseSceneViewExtensionRenderPass = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume", meta = (AdvancedDisplay))
	bool bUseRaymarchShader = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume")
	bool bShowDensityVolume = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume")
	bool bFitDensityPreviewToBounds = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume", meta = (AdvancedDisplay))
	bool bUseExperimentalSliceStack = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume", meta = (ClampMin = "8", ClampMax = "96", UIMin = "16", UIMax = "64"))
	int32 DensitySampleResolution = 48;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume", meta = (ClampMin = "1", ClampMax = "20000", UIMin = "512", UIMax = "8000"))
	int32 MaxDensityVoxelInstances = 4200;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "80.0"))
	float DensityThreshold = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume", meta = (ClampMin = "0.1", UIMin = "0.5", UIMax = "5.0"))
	float DensityVoxelScale = 2.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.02", UIMax = "0.5"))
	float DensityVoxelOpacity = 0.17f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume", meta = (ClampMin = "0.1", ClampMax = "1.0", UIMin = "0.5", UIMax = "1.0"))
	float DensityBoundsFill = 0.88f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume", meta = (ClampMin = "1", UIMin = "4", UIMax = "32"))
	int32 SliceCount = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.01", UIMax = "0.25"))
	float SliceOpacity = 0.075f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume", meta = (ClampMin = "0.0", UIMin = "0.1", UIMax = "8.0"))
	float PreviewBrightness = 1.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume Shader", meta = (ClampMin = "16", ClampMax = "128", UIMin = "32", UIMax = "96"))
	int32 RaymarchTextureResolution = 80;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume Shader")
	bool bRaymarchFitToDensityBounds = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume Shader", meta = (EditCondition = "bRaymarchFitToDensityBounds", ClampMin = "0.0", UIMin = "0.0", UIMax = "32.0"))
	float RaymarchBoundsThreshold = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume Shader", meta = (EditCondition = "bRaymarchFitToDensityBounds", ClampMin = "0.0", ClampMax = "0.25", UIMin = "0.02", UIMax = "0.15"))
	float RaymarchBoundsPadding = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume Shader", meta = (ClampMin = "4", ClampMax = "96", UIMin = "16", UIMax = "64"))
	int32 RaymarchStepCount = 40;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume Shader", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "20.0"))
	float RaymarchInputThreshold = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume Shader", meta = (ClampMin = "1.0", UIMin = "20.0", UIMax = "180.0"))
	float RaymarchNormalizeDensity = 96.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume Shader", meta = (ClampMin = "0.1", UIMin = "0.3", UIMax = "2.0"))
	float RaymarchDensityPower = 0.82f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume Shader", meta = (ClampMin = "0.0", UIMin = "0.01", UIMax = "0.25"))
	float RaymarchOpacity = 0.055f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume Shader", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
	float RaymarchShadowStrength = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume Shader", meta = (ClampMin = "0.0", UIMin = "0.01", UIMax = "0.25"))
	float RaymarchLightStep = 0.075f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Volume Shader")
	FLinearColor RaymarchCloudColor = FLinearColor(0.86f, 0.90f, 0.92f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Direct Light", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "4.0"))
	float RaymarchDirectLightIntensityScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Direct Light", meta = (ClampMin = "0", ClampMax = "8", UIMin = "0", UIMax = "6"))
	int32 RaymarchDirectShadowSteps = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Direct Light", meta = (ClampMin = "0.0", UIMin = "0.1", UIMax = "4.0"))
	float RaymarchDirectShadowDensity = 1.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Direct Light", meta = (ClampMin = "-0.85", ClampMax = "0.85", UIMin = "-0.2", UIMax = "0.75"))
	float RaymarchPhaseG = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Direct Light", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float RaymarchPhaseStrength = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Paper Feature Proxy")
	bool bUseBakedFeatureLighting = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Paper Feature Proxy", meta = (ClampMin = "0", ClampMax = "3", UIMin = "0", UIMax = "3"))
	int32 RaymarchBakedFeatureLevel = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Paper Feature Proxy", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "1.5"))
	float RaymarchBakedFeatureContribution = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Paper Feature Proxy", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "1.5"))
	float RaymarchMultiScatterContribution = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Paper Feature Proxy", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float RaymarchFeatureAlbedoBlend = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Paper Feature Proxy")
	FLinearColor RaymarchBakedFeatureTint = FLinearColor(1.0f, 0.965f, 0.88f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Relight")
	bool bUseDirectionalLightForRelight = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Relight", meta = (EditCondition = "bUseDirectionalLightForRelight"))
	bool bAutoFindDirectionalLight = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Relight", meta = (EditCondition = "bUseDirectionalLightForRelight"))
	TObjectPtr<ADirectionalLight> DirectionalLightActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Relight", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
	float AmbientRelight = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Relight", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
	float DirectionalRelight = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Debug")
	bool bShowDebugText = true;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Setup")
	void ConfigureFromProjectSettings();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Baking")
	bool BakeCurrentDataToPluginData();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Rendering")
	bool RenderPreviewOnce();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Quality")
	void ApplyRealtimePreviewSettings();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Quality")
	void ApplyMobilePreviewSettings();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Quality")
	void ResetAccumulation();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Quality")
	void ApplyPaperPreviewSettings();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Volume")
	bool RebuildDensityVolumePreview();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Volume")
	bool RebuildVolumeShader();

protected:
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
	UTexture* GetPreviewTexture();
	UTexture* LoadFallbackPreviewTexture();
	FString FindFallbackPreviewPath() const;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> VolumeMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BillboardMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DensityVoxelMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RaymarchMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UVolumeTexture> RaymarchDensityTexture;

	UPROPERTY(Transient)
	TObjectPtr<UVolumeTexture> RaymarchFeatureTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture> FallbackPreviewTexture;

	TSharedPtr<FMRBNNSceneViewExtension, ESPMode::ThreadSafe> ComputeViewExtension;
	FString RaymarchTextureBuildKey;
	double NextPresentationRefreshTimeSeconds = 0.0;
	FLinearColor CurrentRaymarchDirectLightColor = FLinearColor::White;
	float CurrentRaymarchDirectLightIntensity = 1.0f;
	TObjectPtr<UVolumeTexture> LastAppliedRaymarchDensityTexture;
	TObjectPtr<UVolumeTexture> LastAppliedRaymarchFeatureTexture;
	FTransform LastAppliedRaymarchTransform;
	FVector LastAppliedRaymarchLightDirection = FVector::ZeroVector;
	FLinearColor LastAppliedRaymarchDirectLightColor = FLinearColor::Transparent;
	FLinearColor LastAppliedRaymarchCloudColor = FLinearColor::Transparent;
	FLinearColor LastAppliedRaymarchBakedFeatureTint = FLinearColor::Transparent;
	FVector LastAppliedRaymarchExtent = FVector::ZeroVector;
	int32 LastAppliedRaymarchStepCount = INDEX_NONE;
	int32 LastAppliedRaymarchDirectShadowSteps = INDEX_NONE;
	float LastAppliedRaymarchOpacity = -1.0f;
	float LastAppliedRaymarchAmbient = -1.0f;
	float LastAppliedRaymarchDirectional = -1.0f;
	float LastAppliedRaymarchShadowStrength = -1.0f;
	float LastAppliedRaymarchLightStep = -1.0f;
	float LastAppliedRaymarchBrightness = -1.0f;
	float LastAppliedRaymarchDirectLightIntensity = -1.0f;
	float LastAppliedRaymarchDirectShadowDensity = -1.0f;
	float LastAppliedRaymarchPhaseG = -2.0f;
	float LastAppliedRaymarchPhaseStrength = -1.0f;
	float LastAppliedRaymarchUseBakedFeatures = -1.0f;
	float LastAppliedRaymarchBakedFeatureContribution = -1.0f;
	float LastAppliedRaymarchMultiScatterContribution = -1.0f;
	float LastAppliedRaymarchFeatureAlbedoBlend = -1.0f;
	bool bEditorPreviewRenderAttempted = false;
	bool bDensityPreviewBuilt = false;
	bool bRaymarchTextureBuilt = false;
};
