#include "MRBNNExampleActor.h"

#include "MRBNNBakedVolumeData.h"
#include "MRBNNProjectSettings.h"
#include "MRBNNVolumeComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Components/TextRenderComponent.h"
#include "UObject/ConstructorHelpers.h"

AMRBNNExampleActor::AMRBNNExampleActor()
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	PreviewBillboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("PreviewBillboard"));
	PreviewBillboard->SetupAttachment(SceneRoot);
	PreviewBillboard->SetRelativeLocation(FVector(0.0f, 0.0f, 100.0f));
	PreviewBillboard->SetRelativeScale3D(FVector(PreviewScale));
	PreviewBillboard->bHiddenInGame = false;
	PreviewBillboard->SetVisibility(true);

	PreviewPlane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewPlane"));
	PreviewPlane->SetupAttachment(SceneRoot);
	PreviewPlane->SetRelativeLocation(PreviewPlaneLocation);
	PreviewPlane->SetRelativeRotation(PreviewPlaneRotation);
	PreviewPlane->SetRelativeScale3D(FVector(6.0f, 6.0f, 1.0f));
	PreviewPlane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewPlane->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	PreviewPlane->SetCastShadow(false);
	PreviewPlane->bHiddenInGame = false;
	PreviewPlane->SetVisibility(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PreviewPlaneMeshFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PreviewPlaneMeshFinder.Succeeded())
	{
		PreviewPlane->SetStaticMesh(PreviewPlaneMeshFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PreviewPlaneMaterialFinder(TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque_OneSided.Widget3DPassThrough_Opaque_OneSided"));
	if (PreviewPlaneMaterialFinder.Succeeded())
	{
		PreviewPlane->SetMaterial(0, PreviewPlaneMaterialFinder.Object);
	}

	DebugText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("DebugText"));
	DebugText->SetupAttachment(SceneRoot);
	DebugText->SetRelativeLocation(FVector(-120.0f, -120.0f, 220.0f));
	DebugText->SetRelativeRotation(FRotator(0.0f, 35.0f, 0.0f));
	DebugText->SetHorizontalAlignment(EHTA_Left);
	DebugText->SetVerticalAlignment(EVRTA_TextTop);
	DebugText->SetTextRenderColor(FColor(220, 235, 255));
	DebugText->SetWorldSize(14.0f);
	DebugText->SetCastShadow(false);
	DebugText->bHiddenInGame = true;

	MRBNNVolume = CreateDefaultSubobject<UMRBNNVolumeComponent>(TEXT("MRBNNVolume"));
	MRBNNVolume->ApplyRealtimePreviewSettings();
	MRBNNVolume->bAutoInitialize = false;
	MRBNNVolume->bRenderEveryTick = false;
	MRBNNVolume->bUsePlayerCamera = false;
	MRBNNVolume->bApplyOutputToMaterials = true;
	MRBNNVolume->OutputTextureParameterName = TEXT("SlateUI");
	MRBNNVolume->bUseOwnerPrimitiveComponentsWhenTargetsEmpty = false;
	MRBNNVolume->RenderSettings.LightColor = FLinearColor(3.5f, 3.5f, 3.5f, 1.0f);
	MRBNNVolume->TargetMaterialComponents.Add(PreviewPlane);
}

void AMRBNNExampleActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	EnsureSampleDataConfigured();
	ConfigurePreviewSurface();
	RefreshPreviewSprite();
	UpdateDebugText();
}

void AMRBNNExampleActor::BeginPlay()
{
	Super::BeginPlay();

	EnsureSampleDataConfigured();
	ConfigurePreviewSurface();
	RefreshPreviewSprite();
	UpdateDebugText();
}

bool AMRBNNExampleActor::ConfigureSampleData()
{
	if (!MRBNNVolume)
	{
		return false;
	}

	FString RepositoryRoot;
	FString WorkingDirectory;
	FString SkyboxPath;
	FString SkyboxBakingDirectory;
	if (!ResolveDefaultSamplePaths(RepositoryRoot, WorkingDirectory, SkyboxPath, SkyboxBakingDirectory))
	{
		return false;
	}

	if (!ExampleBakedData)
	{
		ExampleBakedData = NewObject<UMRBNNBakedVolumeData>(this, TEXT("MRBNNExampleBakedData"), RF_Transient);
	}

	ExampleBakedData->WorkingDirectory.Path = WorkingDirectory;
	ExampleBakedData->RepositoryRoot.Path = RepositoryRoot;
	ExampleBakedData->SkyboxHDRI.FilePath = bEnableSampleSkybox ? SkyboxPath : FString();
	ExampleBakedData->SkyboxExposure = 1.0f;
	ExampleBakedData->SkyboxBakingDirectory.Path = bEnableSampleSkyboxBaking ? SkyboxBakingDirectory : FString();

	MRBNNVolume->BakedData = ExampleBakedData;
	MRBNNVolume->RenderSettings.bFastDirectIllumination = true;
	MRBNNVolume->RenderSettings.bEnableSkybox = bEnableSampleSkybox && !SkyboxPath.IsEmpty();
	MRBNNVolume->RenderSettings.bEnableSkyboxBaking = bEnableSampleSkyboxBaking && !SkyboxBakingDirectory.IsEmpty();
	return true;
}

