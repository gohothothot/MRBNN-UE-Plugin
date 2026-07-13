#include "MRBNNVolumeComponent.h"

#include "MRBNNBackend.h"
#include "MRBNNComputeRenderer.h"
#include "MRBNNProjectSettings.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureDefines.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/VolumeTexture.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Math/Float16Color.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RHICommandList.h"
#include "RenderingThread.h"
#include "Rendering/Texture2DResource.h"
#include "TextureResource.h"
#include "Engine/World.h"

UMRBNNVolumeComponent::UMRBNNVolumeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	bTickInEditor = true;
}

void UMRBNNVolumeComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoInitialize && (BakedData || bUseProjectSettingsWhenBakedDataMissing))
	{
		InitializeRenderer();
	}
}

void UMRBNNVolumeComponent::BeginDestroy()
{
	ReleaseRenderer();
	Super::BeginDestroy();
}

void UMRBNNVolumeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseRenderer();
	Super::EndPlay(EndPlayReason);
}

void UMRBNNVolumeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bRenderEveryTick || !CanRunAutomaticRenderingInCurrentWorld())
	{
		return;
	}

	if (Backend)
	{
		RenderOnce();
		return;
	}

	if (bAutoInitialize && (!bAutoInitializeAttempted || bRetryFailedAutoInitialize))
	{
		RenderOnce();
	}
}

bool UMRBNNVolumeComponent::InitializeRenderer()
{
	bAutoInitializeAttempted = true;

	UMRBNNBakedVolumeData* DataToRender = BakedData;
	if (!DataToRender && bUseProjectSettingsWhenBakedDataMissing)
	{
		FText SettingsError;
		RuntimeProjectSettingsBakedData = UMRBNNProjectSettings::Get()->CreateTransientDefaultBakedData(this, SettingsError);
		DataToRender = RuntimeProjectSettingsBakedData;
		if (DataToRender)
		{
			BakedData = DataToRender;
		}
		else if (!SettingsError.IsEmpty())
		{
			SetLastError(SettingsError);
			return false;
		}
	}

	if (!DataToRender)
	{
		SetLastError(NSLOCTEXT("MRBNN", "NoBakedData", "MRBNN baked data asset is not assigned."));
		return false;
	}

	FText Error;
	if (!DataToRender->ValidateData(Error))
	{
		SetLastError(Error);
		return false;
	}

	FString RepositoryRoot;
	if (!DataToRender->ResolvePaths(ResolvedWorkingDirectory, RepositoryRoot, Error))
	{
		SetLastError(Error);
		return false;
	}
	LastResolvedWorkingDirectory = ResolvedWorkingDirectory;
	LastResolvedRepositoryRoot = RepositoryRoot;

	FString SkyboxPath;
	if (!DataToRender->ResolveSkyboxPath(SkyboxPath, Error))
	{
		SetLastError(Error);
		return false;
	}

	FString SkyboxBakingDirectory;
	if (!DataToRender->ResolveSkyboxBakingDirectory(SkyboxBakingDirectory, Error))
	{
		SetLastError(Error);
		return false;
	}

	TUniquePtr<IMRBNNBackend> NewBackend = CreateMRBNNBackend();
	if (!NewBackend->Initialize(ResolvedWorkingDirectory, RepositoryRoot, SkyboxPath, DataToRender->SkyboxExposure, SkyboxBakingDirectory, Error))
	{
		SetLastError(Error);
		return false;
	}

	ReleaseRenderer();
	Backend = NewBackend.Release();
	FrameIndex = 0;
	ResetProgressiveAccumulation();
	bAutoInitializeAttempted = false;
	EnsureOutputTexture();
	EnsureOutputRenderTarget();
	SetLastError(FText::GetEmpty());
	return true;
}

void UMRBNNVolumeComponent::ReleaseRenderer()
{
	if (Backend)
	{
		Backend->Shutdown();
		delete Backend;
		Backend = nullptr;
	}
	FrameIndex = 0;
	ResetProgressiveAccumulation();
	bAutoInitializeAttempted = true;
}

