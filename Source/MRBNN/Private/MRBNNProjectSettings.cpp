#include "MRBNNProjectSettings.h"

#include "MRBNNBakedVolumeData.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

namespace
{
FString GetPluginBaseDir()
{
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MRBNN")))
	{
		return FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir());
	}

	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineDir(), TEXT("Plugins/Experimental/MRBNN")));
}

FString NormalizeResolvedPath(FString Path)
{
	FPaths::NormalizeFilename(Path);
	if (FPaths::IsRelative(Path))
	{
		Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
	}
	FPaths::CollapseRelativeDirectories(Path);
	return Path;
}

FString ResolvePathTokenString(const FString& InPath)
{
	FString Result = InPath;
	const FString PluginBaseDir = GetPluginBaseDir();
	Result.ReplaceInline(TEXT("$(PluginDir)"), *PluginBaseDir, ESearchCase::IgnoreCase);
	Result.ReplaceInline(TEXT("${PluginDir}"), *PluginBaseDir, ESearchCase::IgnoreCase);
	Result.ReplaceInline(TEXT("%MRBNN_PLUGIN%"), *PluginBaseDir, ESearchCase::IgnoreCase);
	Result.ReplaceInline(TEXT("$(ProjectDir)"), *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), ESearchCase::IgnoreCase);
	Result.ReplaceInline(TEXT("${ProjectDir}"), *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), ESearchCase::IgnoreCase);
	Result.ReplaceInline(TEXT("$(EngineDir)"), *FPaths::ConvertRelativePathToFull(FPaths::EngineDir()), ESearchCase::IgnoreCase);
	Result.ReplaceInline(TEXT("${EngineDir}"), *FPaths::ConvertRelativePathToFull(FPaths::EngineDir()), ESearchCase::IgnoreCase);
	return NormalizeResolvedPath(Result);
}
}

UMRBNNProjectSettings::UMRBNNProjectSettings()
{
	DefaultRepositoryRoot.Path = TEXT("$(PluginDir)");
	DefaultDataRoot.Path = TEXT("$(PluginDir)/Data");
	DefaultSceneName = TEXT("cloud-03");
	DefaultSkyboxHDRI.FilePath = TEXT("$(PluginDir)/Data/qwantani_sunset_puresky_4k.hdr");
	BakeDestinationRepositoryRoot.Path = TEXT("$(PluginDir)");
	BakeConsoleExecutableOverride.FilePath = TEXT("$(PluginDir)/Binaries/ThirdParty/MRBNNBridge/Win64/MRBNNBakeConsole.exe");
	BakeSyncManifestPath.FilePath = TEXT("$(PluginDir)/Binaries/ThirdParty/MRBNNBridge/Win64/CloudInfoBakes/mrbnn_bake_sync_manifest.json");
}

const UMRBNNProjectSettings* UMRBNNProjectSettings::Get()
{
	return GetDefault<UMRBNNProjectSettings>();
}

UMRBNNProjectSettings* UMRBNNProjectSettings::GetMutable()
{
	return GetMutableDefault<UMRBNNProjectSettings>();
}

FName UMRBNNProjectSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FName UMRBNNProjectSettings::GetSectionName() const
{
	return TEXT("MRBNN");
}

FString UMRBNNProjectSettings::ResolveMRBNNPath(const FString& PathWithTokens)
{
	if (PathWithTokens.IsEmpty())
	{
		return FString();
	}

	return ResolvePathTokenString(PathWithTokens);
}

UMRBNNBakedVolumeData* UMRBNNProjectSettings::CreateTransientDefaultBakedData(UObject* Outer, FText& OutError) const
{
	if (!DefaultBakedDataAsset.IsNull())
	{
		UMRBNNBakedVolumeData* LoadedAsset = DefaultBakedDataAsset.LoadSynchronous();
		if (!LoadedAsset)
		{
			OutError = NSLOCTEXT("MRBNN", "DefaultBakedDataAssetMissing", "The MRBNN default baked data asset could not be loaded.");
		}
		return LoadedAsset;
	}

	FString RepositoryRoot;
	FString WorkingDirectory;
	FString SkyboxPath;
	FString SkyboxBakingDirectory;
	if (!ResolveDefaultDataSet(RepositoryRoot, WorkingDirectory, SkyboxPath, SkyboxBakingDirectory, OutError))
	{
		return nullptr;
	}

	UObject* TargetOuter = Outer ? Outer : GetTransientPackage();
	UMRBNNBakedVolumeData* Data = NewObject<UMRBNNBakedVolumeData>(TargetOuter, TEXT("MRBNNProjectDefaultBakedData"), RF_Transient);
	Data->RepositoryRoot.Path = RepositoryRoot;
	Data->WorkingDirectory.Path = WorkingDirectory;
	Data->SkyboxHDRI.FilePath = SkyboxPath;
	Data->SkyboxExposure = 1.0f;
	Data->SkyboxBakingDirectory.Path = SkyboxBakingDirectory;
	OutError = FText::GetEmpty();
	return Data;
}

