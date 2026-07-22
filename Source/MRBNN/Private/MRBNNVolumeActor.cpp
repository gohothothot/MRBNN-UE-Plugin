#include "MRBNNVolumeActor.h"

#include "MRBNNBlueprintLibrary.h"
#include "MRBNNBakedVolumeData.h"
#include "MRBNNProjectSettings.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/Light.h"
#include "Engine/SkyLight.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/VolumeTexture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Math/Float16.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MRBNNSceneViewExtension.h"
#include "MRBNNVolumeComponent.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

namespace
{
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

struct FMRBNNEncodingLevelInfo
{
	int32 Resolution = 0;
	int32 FeatureCount = 0;
	int64 ValueOffset = 0;
	int64 PaddedCellCount = 0;

	bool IsValid() const
	{
		return Resolution > 0 && FeatureCount > 0 && ValueOffset >= 0 && PaddedCellCount > 0;
	}
};

int64 IntPow(int64 Value, int32 Power)
{
	int64 Result = 1;
	for (int32 Index = 0; Index < Power; ++Index)
	{
		Result *= Value;
	}
	return Result;
}

int64 NextMultiple(int64 Value, int64 Multiple)
{
	if (Multiple <= 0)
	{
		return Value;
	}
	return ((Value + Multiple - 1) / Multiple) * Multiple;
}

int32 ComputeEncodingResolution(int32 Level, double BaseResolution, double PerLevelScale)
{
	const double GridScale = FMath::Pow(PerLevelScale, static_cast<double>(Level)) * BaseResolution - 1.0;
	return FMath::Max(FMath::CeilToInt(GridScale) + 1, 1);
}

bool TryBuildEncodingLevelInfo(
	const TSharedPtr<FJsonObject>& Config,
	const FString& EncodingKey,
	int32 InputDims,
	int32 RequestedLevel,
	int64 RawByteCount,
	FMRBNNEncodingLevelInfo& OutInfo)
{
	OutInfo = FMRBNNEncodingLevelInfo();

	const TSharedPtr<FJsonObject>* EncodingObject = nullptr;
	if (!Config.IsValid() || !Config->TryGetObjectField(EncodingKey, EncodingObject) || !EncodingObject || !EncodingObject->IsValid())
	{
		return false;
	}

	double LevelCountNumber = 0.0;
	double BaseResolutionNumber = 0.0;
	double PerLevelScaleNumber = 0.0;
	double FeatureCountNumber = 0.0;
	if (!(*EncodingObject)->TryGetNumberField(TEXT("n_levels"), LevelCountNumber) ||
		!(*EncodingObject)->TryGetNumberField(TEXT("base_resolution"), BaseResolutionNumber) ||
		!(*EncodingObject)->TryGetNumberField(TEXT("per_level_scale"), PerLevelScaleNumber) ||
		!(*EncodingObject)->TryGetNumberField(TEXT("n_features_per_level"), FeatureCountNumber))
	{
		return false;
	}

	const int32 LevelCount = FMath::Max(FMath::RoundToInt(LevelCountNumber), 0);
	const int32 FeatureCount = FMath::Max(FMath::RoundToInt(FeatureCountNumber), 0);
	if (LevelCount <= 0 || FeatureCount <= 0 || InputDims <= 0)
	{
		return false;
	}

	const int32 Level = FMath::Clamp(RequestedLevel, 0, LevelCount - 1);
	int64 CellOffset = 0;
	for (int32 LevelIndex = 0; LevelIndex < Level; ++LevelIndex)
	{
		const int32 LevelResolution = ComputeEncodingResolution(LevelIndex, BaseResolutionNumber, PerLevelScaleNumber);
		CellOffset += NextMultiple(IntPow(LevelResolution, InputDims), 8);
	}

	const int32 Resolution = ComputeEncodingResolution(Level, BaseResolutionNumber, PerLevelScaleNumber);
	const int64 PaddedCellCount = NextMultiple(IntPow(Resolution, InputDims), 8);
	const int64 ValueOffset = CellOffset * FeatureCount;
	const int64 RequiredByteCount = (ValueOffset + PaddedCellCount * FeatureCount) * static_cast<int64>(sizeof(FFloat16));
	if (RequiredByteCount > RawByteCount)
	{
		return false;
	}

	OutInfo.Resolution = Resolution;
	OutInfo.FeatureCount = FeatureCount;
	OutInfo.ValueOffset = ValueOffset;
	OutInfo.PaddedCellCount = PaddedCellCount;
	return true;
}

float ReadHalfFeatureValue(const TArray<uint8>& RawBytes, int64 HalfIndex)
{
	const int64 ByteIndex = HalfIndex * static_cast<int64>(sizeof(FFloat16));
	if (ByteIndex < 0 || ByteIndex + static_cast<int64>(sizeof(FFloat16)) > RawBytes.Num())
	{
		return 0.0f;
	}

	FFloat16 HalfValue;
	FMemory::Memcpy(&HalfValue, RawBytes.GetData() + ByteIndex, sizeof(FFloat16));
	const float Value = HalfValue.GetFloat();
	return FMath::IsFinite(Value) ? Value : 0.0f;
}

float SampleEncodingMeanAbsNearest(const TArray<uint8>& RawBytes, const FMRBNNEncodingLevelInfo& Info, const FVector3f& Coord)
{
	if (!Info.IsValid() || RawBytes.IsEmpty())
	{
		return 0.0f;
	}

	const int32 Last = FMath::Max(Info.Resolution - 1, 0);
	const int32 X = FMath::Clamp(FMath::RoundToInt(FMath::Clamp(Coord.X, 0.0f, 1.0f) * static_cast<float>(Last)), 0, Last);
	const int32 Y = FMath::Clamp(FMath::RoundToInt(FMath::Clamp(Coord.Y, 0.0f, 1.0f) * static_cast<float>(Last)), 0, Last);
	const int32 Z = FMath::Clamp(FMath::RoundToInt(FMath::Clamp(Coord.Z, 0.0f, 1.0f) * static_cast<float>(Last)), 0, Last);
	const int64 CellIndex =
		static_cast<int64>(Z) * Info.Resolution * Info.Resolution +
		static_cast<int64>(Y) * Info.Resolution +
		X;
	if (CellIndex < 0 || CellIndex >= Info.PaddedCellCount)
	{
		return 0.0f;
	}

	float SumAbs = 0.0f;
	for (int32 FeatureIndex = 0; FeatureIndex < Info.FeatureCount; ++FeatureIndex)
	{
		const int64 HalfIndex = Info.ValueOffset + CellIndex * Info.FeatureCount + FeatureIndex;
		SumAbs += FMath::Abs(ReadHalfFeatureValue(RawBytes, HalfIndex));
	}
	return SumAbs / static_cast<float>(Info.FeatureCount);
}

uint8 Quantize01(float Value)
{
	return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Value, 0.0f, 1.0f) * 255.0f));
}

uint8 QuantizeFeatureEnergy(float Value, float MeanValue, float MaxValue)
{
	const float Scale = FMath::Max3(MeanValue * 3.0f, MaxValue * 0.22f, 0.0001f);
	return Quantize01(Value / Scale);
}

FVector GetSafeVolumeExtent(const FVector& VolumeExtent)
{
	return FVector(
		FMath::Max(VolumeExtent.X, 1.0f),
		FMath::Max(VolumeExtent.Y, 1.0f),
		FMath::Max(VolumeExtent.Z, 1.0f));
}

FVector TransformWorldPositionToVolumeUnitBox(const FTransform& ActorTransform, const FVector& VolumeExtent, const FVector& WorldPosition)
{
	const FVector SafeExtent = GetSafeVolumeExtent(VolumeExtent);
	const FVector LocalPosition = ActorTransform.InverseTransformPosition(WorldPosition);
	return FVector(
		LocalPosition.X / (SafeExtent.X * 2.0f),
		LocalPosition.Y / (SafeExtent.Y * 2.0f),
		LocalPosition.Z / (SafeExtent.Z * 2.0f));
}

FVector TransformWorldVectorToVolumeUnitBox(const FTransform& ActorTransform, const FVector& VolumeExtent, const FVector& WorldVector, const FVector& Fallback)
{
	const FVector SafeExtent = GetSafeVolumeExtent(VolumeExtent);
	const FVector LocalVector = ActorTransform.InverseTransformVectorNoScale(WorldVector);
	const FVector VolumeVector(
		LocalVector.X / (SafeExtent.X * 2.0f),
		LocalVector.Y / (SafeExtent.Y * 2.0f),
		LocalVector.Z / (SafeExtent.Z * 2.0f));
	return VolumeVector.GetSafeNormal(UE_SMALL_NUMBER, Fallback);
}