bool UMRBNNVolumeComponent::RenderOnce()
{
	const double TotalStartSeconds = FPlatformTime::Seconds();
	RefreshCameraFromPlayerView();

	if (!Backend)
	{
		if (!InitializeRenderer())
		{
			return false;
		}
	}

	EnsureOutputTexture();
	EnsureOutputRenderTarget();

	TArray<FLinearColor> SingleRenderAveragePixels;
	int32 SingleRenderFrameCount = 0;
	const int32 SafeSamplesPerRender = FMath::Clamp(SamplesPerRender, 1, 256);
	const int32 SafeMaxAccumulatedFrames = FMath::Max(MaxAccumulatedFrames, 1);
	FText Error;
	double BackendRenderSeconds = 0.0;
	for (int32 SampleIndex = 0; SampleIndex < SafeSamplesPerRender; ++SampleIndex)
	{
		TArray<FFloat16Color> SamplePixels;
		const double BackendStartSeconds = FPlatformTime::Seconds();
		if (!Backend->Render(OutputWidth, OutputHeight, FrameIndex, RenderSettings, SamplePixels, Error))
		{
			SetLastError(Error);
			return false;
		}
		BackendRenderSeconds += FPlatformTime::Seconds() - BackendStartSeconds;

		++FrameIndex;

		if (bAccumulateFrames)
		{
			AccumulateSample(SamplePixels, AccumulatedAveragePixels, AccumulatedFrameCount, SafeMaxAccumulatedFrames);
		}
		else
		{
			AccumulateSample(SamplePixels, SingleRenderAveragePixels, SingleRenderFrameCount, SafeSamplesPerRender);
		}
	}

	TArray<FFloat16Color> Pixels;
	TArray<FLinearColor> DisplayAveragePixels;
	if (bAccumulateFrames)
	{
		DisplayAveragePixels = AccumulatedAveragePixels;
	}
	else
	{
		DisplayAveragePixels = SingleRenderAveragePixels;
	}

	const double DenoiseStartSeconds = FPlatformTime::Seconds();
	ApplySpatialDenoise(DisplayAveragePixels);
	LastDenoiseTimeMs = static_cast<float>((FPlatformTime::Seconds() - DenoiseStartSeconds) * 1000.0);
	ConvertAverageToFloat16(DisplayAveragePixels, Pixels);

	const double UploadStartSeconds = FPlatformTime::Seconds();
	UploadPixelsToOutputTexture(Pixels);
	UploadPixelsToOutputRenderTarget(Pixels);
	LastTextureUploadTimeMs = static_cast<float>((FPlatformTime::Seconds() - UploadStartSeconds) * 1000.0);

	const double MaterialApplyStartSeconds = FPlatformTime::Seconds();
	ApplyOutputToMaterialTargets();
	LastMaterialApplyTimeMs = static_cast<float>((FPlatformTime::Seconds() - MaterialApplyStartSeconds) * 1000.0);

	LastFrameIndex = FrameIndex;
	LastRenderedSampleCount = SafeSamplesPerRender;
	LastAccumulatedFrameCount = AccumulatedFrameCount;
	LastBackendRenderTimeMs = static_cast<float>(BackendRenderSeconds * 1000.0);
	LastTotalRenderTimeMs = static_cast<float>((FPlatformTime::Seconds() - TotalStartSeconds) * 1000.0);
	LastEstimatedSamplesPerSecond = BackendRenderSeconds > UE_SMALL_NUMBER ? static_cast<float>(SafeSamplesPerRender / BackendRenderSeconds) : 0.0f;
	if (UMRBNNProjectSettings::Get()->bEnableVerboseLogging)
	{
		UE_LOG(LogTemp, Display, TEXT("MRBNN: %s"), *GetDebugSummary());
	}

	SetLastError(FText::GetEmpty());
	return true;
}