bool AMRBNNExampleActor::InitializeExampleRenderer()
{
	if (!MRBNNVolume)
	{
		return false;
	}

	EnsureSampleDataConfigured();
	ConfigurePreviewSurface();
	const bool bInitialized = MRBNNVolume->InitializeRenderer();
	RefreshPreviewSprite();
	UpdateDebugText();
	return bInitialized;
}

bool AMRBNNExampleActor::RenderExampleOnce()
{
	if (!MRBNNVolume)
	{
		return false;
	}

	EnsureSampleDataConfigured();
	ConfigurePreviewSurface();
	const bool bRendered = MRBNNVolume->RenderOnce();
	RefreshPreviewSprite();
	UpdateDebugText();
	return bRendered;
}

void AMRBNNExampleActor::ReleaseExampleRenderer()
{
	if (!MRBNNVolume)
	{
		return;
	}

	MRBNNVolume->ReleaseRenderer();
	RefreshPreviewSprite();
	UpdateDebugText();
}

void AMRBNNExampleActor::ConfigurePreviewSurface()
{
	if (!PreviewPlane)
	{
		return;
	}

	PreviewPlane->SetRelativeLocation(PreviewPlaneLocation);
	PreviewPlane->SetRelativeRotation(PreviewPlaneRotation);
	PreviewPlane->SetRelativeScale3D(FVector(PreviewScale * 1.5f, PreviewScale * 1.5f, 1.0f));

	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque_OneSided.Widget3DPassThrough_Opaque_OneSided"));
	UTexture* PreviewTexture = MRBNNVolume ? MRBNNVolume->GetOutputRenderTarget() : nullptr;
	if (!PreviewTexture)
	{
		PreviewTexture = LoadFallbackPreviewTexture();
	}

	if (BaseMaterial)
	{
		if (!PreviewPlaneMaterialInstance)
		{
			PreviewPlaneMaterialInstance = UMaterialInstanceDynamic::Create(BaseMaterial, this);
			PreviewPlane->SetMaterial(0, PreviewPlaneMaterialInstance);
		}

		if (PreviewPlaneMaterialInstance)
		{
			const float SafeBrightness = FMath::Max(PreviewBrightness, 0.1f);
			if (PreviewTexture)
			{
				PreviewTexture->Filter = TF_Bilinear;
				PreviewPlaneMaterialInstance->SetTextureParameterValue(TEXT("SlateUI"), PreviewTexture);
			}

			PreviewPlaneMaterialInstance->SetVectorParameterValue(TEXT("TintColorAndOpacity"), FLinearColor(SafeBrightness, SafeBrightness, SafeBrightness, 1.0f));
			PreviewPlaneMaterialInstance->SetScalarParameterValue(TEXT("OpacityFromTexture"), 1.0f);
		}
	}

	UpdatePreviewVisibility(PreviewTexture);
}

UTexture2D* AMRBNNExampleActor::LoadFallbackPreviewTexture()
{
	if (FallbackPreviewTexture)
	{
		return FallbackPreviewTexture;
	}

	const FString FallbackPreviewPath = FindFallbackPreviewPath();
	if (FallbackPreviewPath.IsEmpty() || !FPaths::FileExists(FallbackPreviewPath))
	{
		return nullptr;
	}

	FallbackPreviewTexture = FImageUtils::ImportFileAsTexture2D(FallbackPreviewPath);
	return FallbackPreviewTexture;
}

FString AMRBNNExampleActor::FindFallbackPreviewPath() const
{
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MRBNN")))
	{
		const FString PluginPreviewPath = FPaths::Combine(
			Plugin->GetBaseDir(),
			TEXT("Binaries/ThirdParty/MRBNNBridge/Win64/MRBNNBridgeSmokeTestPreview.png"));
		if (FPaths::FileExists(PluginPreviewPath))
		{
			return PluginPreviewPath;
		}
	}

	return FString();
}