FVector TransformWorldVectorToVolumeUnitBoxUnnormalized(const FTransform& ActorTransform, const FVector& VolumeExtent, const FVector& WorldVector)
{
	const FVector SafeExtent = GetSafeVolumeExtent(VolumeExtent);
	const FVector LocalVector = ActorTransform.InverseTransformVectorNoScale(WorldVector);
	return FVector(
		LocalVector.X / (SafeExtent.X * 2.0f),
		LocalVector.Y / (SafeExtent.Y * 2.0f),
		LocalVector.Z / (SafeExtent.Z * 2.0f));
}

float UnitToSourceCoordinate(float Unit, int32 SourceMin, int32 SourceMax)
{
	return FMath::Lerp(static_cast<float>(SourceMin), static_cast<float>(SourceMax), FMath::Clamp(Unit, 0.0f, 1.0f));
}

FVector3f MakeSourcePositionFromVolumeUnit(
	float UnitX,
	float UnitY,
	float UnitZ,
	const FIntVector& SourceMin,
	const FIntVector& SourceMax,
	bool bUseMRBNNCloudAxisMapping)
{
	if (bUseMRBNNCloudAxisMapping)
	{
		return FVector3f(
			UnitToSourceCoordinate(UnitY, SourceMin.X, SourceMax.X),
			UnitToSourceCoordinate(UnitZ, SourceMin.Y, SourceMax.Y),
			UnitToSourceCoordinate(UnitX, SourceMin.Z, SourceMax.Z));
	}

	return FVector3f(
		UnitToSourceCoordinate(UnitX, SourceMin.X, SourceMax.X),
		UnitToSourceCoordinate(UnitY, SourceMin.Y, SourceMax.Y),
		UnitToSourceCoordinate(UnitZ, SourceMin.Z, SourceMax.Z));
}

struct FMRBNNBoundedSceneLightCandidate
{
	FVector4f PositionAndInvRadius = FVector4f::Zero();
	FVector4f ColorAndIntensity = FVector4f::Zero();
	FVector4f DirectionAndSpot = FVector4f::Zero();
	FVector4f TypeAndShape = FVector4f::Zero();
	float Score = 0.0f;
};

float GetVolumeRadiusFromWorldRadius(const FVector& VolumeExtent, float WorldRadius)
{
	const FVector SafeExtent = GetSafeVolumeExtent(VolumeExtent);
	const float MaxDiameter = FMath::Max3(SafeExtent.X, SafeExtent.Y, SafeExtent.Z) * 2.0f;
	return FMath::Max(WorldRadius / FMath::Max(MaxDiameter, 1.0f), 0.001f);
}

float GetLightLuminance(const FLinearColor& LightColor)
{
	return LightColor.R * 0.2126f + LightColor.G * 0.7152f + LightColor.B * 0.0722f;
}

uint32 HashVector4f(uint32 Seed, const FVector4f& Value)
{
	return FCrc::MemCrc32(&Value, sizeof(Value), Seed);
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

	MRBNNVolume = CreateDefaultSubobject<UMRBNNVolumeComponent>(TEXT("MRBNNVolume"));
	MRBNNVolume->bUseProjectSettingsWhenBakedDataMissing = true;
	MRBNNVolume->bAutoInitialize = false;
	MRBNNVolume->bRenderEveryTick = false;
	MRBNNVolume->bUsePlayerCamera = true;
	MRBNNVolume->bApplyOutputToMaterials = false;
	MRBNNVolume->bAllowAutomaticRenderInEditor = false;
	SyncComponentSettingsFromActor();
}

void AMRBNNVolumeActor::SyncComponentSettingsFromActor()
{
	if (!MRBNNVolume)
	{
		return;
	}

	MRBNNVolume->BakedData = ResolveActiveBakedData();
	MRBNNVolume->bUseProjectSettingsWhenBakedDataMissing = bUseProjectSettingsWhenBakedDataMissing;
	MRBNNVolume->OutputWidth = FMath::Max(OutputWidth, 1);
	MRBNNVolume->OutputHeight = FMath::Max(OutputHeight, 1);
	MRBNNVolume->SamplesPerRender = FMath::Max(SamplesPerRender, 1);
	MRBNNVolume->bAccumulateFrames = bAccumulateFrames;
	MRBNNVolume->MaxAccumulatedFrames = FMath::Max(MaxAccumulatedFrames, 1);
	MRBNNVolume->SpatialDenoisePasses = FMath::Clamp(SpatialDenoisePasses, 0, 4);
	MRBNNVolume->bAutoInitialize = false;
	MRBNNVolume->bRenderEveryTick = false;
	MRBNNVolume->bUsePlayerCamera = true;
	MRBNNVolume->bApplyOutputToMaterials = false;
	MRBNNVolume->bAllowAutomaticRenderInEditor = bAllowLiveRenderInEditor;
}

UMRBNNBakedVolumeData* AMRBNNVolumeActor::ResolveActiveBakedData()
{
	if (BakedData || !bUseProjectSettingsWhenBakedDataMissing)
	{
		return BakedData;
	}

	FText Error;
	BakedData = UMRBNNProjectSettings::Get()->CreateTransientDefaultBakedData(this, Error);
	if (!BakedData && !Error.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("MRBNN project data could not be resolved: %s"), *Error.ToString());
	}
	return BakedData;
}

void AMRBNNVolumeActor::RefreshResolvedDataFields()
{
	ResolvedRepositoryRoot.Reset();
	ResolvedWorkingDirectory.Reset();
	ResolvedDensityVolumePath.Reset();

	UMRBNNBakedVolumeData* Data = ResolveActiveBakedData();
	if (!Data)
	{
		return;
	}

	FText Error;
	if (!Data->ResolvePaths(ResolvedWorkingDirectory, ResolvedRepositoryRoot, Error))
	{
		ResolvedWorkingDirectory = Error.ToString();
		return;
	}

	FString VolumePath;
	FIntVector VolumeResolution;
	int32 SkipByteCount = 0;
	if (ResolveDensityVolumeFile(VolumePath, VolumeResolution, SkipByteCount, Error))
	{
		ResolvedDensityVolumePath = VolumePath;
	}
	else if (!Error.IsEmpty())
	{
		ResolvedDensityVolumePath = Error.ToString();
	}
}

void AMRBNNVolumeActor::ResetRenderState()
{
	if (MRBNNVolume)
	{
		MRBNNVolume->ResetProgressiveAccumulation();
	}
	bEditorPreviewRenderAttempted = false;
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
		SyncComponentSettingsFromActor();
		RefreshResolvedDataFields();
		UpdateVolumeProxy();
		if (!bRaymarchTextureBuilt)
		{
			BuildRaymarchVolumeTexture();
		}
		UpdateRelightFromDirectionalLight();
		EnsureComputeViewExtension();
		if (!ShouldUseSceneViewExtensionRenderPass())
		{
			RenderComputeGlobalShaderPreview();
		}
		UpdateVolumeMaterial();
		UpdateDebugText();
	}
}

#if WITH_EDITOR
void AMRBNNVolumeActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	const FName PropertyName = PropertyChangedEvent.MemberProperty
		? PropertyChangedEvent.MemberProperty->GetFName()
		: (PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None);

	if (PropertyName == GET_MEMBER_NAME_CHECKED(AMRBNNVolumeActor, bAutoConfigureFromProjectSettings) && bAutoConfigureFromProjectSettings)
	{
		ConfigureFromProjectSettings();
		return;
	}

	const bool bNeedsTextureRebuild =
		PropertyName == GET_MEMBER_NAME_CHECKED(AMRBNNVolumeActor, BakedData) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AMRBNNVolumeActor, bUseProjectSettingsWhenBakedDataMissing) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AMRBNNVolumeActor, RaymarchTextureResolution) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AMRBNNVolumeActor, bRaymarchFitToDensityBounds) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AMRBNNVolumeActor, bUseMRBNNCloudAxisMapping) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AMRBNNVolumeActor, RaymarchBoundsThreshold) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AMRBNNVolumeActor, RaymarchBoundsPadding) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AMRBNNVolumeActor, RaymarchInputThreshold) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AMRBNNVolumeActor, RaymarchNormalizeDensity) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AMRBNNVolumeActor, RaymarchDensityPower) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AMRBNNVolumeActor, bUseBakedFeatureLighting) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AMRBNNVolumeActor, RaymarchBakedFeatureLevel);

	SyncComponentSettingsFromActor();
	RefreshResolvedDataFields();
	UpdateVolumeProxy();
	UpdateRelightFromDirectionalLight();

	if (bNeedsTextureRebuild)
	{
		bRaymarchTextureBuilt = false;
		RaymarchDensityTexture = nullptr;
		RaymarchFeatureTexture = nullptr;
		RaymarchTextureBuildKey.Empty();
	}
	ResetRenderState();

	if (bAutoRebuildOnParameterChange)
	{
		if (bNeedsTextureRebuild)
		{
			BuildRaymarchVolumeTexture();
		}
		EnsureComputeViewExtension();
		if (!ShouldUseSceneViewExtensionRenderPass())
		{
			RenderComputeGlobalShaderPreview();
		}
	}
}
#endif