bool UMRBNNVolumeComponent::RenderComputeVolumeOnce(UVolumeTexture* DensityTexture, UVolumeTexture* FeatureTexture, const FMRBNNComputeVolumeSettings& ComputeSettings)
{
	FMRBNNComputeRenderer::FRenderDesc RenderDesc;
	if (!BuildComputeVolumeRenderDesc(DensityTexture, FeatureTexture, ComputeSettings, RenderDesc))
	{
		return false;
	}

	const double DispatchStartSeconds = FPlatformTime::Seconds();
	if (!FMRBNNComputeRenderer::EnqueueRender(RenderDesc))
	{
		SetLastError(NSLOCTEXT("MRBNN", "ComputeDispatchFailed", "MRBNN compute render could not enqueue the global shader dispatch."));
		return false;
	}

	MarkComputeVolumeRenderDispatched(DispatchStartSeconds);
	return true;
}

bool UMRBNNVolumeComponent::BuildComputeVolumeRenderDesc(UVolumeTexture* DensityTexture, UVolumeTexture* FeatureTexture, const FMRBNNComputeVolumeSettings& ComputeSettings, FMRBNNComputeRenderer::FRenderDesc& OutDesc)
{
	RefreshCameraFromPlayerView();
	EnsureOutputRenderTarget();

	if (!DensityTexture || !OutputRenderTarget)
	{
		SetLastError(NSLOCTEXT("MRBNN", "ComputeMissingTexture", "MRBNN compute render requires a density volume texture and an output render target."));
		return false;
	}

	FTextureResource* DensityResource = DensityTexture->GetResource();
	FTextureResource* FeatureResource = FeatureTexture ? FeatureTexture->GetResource() : nullptr;
	FTextureRenderTargetResource* RenderTargetResource = OutputRenderTarget->GameThread_GetRenderTargetResource();
	if (!DensityResource || !DensityResource->TextureRHI.IsValid() || !RenderTargetResource || !RenderTargetResource->GetRenderTargetTexture().IsValid())
	{
		SetLastError(NSLOCTEXT("MRBNN", "ComputeMissingRHI", "MRBNN compute render is waiting for RHI resources to initialize."));
		return false;
	}

	OutDesc = FMRBNNComputeRenderer::FRenderDesc();
	OutDesc.DensityTexture = DensityResource->TextureRHI;
	OutDesc.FeatureTexture = FeatureResource && FeatureResource->TextureRHI.IsValid() ? FeatureResource->TextureRHI : DensityResource->TextureRHI;
	OutDesc.OutputTexture = RenderTargetResource->GetRenderTargetTexture();
	OutDesc.OutputSize = FIntPoint(OutputWidth, OutputHeight);
	OutDesc.RenderSettings = RenderSettings;
	OutDesc.ComputeSettings = ComputeSettings;
	OutDesc.FrameIndex = FrameIndex;
	SetLastError(FText::GetEmpty());
	return true;
}

void UMRBNNVolumeComponent::MarkComputeVolumeRenderDispatched(double DispatchStartSeconds)
{
	const double TotalStartSeconds = DispatchStartSeconds;
	++FrameIndex;
	LastFrameIndex = FrameIndex;
	LastRenderedSampleCount = 1;
	LastAccumulatedFrameCount = 1;
	LastBackendRenderTimeMs = static_cast<float>((FPlatformTime::Seconds() - DispatchStartSeconds) * 1000.0);
	LastDenoiseTimeMs = 0.0f;
	LastTextureUploadTimeMs = 0.0f;

	const double MaterialApplyStartSeconds = FPlatformTime::Seconds();
	ApplyOutputToMaterialTargets();
	LastMaterialApplyTimeMs = static_cast<float>((FPlatformTime::Seconds() - MaterialApplyStartSeconds) * 1000.0);
	LastTotalRenderTimeMs = static_cast<float>((FPlatformTime::Seconds() - TotalStartSeconds) * 1000.0);
	LastEstimatedSamplesPerSecond = LastTotalRenderTimeMs > UE_SMALL_NUMBER ? 1000.0f / LastTotalRenderTimeMs : 0.0f;
	SetLastError(FText::GetEmpty());
}

