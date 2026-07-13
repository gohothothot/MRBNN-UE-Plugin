#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MRBNNExampleActor.generated.h"

class UBillboardComponent;
class UMaterialInstanceDynamic;
class UMRBNNBakedVolumeData;
class UMRBNNVolumeComponent;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UTexture;
class UTexture2D;

UENUM(BlueprintType)
enum class EMRBNNExampleDisplayMode : uint8
{
	Plane,
	Billboard,
	PlaneAndBillboard,
	Hidden
};

UCLASS(BlueprintType, Blueprintable)
class MRBNN_API AMRBNNExampleActor : public AActor
{
	GENERATED_BODY()

public:
	AMRBNNExampleActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual bool ShouldTickIfViewportsOnly() const override { return false; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRBNN|Example")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRBNN|Example")
	TObjectPtr<UBillboardComponent> PreviewBillboard;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRBNN|Example")
	TObjectPtr<UStaticMeshComponent> PreviewPlane;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MRBNN|Example")
	TObjectPtr<UTextRenderComponent> DebugText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRBNN|Example")
	TObjectPtr<UMRBNNVolumeComponent> MRBNNVolume;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Example")
	bool bAutoConfigureSampleData = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Example")
	FString SampleSceneName = TEXT("cloud-03");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Example")
	bool bEnableSampleSkybox = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Example")
	bool bEnableSampleSkyboxBaking = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Example", meta = (ClampMin = "0.01", UIMin = "0.1", UIMax = "32.0"))
	float PreviewScale = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Example", meta = (ClampMin = "0.1", UIMin = "0.5", UIMax = "16.0"))
	float PreviewBrightness = 2.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Display")
	EMRBNNExampleDisplayMode DisplayMode = EMRBNNExampleDisplayMode::Plane;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Display")
	FVector PreviewPlaneLocation = FVector(260.0f, 0.0f, 140.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Display")
	FRotator PreviewPlaneRotation = FRotator(-90.0f, 0.0f, 0.0f);

	UPROPERTY(Transient, BlueprintReadWrite, Category = "MRBNN|Example")
	bool bShowDebugLabel = true;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Example")
	bool ConfigureSampleData();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Example")
	bool InitializeExampleRenderer();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Example")
	bool RenderExampleOnce();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Example")
	void ReleaseExampleRenderer();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Quality")
	void ApplyGamePreviewSettings();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Quality")
	void ApplyBalancedPreviewSettings();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Quality")
	void ApplyHighQualityPreviewSettings();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MRBNN|Quality")
	void ResetPreviewAccumulation();

protected:
	void EnsureSampleDataConfigured();
	void RefreshPreviewSprite();
	void ConfigurePreviewSurface();
	void UpdatePreviewVisibility(UTexture* PreviewTexture);
	void UpdateDebugText();
	bool ShouldShowPreviewPlane() const;
	bool ShouldShowPreviewBillboard() const;
	UTexture2D* LoadFallbackPreviewTexture();
	FString FindFallbackPreviewPath() const;
	bool ResolveDefaultSamplePaths(FString& OutRepositoryRoot, FString& OutWorkingDirectory, FString& OutSkyboxPath, FString& OutSkyboxBakingDirectory) const;

	UPROPERTY(Transient)
	TObjectPtr<UMRBNNBakedVolumeData> ExampleBakedData;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> LastPreviewTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FallbackPreviewTexture;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PreviewPlaneMaterialInstance;
};
