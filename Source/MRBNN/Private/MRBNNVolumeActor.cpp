#include "MRBNNVolumeActor.h"

#include "MRBNNBlueprintLibrary.h"
#include "MRBNNBakedVolumeData.h"
#include "MRBNNProjectSettings.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/VolumeTexture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MRBNNVolumeComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
struct FMRBNNDensityVoxelCandidate
{
	FVector LocalPosition = FVector::ZeroVector;
	float Density = 0.0f;
};

bool ReadJsonObject(const FString& Path, TSharedPtr<FJsonObject>& OutObject)
{
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *Path))
	{
		return false;
	}

	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
}

bool ReadIntArray3(const TArray<TSharedPtr<FJsonValue>>* Values, FIntVector& OutValue)
{
	if (!Values || Values->Num() < 3)
	{
		return false;
	}

	OutValue.X = static_cast<int32>((*Values)[0]->AsNumber());
	OutValue.Y = static_cast<int32>((*Values)[1]->AsNumber());
	OutValue.Z = static_cast<int32>((*Values)[2]->AsNumber());
	return OutValue.X > 0 && OutValue.Y > 0 && OutValue.Z > 0;
}

float ReadDensityValue(const float* Values, const FIntVector& Resolution, int32 X, int32 Y, int32 Z)
{
	const int64 Index =
		static_cast<int64>(Z) * Resolution.X * Resolution.Y +
		static_cast<int64>(Y) * Resolution.X +
		X;
	const float Value = Values[Index];
	return FMath::IsFinite(Value) ? Value : 0.0f;
}

float SampleDensityTrilinear(const float* Values, const FIntVector& Resolution, const FVector3f& SourcePosition)
{
	const int32 LastX = FMath::Max(Resolution.X - 1, 0);
	const int32 LastY = FMath::Max(Resolution.Y - 1, 0);
	const int32 LastZ = FMath::Max(Resolution.Z - 1, 0);

	const float ClampedX = FMath::Clamp(SourcePosition.X, 0.0f, static_cast<float>(LastX));
	const float ClampedY = FMath::Clamp(SourcePosition.Y, 0.0f, static_cast<float>(LastY));
	const float ClampedZ = FMath::Clamp(SourcePosition.Z, 0.0f, static_cast<float>(LastZ));

	const int32 X0 = FMath::Clamp(FMath::FloorToInt(ClampedX), 0, LastX);
	const int32 Y0 = FMath::Clamp(FMath::FloorToInt(ClampedY), 0, LastY);
	const int32 Z0 = FMath::Clamp(FMath::FloorToInt(ClampedZ), 0, LastZ);
	const int32 X1 = FMath::Min(X0 + 1, LastX);
	const int32 Y1 = FMath::Min(Y0 + 1, LastY);
	const int32 Z1 = FMath::Min(Z0 + 1, LastZ);
	const float Tx = ClampedX - static_cast<float>(X0);
	const float Ty = ClampedY - static_cast<float>(Y0);
	const float Tz = ClampedZ - static_cast<float>(Z0);

	const float C000 = ReadDensityValue(Values, Resolution, X0, Y0, Z0);
	const float C100 = ReadDensityValue(Values, Resolution, X1, Y0, Z0);
	const float C010 = ReadDensityValue(Values, Resolution, X0, Y1, Z0);
	const float C110 = ReadDensityValue(Values, Resolution, X1, Y1, Z0);
	const float C001 = ReadDensityValue(Values, Resolution, X0, Y0, Z1);
	const float C101 = ReadDensityValue(Values, Resolution, X1, Y0, Z1);
	const float C011 = ReadDensityValue(Values, Resolution, X0, Y1, Z1);
	const float C111 = ReadDensityValue(Values, Resolution, X1, Y1, Z1);

	const float C00 = FMath::Lerp(C000, C100, Tx);
	const float C10 = FMath::Lerp(C010, C110, Tx);
	const float C01 = FMath::Lerp(C001, C101, Tx);
	const float C11 = FMath::Lerp(C011, C111, Tx);
	const float C0 = FMath::Lerp(C00, C10, Ty);
	const float C1 = FMath::Lerp(C01, C11, Ty);
	return FMath::Lerp(C0, C1, Tz);
}

float Smooth01(float Value)
{
	const float T = FMath::Clamp(Value, 0.0f, 1.0f);
	return T * T * (3.0f - 2.0f * T);
}
}

AMRBNNVolumeActor::AMRBNNVolumeActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickInterval = 0.1f;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	VolumeBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("VolumeBounds"));
	VolumeBounds->SetupAttachment(SceneRoot);
	VolumeBounds->SetBoxExtent(VolumeExtent);
	VolumeBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VolumeBounds->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	VolumeBounds->SetLineThickness(2.0f);

	VolumeRaymarchMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VolumeRaymarchMesh"));
	VolumeRaymarchMesh->SetupAttachment(SceneRoot);
	VolumeRaymarchMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VolumeRaymarchMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	VolumeRaymarchMesh->SetCastShadow(false);
	VolumeRaymarchMesh->bReceivesDecals = false;
	VolumeRaymarchMesh->TranslucencySortPriority = 9;

	VolumeSlices = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("VolumeSlices"));
	VolumeSlices->SetupAttachment(SceneRoot);
	VolumeSlices->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VolumeSlices->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	VolumeSlices->SetCastShadow(false);
	VolumeSlices->bReceivesDecals = false;
	VolumeSlices->TranslucencySortPriority = 10;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMeshFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));

	VolumeBillboard = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VolumeBillboard"));
	VolumeBillboard->SetupAttachment(SceneRoot);
	VolumeBillboard->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VolumeBillboard->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	VolumeBillboard->SetCastShadow(false);
	VolumeBillboard->bReceivesDecals = false;
	VolumeBillboard->TranslucencySortPriority = 11;
	if (PlaneMeshFinder.Succeeded())
	{
		VolumeBillboard->SetStaticMesh(PlaneMeshFinder.Object);
		VolumeSlices->SetStaticMesh(PlaneMeshFinder.Object);
	}

	VolumeDensityVoxels = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("VolumeDensityVoxels"));
	VolumeDensityVoxels->SetupAttachment(SceneRoot);
	VolumeDensityVoxels->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VolumeDensityVoxels->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	VolumeDensityVoxels->SetCastShadow(false);
	VolumeDensityVoxels->bReceivesDecals = false;
	VolumeDensityVoxels->TranslucencySortPriority = 12;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshFinder.Succeeded())
	{
		VolumeRaymarchMesh->SetStaticMesh(CubeMeshFinder.Object);
		VolumeDensityVoxels->SetStaticMesh(CubeMeshFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SliceMaterialFinder(TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent_OneSided.Widget3DPassThrough_Translucent_OneSided"));
	if (SliceMaterialFinder.Succeeded())
	{
		VolumeBillboard->SetMaterial(0, SliceMaterialFinder.Object);
		VolumeDensityVoxels->SetMaterial(0, SliceMaterialFinder.Object);
		VolumeSlices->SetMaterial(0, SliceMaterialFinder.Object);
	}

	DebugText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("DebugText"));
	DebugText->SetupAttachment(SceneRoot);
	DebugText->SetRelativeLocation(FVector(-160.0f, -230.0f, 190.0f));
	DebugText->SetRelativeRotation(FRotator(0.0f, 35.0f, 0.0f));
	DebugText->SetHorizontalAlignment(EHTA_Left);
	DebugText->SetVerticalAlignment(EVRTA_TextTop);
	DebugText->SetTextRenderColor(FColor(220, 235, 255));
	DebugText->SetWorldSize(12.0f);
	DebugText->SetCastShadow(false);

	MRBNNVolume = CreateDefaultSubobject<UMRBNNVolumeComponent>(TEXT("MRBNNVolume"));
	MRBNNVolume->bUseProjectSettingsWhenBakedDataMissing = true;
	MRBNNVolume->bAutoInitialize = false;
	MRBNNVolume->bRenderEveryTick = false;
	MRBNNVolume->bUsePlayerCamera = true;
	MRBNNVolume->bApplyOutputToMaterials = false;
	MRBNNVolume->bAllowAutomaticRenderInEditor = false;
	MRBNNVolume->ApplyRealtimePreviewSettings();
}

void AMRBNNVolumeActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (bAutoConfigureFromProjectSettings)
	{
		ConfigureFromProjectSettings();
	}
	else
	{
		UpdateVolumeProxy();
		if (!bRaymarchTextureBuilt)
		{
			BuildRaymarchVolumeTexture();
		}
		if (!bDensityPreviewBuilt)
		{
			BuildDensityVolumePreview();
		}
		UpdateRelightFromDirectionalLight();
		UpdateVolumeMaterial();
		UpdateDebugText();
	}
}