void UMRBNNVolumeComponent::ResetProgressiveAccumulation()
{
	AccumulatedAveragePixels.Reset();
	AccumulatedFrameCount = 0;
	LastAccumulatedFrameCount = 0;
}

void UMRBNNVolumeComponent::ApplyGamePreviewSettings()
{
	OutputWidth = 512;
	OutputHeight = 512;
	SamplesPerRender = 1;
	bAccumulateFrames = true;
	MaxAccumulatedFrames = 32;
	SpatialDenoisePasses = 0;
	ResetProgressiveAccumulation();
}

void UMRBNNVolumeComponent::ApplyRealtimePreviewSettings()
{
	OutputWidth = 384;
	OutputHeight = 384;
	SamplesPerRender = 1;
	bAccumulateFrames = true;
	MaxAccumulatedFrames = 12;
	SpatialDenoisePasses = 0;
	RenderSettings.LightColor = FLinearColor(2.2f, 2.2f, 2.2f, 1.0f);
	RenderSettings.bFastDirectIllumination = true;
	RenderSettings.bEnableSkybox = false;
	RenderSettings.bEnableSkyboxBaking = false;
	ResetProgressiveAccumulation();
}

void UMRBNNVolumeComponent::ApplyMobilePreviewSettings()
{
	OutputWidth = 256;
	OutputHeight = 256;
	SamplesPerRender = 1;
	bAccumulateFrames = true;
	MaxAccumulatedFrames = 6;
	SpatialDenoisePasses = 0;
	RenderSettings.bFastDirectIllumination = true;
	RenderSettings.bEnableSkybox = false;
	RenderSettings.bEnableSkyboxBaking = false;
	ResetProgressiveAccumulation();
}

void UMRBNNVolumeComponent::ApplyBalancedPreviewSettings()
{
	OutputWidth = 768;
	OutputHeight = 768;
	SamplesPerRender = 4;
	bAccumulateFrames = true;
	MaxAccumulatedFrames = 96;
	SpatialDenoisePasses = 1;
	ResetProgressiveAccumulation();
}

void UMRBNNVolumeComponent::ApplyHighQualityPreviewSettings()
{
	OutputWidth = 1024;
	OutputHeight = 1024;
	SamplesPerRender = 16;
	bAccumulateFrames = true;
	MaxAccumulatedFrames = 256;
	SpatialDenoisePasses = 2;
	ResetProgressiveAccumulation();
}

void UMRBNNVolumeComponent::ApplyPaperPreviewSettings()
{
	OutputWidth = 1024;
	OutputHeight = 1024;
	SamplesPerRender = 8;
	bAccumulateFrames = true;
	MaxAccumulatedFrames = 128;
	SpatialDenoisePasses = 2;
	RenderSettings.ToneMapping = EMRBNNToneMapping::ACES;
	RenderSettings.Denoise = EMRBNNDenoiseMode::VisualPlausible;
	RenderSettings.Compatibility = EMRBNNCompatibilityMode::Normal;
	RenderSettings.bExcludeLightEncoding = true;
	RenderSettings.bFastDirectIllumination = false;
	RenderSettings.bEnableSkybox = false;
	RenderSettings.bEnableSkyboxBaking = true;
	ResetProgressiveAccumulation();
}

FString UMRBNNVolumeComponent::GetDebugSummary() const
{
	return FString::Printf(
		TEXT("%dx%d, samples=%d, accumulated=%d/%d, frame=%d, backend=%.2fms, denoise=%.2fms, upload=%.2fms, material=%.2fms, total=%.2fms, %.1f samples/s"),
		OutputWidth,
		OutputHeight,
		LastRenderedSampleCount,
		LastAccumulatedFrameCount,
		MaxAccumulatedFrames,
		LastFrameIndex,
		LastBackendRenderTimeMs,
		LastDenoiseTimeMs,
		LastTextureUploadTimeMs,
		LastMaterialApplyTimeMs,
		LastTotalRenderTimeMs,
		LastEstimatedSamplesPerSecond);
}

