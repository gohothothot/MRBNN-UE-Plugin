#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "MRBNNBakedVolumeData.generated.h"

UCLASS(BlueprintType)
class MRBNN_API UMRBNNBakedVolumeData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MRBNN")
	FDirectoryPath WorkingDirectory;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MRBNN")
	FDirectoryPath RepositoryRoot;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MRBNN")
	FFilePath SkyboxHDRI;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MRBNN", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SkyboxExposure = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MRBNN")
	FDirectoryPath SkyboxBakingDirectory;

	UFUNCTION(BlueprintCallable, Category = "MRBNN")
	bool ValidateData(UPARAM(ref) FText& OutError) const;

	bool ResolvePaths(FString& OutWorkingDirectory, FString& OutRepositoryRoot, FText& OutError) const;
	bool ResolveSkyboxPath(FString& OutSkyboxPath, FText& OutError) const;
	bool ResolveSkyboxBakingDirectory(FString& OutSkyboxBakingDirectory, FText& OutError) const;
};