void AMRBNNVolumeActor::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoConfigureFromProjectSettings)
	{
		ConfigureFromProjectSettings();
	}
	else
	{
		SyncComponentSettingsFromActor();
		RefreshResolvedDataFields();
	}

	EnsureComputeViewExtension();

	if (bAutoRenderOnBeginPlay && MRBNNVolume && !bUseComputeGlobalShader)
	{
		MRBNNVolume->InitializeRenderer();
		MRBNNVolume->RenderOnce();
	}
	if (!ShouldUseSceneViewExtensionRenderPass())
	{
		RenderComputeGlobalShaderPreview();
	}

	UpdateVolumeMaterial();
	UpdateDebugText();
}

void AMRBNNVolumeActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ComputeViewExtension.Reset();
	Super::EndPlay(EndPlayReason);
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
	SyncComponentSettingsFromActor();
	UpdateRelightFromDirectionalLight();
	EnsureComputeViewExtension();
	if (!ShouldUseSceneViewExtensionRenderPass())
	{
		RenderComputeGlobalShaderPreview();
	}
	UpdateVolumeMaterial();
	UpdateDebugText();
}

void AMRBNNVolumeActor::ConfigureFromProjectSettings()
{
	const UMRBNNProjectSettings* Settings = UMRBNNProjectSettings::Get();
	bUseProjectSettingsWhenBakedDataMissing = true;
	if (!BakedData)
	{
		FText Error;
		BakedData = Settings->CreateTransientDefaultBakedData(this, Error);
	}
	SyncComponentSettingsFromActor();
	RefreshResolvedDataFields();
	ResetRenderState();
	bEditorPreviewRenderAttempted = false;

	UpdateVolumeProxy();
	BuildRaymarchVolumeTexture();
	UpdateRelightFromDirectionalLight();
	EnsureComputeViewExtension();
	if (!ShouldUseSceneViewExtensionRenderPass())
	{
		RenderComputeGlobalShaderPreview();
	}
	UpdateVolumeMaterial();
	UpdateDebugText();
}

bool AMRBNNVolumeActor::BakeCurrentDataToPluginData()
{
	if (!MRBNNVolume)
	{
		return false;
	}

	UMRBNNBakedVolumeData* SourceData = ResolveActiveBakedData();
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
		BakedData = nullptr;
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
	EnsureComputeViewExtension();
	const bool bRendered = bUseComputeGlobalShader
		? (ShouldUseSceneViewExtensionRenderPass() || RenderComputeGlobalShaderPreview())
		: MRBNNVolume->RenderOnce();
	bEditorPreviewRenderAttempted = true;
	UpdateVolumeMaterial();
	UpdateDebugText();
	return bRendered;
}

void AMRBNNVolumeActor::ApplyRealtimePreviewSettings()
{
	bUseComputeGlobalShader = true;
	bUseSceneViewExtensionRenderPass = true;
	bUseRaymarchShader = false;
	OutputWidth = 384;
	OutputHeight = 384;
	SamplesPerRender = 1;
	bAccumulateFrames = true;
	MaxAccumulatedFrames = 12;
	SpatialDenoisePasses = 0;
	bUsePaperStyleCinematicPath = false;
	CinematicLightOpticalDepthSteps = 12;
	CinematicInscatterSteps = 3;
	CinematicTransmittanceScale = 1.0f;
	CinematicMultiScatterStrength = 0.55f;
	CinematicFeatureParticipation = 0.45f;
	VolumeExtent = FVector(240.0f, 260.0f, 95.0f);
	RaymarchTextureResolution = 80;
	bRaymarchFitToDensityBounds = true;
	bUseMRBNNCloudAxisMapping = true;
	RaymarchBoundsThreshold = 1.0f;
	RaymarchBoundsPadding = 0.14f;
	RaymarchCloudFlowDirection = FVector(1.0, 0.15, 0.0);
	RaymarchCloudFlowSpeed = 0.0f;
	RaymarchStepCount = 40;
	RaymarchInputThreshold = 4.0f;
	RaymarchNormalizeDensity = 96.0f;
	RaymarchDensityPower = 0.82f;
	RaymarchOpacity = 0.055f;
	RaymarchShadowStrength = 0.55f;
	RaymarchLightStep = 0.075f;
	RaymarchDirectLightIntensityScale = 1.0f;
	RaymarchDirectShadowSteps = 4;
	RaymarchDirectShadowDensity = 1.35f;
	RaymarchPhaseG = 0.35f;
	RaymarchPhaseStrength = 0.75f;
	RaymarchEdgeSilverStrength = 0.55f;
	RaymarchSilverLiningSharpness = 1.2f;
	RaymarchDeepShadowStrength = 0.55f;
	RaymarchPowderStrength = 0.45f;
	bUseBakedFeatureLighting = true;
	RaymarchBakedFeatureLevel = 2;
	RaymarchBakedFeatureContribution = 0.4f;
	RaymarchMultiScatterContribution = 0.55f;
	RaymarchMultiScatterIsotropy = 0.6f;
	RaymarchFeatureAlbedoBlend = 0.18f;
	RaymarchBakedFeatureTint = FLinearColor(1.0f, 0.965f, 0.88f, 1.0f);
	SceneSkyAtmosphereIntensityScale = 0.4f;
	SceneFogContributionScale = 1.0f;
	SceneColorContributionScale = 1.0f;
	AmbientRelight = 0.22f;
	DirectionalRelight = 1.25f;
	bEditorPreviewRenderAttempted = false;
	SyncComponentSettingsFromActor();
	if (MRBNNVolume)
	{
		MRBNNVolume->RenderSettings.bFastDirectIllumination = true;
		MRBNNVolume->RenderSettings.bEnableSkybox = false;
		MRBNNVolume->RenderSettings.bEnableSkyboxBaking = false;
	}
	RefreshResolvedDataFields();
	ResetRenderState();
	RebuildVolumeShader();
	EnsureComputeViewExtension();
	if (!ShouldUseSceneViewExtensionRenderPass())
	{
		RenderComputeGlobalShaderPreview();
	}
	UpdateVolumeProxy();
	UpdateVolumeMaterial();
	UpdateDebugText();
}

void AMRBNNVolumeActor::ApplyMobilePreviewSettings()
{
	bUseComputeGlobalShader = true;
	bUseSceneViewExtensionRenderPass = true;
	bUseRaymarchShader = false;
	OutputWidth = 256;
	OutputHeight = 256;
	SamplesPerRender = 1;
	bAccumulateFrames = true;
	MaxAccumulatedFrames = 6;
	SpatialDenoisePasses = 0;
	bUsePaperStyleCinematicPath = false;
	CinematicLightOpticalDepthSteps = 8;
	CinematicInscatterSteps = 2;
	CinematicTransmittanceScale = 1.0f;
	CinematicMultiScatterStrength = 0.4f;
	CinematicFeatureParticipation = 0.35f;
	VolumeExtent = FVector(240.0f, 260.0f, 95.0f);
	RaymarchTextureResolution = 48;
	bRaymarchFitToDensityBounds = true;
	bUseMRBNNCloudAxisMapping = true;
	RaymarchBoundsThreshold = 2.0f;
	RaymarchBoundsPadding = 0.12f;
	RaymarchCloudFlowDirection = FVector(1.0, 0.15, 0.0);
	RaymarchCloudFlowSpeed = 0.0f;
	RaymarchStepCount = 22;
	RaymarchInputThreshold = 6.0f;
	RaymarchNormalizeDensity = 104.0f;
	RaymarchDensityPower = 0.88f;
	RaymarchOpacity = 0.075f;
	RaymarchShadowStrength = 0.35f;
	RaymarchLightStep = 0.08f;
	RaymarchDirectLightIntensityScale = 0.85f;
	RaymarchDirectShadowSteps = 2;
	RaymarchDirectShadowDensity = 1.1f;
	RaymarchPhaseG = 0.25f;
	RaymarchPhaseStrength = 0.55f;
	RaymarchEdgeSilverStrength = 0.35f;
	RaymarchSilverLiningSharpness = 1.0f;
	RaymarchDeepShadowStrength = 0.4f;
	RaymarchPowderStrength = 0.3f;
	bUseBakedFeatureLighting = true;
	RaymarchBakedFeatureLevel = 1;
	RaymarchBakedFeatureContribution = 0.45f;
	RaymarchMultiScatterContribution = 0.5f;
	RaymarchMultiScatterIsotropy = 0.45f;
	RaymarchFeatureAlbedoBlend = 0.18f;
	RaymarchBakedFeatureTint = FLinearColor(1.0f, 0.965f, 0.88f, 1.0f);
	SceneSkyAtmosphereIntensityScale = 0.28f;
	SceneFogContributionScale = 0.65f;
	SceneColorContributionScale = 0.75f;
	AmbientRelight = 0.18f;
	DirectionalRelight = 1.0f;
	bEditorPreviewRenderAttempted = false;
	SyncComponentSettingsFromActor();
	if (MRBNNVolume)
	{
		MRBNNVolume->RenderSettings.bFastDirectIllumination = true;
		MRBNNVolume->RenderSettings.bEnableSkybox = false;
		MRBNNVolume->RenderSettings.bEnableSkyboxBaking = false;
	}
	RefreshResolvedDataFields();
	ResetRenderState();
	RebuildVolumeShader();
	EnsureComputeViewExtension();
	if (!ShouldUseSceneViewExtensionRenderPass())
	{
		RenderComputeGlobalShaderPreview();
	}
	UpdateVolumeProxy();
	UpdateVolumeMaterial();
	UpdateDebugText();
}