bool UMRBNNVolumeComponent::CanRunAutomaticRenderingInCurrentWorld() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (World->IsGameWorld())
	{
		return true;
	}

#if WITH_EDITOR
	return bAllowAutomaticRenderInEditor;
#else
	return false;
#endif
}

void UMRBNNVolumeComponent::SetCameraFromWorldLocation(const FVector& WorldLocation)
{
	const AActor* Owner = GetOwner();
	const FVector LocalLocation = Owner ? Owner->GetActorTransform().InverseTransformPosition(WorldLocation) : WorldLocation;
	const float SafeScale = FMath::Max(WorldUnitsPerMRBNNUnit, UE_KINDA_SMALL_NUMBER);
	RenderSettings.CameraPosition = LocalLocation / SafeScale;
}

void UMRBNNVolumeComponent::EnsureOutputTexture()
{
	if (OutputWidth <= 0)
	{
		OutputWidth = 1;
	}
	if (OutputHeight <= 0)
	{
		OutputHeight = 1;
	}

	if (OutputTexture && OutputTexture->GetSizeX() == OutputWidth && OutputTexture->GetSizeY() == OutputHeight)
	{
		return;
	}

	OutputTexture = UTexture2D::CreateTransient(OutputWidth, OutputHeight, PF_FloatRGBA, TEXT("MRBNN_Output"));
	OutputTexture->SRGB = false;
	OutputTexture->Filter = TF_Bilinear;
	OutputTexture->CompressionSettings = TC_HDR;
	OutputTexture->MipGenSettings = TMGS_NoMipmaps;
	OutputTexture->UpdateResource();
}

void UMRBNNVolumeComponent::EnsureOutputRenderTarget()
{
	if (OutputWidth <= 0)
	{
		OutputWidth = 1;
	}
	if (OutputHeight <= 0)
	{
		OutputHeight = 1;
	}

	if (!OutputRenderTarget)
	{
		OutputRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("MRBNN_OutputRT"), RF_Transient);
		OutputRenderTarget->ClearColor = FLinearColor::Black;
		OutputRenderTarget->bAutoGenerateMips = false;
		OutputRenderTarget->bSupportsUAV = true;
		OutputRenderTarget->Filter = TF_Bilinear;
		OutputRenderTarget->InitCustomFormat(OutputWidth, OutputHeight, PF_FloatRGBA, false);
		OutputRenderTarget->UpdateResourceImmediate(true);
		return;
	}

	if (OutputRenderTarget->SizeX != OutputWidth ||
		OutputRenderTarget->SizeY != OutputHeight ||
		OutputRenderTarget->OverrideFormat != PF_FloatRGBA ||
		!OutputRenderTarget->bSupportsUAV)
	{
		OutputRenderTarget->bSupportsUAV = true;
		OutputRenderTarget->InitCustomFormat(OutputWidth, OutputHeight, PF_FloatRGBA, false);
		OutputRenderTarget->UpdateResourceImmediate(true);
	}
}