void AMRBNNVolumeActor::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoConfigureFromProjectSettings)
	{
		ConfigureFromProjectSettings();
	}

	if (bAutoRenderOnBeginPlay && MRBNNVolume)
	{
		MRBNNVolume->InitializeRenderer();
		MRBNNVolume->RenderOnce();
	}

	UpdateVolumeMaterial();
	UpdateDebugText();
}

void AMRBNNVolumeActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	MaybeRenderEditorPreviewOnce();
	const UWorld* World = GetWorld();
	const double NowSeconds = World ? World->GetTimeSeconds() : FPlatformTime::Seconds();
	if (NowSeconds < NextPresentationRefreshTimeSeconds)
	{
		return;
	}

	NextPresentationRefreshTimeSeconds = NowSeconds + 0.1;
	UpdateRelightFromDirectionalLight();
	UpdateVolumeMaterial();
	UpdateDebugText();
}

void AMRBNNVolumeActor::ConfigureFromProjectSettings()
{
	const UMRBNNProjectSettings* Settings = UMRBNNProjectSettings::Get();
	VolumeExtent = Settings->VolumeExtent;
	bShowVolumeBillboard = Settings->bVolumeShowBillboard;
	bUseRaymarchShader = Settings->bVolumeUseRaymarchShader;
	bShowDensityVolume = Settings->bVolumeShowDensityPreview;
	bFitDensityPreviewToBounds = Settings->bVolumeFitDensityPreviewToBounds;
	SliceCount = Settings->VolumeSliceCount;
	SliceOpacity = Settings->VolumeSliceOpacity;
	PreviewBrightness = Settings->VolumeBrightness;
	RaymarchTextureResolution = Settings->VolumeRaymarchTextureResolution;
	bRaymarchFitToDensityBounds = Settings->bVolumeRaymarchFitToDensityBounds;
	RaymarchBoundsThreshold = Settings->VolumeRaymarchBoundsThreshold;
	RaymarchBoundsPadding = Settings->VolumeRaymarchBoundsPadding;
	RaymarchStepCount = Settings->VolumeRaymarchStepCount;
	RaymarchInputThreshold = Settings->VolumeRaymarchInputThreshold;
	RaymarchNormalizeDensity = Settings->VolumeRaymarchNormalizeDensity;
	RaymarchDensityPower = Settings->VolumeRaymarchDensityPower;
	RaymarchOpacity = Settings->VolumeRaymarchOpacity;
	RaymarchShadowStrength = Settings->VolumeRaymarchShadowStrength;
	RaymarchLightStep = Settings->VolumeRaymarchLightStep;
	RaymarchCloudColor = Settings->VolumeRaymarchCloudColor;
	DensitySampleResolution = Settings->VolumeDensitySampleResolution;
	MaxDensityVoxelInstances = Settings->VolumeMaxDensityVoxelInstances;
	DensityThreshold = Settings->VolumeDensityThreshold;
	DensityVoxelScale = Settings->VolumeDensityVoxelScale;
	DensityVoxelOpacity = Settings->VolumeDensityVoxelOpacity;
	DensityBoundsFill = Settings->VolumeDensityBoundsFill;
	AmbientRelight = Settings->VolumeAmbientRelight;
	DirectionalRelight = Settings->VolumeDirectionalRelight;

	if (MRBNNVolume)
	{
		MRBNNVolume->ApplyRealtimePreviewSettings();
		MRBNNVolume->bAutoInitialize = false;
		MRBNNVolume->bRenderEveryTick = false;
		MRBNNVolume->bUseProjectSettingsWhenBakedDataMissing = true;
		if (!MRBNNVolume->BakedData)
		{
			FText Error;
			MRBNNVolume->BakedData = Settings->CreateTransientDefaultBakedData(this, Error);
		}
	}
	bEditorPreviewRenderAttempted = false;

	UpdateVolumeProxy();
	BuildRaymarchVolumeTexture();
	BuildDensityVolumePreview();
	UpdateRelightFromDirectionalLight();
	UpdateVolumeMaterial();
	UpdateDebugText();
}

bool AMRBNNVolumeActor::BakeCurrentDataToPluginData()
{
	if (!MRBNNVolume)
	{
		return false;
	}

	UMRBNNBakedVolumeData* SourceData = MRBNNVolume->BakedData;
	if (!SourceData)
	{
		FText Error;
		SourceData = UMRBNNProjectSettings::Get()->CreateTransientDefaultBakedData(this, Error);
	}

	FText BakeError;
	const bool bBaked = UMRBNNBlueprintLibrary::BakeMRBNNDataToPluginData(
		SourceData,
		UMRBNNProjectSettings::Get()->DefaultSceneName,
		UMRBNNProjectSettings::Get()->bEnableDefaultSkybox,
		UMRBNNProjectSettings::Get()->bEnableDefaultSkyboxBaking,
		BakeError);
	if (!bBaked)
	{
		UE_LOG(LogTemp, Warning, TEXT("MRBNN bake failed: %s"), *BakeError.ToString());
	}
	else
	{
		ConfigureFromProjectSettings();
	}

	UpdateDebugText();
	return bBaked;
}

bool AMRBNNVolumeActor::RenderPreviewOnce()
{
	if (!MRBNNVolume)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	if (World && !World->IsGameWorld() && !bAllowLiveRenderInEditor)
	{
		UE_LOG(LogTemp, Warning, TEXT("MRBNN live render is disabled in the editor world. Press Play for live MRBNN output, or enable Allow Live Render In Editor at your own risk."));
		UpdateVolumeMaterial();
		UpdateDebugText();
		return false;
	}

	UpdateRelightFromDirectionalLight();
	const bool bRendered = MRBNNVolume->RenderOnce();
	bEditorPreviewRenderAttempted = true;
	UpdateVolumeMaterial();
	UpdateDebugText();
	return bRendered;
}

