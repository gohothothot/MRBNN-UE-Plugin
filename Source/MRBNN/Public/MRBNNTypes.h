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