void AMRBNNVolumeActor::ApplyPaperPreviewSettings()
{
	bUseComputeGlobalShader = true;
	bUseSceneViewExtensionRenderPass = true;
	bUseRaymarchShader = false;
	bUseBakedFeatureLighting = true;
	bAutoRenderOnBeginPlay = true;
	bAllowLiveRenderInEditor = true;
	bAutoRenderEditorPreviewOnce = true;
	OutputWidth = 1024;
	OutputHeight = 1024;
	SamplesPerRender = 8;
	bAccumulateFrames = true;
	MaxAccumulatedFrames = 128;
	SpatialDenoisePasses = 2;
	bUsePaperStyleCinematicPath = true;
	CinematicLightOpticalDepthSteps = 24;
	CinematicInscatterSteps = 4;
	CinematicTransmittanceScale = 1.45f;
	CinematicMultiScatterStrength = 1.15f;
	CinematicFeatureParticipation = 0.85f;
	VolumeExtent = FVector(240.0f, 260.0f, 95.0f);
	RaymarchTextureResolution = 96;
	bRaymarchFitToDensityBounds = true;
	bUseMRBNNCloudAxisMapping = true;
	RaymarchBoundsThreshold = 0.45f;
	RaymarchBoundsPadding = 0.18f;
	RaymarchCloudFlowDirection = FVector(1.0, 0.25, 0.04);
	RaymarchCloudFlowSpeed = 0.018f;
	RaymarchInputThreshold = 0.8f;
	RaymarchNormalizeDensity = 88.0f;
	RaymarchDensityPower = 0.62f;
	RaymarchStepCount = 72;
	RaymarchBakedFeatureLevel = 3;
	RaymarchBakedFeatureContribution = 0.55f;
	RaymarchMultiScatterContribution = 0.95f;
	RaymarchMultiScatterIsotropy = 0.86f;
	RaymarchFeatureAlbedoBlend = 0.26f;
	RaymarchOpacity = 0.048f;
	RaymarchShadowStrength = 0.62f;
	RaymarchDirectShadowSteps = 6;
	RaymarchEdgeSilverStrength = 0.95f;
	RaymarchSilverLiningSharpness = 2.35f;
	RaymarchDeepShadowStrength = 0.68f;
	RaymarchPowderStrength = 0.58f;
	SceneSkyAtmosphereIntensityScale = 0.58f;
	SceneFogContributionScale = 1.35f;
	SceneColorContributionScale = 1.45f;
	PreviewBrightness = 1.65f;
	AmbientRelight = 0.18f;
	DirectionalRelight = 1.45f;

	SyncComponentSettingsFromActor();
	if (MRBNNVolume)
	{
		MRBNNVolume->RenderSettings.ToneMapping = EMRBNNToneMapping::ACES;
		MRBNNVolume->RenderSettings.Denoise = EMRBNNDenoiseMode::VisualPlausible;
		MRBNNVolume->RenderSettings.Compatibility = EMRBNNCompatibilityMode::Normal;
		MRBNNVolume->RenderSettings.bExcludeLightEncoding = true;
		MRBNNVolume->RenderSettings.bFastDirectIllumination = false;
		MRBNNVolume->RenderSettings.bEnableSkybox = false;
		MRBNNVolume->RenderSettings.bEnableSkyboxBaking = true;
	}
	RefreshResolvedDataFields();
	ResetRenderState();
	BuildRaymarchVolumeTexture();
	UpdateRelightFromDirectionalLight();
	EnsureComputeViewExtension();
	if (!ShouldUseSceneViewExtensionRenderPass())
	{
		RenderComputeGlobalShaderPreview();
	}
	UpdateVolumeProxy();
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
	SyncComponentSettingsFromActor();
	RefreshResolvedDataFields();
	ResetRenderState();
	bRaymarchTextureBuilt = false;
	const bool bBuilt = BuildRaymarchVolumeTexture();
	UpdateRelightFromDirectionalLight();
	UpdateVolumeProxy();
	EnsureComputeViewExtension();
	if (!ShouldUseSceneViewExtensionRenderPass())
	{
		RenderComputeGlobalShaderPreview();
	}
	return bBuilt;
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
}