bool UMRBNNProjectSettings::ResolveDefaultDataSet(FString& OutRepositoryRoot, FString& OutWorkingDirectory, FString& OutSkyboxPath, FString& OutSkyboxBakingDirectory, FText& OutError) const
{
	OutRepositoryRoot = ResolveMRBNNPath(DefaultRepositoryRoot.Path.IsEmpty() ? TEXT("$(PluginDir)") : DefaultRepositoryRoot.Path);
	const FString DataRoot = ResolveMRBNNPath(DefaultDataRoot.Path.IsEmpty() ? TEXT("$(PluginDir)/Data") : DefaultDataRoot.Path);
	const FString SceneName = DefaultSceneName.IsEmpty() ? TEXT("cloud-03") : DefaultSceneName;
	OutWorkingDirectory = NormalizeResolvedPath(FPaths::Combine(DataRoot, SceneName));
	OutSkyboxPath = bEnableDefaultSkybox ? ResolveMRBNNPath(DefaultSkyboxHDRI.FilePath) : FString();
	OutSkyboxBakingDirectory = bEnableDefaultSkyboxBaking ? NormalizeResolvedPath(FPaths::Combine(OutWorkingDirectory, TEXT("skybox"))) : FString();

	if (!FPaths::DirectoryExists(OutRepositoryRoot))
	{
		OutError = FText::Format(NSLOCTEXT("MRBNN", "SettingsRepositoryRootMissing", "MRBNN repository root does not exist: {0}"), FText::FromString(OutRepositoryRoot));
		return false;
	}

	if (!FPaths::DirectoryExists(OutWorkingDirectory))
	{
		OutError = FText::Format(NSLOCTEXT("MRBNN", "SettingsWorkingDirectoryMissing", "MRBNN default data directory does not exist: {0}"), FText::FromString(OutWorkingDirectory));
		return false;
	}

	if (!OutSkyboxPath.IsEmpty() && !FPaths::FileExists(OutSkyboxPath))
	{
		OutError = FText::Format(NSLOCTEXT("MRBNN", "SettingsSkyboxMissing", "MRBNN default skybox HDRI does not exist: {0}"), FText::FromString(OutSkyboxPath));
		return false;
	}

	if (!OutSkyboxBakingDirectory.IsEmpty() && !FPaths::DirectoryExists(OutSkyboxBakingDirectory))
	{
		OutError = FText::Format(NSLOCTEXT("MRBNN", "SettingsSkyboxBakingMissing", "MRBNN default skybox baking directory does not exist: {0}"), FText::FromString(OutSkyboxBakingDirectory));
		return false;
	}

	OutError = FText::GetEmpty();
	return true;
}

void UMRBNNProjectSettings::EnsureSceneViewDebugPreviewRenderTarget(FIntPoint OutputSize)
{
	OutputSize.X = FMath::Max(OutputSize.X, 1);
	OutputSize.Y = FMath::Max(OutputSize.Y, 1);

	if (!SceneViewDebugPreviewRenderTarget)
	{
		SceneViewDebugPreviewRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("MRBNN_ProjectSceneViewDebugRT"), RF_Transient);
		SceneViewDebugPreviewRenderTarget->ClearColor = FLinearColor::Transparent;
		SceneViewDebugPreviewRenderTarget->bAutoGenerateMips = false;
		SceneViewDebugPreviewRenderTarget->Filter = TF_Bilinear;
		SceneViewDebugPreviewRenderTarget->InitCustomFormat(OutputSize.X, OutputSize.Y, PF_FloatRGBA, false);
		SceneViewDebugPreviewRenderTarget->UpdateResourceImmediate(true);
		return;
	}

	if (SceneViewDebugPreviewRenderTarget->SizeX != OutputSize.X ||
		SceneViewDebugPreviewRenderTarget->SizeY != OutputSize.Y ||
		SceneViewDebugPreviewRenderTarget->OverrideFormat != PF_FloatRGBA)
	{
		SceneViewDebugPreviewRenderTarget->InitCustomFormat(OutputSize.X, OutputSize.Y, PF_FloatRGBA, false);
		SceneViewDebugPreviewRenderTarget->UpdateResourceImmediate(true);
	}
}

void UMRBNNProjectSettings::MarkSceneViewDebugPreviewUpdated(const FString& DebugName, FIntPoint OutputSize, int32 FrameIndex, int32 CloudCount)
{
	LastSceneViewDebugRenderTargetName = DebugName;
	LastSceneViewDebugOutputSize = OutputSize;
	LastSceneViewDebugFrameIndex = FrameIndex;
	LastSceneViewDebugCloudCount = CloudCount;
}