void AMRBNNVolumeActor::ApplyRealtimePreviewSettings()
{
	if (MRBNNVolume)
	{
		MRBNNVolume->ApplyRealtimePreviewSettings();
		MRBNNVolume->bAutoInitialize = false;
		MRBNNVolume->bRenderEveryTick = false;
	}
	bShowVolumeBillboard = false;
	bUseRaymarchShader = true;
	bShowDensityVolume = false;
	bFitDensityPreviewToBounds = true;
	RaymarchTextureResolution = 80;
	bRaymarchFitToDensityBounds = true;
	RaymarchBoundsThreshold = 4.0f;
	RaymarchBoundsPadding = 0.08f;
	RaymarchStepCount = 40;
	RaymarchInputThreshold = 4.0f;
	RaymarchNormalizeDensity = 96.0f;
	RaymarchDensityPower = 0.82f;
	RaymarchOpacity = 0.055f;
	RaymarchShadowStrength = 0.55f;
	RaymarchLightStep = 0.075f;
	DensitySampleResolution = 48;
	MaxDensityVoxelInstances = 4200;
	DensityThreshold = 10.0f;
	DensityVoxelScale = 2.85f;
	DensityVoxelOpacity = 0.17f;
	DensityBoundsFill = 0.88f;
	bEditorPreviewRenderAttempted = false;
	RebuildVolumeShader();
	RebuildDensityVolumePreview();
	UpdateVolumeMaterial();
	UpdateDebugText();
}

void AMRBNNVolumeActor::ApplyMobilePreviewSettings()
{
	if (MRBNNVolume)
	{
		MRBNNVolume->ApplyMobilePreviewSettings();
		MRBNNVolume->bAutoInitialize = false;
		MRBNNVolume->bRenderEveryTick = false;
	}
	bShowVolumeBillboard = false;
	bUseRaymarchShader = true;
	bShowDensityVolume = false;
	bFitDensityPreviewToBounds = true;
	RaymarchTextureResolution = 48;
	bRaymarchFitToDensityBounds = true;
	RaymarchBoundsThreshold = 6.0f;
	RaymarchBoundsPadding = 0.08f;
	RaymarchStepCount = 22;
	RaymarchInputThreshold = 6.0f;
	RaymarchNormalizeDensity = 104.0f;
	RaymarchDensityPower = 0.88f;
	RaymarchOpacity = 0.075f;
	RaymarchShadowStrength = 0.35f;
	RaymarchLightStep = 0.08f;
	DensitySampleResolution = 36;
	MaxDensityVoxelInstances = 2200;
	DensityThreshold = 20.0f;
	DensityVoxelScale = 3.2f;
	DensityVoxelOpacity = 0.2f;
	DensityBoundsFill = 0.86f;
	bEditorPreviewRenderAttempted = false;
	RebuildVolumeShader();
	RebuildDensityVolumePreview();
	UpdateVolumeMaterial();
	UpdateDebugText();
}

void AMRBNNVolumeActor::ResetAccumulation()
{
	if (MRBNNVolume)
	{
		MRBNNVolume->ResetProgressiveAccumulation();
	}
	bEditorPreviewRenderAttempted = false;
	UpdateDebugText();
}

bool AMRBNNVolumeActor::RebuildDensityVolumePreview()
{
	bDensityPreviewBuilt = false;
	return BuildDensityVolumePreview();
}

bool AMRBNNVolumeActor::RebuildVolumeShader()
{
	bRaymarchTextureBuilt = false;
	return BuildRaymarchVolumeTexture();
}

void AMRBNNVolumeActor::UpdateVolumeProxy()
{
	const FVector SafeExtent(
		FMath::Max(VolumeExtent.X, 1.0f),
		FMath::Max(VolumeExtent.Y, 1.0f),
		FMath::Max(VolumeExtent.Z, 1.0f));
	if (VolumeBounds)
	{
		VolumeBounds->SetBoxExtent(SafeExtent);
	}

	if (VolumeRaymarchMesh)
	{
		VolumeRaymarchMesh->SetRelativeLocation(FVector::ZeroVector);
		VolumeRaymarchMesh->SetRelativeRotation(FRotator::ZeroRotator);
		VolumeRaymarchMesh->SetRelativeScale3D(SafeExtent * 2.0f / 100.0f);
		VolumeRaymarchMesh->SetVisibility(bUseRaymarchShader, true);
	}

	if (VolumeBillboard)
	{
		VolumeBillboard->SetRelativeLocation(FVector::ZeroVector);
		VolumeBillboard->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
		VolumeBillboard->SetRelativeScale3D(FVector(SafeExtent.Y * 2.0f / 100.0f, SafeExtent.Z * 2.0f / 100.0f, 1.0f));
	}

	if (VolumeDensityVoxels)
	{
		VolumeDensityVoxels->SetVisibility(bShowDensityVolume, true);
	}

	if (!VolumeSlices)
	{
		return;
	}

	VolumeSlices->ClearInstances();
	if (!bUseExperimentalSliceStack)
	{
		VolumeSlices->SetVisibility(false, true);
		return;
	}

	const int32 SafeSliceCount = FMath::Clamp(SliceCount, 1, 64);
	const float Depth = SafeExtent.X * 2.0f;
	const FVector SliceScale(SafeExtent.Y * 2.0f / 100.0f, SafeExtent.Z * 2.0f / 100.0f, 1.0f);
	for (int32 SliceIndex = 0; SliceIndex < SafeSliceCount; ++SliceIndex)
	{
		const float Unit = (static_cast<float>(SliceIndex) + 0.5f) / static_cast<float>(SafeSliceCount);
		const float LocalX = (Unit - 0.5f) * Depth;
		const FTransform SliceTransform(FRotator(90.0f, 0.0f, 0.0f), FVector(LocalX, 0.0f, 0.0f), SliceScale);
		VolumeSlices->AddInstance(SliceTransform);
	}
}