void UMRBNNVolumeComponent::AccumulateSample(TConstArrayView<FFloat16Color> SamplePixels, TArray<FLinearColor>& InOutAveragePixels, int32& InOutFrameCount, int32 MaxFrameCount) const
{
	const int32 PixelCount = OutputWidth * OutputHeight;
	if (SamplePixels.Num() != PixelCount)
	{
		return;
	}

	if (InOutAveragePixels.Num() != PixelCount)
	{
		InOutAveragePixels.SetNumZeroed(PixelCount);
		InOutFrameCount = 0;
	}

	const int32 SafeMaxFrameCount = FMath::Max(MaxFrameCount, 1);
	const bool bGrowAverage = InOutFrameCount < SafeMaxFrameCount;
	const float SampleWeight = bGrowAverage ? 1.0f / static_cast<float>(InOutFrameCount + 1) : 1.0f / static_cast<float>(SafeMaxFrameCount);

	for (int32 Index = 0; Index < PixelCount; ++Index)
	{
		const FLinearColor SampleColor = SamplePixels[Index].GetFloats();
		FLinearColor& AverageColor = InOutAveragePixels[Index];
		AverageColor.R += (SampleColor.R - AverageColor.R) * SampleWeight;
		AverageColor.G += (SampleColor.G - AverageColor.G) * SampleWeight;
		AverageColor.B += (SampleColor.B - AverageColor.B) * SampleWeight;
		AverageColor.A += (SampleColor.A - AverageColor.A) * SampleWeight;
	}

	if (bGrowAverage)
	{
		++InOutFrameCount;
	}
}

void UMRBNNVolumeComponent::ApplySpatialDenoise(TArray<FLinearColor>& InOutPixels) const
{
	const int32 PixelCount = OutputWidth * OutputHeight;
	if (SpatialDenoisePasses <= 0 || InOutPixels.Num() != PixelCount || OutputWidth < 3 || OutputHeight < 3)
	{
		return;
	}

	TArray<FLinearColor> ScratchPixels;
	ScratchPixels.SetNumUninitialized(PixelCount);
	const int32 SafePassCount = FMath::Clamp(SpatialDenoisePasses, 0, 4);

	for (int32 PassIndex = 0; PassIndex < SafePassCount; ++PassIndex)
	{
		ScratchPixels = InOutPixels;
		for (int32 Y = 1; Y < OutputHeight - 1; ++Y)
		{
			for (int32 X = 1; X < OutputWidth - 1; ++X)
			{
				const int32 Index = Y * OutputWidth + X;
				const FLinearColor Filtered =
					(ScratchPixels[Index - OutputWidth - 1] +
					 ScratchPixels[Index - OutputWidth + 1] +
					 ScratchPixels[Index + OutputWidth - 1] +
					 ScratchPixels[Index + OutputWidth + 1] +
					 (ScratchPixels[Index - OutputWidth] + ScratchPixels[Index - 1] + ScratchPixels[Index + 1] + ScratchPixels[Index + OutputWidth]) * 2.0f +
					 ScratchPixels[Index] * 4.0f) / 16.0f;

				InOutPixels[Index] = Filtered;
			}
		}
	}
}

void UMRBNNVolumeComponent::ConvertAverageToFloat16(TConstArrayView<FLinearColor> AveragePixels, TArray<FFloat16Color>& OutPixels) const
{
	const int32 PixelCount = OutputWidth * OutputHeight;
	if (AveragePixels.Num() != PixelCount)
	{
		OutPixels.Reset();
		return;
	}

	OutPixels.SetNumUninitialized(PixelCount);
	for (int32 Index = 0; Index < PixelCount; ++Index)
	{
		OutPixels[Index] = FFloat16Color(AveragePixels[Index]);
	}
}

void UMRBNNVolumeComponent::RefreshCameraFromPlayerView()
{
	if (!bUsePlayerCamera)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const APlayerController* PlayerController = World->GetFirstPlayerController();
	if (!PlayerController || !PlayerController->PlayerCameraManager)
	{
		return;
	}

	SetCameraFromWorldLocation(PlayerController->PlayerCameraManager->GetCameraLocation());
}