bool AMRBNNVolumeActor::ResolveDensityVolumeFile(FString& OutVolumePath, FIntVector& OutResolution, int32& OutSkipByteCount, FText& OutError)
{
	OutVolumePath.Empty();
	OutResolution = FIntVector(257, 257, 257);
	OutSkipByteCount = 0;

	UMRBNNBakedVolumeData* Data = ResolveActiveBakedData();
	if (!Data)
	{
		FText DefaultDataError;
		Data = UMRBNNProjectSettings::Get()->CreateTransientDefaultBakedData(this, DefaultDataError);
		BakedData = Data;
		if (MRBNNVolume)
		{
			MRBNNVolume->BakedData = Data;
		}
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

	if (!bUseRaymarchShader && !bUseComputeGlobalShader)
	{
		RaymarchDensityTexture = nullptr;
		RaymarchFeatureTexture = nullptr;
		RaymarchTextureBuildKey.Empty();
		return false;
	}

	FString VolumePath;
	FIntVector VolumeResolution(257, 257, 257);
	int32 SkipByteCount = 0;
	FText Error;
	if (!ResolveDensityVolumeFile(VolumePath, VolumeResolution, SkipByteCount, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("MRBNN compute density texture could not resolve density volume: %s"), *Error.ToString());
		RaymarchDensityTexture = nullptr;
		RaymarchFeatureTexture = nullptr;
		RaymarchTextureBuildKey.Empty();
		return false;
	}

	const FDateTime VolumeTimestamp = IFileManager::Get().GetTimeStamp(*VolumePath);
	TArray<uint8> RawBaseFeatureBytes;
	TArray<uint8> RawMultiScatterBytes;
	TArray<uint8> RawAnisotropyBytes;
	FMRBNNEncodingLevelInfo BaseFeatureInfo;
	FMRBNNEncodingLevelInfo MultiScatterInfo;
	FMRBNNEncodingLevelInfo AnisotropyInfo;
	bool bHasBakedFeatureSource = false;
	FString FeatureSourceKey = TEXT("feature=off");

	if (bUseBakedFeatureLighting && MRBNNVolume)
	{
		UMRBNNBakedVolumeData* Data = MRBNNVolume->BakedData;
		if (!Data)
		{
			FText DefaultDataError;
			Data = UMRBNNProjectSettings::Get()->CreateTransientDefaultBakedData(this, DefaultDataError);
			MRBNNVolume->BakedData = Data;
		}

		FString WorkingDirectory;
		FString RepositoryRoot;
		FText PathError;
		TSharedPtr<FJsonObject> Config;
		const FString ConfigPath = Data && Data->ResolvePaths(WorkingDirectory, RepositoryRoot, PathError)
			? FPaths::Combine(WorkingDirectory, TEXT("config.json"))
			: FString();
		const FString BaseFeaturePath = FPaths::Combine(WorkingDirectory, TEXT("base.bin"));
		const FString MultiScatterPath = FPaths::Combine(WorkingDirectory, TEXT("ms0.bin"));
		const FString AnisotropyPath = FPaths::Combine(WorkingDirectory, TEXT("ms1.bin"));
		const int32 FeatureLevel = FMath::Clamp(RaymarchBakedFeatureLevel, 0, 3);
		if (!ConfigPath.IsEmpty() &&
			ReadJsonObject(ConfigPath, Config) &&
			FFileHelper::LoadFileToArray(RawBaseFeatureBytes, *BaseFeaturePath) &&
			FFileHelper::LoadFileToArray(RawMultiScatterBytes, *MultiScatterPath) &&
			TryBuildEncodingLevelInfo(Config, TEXT("volume_encoding"), 3, FeatureLevel, RawBaseFeatureBytes.Num(), BaseFeatureInfo) &&
			TryBuildEncodingLevelInfo(Config, TEXT("ms_encoding"), 3, FeatureLevel, RawMultiScatterBytes.Num(), MultiScatterInfo))
		{
			bHasBakedFeatureSource = true;
			if (!FFileHelper::LoadFileToArray(RawAnisotropyBytes, *AnisotropyPath) ||
				!TryBuildEncodingLevelInfo(Config, TEXT("ms_encoding"), 3, FeatureLevel, RawAnisotropyBytes.Num(), AnisotropyInfo))
			{
				RawAnisotropyBytes.Reset();
				AnisotropyInfo = FMRBNNEncodingLevelInfo();
			}

			const FDateTime BaseTimestamp = IFileManager::Get().GetTimeStamp(*BaseFeaturePath);
			const FDateTime MultiScatterTimestamp = IFileManager::Get().GetTimeStamp(*MultiScatterPath);
			const FDateTime AnisotropyTimestamp = RawAnisotropyBytes.IsEmpty() ? FDateTime() : IFileManager::Get().GetTimeStamp(*AnisotropyPath);
			FeatureSourceKey = FString::Printf(
				TEXT("feature=on|level=%d|base=%lld|ms0=%lld|ms1=%lld"),
				FeatureLevel,
				BaseTimestamp.GetTicks(),
				MultiScatterTimestamp.GetTicks(),
				AnisotropyTimestamp.GetTicks());
		}
		else
		{
			FeatureSourceKey = FString::Printf(TEXT("feature=missing|level=%d"), FeatureLevel);
			if (UMRBNNProjectSettings::Get()->bEnableVerboseLogging)
			{
				UE_LOG(LogTemp, Log, TEXT("MRBNN baked feature proxy is disabled for this build because base.bin/ms0.bin could not be read from the active data set."));
			}
		}
	}

	FString BuildKey = FString::Printf(
		TEXT("%s|%lld|%d,%d,%d|%d|%d|%d|%d|%.6f|%.6f|%.6f|%.6f|%.6f"),
		*VolumePath,
		VolumeTimestamp.GetTicks(),
		VolumeResolution.X,
		VolumeResolution.Y,
		VolumeResolution.Z,
		SkipByteCount,
		FMath::Clamp(RaymarchTextureResolution, 16, 128),
		bRaymarchFitToDensityBounds ? 1 : 0,
		bUseMRBNNCloudAxisMapping ? 1 : 0,
		RaymarchBoundsThreshold,
		RaymarchBoundsPadding,
		RaymarchInputThreshold,
		RaymarchNormalizeDensity,
		RaymarchDensityPower);
	BuildKey += TEXT("|");
	BuildKey += FeatureSourceKey;
	if (RaymarchDensityTexture && RaymarchTextureBuildKey == BuildKey)
	{
		UpdateRaymarchMaterial();
		return true;
	}

	TArray<uint8> RawBytes;
	if (!FFileHelper::LoadFileToArray(RawBytes, *VolumePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("MRBNN compute density texture could not read volume file: %s"), *VolumePath);
		RaymarchDensityTexture = nullptr;
		RaymarchFeatureTexture = nullptr;
		RaymarchTextureBuildKey.Empty();
		return false;
	}

	const int64 SourceVoxelCount = static_cast<int64>(VolumeResolution.X) * VolumeResolution.Y * VolumeResolution.Z;
	if (SourceVoxelCount <= 0 || RawBytes.Num() < SkipByteCount + SourceVoxelCount * static_cast<int64>(sizeof(float)))
	{
		UE_LOG(LogTemp, Warning, TEXT("MRBNN compute density texture volume file is smaller than config resolution requires."));
		RaymarchDensityTexture = nullptr;
		RaymarchFeatureTexture = nullptr;
		RaymarchTextureBuildKey.Empty();
		return false;
	}

	const int32 TextureResolution = FMath::Clamp(RaymarchTextureResolution, 16, 128);
	UVolumeTexture* NewTexture = UVolumeTexture::CreateTransient(TextureResolution, TextureResolution, TextureResolution, PF_B8G8R8A8, TEXT("MRBNN_DensityVolume"));
	if (!NewTexture || !NewTexture->GetPlatformData() || NewTexture->GetPlatformData()->Mips.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("MRBNN compute density texture could not allocate a transient volume texture."));
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

	const int64 TextureVoxelCount = static_cast<int64>(TextureResolution) * TextureResolution * TextureResolution;
	bool bBuildFeatureTexture = bHasBakedFeatureSource;
	TArray<float> NormalizedDensityValues;
	TArray<float> BaseFeatureValues;
	TArray<float> MultiScatterValues;
	TArray<float> AnisotropyValues;
	float BaseFeatureSum = 0.0f;
	float MultiScatterSum = 0.0f;
	float AnisotropySum = 0.0f;
	float BaseFeatureMax = 0.0f;
	float MultiScatterMax = 0.0f;
	float AnisotropyMax = 0.0f;
	if (bBuildFeatureTexture)
	{
		NormalizedDensityValues.SetNumZeroed(TextureVoxelCount);
		BaseFeatureValues.SetNumZeroed(TextureVoxelCount);
		MultiScatterValues.SetNumZeroed(TextureVoxelCount);
		AnisotropyValues.SetNumZeroed(TextureVoxelCount);
	}

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
		const float BoundsThreshold = FMath::Max(RaymarchBoundsThreshold, 0.0f);
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

			UE_LOG(LogTemp, Log, TEXT("MRBNN compute density bounds: min=(%d,%d,%d) max=(%d,%d,%d) active=%lld threshold=%.3f"),
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
		RaymarchFeatureTexture = nullptr;
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
				const int64 TextureIndex =
					static_cast<int64>(Z) * TextureResolution * TextureResolution +
					static_cast<int64>(Y) * TextureResolution +
					X;
				const float UnitX = (static_cast<float>(X) + 0.5f) / static_cast<float>(TextureResolution);
				const FVector3f SourcePosition = MakeSourcePositionFromVolumeUnit(
					UnitX,
					UnitY,
					UnitZ,
					SourceMin,
					SourceMax,
					bUseMRBNNCloudAxisMapping);
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
				const float EdgeFade = Smooth01(EdgeDistance / 0.105f);
				NormalizedDensity *= EdgeFade;

				if (bBuildFeatureTexture)
				{
					NormalizedDensityValues[TextureIndex] = NormalizedDensity;
					const FVector3f FeatureCoord(
						LastSourceZ > 0 ? SourcePosition.Z / static_cast<float>(LastSourceZ) : 0.0f,
						LastSourceY > 0 ? SourcePosition.Y / static_cast<float>(LastSourceY) : 0.0f,
						LastSourceX > 0 ? SourcePosition.X / static_cast<float>(LastSourceX) : 0.0f);
					const float BaseEnergy = SampleEncodingMeanAbsNearest(RawBaseFeatureBytes, BaseFeatureInfo, FeatureCoord);
					const float MultiScatterEnergy = SampleEncodingMeanAbsNearest(RawMultiScatterBytes, MultiScatterInfo, FeatureCoord);
					const float AnisotropyEnergy = RawAnisotropyBytes.IsEmpty()
						? BaseEnergy * 0.35f
						: SampleEncodingMeanAbsNearest(RawAnisotropyBytes, AnisotropyInfo, FeatureCoord);

					BaseFeatureValues[TextureIndex] = BaseEnergy;
					MultiScatterValues[TextureIndex] = MultiScatterEnergy;
					AnisotropyValues[TextureIndex] = AnisotropyEnergy;
					BaseFeatureSum += BaseEnergy;
					MultiScatterSum += MultiScatterEnergy;
					AnisotropySum += AnisotropyEnergy;
					BaseFeatureMax = FMath::Max(BaseFeatureMax, BaseEnergy);
					MultiScatterMax = FMath::Max(MultiScatterMax, MultiScatterEnergy);
					AnisotropyMax = FMath::Max(AnisotropyMax, AnisotropyEnergy);
				}

				const uint8 QuantizedDensity = static_cast<uint8>(FMath::RoundToInt(NormalizedDensity * 255.0f));
				*MipData++ = FColor(QuantizedDensity, QuantizedDensity, QuantizedDensity, QuantizedDensity);
			}
		}
	}

	Mip.BulkData.Unlock();
	NewTexture->UpdateResource();

	UVolumeTexture* NewFeatureTexture = nullptr;
	if (bBuildFeatureTexture)
	{
		NewFeatureTexture = UVolumeTexture::CreateTransient(TextureResolution, TextureResolution, TextureResolution, PF_B8G8R8A8, TEXT("MRBNN_BakedFeatureVolume"));
		if (NewFeatureTexture && NewFeatureTexture->GetPlatformData() && !NewFeatureTexture->GetPlatformData()->Mips.IsEmpty())
		{
			NewFeatureTexture->SRGB = false;
			NewFeatureTexture->CompressionSettings = TC_VectorDisplacementmap;
			NewFeatureTexture->MipGenSettings = TMGS_NoMipmaps;
			NewFeatureTexture->Filter = TF_Bilinear;
			NewFeatureTexture->AddressMode = TA_Clamp;
			NewFeatureTexture->NeverStream = true;

			FTexture2DMipMap& FeatureMip = NewFeatureTexture->GetPlatformData()->Mips[0];
			FColor* FeatureMipData = reinterpret_cast<FColor*>(FeatureMip.BulkData.Lock(LOCK_READ_WRITE));
			if (FeatureMipData)
			{
				const float InvVoxelCount = TextureVoxelCount > 0 ? 1.0f / static_cast<float>(TextureVoxelCount) : 0.0f;
				const float BaseMean = BaseFeatureSum * InvVoxelCount;
				const float MultiScatterMean = MultiScatterSum * InvVoxelCount;
				const float AnisotropyMean = AnisotropySum * InvVoxelCount;
				for (int64 TextureIndex = 0; TextureIndex < TextureVoxelCount; ++TextureIndex)
				{
					const uint8 BaseQ = QuantizeFeatureEnergy(BaseFeatureValues[TextureIndex], BaseMean, BaseFeatureMax);
					const uint8 MultiScatterQ = QuantizeFeatureEnergy(MultiScatterValues[TextureIndex], MultiScatterMean, MultiScatterMax);
					const uint8 AnisotropyQ = QuantizeFeatureEnergy(AnisotropyValues[TextureIndex], AnisotropyMean, AnisotropyMax);
					const float FeatureConfidence = FMath::Max(
						NormalizedDensityValues[TextureIndex],
						FMath::Max(static_cast<float>(BaseQ), static_cast<float>(MultiScatterQ)) / 255.0f * 0.65f);
					*FeatureMipData++ = FColor(BaseQ, MultiScatterQ, AnisotropyQ, Quantize01(FeatureConfidence));
				}
				FeatureMip.BulkData.Unlock();
				NewFeatureTexture->UpdateResource();

				if (UMRBNNProjectSettings::Get()->bEnableVerboseLogging)
				{
					UE_LOG(LogTemp, Log, TEXT("MRBNN baked feature proxy built: %d^3 level=%d baseRes=%d msRes=%d"),
						TextureResolution,
						FMath::Clamp(RaymarchBakedFeatureLevel, 0, 3),
						BaseFeatureInfo.Resolution,
						MultiScatterInfo.Resolution);
				}
			}
			else
			{
				FeatureMip.BulkData.Unlock();
				NewFeatureTexture = nullptr;
			}
		}
		else
		{
			NewFeatureTexture = nullptr;
		}
	}

	RaymarchDensityTexture = NewTexture;
	RaymarchFeatureTexture = NewFeatureTexture;
	RaymarchTextureBuildKey = BuildKey;
	UpdateRaymarchMaterial();
	return true;
}