bool AMRBNNVolumeActor::ResolveDensityVolumeFile(FString& OutVolumePath, FIntVector& OutResolution, int32& OutSkipByteCount, FText& OutError)
{
	OutVolumePath.Empty();
	OutResolution = FIntVector(257, 257, 257);
	OutSkipByteCount = 0;

	if (!MRBNNVolume)
	{
		OutError = FText::FromString(TEXT("MRBNN volume component is missing."));
		return false;
	}

	UMRBNNBakedVolumeData* Data = MRBNNVolume->BakedData;
	if (!Data)
	{
		FText DefaultDataError;
		Data = UMRBNNProjectSettings::Get()->CreateTransientDefaultBakedData(this, DefaultDataError);
		MRBNNVolume->BakedData = Data;
		if (!Data)
		{
			OutError = DefaultDataError.IsEmpty() ? FText::FromString(TEXT("MRBNN default baked data is not available.")) : DefaultDataError;
			return false;
		}
	}

	FString WorkingDirectory;
	FString RepositoryRoot;
	if (!Data->ResolvePaths(WorkingDirectory, RepositoryRoot, OutError))
	{
		return false;
	}

	TSharedPtr<FJsonObject> Config;
	if (!ReadJsonObject(FPaths::Combine(WorkingDirectory, TEXT("config.json")), Config))
	{
		OutError = FText::FromString(TEXT("MRBNN density config.json could not be read."));
		return false;
	}

	const TSharedPtr<FJsonObject>* VolumeObject = nullptr;
	if (!Config->TryGetObjectField(TEXT("volume"), VolumeObject) || !VolumeObject || !VolumeObject->IsValid())
	{
		OutError = FText::FromString(TEXT("MRBNN config.json does not contain a volume object."));
		return false;
	}

	FString VolumePath;
	if (!(*VolumeObject)->TryGetStringField(TEXT("path"), VolumePath) || VolumePath.IsEmpty())
	{
		OutError = FText::FromString(TEXT("MRBNN config.json does not contain a volume path."));
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* ResolutionArray = nullptr;
	if ((*VolumeObject)->TryGetArrayField(TEXT("resolution"), ResolutionArray))
	{
		ReadIntArray3(ResolutionArray, OutResolution);
	}
	(*VolumeObject)->TryGetNumberField(TEXT("skip_byte_num"), OutSkipByteCount);

	FPaths::NormalizeFilename(VolumePath);
	if (FPaths::IsRelative(VolumePath))
	{
		VolumePath = FPaths::Combine(RepositoryRoot, VolumePath);
	}
	FPaths::CollapseRelativeDirectories(VolumePath);

	if (!FPaths::FileExists(VolumePath))
	{
		OutError = FText::Format(FText::FromString(TEXT("MRBNN volume file does not exist: {0}")), FText::FromString(VolumePath));
		return false;
	}

	OutVolumePath = VolumePath;
	OutError = FText::GetEmpty();
	return true;
}

bool AMRBNNVolumeActor::BuildRaymarchVolumeTexture()
{
	bRaymarchTextureBuilt = true;

	if (!bUseRaymarchShader)
	{
		RaymarchDensityTexture = nullptr;
		RaymarchTextureBuildKey.Empty();
		if (VolumeRaymarchMesh)
		{
			VolumeRaymarchMesh->SetVisibility(false, true);
		}
		return false;
	}

	FString VolumePath;
	FIntVector VolumeResolution(257, 257, 257);
	int32 SkipByteCount = 0;
	FText Error;
	if (!ResolveDensityVolumeFile(VolumePath, VolumeResolution, SkipByteCount, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("MRBNN raymarch texture could not resolve density volume: %s"), *Error.ToString());
		RaymarchDensityTexture = nullptr;
		RaymarchTextureBuildKey.Empty();
		if (VolumeRaymarchMesh)
		{
			VolumeRaymarchMesh->SetVisibility(false, true);
		}
		return false;
	}

	const FDateTime VolumeTimestamp = IFileManager::Get().GetTimeStamp(*VolumePath);
	const FString BuildKey = FString::Printf(
		TEXT("%s|%lld|%d,%d,%d|%d|%d|%d|%.6f|%.6f|%.6f|%.6f|%.6f"),
		*VolumePath,
		VolumeTimestamp.GetTicks(),
		VolumeResolution.X,
		VolumeResolution.Y,
		VolumeResolution.Z,
		SkipByteCount,
		FMath::Clamp(RaymarchTextureResolution, 16, 128),
		bRaymarchFitToDensityBounds ? 1 : 0,
		RaymarchBoundsThreshold,
		RaymarchBoundsPadding,
		RaymarchInputThreshold,
		RaymarchNormalizeDensity,
		RaymarchDensityPower);
	if (RaymarchDensityTexture && RaymarchTextureBuildKey == BuildKey)
	{
		if (VolumeRaymarchMesh)
		{
			VolumeRaymarchMesh->SetVisibility(true, true);
		}
		UpdateRaymarchMaterial();
		return true;
	}

	TArray<uint8> RawBytes;
	if (!FFileHelper::LoadFileToArray(RawBytes, *VolumePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("MRBNN raymarch texture could not read volume file: %s"), *VolumePath);
		RaymarchDensityTexture = nullptr;
		RaymarchTextureBuildKey.Empty();
		if (VolumeRaymarchMesh)
		{
			VolumeRaymarchMesh->SetVisibility(false, true);
		}
		return false;
	}

	const int64 SourceVoxelCount = static_cast<int64>(VolumeResolution.X) * VolumeResolution.Y * VolumeResolution.Z;
	if (SourceVoxelCount <= 0 || RawBytes.Num() < SkipByteCount + SourceVoxelCount * static_cast<int64>(sizeof(float)))
	{
		UE_LOG(LogTemp, Warning, TEXT("MRBNN raymarch texture volume file is smaller than config resolution requires."));
		RaymarchDensityTexture = nullptr;
		RaymarchTextureBuildKey.Empty();
		if (VolumeRaymarchMesh)
		{
			VolumeRaymarchMesh->SetVisibility(false, true);
		}
		return false;
	}

	const int32 TextureResolution = FMath::Clamp(RaymarchTextureResolution, 16, 128);
	UVolumeTexture* NewTexture = UVolumeTexture::CreateTransient(TextureResolution, TextureResolution, TextureResolution, PF_B8G8R8A8, TEXT("MRBNN_DensityVolume"));
	if (!NewTexture || !NewTexture->GetPlatformData() || NewTexture->GetPlatformData()->Mips.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("MRBNN raymarch texture could not allocate a transient volume texture."));
		RaymarchDensityTexture = nullptr;
		RaymarchTextureBuildKey.Empty();
		return false;
	}

	NewTexture->SRGB = false;
	NewTexture->CompressionSettings = TC_VectorDisplacementmap;
	NewTexture->MipGenSettings = TMGS_NoMipmaps;
	NewTexture->Filter = TF_Bilinear;
	NewTexture->AddressMode = TA_Clamp;
	NewTexture->NeverStream = true;

	const float* DensityValues = reinterpret_cast<const float*>(RawBytes.GetData() + SkipByteCount);
	const float SafeNormalizeDensity = FMath::Max(RaymarchNormalizeDensity, 1.0f);
	const float SafeDensityPower = FMath::Max(RaymarchDensityPower, 0.1f);
	const int32 LastSourceX = FMath::Max(VolumeResolution.X - 1, 0);
	const int32 LastSourceY = FMath::Max(VolumeResolution.Y - 1, 0);
	const int32 LastSourceZ = FMath::Max(VolumeResolution.Z - 1, 0);
	FIntVector SourceMin(0, 0, 0);
	FIntVector SourceMax(LastSourceX, LastSourceY, LastSourceZ);

	if (bRaymarchFitToDensityBounds)
	{
		const float BoundsThreshold = FMath::Max(RaymarchBoundsThreshold, RaymarchInputThreshold);
		FIntVector ActiveMin(LastSourceX, LastSourceY, LastSourceZ);
		FIntVector ActiveMax(0, 0, 0);
		int64 ActiveVoxelCount = 0;

		for (int32 Z = 0; Z < VolumeResolution.Z; ++Z)
		{
			for (int32 Y = 0; Y < VolumeResolution.Y; ++Y)
			{
				for (int32 X = 0; X < VolumeResolution.X; ++X)
				{
					const float Density = ReadDensityValue(DensityValues, VolumeResolution, X, Y, Z);
					if (Density <= BoundsThreshold)
					{
						continue;
					}

					++ActiveVoxelCount;
					ActiveMin.X = FMath::Min(ActiveMin.X, X);
					ActiveMin.Y = FMath::Min(ActiveMin.Y, Y);
					ActiveMin.Z = FMath::Min(ActiveMin.Z, Z);
					ActiveMax.X = FMath::Max(ActiveMax.X, X);
					ActiveMax.Y = FMath::Max(ActiveMax.Y, Y);
					ActiveMax.Z = FMath::Max(ActiveMax.Z, Z);
				}
			}
		}

		if (ActiveVoxelCount > 0)
		{
			const FIntVector ActiveSize(
				FMath::Max(ActiveMax.X - ActiveMin.X + 1, 1),
				FMath::Max(ActiveMax.Y - ActiveMin.Y + 1, 1),
				FMath::Max(ActiveMax.Z - ActiveMin.Z + 1, 1));
			const float PaddingFraction = FMath::Clamp(RaymarchBoundsPadding, 0.0f, 0.25f);
			const FIntVector Padding(
				FMath::Max(2, FMath::CeilToInt(static_cast<float>(ActiveSize.X) * PaddingFraction)),
				FMath::Max(2, FMath::CeilToInt(static_cast<float>(ActiveSize.Y) * PaddingFraction)),
				FMath::Max(2, FMath::CeilToInt(static_cast<float>(ActiveSize.Z) * PaddingFraction)));

			SourceMin.X = FMath::Clamp(ActiveMin.X - Padding.X, 0, LastSourceX);
			SourceMin.Y = FMath::Clamp(ActiveMin.Y - Padding.Y, 0, LastSourceY);
			SourceMin.Z = FMath::Clamp(ActiveMin.Z - Padding.Z, 0, LastSourceZ);
			SourceMax.X = FMath::Clamp(ActiveMax.X + Padding.X, 0, LastSourceX);
			SourceMax.Y = FMath::Clamp(ActiveMax.Y + Padding.Y, 0, LastSourceY);
			SourceMax.Z = FMath::Clamp(ActiveMax.Z + Padding.Z, 0, LastSourceZ);

			UE_LOG(LogTemp, Log, TEXT("MRBNN raymarch density bounds: min=(%d,%d,%d) max=(%d,%d,%d) active=%lld threshold=%.3f"),
				SourceMin.X, SourceMin.Y, SourceMin.Z,
				SourceMax.X, SourceMax.Y, SourceMax.Z,
				ActiveVoxelCount,
				BoundsThreshold);
		}
	}

	FTexture2DMipMap& Mip = NewTexture->GetPlatformData()->Mips[0];
	FColor* MipData = reinterpret_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	if (!MipData)
	{
		Mip.BulkData.Unlock();
		RaymarchDensityTexture = nullptr;
		RaymarchTextureBuildKey.Empty();
		return false;
	}

	for (int32 Z = 0; Z < TextureResolution; ++Z)
	{
		const float UnitZ = (static_cast<float>(Z) + 0.5f) / static_cast<float>(TextureResolution);
		for (int32 Y = 0; Y < TextureResolution; ++Y)
		{
			const float UnitY = (static_cast<float>(Y) + 0.5f) / static_cast<float>(TextureResolution);
			for (int32 X = 0; X < TextureResolution; ++X)
			{
				const float UnitX = (static_cast<float>(X) + 0.5f) / static_cast<float>(TextureResolution);
				const FVector3f SourcePosition(
					FMath::Lerp(static_cast<float>(SourceMin.X), static_cast<float>(SourceMax.X), UnitX),
					FMath::Lerp(static_cast<float>(SourceMin.Y), static_cast<float>(SourceMax.Y), UnitY),
					FMath::Lerp(static_cast<float>(SourceMin.Z), static_cast<float>(SourceMax.Z), UnitZ));
				const float RawDensity = SampleDensityTrilinear(DensityValues, VolumeResolution, SourcePosition);
				float NormalizedDensity = 0.0f;
				if (FMath::IsFinite(RawDensity))
				{
					NormalizedDensity = FMath::Clamp((RawDensity - RaymarchInputThreshold) / SafeNormalizeDensity, 0.0f, 1.0f);
					NormalizedDensity = FMath::Pow(NormalizedDensity, SafeDensityPower);
				}

				const float EdgeDistance = FMath::Min(
					FMath::Min(UnitX, 1.0f - UnitX),
					FMath::Min(FMath::Min(UnitY, 1.0f - UnitY), FMath::Min(UnitZ, 1.0f - UnitZ)));
				const float EdgeFade = Smooth01(EdgeDistance / 0.055f);
				NormalizedDensity *= EdgeFade;

				const uint8 QuantizedDensity = static_cast<uint8>(FMath::RoundToInt(NormalizedDensity * 255.0f));
				*MipData++ = FColor(QuantizedDensity, QuantizedDensity, QuantizedDensity, QuantizedDensity);
			}
		}
	}

	Mip.BulkData.Unlock();
	NewTexture->UpdateResource();

	RaymarchDensityTexture = NewTexture;
	RaymarchTextureBuildKey = BuildKey;
	if (VolumeRaymarchMesh)
	{
		VolumeRaymarchMesh->SetVisibility(true, true);
	}
	UpdateRaymarchMaterial();
	return true;
}

