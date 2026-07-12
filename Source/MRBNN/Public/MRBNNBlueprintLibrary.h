#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MRBNNBakedVolumeData.h"
#include "MRBNNBlueprintLibrary.generated.h"

UCLASS()
class MRBNN_API UMRBNNBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MRBNN")
	static bool ValidateMRBNNBakedVolumeData(UMRBNNBakedVolumeData* BakedData, UPARAM(ref) FText& OutError);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MRBNN")
	static FString GetExpectedMRBNNBridgePath();

	UFUNCTION(BlueprintCallable, Category = "MRBNN")
	static bool IsMRBNNBridgeAvailable(FString& OutBridgePath, UPARAM(ref) FText& OutError);

	UFUNCTION(BlueprintCallable, Category = "MRBNN|Paths")
	static FString ResolveMRBNNPath(const FString& PathWithTokens);

	UFUNCTION(BlueprintCallable, Category = "MRBNN|Data")
	static UMRBNNBakedVolumeData* CreateMRBNNBakedVolumeDataFromProjectSettings(UObject* Outer, UPARAM(ref) FText& OutError);

	UFUNCTION(BlueprintCallable, Category = "MRBNN|Baking")
	static bool BakeMRBNNDataToPluginData(UMRBNNBakedVolumeData* SourceData, const FString& SceneName, bool bIncludeSkyboxHDRI, bool bIncludeSkyboxBakingData, UPARAM(ref) FText& OutError);
};