FMRBNNComputeVolumeSettings AMRBNNVolumeActor::MakeComputeVolumeSettings() const
{
	FMRBNNComputeVolumeSettings ComputeSettings;
	ComputeSettings.StepCount = FMath::Clamp(RaymarchStepCount * 2, 32, 160);
	ComputeSettings.DirectShadowSteps = FMath::Clamp(RaymarchDirectShadowSteps + 1, 0, 16);
	ComputeSettings.Opacity = FMath::Max(RaymarchOpacity, 0.0f);
	ComputeSettings.Ambient = FMath::Max(AmbientRelight, 0.0f);
	ComputeSettings.Directional = FMath::Max(DirectionalRelight, 0.0f);
	ComputeSettings.ShadowStrength = FMath::Max(RaymarchShadowStrength, 0.0f);
	ComputeSettings.LightStep = FMath::Max(RaymarchLightStep, 0.001f);
	ComputeSettings.Brightness = FMath::Max(PreviewBrightness, 0.0f);
	ComputeSettings.DirectLightIntensity = FMath::Max(CurrentRaymarchDirectLightIntensity, 0.0f);
	ComputeSettings.DirectShadowDensity = FMath::Max(RaymarchDirectShadowDensity, 0.0f);
	ComputeSettings.PhaseG = FMath::Clamp(RaymarchPhaseG, -0.85f, 0.85f);
	ComputeSettings.PhaseStrength = FMath::Clamp(RaymarchPhaseStrength, 0.0f, 1.0f);
	ComputeSettings.EdgeSilverStrength = FMath::Clamp(RaymarchEdgeSilverStrength, 0.0f, 2.0f);
	ComputeSettings.DeepShadowStrength = FMath::Clamp(RaymarchDeepShadowStrength, 0.0f, 2.0f);
	ComputeSettings.PowderStrength = FMath::Clamp(RaymarchPowderStrength, 0.0f, 2.0f);
	ComputeSettings.bUseBakedFeatures = bUseBakedFeatureLighting && RaymarchFeatureTexture;
	ComputeSettings.BakedFeatureContribution = FMath::Clamp(RaymarchBakedFeatureContribution, 0.0f, 2.0f);
	ComputeSettings.MultiScatterContribution = FMath::Clamp(RaymarchMultiScatterContribution, 0.0f, 2.0f);
	ComputeSettings.MultiScatterIsotropy = FMath::Clamp(RaymarchMultiScatterIsotropy, 0.0f, 1.0f);
	ComputeSettings.SilverLiningSharpness = FMath::Clamp(RaymarchSilverLiningSharpness, 0.25f, 4.0f);
	ComputeSettings.SceneColorContribution = FMath::Clamp(SceneColorContributionScale, 0.0f, 3.0f);
	ComputeSettings.CloudFlowDirection = RaymarchCloudFlowDirection.GetSafeNormal(UE_SMALL_NUMBER, FVector(1.0, 0.0, 0.0));
	ComputeSettings.CloudFlowSpeed = FMath::Clamp(RaymarchCloudFlowSpeed, 0.0f, 0.25f);
	ComputeSettings.FeatureAlbedoBlend = FMath::Clamp(RaymarchFeatureAlbedoBlend, 0.0f, 1.0f);
	ComputeSettings.CloudColor = RaymarchCloudColor;
	ComputeSettings.BakedFeatureTint = RaymarchBakedFeatureTint;
	ComputeSettings.bUsePaperStyleCinematic = bUsePaperStyleCinematicPath;
	ComputeSettings.CinematicLightOpticalDepthSteps = FMath::Clamp(CinematicLightOpticalDepthSteps, 1, 48);
	ComputeSettings.CinematicInscatterSteps = FMath::Clamp(CinematicInscatterSteps, 1, 16);
	ComputeSettings.CinematicTransmittanceScale = FMath::Clamp(CinematicTransmittanceScale, 0.1f, 4.0f);
	ComputeSettings.CinematicMultiScatterStrength = FMath::Clamp(CinematicMultiScatterStrength, 0.0f, 3.0f);
	ComputeSettings.CinematicFeatureParticipation = FMath::Clamp(CinematicFeatureParticipation, 0.0f, 1.0f);
	return ComputeSettings;
}

bool AMRBNNVolumeActor::RenderComputeGlobalShaderPreview()
{
	if (!bUseComputeGlobalShader || !MRBNNVolume || !RaymarchDensityTexture)
	{
		return false;
	}

	const bool bRendered = MRBNNVolume->RenderComputeVolumeOnce(RaymarchDensityTexture, RaymarchFeatureTexture, MakeComputeVolumeSettings());
	return bRendered;
}

bool AMRBNNVolumeActor::BuildComputeRenderDescForView(const FSceneView& View, FMRBNNComputeRenderer::FRenderDesc& OutDesc)
{
	if (!bUseComputeGlobalShader || !MRBNNVolume)
	{
		return false;
	}

	if (!RaymarchDensityTexture)
	{
		if (bRaymarchTextureBuilt || !BuildRaymarchVolumeTexture())
		{
			return false;
		}
	}

	UpdateRelightFromDirectionalLight();
	if (!MRBNNVolume->BuildComputeVolumeRenderDesc(RaymarchDensityTexture, RaymarchFeatureTexture, MakeComputeVolumeSettings(), OutDesc))
	{
		return false;
	}

	const FTransform ActorTransform = GetActorTransform();
	const FVector CameraPosition = TransformWorldPositionToVolumeUnitBox(ActorTransform, VolumeExtent, View.ViewMatrices.GetViewOrigin());
	const FVector CameraForward = TransformWorldVectorToVolumeUnitBoxUnnormalized(ActorTransform, VolumeExtent, View.GetViewDirection());
	const FVector CameraRight = TransformWorldVectorToVolumeUnitBoxUnnormalized(ActorTransform, VolumeExtent, View.GetViewRight());
	const FVector CameraUp = TransformWorldVectorToVolumeUnitBoxUnnormalized(ActorTransform, VolumeExtent, View.GetViewUp());
	const FVector LightDirection = TransformWorldVectorToVolumeUnitBox(
		ActorTransform,
		VolumeExtent,
		ActorTransform.TransformVectorNoScale(MRBNNVolume->RenderSettings.LightDirection),
		FVector(0.35f, 0.7f, 0.62f).GetSafeNormal());
	const FVector2f TanHalfFov = View.ViewMatrices.GetTanHalfFov();

	OutDesc.CameraPosition = FVector3f(CameraPosition);
	OutDesc.CameraForward = FVector3f(CameraForward);
	OutDesc.CameraRight = FVector3f(CameraRight);
	OutDesc.CameraUp = FVector3f(CameraUp);
	OutDesc.TanHalfFov = FVector2f(FMath::Max(TanHalfFov.X, 0.001f), FMath::Max(TanHalfFov.Y, 0.001f));
	OutDesc.RenderSettings.CameraPosition = CameraPosition;
	OutDesc.RenderSettings.LightDirection = LightDirection;
	OutDesc.bUseExplicitCamera = true;
	BuildBoundedSceneLightingForView(View, OutDesc);
	return true;
}

