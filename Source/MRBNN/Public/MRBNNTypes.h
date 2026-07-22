#pragma once

#include "CoreMinimal.h"
#include "MRBNNTypes.generated.h"

UENUM(BlueprintType)
enum class EMRBNNToneMapping : uint8
{
	None,
	Gamma,
	ACES
};

UENUM(BlueprintType)
enum class EMRBNNDenoiseMode : uint8
{
	None,
	Unbiased,
	VisualPlausible
};

UENUM(BlueprintType)
enum class EMRBNNCompatibilityMode : uint8
{
	Normal,
	MRPNN,
	NoDirectIllum
};

USTRUCT(BlueprintType)
struct MRBNN_API FMRBNNRenderSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN")
	FVector CameraPosition = FVector(0.67085, -0.03808, -0.04856);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN")
	FVector LightDirection = FVector(0.34281, 0.70711, 0.61845);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN")
	FLinearColor LightColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FLinearColor Albedo = FLinearColor(0.999001f, 0.999001f, 0.999001f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN", meta = (ClampMin = "-0.99", ClampMax = "0.99"))
	float PhaseG = 0.857f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN")
	EMRBNNToneMapping ToneMapping = EMRBNNToneMapping::ACES;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN")
	EMRBNNDenoiseMode Denoise = EMRBNNDenoiseMode::VisualPlausible;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN")
	EMRBNNCompatibilityMode Compatibility = EMRBNNCompatibilityMode::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN")
	bool bExcludeLightEncoding = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN")
	bool bFastDirectIllumination = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN")
	bool bEnableSkybox = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN")
	bool bEnableSkyboxBaking = false;
};

USTRUCT(BlueprintType)
struct MRBNN_API FMRBNNComputeVolumeSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "4", ClampMax = "192", UIMin = "32", UIMax = "128"))
	int32 StepCount = 96;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "0", ClampMax = "16", UIMin = "2", UIMax = "8"))
	int32 DirectShadowSteps = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "0.2"))
	float Opacity = 0.065f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "4.0"))
	float Ambient = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "6.0"))
	float Directional = 1.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
	float ShadowStrength = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "0.001", UIMin = "0.01", UIMax = "0.25"))
	float LightStep = 0.065f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "8.0"))
	float Brightness = 1.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "6.0"))
	float DirectLightIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "4.0"))
	float DirectShadowDensity = 1.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "-0.85", ClampMax = "0.85", UIMin = "-0.2", UIMax = "0.75"))
	float PhaseG = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float PhaseStrength = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "1.2"))
	float EdgeSilverStrength = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "1.2"))
	float DeepShadowStrength = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "1.2"))
	float PowderStrength = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute")
	bool bUseBakedFeatures = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "1.5"))
	float BakedFeatureContribution = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "1.5"))
	float MultiScatterContribution = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float MultiScatterIsotropy = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "0.25", ClampMax = "4.0", UIMin = "0.5", UIMax = "3.0"))
	float SilverLiningSharpness = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "0.0", ClampMax = "3.0", UIMin = "0.0", UIMax = "2.0"))
	float SceneColorContribution = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute")
	FVector CloudFlowDirection = FVector(1.0, 0.0, 0.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "0.0", ClampMax = "0.25", UIMin = "0.0", UIMax = "0.08"))
	float CloudFlowSpeed = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float FeatureAlbedoBlend = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute")
	FLinearColor CloudColor = FLinearColor(0.86f, 0.90f, 0.92f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute")
	FLinearColor BakedFeatureTint = FLinearColor(1.0f, 0.965f, 0.88f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute|Paper-Style Cinematic")
	bool bUsePaperStyleCinematic = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute|Paper-Style Cinematic", meta = (ClampMin = "1", ClampMax = "48", UIMin = "8", UIMax = "32"))
	int32 CinematicLightOpticalDepthSteps = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute|Paper-Style Cinematic", meta = (ClampMin = "1", ClampMax = "16", UIMin = "2", UIMax = "8"))
	int32 CinematicInscatterSteps = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute|Paper-Style Cinematic", meta = (ClampMin = "0.1", ClampMax = "4.0", UIMin = "0.5", UIMax = "2.0"))
	float CinematicTransmittanceScale = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute|Paper-Style Cinematic", meta = (ClampMin = "0.0", ClampMax = "3.0", UIMin = "0.0", UIMax = "2.0"))
	float CinematicMultiScatterStrength = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRBNN|Compute|Paper-Style Cinematic", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float CinematicFeatureParticipation = 0.75f;
};