bool AMRBNNVolumeActor::BuildDensityVolumePreview()
{
	if (!VolumeDensityVoxels)
	{
		return false;
	}

	VolumeDensityVoxels->ClearInstances();
	bDensityPreviewBuilt = true;

	if (!bShowDensityVolume || !MRBNNVolume)
	{
		VolumeDensityVoxels->SetVisibility(false, true);
		return false;
	}

	UMRBNNBakedVolumeData* Data = MRBNNVolume->BakedData;
	if (!Data)
	{
		FText DefaultDataError;
		Data = UMRBNNProjectSettings::Get()->CreateTransientDefaultBakedData(this, DefaultDataError);
		MRBNNVolume->BakedData = Data;
	}
	if (!Data)
	{
		VolumeDensityVoxels->SetVisibility(false, true);
		return false;
	}

	FText Error;
	FString WorkingDirectory;
	FString RepositoryRoot;
	if (!Data->ResolvePaths(WorkingDirectory, RepositoryRoot, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("MRBNN density preview could not resolve data paths: %s"), *Error.ToString());
		VolumeDensityVoxels->SetVisibility(false, true);
		return false;
	}

	TSharedPtr<FJsonObject> Config;
	if (!ReadJsonObject(FPaths::Combine(WorkingDirectory, TEXT("config.json")), Config))
	{
		UE_LOG(LogTemp, Warning, TEXT("MRBNN density preview could not read config.json."));
		VolumeDensityVoxels->SetVisibility(false, true);
		return false;
	}

	const TSharedPtr<FJsonObject>* VolumeObject = nullptr;
	if (!Config->TryGetObjectField(TEXT("volume"), VolumeObject) || !VolumeObject || !VolumeObject->IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("MRBNN density preview config has no volume object."));
		VolumeDensityVoxels->SetVisibility(false, true);
		return false;
	}

	FString VolumePath;
	if (!(*VolumeObject)->TryGetStringField(TEXT("path"), VolumePath) || VolumePath.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("MRBNN density preview config has no volume path."));
		VolumeDensityVoxels->SetVisibility(false, true);
		return false;
	}

	FIntVector VolumeResolution(257, 257, 257);
	const TArray<TSharedPtr<FJsonValue>>* ResolutionArray = nullptr;
	if ((*VolumeObject)->TryGetArrayField(TEXT("resolution"), ResolutionArray))
	{
		ReadIntArray3(ResolutionArray, VolumeResolution);
	}

	int32 SkipByteCount = 0;
	(*VolumeObject)->TryGetNumberField(TEXT("skip_byte_num"), SkipByteCount);

	FString ResolvedVolumePath = VolumePath;
	FPaths::NormalizeFilename(ResolvedVolumePath);
	if (FPaths::IsRelative(ResolvedVolumePath))
	{
		ResolvedVolumePath = FPaths::Combine(RepositoryRoot, ResolvedVolumePath);
	}
	FPaths::CollapseRelativeDirectories(ResolvedVolumePath);

	TArray<uint8> RawBytes;
	if (!FFileHelper::LoadFileToArray(RawBytes, *ResolvedVolumePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("MRBNN density preview could not read volume file: %s"), *ResolvedVolumePath);
		VolumeDensityVoxels->SetVisibility(false, true);
		return false;
	}

	const int64 VoxelCount = static_cast<int64>(VolumeResolution.X) * VolumeResolution.Y * VolumeResolution.Z;
	if (RawBytes.Num() < SkipByteCount + VoxelCount * static_cast<int64>(sizeof(float)))
	{
		UE_LOG(LogTemp, Warning, TEXT("MRBNN density preview volume file is smaller than config resolution requires."));
		VolumeDensityVoxels->SetVisibility(false, true);
		return false;
	}

	const float* DensityValues = reinterpret_cast<const float*>(RawBytes.GetData() + SkipByteCount);
	const int32 SafeSampleResolution = FMath::Clamp(DensitySampleResolution, 8, 96);
	const int32 SafeMaxInstances = FMath::Clamp(MaxDensityVoxelInstances, 1, 20000);
	TArray<FMRBNNDensityVoxelCandidate> Candidates;
	Candidates.Reserve(SafeMaxInstances * 4);

	const FVector SafeExtent(
		FMath::Max(VolumeExtent.X, 1.0f),
		FMath::Max(VolumeExtent.Y, 1.0f),
		FMath::Max(VolumeExtent.Z, 1.0f));
	const FVector CellSize = SafeExtent * 2.0f / static_cast<float>(SafeSampleResolution);

	for (int32 Z = 0; Z < SafeSampleResolution; ++Z)
	{
		const int32 SourceZ = FMath::Clamp(FMath::RoundToInt((static_cast<float>(Z) + 0.5f) / SafeSampleResolution * (VolumeResolution.Z - 1)), 0, VolumeResolution.Z - 1);
		for (int32 Y = 0; Y < SafeSampleResolution; ++Y)
		{
			const int32 SourceY = FMath::Clamp(FMath::RoundToInt((static_cast<float>(Y) + 0.5f) / SafeSampleResolution * (VolumeResolution.Y - 1)), 0, VolumeResolution.Y - 1);
			for (int32 X = 0; X < SafeSampleResolution; ++X)
			{
				const int32 SourceX = FMath::Clamp(FMath::RoundToInt((static_cast<float>(X) + 0.5f) / SafeSampleResolution * (VolumeResolution.X - 1)), 0, VolumeResolution.X - 1);
				const int64 SourceIndex =
					static_cast<int64>(SourceZ) * VolumeResolution.X * VolumeResolution.Y +
					static_cast<int64>(SourceY) * VolumeResolution.X +
					SourceX;
				const float Density = DensityValues[SourceIndex];
				if (!FMath::IsFinite(Density) || Density < DensityThreshold)
				{
					continue;
				}

				FMRBNNDensityVoxelCandidate Candidate;
				Candidate.Density = Density;
				Candidate.LocalPosition = FVector(
					(static_cast<float>(X) + 0.5f) / SafeSampleResolution * SafeExtent.X * 2.0f - SafeExtent.X,
					(static_cast<float>(Y) + 0.5f) / SafeSampleResolution * SafeExtent.Y * 2.0f - SafeExtent.Y,
					(static_cast<float>(Z) + 0.5f) / SafeSampleResolution * SafeExtent.Z * 2.0f - SafeExtent.Z);
				Candidates.Add(Candidate);
			}
		}
	}

	if (Candidates.IsEmpty())
	{
		VolumeDensityVoxels->SetVisibility(false, true);
		return false;
	}

	if (bFitDensityPreviewToBounds)
	{
		FBox DensityBounds(ForceInit);
		for (const FMRBNNDensityVoxelCandidate& Candidate : Candidates)
		{
			DensityBounds += Candidate.LocalPosition;
		}

		const FVector SourceSize = DensityBounds.GetSize();
		const FVector SourceCenter = DensityBounds.GetCenter();
		const FVector TargetSize = SafeExtent * 2.0f * FMath::Clamp(DensityBoundsFill, 0.1f, 1.0f);
		const FVector FitScale(
			SourceSize.X > KINDA_SMALL_NUMBER ? TargetSize.X / SourceSize.X : 1.0f,
			SourceSize.Y > KINDA_SMALL_NUMBER ? TargetSize.Y / SourceSize.Y : 1.0f,
			SourceSize.Z > KINDA_SMALL_NUMBER ? TargetSize.Z / SourceSize.Z : 1.0f);

		for (FMRBNNDensityVoxelCandidate& Candidate : Candidates)
		{
			Candidate.LocalPosition = (Candidate.LocalPosition - SourceCenter) * FitScale;
		}
	}

	TArray<FMRBNNDensityVoxelCandidate> ThinnedCandidates;
	const TArray<FMRBNNDensityVoxelCandidate>* InstanceCandidates = &Candidates;
	if (Candidates.Num() > SafeMaxInstances)
	{
		ThinnedCandidates.Reserve(SafeMaxInstances);
		const float Step = static_cast<float>(Candidates.Num()) / static_cast<float>(SafeMaxInstances);
		for (int32 Index = 0; Index < SafeMaxInstances; ++Index)
		{
			const int32 SourceIndex = FMath::Clamp(FMath::FloorToInt((static_cast<float>(Index) + 0.5f) * Step), 0, Candidates.Num() - 1);
			ThinnedCandidates.Add(Candidates[SourceIndex]);
		}
		InstanceCandidates = &ThinnedCandidates;
	}

	const int32 InstanceCount = InstanceCandidates->Num();
	const float VoxelWorldSize = FMath::Max3(CellSize.X, CellSize.Y, CellSize.Z) * FMath::Max(DensityVoxelScale, 0.1f);
	const FVector VoxelScale(VoxelWorldSize / 100.0f);
	for (int32 Index = 0; Index < InstanceCount; ++Index)
	{
		VolumeDensityVoxels->AddInstance(FTransform(FRotator::ZeroRotator, (*InstanceCandidates)[Index].LocalPosition, VoxelScale));
	}

	VolumeDensityVoxels->SetVisibility(InstanceCount > 0, true);
	return InstanceCount > 0;
}