void AMRBNNVolumeActor::BuildBoundedSceneLightingForView(const FSceneView& View, FMRBNNComputeRenderer::FRenderDesc& InOutDesc)
{
	static_cast<void>(View);
	InOutDesc.SceneSkyLightColorAndIntensity = FVector4f::Zero();
	InOutDesc.SceneFogColorAndDensity = FVector4f::Zero();
	InOutDesc.SceneAtmosphereParams = FVector4f::Zero();
	InOutDesc.SceneLightCount = 0;
	for (int32 LightIndex = 0; LightIndex < FMRBNNComputeRenderer::MaxSceneLightSamples; ++LightIndex)
	{
		InOutDesc.SceneLightPositionAndInvRadius[LightIndex] = FVector4f::Zero();
		InOutDesc.SceneLightColorAndIntensity[LightIndex] = FVector4f::Zero();
		InOutDesc.SceneLightDirectionAndSpot[LightIndex] = FVector4f::Zero();
		InOutDesc.SceneLightTypeAndShape[LightIndex] = FVector4f::Zero();
	}

	UWorld* World = GetWorld();
	if (!bUseBoundedSceneLighting || !World)
	{
		return;
	}

	const FTransform ActorTransform = GetActorTransform();
	const FVector ActorLocation = GetActorLocation();
	const int32 MaxLocalLightCount = FMath::Clamp(MaxSceneLocalLightSamples, 0, FMRBNNComputeRenderer::MaxSceneLightSamples);

	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		const ASkyLight* SkyLight = *It;
		const USkyLightComponent* SkyLightComponent = SkyLight ? SkyLight->GetLightComponent() : nullptr;
		if (!SkyLightComponent || !SkyLightComponent->bAffectsWorld || !SkyLightComponent->IsVisible())
		{
			continue;
		}

		const FLinearColor SkyColor = SkyLightComponent->GetLightColor() * FMath::Max(SkyLightComponent->Intensity, 0.0f);
		InOutDesc.SceneSkyLightColorAndIntensity = FVector4f(
			FMath::Max(SkyColor.R, 0.0f),
			FMath::Max(SkyColor.G, 0.0f),
			FMath::Max(SkyColor.B, 0.0f),
			FMath::Max(SceneSkyAtmosphereIntensityScale, 0.0f));
		break;
	}

	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		const AExponentialHeightFog* FogActor = *It;
		const UExponentialHeightFogComponent* FogComponent = FogActor ? FogActor->GetComponent() : nullptr;
		if (!FogComponent || !FogComponent->IsVisible())
		{
			continue;
		}

		const FLinearColor FogColor = FogComponent->FogInscatteringLuminance + FogComponent->VolumetricFogEmissive;
		const float FogDensity = FMath::Max(FogComponent->FogDensity, 0.0f) *
			FMath::Clamp(FogComponent->FogMaxOpacity, 0.0f, 1.0f) *
			FMath::Max(SceneFogContributionScale, 0.0f);
		InOutDesc.SceneFogColorAndDensity = FVector4f(
			FMath::Max(FogColor.R, 0.0f),
			FMath::Max(FogColor.G, 0.0f),
			FMath::Max(FogColor.B, 0.0f),
			FogDensity);
		break;
	}

	for (TActorIterator<ASkyAtmosphere> It(World); It; ++It)
	{
		const ASkyAtmosphere* SkyAtmosphere = *It;
		const USkyAtmosphereComponent* SkyComponent = SkyAtmosphere ? SkyAtmosphere->GetComponent() : nullptr;
		if (!SkyComponent || !SkyComponent->IsVisible())
		{
			continue;
		}

		const float SkyScale = FMath::Max(SceneSkyAtmosphereIntensityScale, 0.0f);
		const FLinearColor AtmosphereTint = SkyComponent->SkyAndAerialPerspectiveLuminanceFactor;
		InOutDesc.SceneAtmosphereParams = FVector4f(
			FMath::Max(SkyComponent->MultiScatteringFactor, 0.0f) * SkyScale,
			FMath::Max(SkyComponent->RayleighScatteringScale, 0.0f) * SkyScale,
			FMath::Max(SkyComponent->MieScatteringScale, 0.0f) * SkyScale,
			FMath::Max(SkyComponent->AerialPespectiveViewDistanceScale, 0.0f));
		InOutDesc.SceneSkyLightColorAndIntensity.X += FMath::Max(AtmosphereTint.R, 0.0f) * 0.08f * SkyScale;
		InOutDesc.SceneSkyLightColorAndIntensity.Y += FMath::Max(AtmosphereTint.G, 0.0f) * 0.08f * SkyScale;
		InOutDesc.SceneSkyLightColorAndIntensity.Z += FMath::Max(AtmosphereTint.B, 0.0f) * 0.08f * SkyScale;
		InOutDesc.SceneSkyLightColorAndIntensity.W = FMath::Max(InOutDesc.SceneSkyLightColorAndIntensity.W, SkyScale);
		break;
	}

	TArray<FMRBNNBoundedSceneLightCandidate> LocalLights;
	LocalLights.Reserve(MaxLocalLightCount);
	for (TActorIterator<ALight> It(World); It; ++It)
	{
		const ALight* LightActor = *It;
		const ULightComponent* LightComponent = LightActor ? LightActor->GetLightComponent() : nullptr;
		const ULocalLightComponent* LocalLightComponent = Cast<ULocalLightComponent>(LightComponent);
		if (!LightActor || !LightComponent || !LocalLightComponent || !LightComponent->bAffectsWorld || !LightComponent->IsVisible())
		{
			continue;
		}

		const float Radius = FMath::Max(LocalLightComponent->AttenuationRadius, 1.0f);
		const float DistSq = FVector::DistSquared(ActorLocation, LightComponent->GetComponentLocation());
		if (DistSq > FMath::Square(Radius + VolumeExtent.GetMax()))
		{
			continue;
		}

		FMRBNNBoundedSceneLightCandidate Candidate;
		const float VolumeRadius = GetVolumeRadiusFromWorldRadius(VolumeExtent, Radius);
		const FVector VolumePosition = TransformWorldPositionToVolumeUnitBox(ActorTransform, VolumeExtent, LightComponent->GetComponentLocation());
		const FLinearColor LightEnergy = LightComponent->GetColoredLightBrightness();
		const float DistanceAttenuation = FMath::Square(Radius) / FMath::Max(DistSq + FMath::Square(Radius * 0.25f), 1.0f);
		Candidate.Score = GetLightLuminance(LightEnergy) * DistanceAttenuation * FMath::Max(LightComponent->VolumetricScatteringIntensity, 0.0f);
		if (Candidate.Score <= UE_SMALL_NUMBER)
		{
			continue;
		}

		Candidate.PositionAndInvRadius = FVector4f(
			static_cast<float>(VolumePosition.X),
			static_cast<float>(VolumePosition.Y),
			static_cast<float>(VolumePosition.Z),
			1.0f / VolumeRadius);
		Candidate.ColorAndIntensity = FVector4f(
			FMath::Max(LightEnergy.R, 0.0f),
			FMath::Max(LightEnergy.G, 0.0f),
			FMath::Max(LightEnergy.B, 0.0f),
			FMath::Max(SceneLocalLightIntensityScale, 0.0f) * FMath::Max(LightComponent->VolumetricScatteringIntensity, 0.0f));

		if (const USpotLightComponent* SpotComponent = Cast<USpotLightComponent>(LightComponent))
		{
			const FVector SpotDirection = TransformWorldVectorToVolumeUnitBox(
				ActorTransform,
				VolumeExtent,
				SpotComponent->GetDirection(),
				FVector(1.0f, 0.0f, 0.0f));
			Candidate.DirectionAndSpot = FVector4f(
				static_cast<float>(SpotDirection.X),
				static_cast<float>(SpotDirection.Y),
				static_cast<float>(SpotDirection.Z),
				FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(SpotComponent->OuterConeAngle, 1.0f, 89.0f))));
			Candidate.TypeAndShape = FVector4f(2.0f, FMath::Clamp(SpotComponent->InnerConeAngle / FMath::Max(SpotComponent->OuterConeAngle, 1.0f), 0.0f, 1.0f), 0.0f, 0.0f);
		}
		else if (const URectLightComponent* RectComponent = Cast<URectLightComponent>(LightComponent))
		{
			const FVector RectDirection = TransformWorldVectorToVolumeUnitBox(
				ActorTransform,
				VolumeExtent,
				RectComponent->GetDirection(),
				FVector(1.0f, 0.0f, 0.0f));
			Candidate.DirectionAndSpot = FVector4f(
				static_cast<float>(RectDirection.X),
				static_cast<float>(RectDirection.Y),
				static_cast<float>(RectDirection.Z),
				0.0f);
			Candidate.TypeAndShape = FVector4f(
				3.0f,
				GetVolumeRadiusFromWorldRadius(VolumeExtent, RectComponent->SourceWidth * 0.5f),
				GetVolumeRadiusFromWorldRadius(VolumeExtent, RectComponent->SourceHeight * 0.5f),
				0.0f);
		}
		else
		{
			Candidate.DirectionAndSpot = FVector4f::Zero();
			Candidate.TypeAndShape = FVector4f(1.0f, 0.0f, 0.0f, 0.0f);
		}

		LocalLights.Add(Candidate);
	}

	LocalLights.Sort(
		[](const FMRBNNBoundedSceneLightCandidate& Left, const FMRBNNBoundedSceneLightCandidate& Right)
		{
			return Left.Score > Right.Score;
		});

	InOutDesc.SceneLightCount = FMath::Min(MaxLocalLightCount, LocalLights.Num());
	for (int32 LightIndex = 0; LightIndex < InOutDesc.SceneLightCount; ++LightIndex)
	{
		InOutDesc.SceneLightPositionAndInvRadius[LightIndex] = LocalLights[LightIndex].PositionAndInvRadius;
		InOutDesc.SceneLightColorAndIntensity[LightIndex] = LocalLights[LightIndex].ColorAndIntensity;
		InOutDesc.SceneLightDirectionAndSpot[LightIndex] = LocalLights[LightIndex].DirectionAndSpot;
		InOutDesc.SceneLightTypeAndShape[LightIndex] = LocalLights[LightIndex].TypeAndShape;
	}

	uint32 SceneLightingHash = HashVector4f(0, InOutDesc.SceneSkyLightColorAndIntensity);
	SceneLightingHash = HashVector4f(SceneLightingHash, InOutDesc.SceneFogColorAndDensity);
	SceneLightingHash = HashVector4f(SceneLightingHash, InOutDesc.SceneAtmosphereParams);
	for (int32 LightIndex = 0; LightIndex < FMRBNNComputeRenderer::MaxSceneLightSamples; ++LightIndex)
	{
		SceneLightingHash = HashVector4f(SceneLightingHash, InOutDesc.SceneLightPositionAndInvRadius[LightIndex]);
		SceneLightingHash = HashVector4f(SceneLightingHash, InOutDesc.SceneLightColorAndIntensity[LightIndex]);
		SceneLightingHash = HashVector4f(SceneLightingHash, InOutDesc.SceneLightDirectionAndSpot[LightIndex]);
		SceneLightingHash = HashVector4f(SceneLightingHash, InOutDesc.SceneLightTypeAndShape[LightIndex]);
	}
	SceneLightingHash = FCrc::MemCrc32(&InOutDesc.SceneLightCount, sizeof(InOutDesc.SceneLightCount), SceneLightingHash);
	if (bHasLastSceneLightingState && LastSceneLightingHash != SceneLightingHash)
	{
		ResetRenderState();
	}
	LastSceneLightingHash = SceneLightingHash;
	bHasLastSceneLightingState = true;
}