void AMRBNNExampleActor::EnsureSampleDataConfigured()
{
	if (!bAutoConfigureSampleData || !MRBNNVolume)
	{
		return;
	}

	if (MRBNNVolume->BakedData && MRBNNVolume->BakedData != ExampleBakedData)
	{
		return;
	}

	ConfigureSampleData();
}

void AMRBNNExampleActor::RefreshPreviewSprite()
{
	if (!PreviewBillboard)
	{
		return;
	}

	PreviewBillboard->SetRelativeScale3D(FVector(PreviewScale));
	ConfigurePreviewSurface();

	UTexture2D* CurrentTexture = MRBNNVolume ? MRBNNVolume->GetOutputTexture() : nullptr;
	if (!CurrentTexture)
	{
		CurrentTexture = LoadFallbackPreviewTexture();
	}
	if (CurrentTexture != LastPreviewTexture)
	{
		PreviewBillboard->SetSprite(CurrentTexture);
		LastPreviewTexture = CurrentTexture;
	}

	UpdatePreviewVisibility(CurrentTexture);
	UpdateDebugText();
}

void AMRBNNExampleActor::ApplyGamePreviewSettings()
{
	if (MRBNNVolume)
	{
		MRBNNVolume->ApplyGamePreviewSettings();
	}

	ConfigurePreviewSurface();
	RefreshPreviewSprite();
}

void AMRBNNExampleActor::ApplyBalancedPreviewSettings()
{
	if (MRBNNVolume)
	{
		MRBNNVolume->ApplyBalancedPreviewSettings();
	}

	ConfigurePreviewSurface();
	RefreshPreviewSprite();
}

void AMRBNNExampleActor::ApplyHighQualityPreviewSettings()
{
	if (MRBNNVolume)
	{
		MRBNNVolume->ApplyHighQualityPreviewSettings();
	}

	ConfigurePreviewSurface();
	RefreshPreviewSprite();
}

void AMRBNNExampleActor::ResetPreviewAccumulation()
{
	if (MRBNNVolume)
	{
		MRBNNVolume->ResetProgressiveAccumulation();
	}

	UpdateDebugText();
}

void AMRBNNExampleActor::UpdatePreviewVisibility(UTexture* PreviewTexture)
{
	const bool bHasTexture = PreviewTexture != nullptr;
	if (PreviewPlane)
	{
		PreviewPlane->SetVisibility(bHasTexture && ShouldShowPreviewPlane(), true);
	}

	if (PreviewBillboard)
	{
		PreviewBillboard->SetVisibility(bHasTexture && ShouldShowPreviewBillboard(), true);
	}
}

void AMRBNNExampleActor::UpdateDebugText()
{
	if (!DebugText)
	{
		return;
	}

	DebugText->SetVisibility(bShowDebugLabel, true);
	if (!bShowDebugLabel)
	{
		return;
	}

	FString DebugSummary = TEXT("MRBNN Debug\nRenderer: not initialized");
	if (MRBNNVolume)
	{
		DebugSummary = FString::Printf(
			TEXT("MRBNN Debug\n%s\nLastError: %s"),
			*MRBNNVolume->GetDebugSummary(),
			*MRBNNVolume->GetLastError().ToString());
	}

	DebugText->SetText(FText::FromString(DebugSummary));
}

bool AMRBNNExampleActor::ShouldShowPreviewPlane() const
{
	return DisplayMode == EMRBNNExampleDisplayMode::Plane || DisplayMode == EMRBNNExampleDisplayMode::PlaneAndBillboard;
}

bool AMRBNNExampleActor::ShouldShowPreviewBillboard() const
{
	return DisplayMode == EMRBNNExampleDisplayMode::Billboard || DisplayMode == EMRBNNExampleDisplayMode::PlaneAndBillboard;
}

bool AMRBNNExampleActor::ResolveDefaultSamplePaths(FString& OutRepositoryRoot, FString& OutWorkingDirectory, FString& OutSkyboxPath, FString& OutSkyboxBakingDirectory) const
{
	FText Error;
	const bool bResolved = UMRBNNProjectSettings::Get()->ResolveDefaultDataSet(OutRepositoryRoot, OutWorkingDirectory, OutSkyboxPath, OutSkyboxBakingDirectory, Error);
	if (!bResolved)
	{
		UE_LOG(LogTemp, Warning, TEXT("MRBNN example failed to resolve default sample data: %s"), *Error.ToString());
	}
	return bResolved;
}