void AMRBNNVolumeActor::UpdateRelightFromDirectionalLight()
{
	if (!MRBNNVolume || !bUseDirectionalLightForRelight)
	{
		return;
	}

	ADirectionalLight* LightActor = DirectionalLightActor;
	if (!LightActor && bAutoFindDirectionalLight)
	{
		LightActor = FindDirectionalLight();
		if (LightActor)
		{
			DirectionalLightActor = LightActor;
		}
	}

	if (!LightActor)
	{
		return;
	}

	const FVector WorldLightDirection = (-LightActor->GetActorForwardVector()).GetSafeNormal();
	const FVector LocalLightDirection = GetActorTransform().InverseTransformVectorNoScale(WorldLightDirection).GetSafeNormal();
	MRBNNVolume->RenderSettings.LightDirection = LocalLightDirection;

	FLinearColor LightColor = LightActor->GetLightColor();
	if (const UDirectionalLightComponent* DirectionalComponent = Cast<UDirectionalLightComponent>(LightActor->GetLightComponent()))
	{
		LightColor = DirectionalComponent->GetLightColor();
	}
	MRBNNVolume->RenderSettings.LightColor = FLinearColor(
		FMath::Max(LightColor.R, 0.05f) * 2.2f,
		FMath::Max(LightColor.G, 0.05f) * 2.2f,
		FMath::Max(LightColor.B, 0.05f) * 2.2f,
		1.0f);
}