void AMRBNNVolumeActor::EnsureComputeViewExtension()
{
	if (HasAnyFlags(RF_ClassDefaultObject) || !ShouldUseSceneViewExtensionRenderPass())
	{
		ComputeViewExtension.Reset();
		return;
	}

	if (!ComputeViewExtension.IsValid())
	{
		ComputeViewExtension = FSceneViewExtensions::NewExtension<FMRBNNSceneViewExtension>(TWeakObjectPtr<AMRBNNVolumeActor>(this));
	}
	else
	{
		ComputeViewExtension->SetActor(TWeakObjectPtr<AMRBNNVolumeActor>(this));
	}
}

bool AMRBNNVolumeActor::ShouldUseSceneViewExtensionRenderPass() const
{
	return bUseComputeGlobalShader && bUseSceneViewExtensionRenderPass;
}

bool AMRBNNVolumeActor::BuildDensityVolumePreview()
{
	bDensityPreviewBuilt = true;
	return false;
}

void AMRBNNVolumeActor::UpdateRelightFromDirectionalLight()
{
	if (!MRBNNVolume)
	{
		return;
	}

	auto ApplyRelightState = [this](const FVector& LocalLightDirection, const FLinearColor& SourceLightColor, float DirectLightIntensity)
	{
		const FLinearColor SanitizedColor(
			FMath::Max(SourceLightColor.R, 0.0f),
			FMath::Max(SourceLightColor.G, 0.0f),
			FMath::Max(SourceLightColor.B, 0.0f),
			1.0f);
		const float SafeIntensity = FMath::Max(DirectLightIntensity, 0.0f);
		const bool bChanged =
			!bHasLastRelightState ||
			FVector::DistSquared(LastRelightDirection, LocalLightDirection) > 0.0001 ||
			FMath::Abs(LastRelightColor.R - SanitizedColor.R) > 0.001f ||
			FMath::Abs(LastRelightColor.G - SanitizedColor.G) > 0.001f ||
			FMath::Abs(LastRelightColor.B - SanitizedColor.B) > 0.001f ||
			FMath::Abs(LastRelightIntensity - SafeIntensity) > 0.001f;

		if (bChanged && bHasLastRelightState)
		{
			ResetRenderState();
		}

		LastRelightDirection = LocalLightDirection;
		LastRelightColor = SanitizedColor;
		LastRelightIntensity = SafeIntensity;
		bHasLastRelightState = true;

		CurrentRaymarchDirectLightColor = SanitizedColor;
		CurrentRaymarchDirectLightIntensity = SafeIntensity;
		MRBNNVolume->RenderSettings.LightDirection = LocalLightDirection;

		const float LightColorScale = FMath::Clamp(0.35f + SafeIntensity * 1.65f, 0.0f, 8.0f);
		MRBNNVolume->RenderSettings.LightColor = FLinearColor(
			FMath::Max(SanitizedColor.R, 0.04f) * LightColorScale,
			FMath::Max(SanitizedColor.G, 0.04f) * LightColorScale,
			FMath::Max(SanitizedColor.B, 0.04f) * LightColorScale,
			1.0f);
	};

	if (!bUseDirectionalLightForRelight)
	{
		ApplyRelightState(FVector(0.35f, 0.7f, 0.62f).GetSafeNormal(), FLinearColor::White, RaymarchDirectLightIntensityScale);
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
		ApplyRelightState(FVector(0.35f, 0.7f, 0.62f).GetSafeNormal(), FLinearColor::White, 0.0f);
		return;
	}

	const FVector WorldLightDirection = (-LightActor->GetActorForwardVector()).GetSafeNormal();
	const FVector LocalLightDirection = GetActorTransform().InverseTransformVectorNoScale(WorldLightDirection).GetSafeNormal();

	FLinearColor LightColor = LightActor->GetLightColor();
	float DirectLightIntensity = FMath::Max(RaymarchDirectLightIntensityScale, 0.0f);
	if (const UDirectionalLightComponent* DirectionalComponent = Cast<UDirectionalLightComponent>(LightActor->GetLightComponent()))
	{
		LightColor = DirectionalComponent->GetLightColor();
		DirectLightIntensity = FMath::Clamp(DirectionalComponent->Intensity / 5.0f, 0.0f, 16.0f) * FMath::Max(RaymarchDirectLightIntensityScale, 0.0f);
	}
	ApplyRelightState(LocalLightDirection, LightColor, DirectLightIntensity);
}

void AMRBNNVolumeActor::UpdateVolumeMaterial()
{
	// The actor renders through the RDG compute composite path; no preview mesh materials are maintained.
}

void AMRBNNVolumeActor::UpdateRaymarchMaterial()
{
	// The final preview path is the RDG GlobalShader pass. This hook remains as
	// a compatibility no-op for older maps that still call the old rebuild action.
}

void AMRBNNVolumeActor::UpdateDebugText()
{
	// Runtime status is exposed through actor Details and the MRBNNVolume component debug fields.
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
	EnsureComputeViewExtension();
	if (bUseComputeGlobalShader)
	{
		if (!ShouldUseSceneViewExtensionRenderPass())
		{
			RenderComputeGlobalShaderPreview();
		}
	}
	else
	{
		MRBNNVolume->RenderOnce();
	}
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