void UMRBNNVolumeComponent::UploadPixelsToOutputTexture(TConstArrayView<FFloat16Color> Pixels)
{
	if (!OutputTexture || Pixels.Num() != OutputWidth * OutputHeight)
	{
		return;
	}

	const uint32 BytesPerPixel = sizeof(FFloat16Color);
	const uint32 SrcPitch = static_cast<uint32>(OutputWidth) * BytesPerPixel;
	const SIZE_T DataSize = Pixels.Num() * BytesPerPixel;

	uint8* TextureData = static_cast<uint8*>(FMemory::Malloc(DataSize));
	FMemory::Memcpy(TextureData, Pixels.GetData(), DataSize);

	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, OutputWidth, OutputHeight);
	OutputTexture->UpdateTextureRegions(
		0,
		1,
		Region,
		SrcPitch,
		BytesPerPixel,
		TextureData,
		[](uint8* SrcData, const FUpdateTextureRegion2D* Regions)
		{
			FMemory::Free(SrcData);
			delete Regions;
		});
}

void UMRBNNVolumeComponent::UploadPixelsToOutputRenderTarget(TConstArrayView<FFloat16Color> Pixels)
{
	if (!OutputRenderTarget || Pixels.Num() != OutputWidth * OutputHeight)
	{
		return;
	}

	FTextureRenderTargetResource* RenderTargetResource = OutputRenderTarget->GameThread_GetRenderTargetResource();
	if (!RenderTargetResource)
	{
		return;
	}

	const FTextureRHIRef TextureRHI = RenderTargetResource->GetRenderTargetTexture();
	if (!TextureRHI.IsValid())
	{
		return;
	}

	const uint32 BytesPerPixel = sizeof(FFloat16Color);
	const uint32 SrcPitch = static_cast<uint32>(OutputWidth) * BytesPerPixel;
	const SIZE_T DataSize = Pixels.Num() * BytesPerPixel;

	uint8* TextureData = static_cast<uint8*>(FMemory::Malloc(DataSize));
	FMemory::Memcpy(TextureData, Pixels.GetData(), DataSize);

	const FUpdateTextureRegion2D Region(0, 0, 0, 0, OutputWidth, OutputHeight);
	ENQUEUE_RENDER_COMMAND(MRBNN_UpdateOutputRenderTarget)(
		[TextureRHI, Region, SrcPitch, TextureData](FRHICommandListImmediate& RHICmdList)
		{
			if (TextureRHI.IsValid())
			{
				RHICmdList.UpdateTexture2D(TextureRHI, 0, Region, SrcPitch, TextureData);
			}
			FMemory::Free(TextureData);
	});
}

void UMRBNNVolumeComponent::ApplyOutputToMaterialTargets()
{
	if (!bApplyOutputToMaterials || !OutputRenderTarget || OutputTextureParameterName.IsNone())
	{
		return;
	}

	DynamicMaterialInstances.Reset();
	TArray<UPrimitiveComponent*> ComponentsToBind;
	for (UPrimitiveComponent* TargetComponent : TargetMaterialComponents)
	{
		if (TargetComponent)
		{
			ComponentsToBind.Add(TargetComponent);
		}
	}

	if (ComponentsToBind.IsEmpty() && bUseOwnerPrimitiveComponentsWhenTargetsEmpty)
	{
		if (AActor* Owner = GetOwner())
		{
			Owner->GetComponents<UPrimitiveComponent>(ComponentsToBind);
		}
	}

	for (UPrimitiveComponent* TargetComponent : ComponentsToBind)
	{
		if (!TargetComponent)
		{
			continue;
		}

		const int32 MaterialCount = TargetComponent->GetNumMaterials();
		for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
		{
			UMaterialInstanceDynamic* DynamicMaterial = Cast<UMaterialInstanceDynamic>(TargetComponent->GetMaterial(MaterialIndex));
			if (!DynamicMaterial)
			{
				DynamicMaterial = TargetComponent->CreateDynamicMaterialInstance(MaterialIndex);
			}

			if (DynamicMaterial)
			{
				DynamicMaterial->SetTextureParameterValue(OutputTextureParameterName, OutputRenderTarget);
				DynamicMaterialInstances.Add(DynamicMaterial);
			}
		}
	}
}

void UMRBNNVolumeComponent::SetLastError(const FText& Error)
{
	LastError = Error;
	if (!Error.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("MRBNN: %s"), *Error.ToString());
	}
}