void AMRBNNVolumeActor::UpdateVolumeMaterial()
{
	if (!VolumeSlices || !VolumeBillboard || !VolumeRaymarchMesh)
	{
		return;
	}

	if (!RaymarchMaterialInstance)
	{
		UMaterialInterface* RaymarchBaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/MRBNN/Materials/M_MRBNN_VolumeRaymarch.M_MRBNN_VolumeRaymarch"));
		if (RaymarchBaseMaterial)
		{
			RaymarchMaterialInstance = UMaterialInstanceDynamic::Create(RaymarchBaseMaterial, this);
			VolumeRaymarchMesh->SetMaterial(0, RaymarchMaterialInstance);
		}
	}

	UMaterialInterface* BaseMaterial = nullptr;
	if (!BillboardMaterialInstance || !VolumeMaterialInstance || (!DensityVoxelMaterialInstance && VolumeDensityVoxels))
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent_OneSided.Widget3DPassThrough_Translucent_OneSided"));
		if (!BaseMaterial)
		{
			BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque_OneSided.Widget3DPassThrough_Opaque_OneSided"));
		}
	}

	if (!BillboardMaterialInstance && BaseMaterial)
	{
		BillboardMaterialInstance = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		VolumeBillboard->SetMaterial(0, BillboardMaterialInstance);
	}

	if (!VolumeMaterialInstance && BaseMaterial)
	{
		VolumeMaterialInstance = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		VolumeSlices->SetMaterial(0, VolumeMaterialInstance);
	}

	if (!DensityVoxelMaterialInstance && BaseMaterial && VolumeDensityVoxels)
	{
		DensityVoxelMaterialInstance = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		VolumeDensityVoxels->SetMaterial(0, DensityVoxelMaterialInstance);
	}

	if (!BillboardMaterialInstance && !VolumeMaterialInstance && !DensityVoxelMaterialInstance && !RaymarchMaterialInstance)
	{
		return;
	}

	UTexture* PreviewTexture = GetPreviewTexture();
	const bool bHasLiveRender = HasRenderedPreviewTexture();
	const bool bCanShowFallback = bUseFallbackPreviewBeforeRender && PreviewTexture && !bHasLiveRender;
	const bool bCanShowPreview = PreviewTexture && (!bHideSlicesUntilFirstRender || bHasLiveRender || bCanShowFallback);
	const bool bCanShowBillboard = bShowVolumeBillboard && bCanShowPreview;
	const bool bCanShowSlices = bUseExperimentalSliceStack && bCanShowPreview;
	VolumeBillboard->SetVisibility(bCanShowBillboard, true);
	VolumeSlices->SetVisibility(bCanShowSlices, true);
	if (VolumeDensityVoxels)
	{
		VolumeDensityVoxels->SetVisibility(bShowDensityVolume && VolumeDensityVoxels->GetInstanceCount() > 0, true);
	}
	VolumeRaymarchMesh->SetVisibility(bUseRaymarchShader && RaymarchMaterialInstance && RaymarchDensityTexture, true);
	if (!bCanShowPreview && !DensityVoxelMaterialInstance)
	{
		UpdateRaymarchMaterial();
		return;
	}

	if (PreviewTexture)
	{
		PreviewTexture->Filter = TF_Bilinear;
	}

	ADirectionalLight* LightForMaterial = DirectionalLightActor;
	if (!LightForMaterial && bAutoFindDirectionalLight)
	{
		LightForMaterial = FindDirectionalLight();
	}

	const FVector WorldLightDirection = bUseDirectionalLightForRelight && LightForMaterial
		? (-LightForMaterial->GetActorForwardVector()).GetSafeNormal()
		: GetActorForwardVector();
	const float Facing = FMath::Clamp(FVector::DotProduct(GetActorForwardVector(), WorldLightDirection), 0.0f, 1.0f);
	const float RelitBrightness = PreviewBrightness * (AmbientRelight + Facing * DirectionalRelight);
	const float SafeOpacity = FMath::Clamp(SliceOpacity, 0.0f, 1.0f);
	if (BillboardMaterialInstance && PreviewTexture)
	{
		BillboardMaterialInstance->SetTextureParameterValue(TEXT("SlateUI"), PreviewTexture);
		BillboardMaterialInstance->SetVectorParameterValue(TEXT("TintColorAndOpacity"), FLinearColor(RelitBrightness, RelitBrightness, RelitBrightness, 1.0f));
		BillboardMaterialInstance->SetScalarParameterValue(TEXT("OpacityFromTexture"), 1.0f);
	}
	if (VolumeMaterialInstance && PreviewTexture)
	{
		VolumeMaterialInstance->SetTextureParameterValue(TEXT("SlateUI"), PreviewTexture);
		VolumeMaterialInstance->SetVectorParameterValue(TEXT("TintColorAndOpacity"), FLinearColor(RelitBrightness, RelitBrightness, RelitBrightness, SafeOpacity));
		VolumeMaterialInstance->SetScalarParameterValue(TEXT("OpacityFromTexture"), 1.0f);
	}
	if (DensityVoxelMaterialInstance)
	{
		UTexture* WhiteTexture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
		if (WhiteTexture)
		{
			DensityVoxelMaterialInstance->SetTextureParameterValue(TEXT("SlateUI"), WhiteTexture);
		}
		DensityVoxelMaterialInstance->SetVectorParameterValue(TEXT("TintColorAndOpacity"), FLinearColor(RelitBrightness, RelitBrightness, RelitBrightness, FMath::Clamp(DensityVoxelOpacity, 0.0f, 1.0f)));
		DensityVoxelMaterialInstance->SetScalarParameterValue(TEXT("OpacityFromTexture"), 1.0f);
	}

	UpdateRaymarchMaterial();
}

void AMRBNNVolumeActor::UpdateRaymarchMaterial()
{
	if (!RaymarchMaterialInstance || !VolumeRaymarchMesh)
	{
		return;
	}

	const bool bCanShowRaymarch = bUseRaymarchShader && RaymarchDensityTexture;
	VolumeRaymarchMesh->SetVisibility(bCanShowRaymarch, true);
	if (!bCanShowRaymarch)
	{
		return;
	}

	const FMatrix WorldToLocal = GetActorTransform().ToInverseMatrixWithScale();
	const FVector SafeExtent(
		FMath::Max(VolumeExtent.X, 1.0f),
		FMath::Max(VolumeExtent.Y, 1.0f),
		FMath::Max(VolumeExtent.Z, 1.0f));
	FVector LightDirection = FVector(0.35f, -0.35f, 0.86f).GetSafeNormal();
	if (MRBNNVolume)
	{
		LightDirection = MRBNNVolume->RenderSettings.LightDirection.GetSafeNormal(UE_SMALL_NUMBER, LightDirection);
	}

	const FTransform CurrentTransform = GetActorTransform();
	const int32 SafeStepCount = FMath::Clamp(RaymarchStepCount, 4, 96);
	const float SafeOpacity = FMath::Max(RaymarchOpacity, 0.0f);
	const float SafeAmbient = FMath::Max(AmbientRelight, 0.0f);
	const float SafeDirectional = FMath::Max(DirectionalRelight, 0.0f);
	const float SafeShadowStrength = FMath::Max(RaymarchShadowStrength, 0.0f);
	const float SafeLightStep = FMath::Max(RaymarchLightStep, 0.0f);
	const float SafeBrightness = FMath::Max(PreviewBrightness, 0.0f);
	const bool bParametersUnchanged =
		LastAppliedRaymarchDensityTexture == RaymarchDensityTexture &&
		LastAppliedRaymarchTransform.Equals(CurrentTransform) &&
		LastAppliedRaymarchLightDirection.Equals(LightDirection, KINDA_SMALL_NUMBER) &&
		LastAppliedRaymarchCloudColor == RaymarchCloudColor &&
		LastAppliedRaymarchExtent.Equals(SafeExtent, KINDA_SMALL_NUMBER) &&
		LastAppliedRaymarchStepCount == SafeStepCount &&
		FMath::IsNearlyEqual(LastAppliedRaymarchOpacity, SafeOpacity) &&
		FMath::IsNearlyEqual(LastAppliedRaymarchAmbient, SafeAmbient) &&
		FMath::IsNearlyEqual(LastAppliedRaymarchDirectional, SafeDirectional) &&
		FMath::IsNearlyEqual(LastAppliedRaymarchShadowStrength, SafeShadowStrength) &&
		FMath::IsNearlyEqual(LastAppliedRaymarchLightStep, SafeLightStep) &&
		FMath::IsNearlyEqual(LastAppliedRaymarchBrightness, SafeBrightness);
	if (bParametersUnchanged)
	{
		return;
	}

	RaymarchMaterialInstance->SetTextureParameterValue(TEXT("MRBNNDensityTexture"), RaymarchDensityTexture);
	RaymarchMaterialInstance->SetVectorParameterValue(TEXT("MRBNNWorldToLocal0"), FLinearColor(WorldToLocal.M[0][0], WorldToLocal.M[1][0], WorldToLocal.M[2][0], 0.0f));
	RaymarchMaterialInstance->SetVectorParameterValue(TEXT("MRBNNWorldToLocal1"), FLinearColor(WorldToLocal.M[0][1], WorldToLocal.M[1][1], WorldToLocal.M[2][1], 0.0f));
	RaymarchMaterialInstance->SetVectorParameterValue(TEXT("MRBNNWorldToLocal2"), FLinearColor(WorldToLocal.M[0][2], WorldToLocal.M[1][2], WorldToLocal.M[2][2], 0.0f));
	const FVector ActorLocation = CurrentTransform.GetLocation();
	RaymarchMaterialInstance->SetVectorParameterValue(TEXT("MRBNNActorWorldPosition"), FLinearColor(ActorLocation.X, ActorLocation.Y, ActorLocation.Z, 0.0f));
	RaymarchMaterialInstance->SetVectorParameterValue(TEXT("MRBNNHalfExtent"), FLinearColor(SafeExtent.X, SafeExtent.Y, SafeExtent.Z, 1.0f));
	RaymarchMaterialInstance->SetVectorParameterValue(TEXT("MRBNNLightDirectionLocal"), FLinearColor(LightDirection.X, LightDirection.Y, LightDirection.Z, 0.0f));
	RaymarchMaterialInstance->SetVectorParameterValue(TEXT("MRBNNCloudColor"), RaymarchCloudColor);

	RaymarchMaterialInstance->SetScalarParameterValue(TEXT("MRBNNRaySteps"), static_cast<float>(SafeStepCount));
	RaymarchMaterialInstance->SetScalarParameterValue(TEXT("MRBNNOpacity"), SafeOpacity);
	RaymarchMaterialInstance->SetScalarParameterValue(TEXT("MRBNNAmbient"), SafeAmbient);
	RaymarchMaterialInstance->SetScalarParameterValue(TEXT("MRBNNDirectional"), SafeDirectional);
	RaymarchMaterialInstance->SetScalarParameterValue(TEXT("MRBNNShadowStrength"), SafeShadowStrength);
	RaymarchMaterialInstance->SetScalarParameterValue(TEXT("MRBNNLightStep"), SafeLightStep);
	RaymarchMaterialInstance->SetScalarParameterValue(TEXT("MRBNNBrightness"), SafeBrightness);

	LastAppliedRaymarchDensityTexture = RaymarchDensityTexture;
	LastAppliedRaymarchTransform = CurrentTransform;
	LastAppliedRaymarchLightDirection = LightDirection;
	LastAppliedRaymarchCloudColor = RaymarchCloudColor;
	LastAppliedRaymarchExtent = SafeExtent;
	LastAppliedRaymarchStepCount = SafeStepCount;
	LastAppliedRaymarchOpacity = SafeOpacity;
	LastAppliedRaymarchAmbient = SafeAmbient;
	LastAppliedRaymarchDirectional = SafeDirectional;
	LastAppliedRaymarchShadowStrength = SafeShadowStrength;
	LastAppliedRaymarchLightStep = SafeLightStep;
	LastAppliedRaymarchBrightness = SafeBrightness;
}

void AMRBNNVolumeActor::UpdateDebugText()
{
	if (!DebugText)
	{
		return;
	}

	DebugText->SetVisibility(bShowDebugText, true);
	if (!bShowDebugText)
	{
		return;
	}

	FString Summary = TEXT("MRBNN Volume\nRenderer: not initialized");
	if (MRBNNVolume)
	{
		Summary = FString::Printf(
			TEXT("MRBNN Volume\nPreview=%s  Display=%s  Raymarch=%s %d^3/%d steps  Voxels=%d\nExtent=(%.0f %.0f %.0f)\n%s\nLastError: %s"),
			HasRenderedPreviewTexture() ? TEXT("live") : TEXT("fallback"),
			bUseRaymarchShader ? TEXT("volume shader") : (bShowDensityVolume ? TEXT("density voxels") : (bUseExperimentalSliceStack ? TEXT("experimental slices") : TEXT("billboard"))),
			RaymarchDensityTexture ? TEXT("ready") : TEXT("missing"),
			RaymarchDensityTexture ? RaymarchDensityTexture->GetSizeX() : 0,
			FMath::Clamp(RaymarchStepCount, 4, 96),
			VolumeDensityVoxels ? VolumeDensityVoxels->GetInstanceCount() : 0,
			VolumeExtent.X,
			VolumeExtent.Y,
			VolumeExtent.Z,
			*MRBNNVolume->GetDebugSummary(),
			*MRBNNVolume->GetLastError().ToString());
	}
	DebugText->SetText(FText::FromString(Summary));
}

void AMRBNNVolumeActor::MaybeRenderEditorPreviewOnce()
{
	if (!bAllowLiveRenderInEditor || !bAutoRenderEditorPreviewOnce || bEditorPreviewRenderAttempted || HasRenderedPreviewTexture() || !MRBNNVolume)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World || World->IsGameWorld())
	{
		return;
	}

	UpdateRelightFromDirectionalLight();
	bEditorPreviewRenderAttempted = true;
	MRBNNVolume->RenderOnce();
}

bool AMRBNNVolumeActor::HasRenderedPreviewTexture() const
{
	return MRBNNVolume && MRBNNVolume->GetOutputRenderTarget() && MRBNNVolume->LastFrameIndex > 0;
}

ADirectionalLight* AMRBNNVolumeActor::FindDirectionalLight() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		return *It;
	}

	return nullptr;
}

UTexture* AMRBNNVolumeActor::GetPreviewTexture()
{
	if (HasRenderedPreviewTexture())
	{
		return Cast<UTexture>(MRBNNVolume->GetOutputRenderTarget());
	}

	return bUseFallbackPreviewBeforeRender ? LoadFallbackPreviewTexture() : nullptr;
}

UTexture* AMRBNNVolumeActor::LoadFallbackPreviewTexture()
{
	if (FallbackPreviewTexture)
	{
		return FallbackPreviewTexture;
	}

	const FString PreviewPath = FindFallbackPreviewPath();
	if (PreviewPath.IsEmpty() || !FPaths::FileExists(PreviewPath))
	{
		return nullptr;
	}

	FallbackPreviewTexture = FImageUtils::ImportFileAsTexture2D(PreviewPath);
	if (FallbackPreviewTexture)
	{
		FallbackPreviewTexture->Filter = TF_Bilinear;
	}
	return FallbackPreviewTexture;
}

FString AMRBNNVolumeActor::FindFallbackPreviewPath() const
{
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MRBNN")))
	{
		const FString PluginBaseDir = Plugin->GetBaseDir();
		const FString PreviewPath = FPaths::Combine(PluginBaseDir, TEXT("Binaries/ThirdParty/MRBNNBridge/Win64/MRBNNBridgeSmokeTestPreview.png"));
		if (FPaths::FileExists(PreviewPath))
		{
			return PreviewPath;
		}
	}

	return FString();
}
