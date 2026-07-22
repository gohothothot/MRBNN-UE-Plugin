#include "MRBNNBlueprintLibrary.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Interfaces/IPluginManager.h"
#include "MRBNNProjectSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
FString GetPrimaryBridgePath()
{
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MRBNN")))
	{
		return FPaths::Combine(Plugin->GetBaseDir(), TEXT("Binaries/ThirdParty/MRBNNBridge/Win64/MRBNNBridge.dll"));
	}

	return FString();
}

FString NormalizePath(FString Path)
{
	FPaths::NormalizeFilename(Path);
	if (FPaths::IsRelative(Path))
	{
		Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
	}
	FPaths::CollapseRelativeDirectories(Path);
	return Path;
}

bool IsPathInsideRoot(const FString& CandidatePath, const FString& RootPath)
{
	FString Candidate = NormalizePath(CandidatePath);
	FString Root = NormalizePath(RootPath);
	FPaths::MakeStandardFilename(Candidate);
	FPaths::MakeStandardFilename(Root);
	if (Candidate.Equals(Root, ESearchCase::IgnoreCase))
	{
		return true;
	}

	if (!Root.EndsWith(TEXT("/")))
	{
		Root += TEXT("/");
	}
	return Candidate.StartsWith(Root, ESearchCase::IgnoreCase);
}

bool IsSafeRelativePath(const FString& RelativePath)
{
	FString NormalizedPath = RelativePath;
	FPaths::NormalizeFilename(NormalizedPath);
	if (NormalizedPath.IsEmpty() || !FPaths::IsRelative(NormalizedPath) || NormalizedPath.Contains(TEXT(":")))
	{
		return false;
	}

	TArray<FString> PathParts;
	NormalizedPath.ParseIntoArray(PathParts, TEXT("/"), true);
	for (const FString& PathPart : PathParts)
	{
		if (PathPart == TEXT(".."))
		{
			return false;
		}
	}

	return true;
}

bool ValidateNoSymlinkInExistingPath(const FString& Path, FText& OutError)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformPhysical();
	FString CurrentPath = NormalizePath(Path);
	for (;;)
	{
		if (PlatformFile.FileExists(*CurrentPath) || PlatformFile.DirectoryExists(*CurrentPath))
		{
			const ESymlinkResult SymlinkResult = PlatformFile.IsSymlink(*CurrentPath);
			if (SymlinkResult == ESymlinkResult::Symlink)
			{
				OutError = FText::Format(NSLOCTEXT("MRBNN", "BakeSymlinkPathRejected", "MRBNN bake refuses to copy through a symlink or junction: {0}"), FText::FromString(CurrentPath));
				return false;
			}
		}

		const FString ParentPath = FPaths::GetPath(CurrentPath);
		if (ParentPath.IsEmpty() || ParentPath.Equals(CurrentPath, ESearchCase::IgnoreCase))
		{
			return true;
		}

		CurrentPath = ParentPath;
	}
}

bool CopyFileEnsuringDirectory(const FString& SourcePath, const FString& DestinationPath, FText& OutError)
{
	if (!FPaths::FileExists(SourcePath))
	{
		OutError = FText::Format(NSLOCTEXT("MRBNN", "BakeSourceFileMissing", "MRBNN bake source file is missing: {0}"), FText::FromString(SourcePath));
		return false;
	}

	const FString NormalizedSourcePath = NormalizePath(SourcePath);
	const FString NormalizedDestinationPath = NormalizePath(DestinationPath);
	if (NormalizedSourcePath.Equals(NormalizedDestinationPath, ESearchCase::IgnoreCase))
	{
		return true;
	}
	if (!ValidateNoSymlinkInExistingPath(NormalizedSourcePath, OutError))
	{
		return false;
	}

	IFileManager& FileManager = IFileManager::Get();
	FileManager.MakeDirectory(*FPaths::GetPath(NormalizedDestinationPath), true);
	if (!ValidateNoSymlinkInExistingPath(NormalizedDestinationPath, OutError))
	{
		return false;
	}
	if (FileManager.Copy(*NormalizedDestinationPath, *NormalizedSourcePath, true, true) != COPY_OK)
	{
		OutError = FText::Format(NSLOCTEXT("MRBNN", "BakeCopyFailed", "Failed to copy MRBNN bake file from {0} to {1}."), FText::FromString(NormalizedSourcePath), FText::FromString(NormalizedDestinationPath));
		return false;
	}

	return true;
}

bool ReadConfigJson(const FString& WorkingDirectory, TSharedPtr<FJsonObject>& OutConfig, FText& OutError)
{
	const FString ConfigPath = FPaths::Combine(WorkingDirectory, TEXT("config.json"));
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *ConfigPath))
	{
		OutError = FText::Format(NSLOCTEXT("MRBNN", "BakeConfigReadFailed", "Failed to read MRBNN config during bake: {0}"), FText::FromString(ConfigPath));
		return false;
	}

	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, OutConfig) || !OutConfig.IsValid())
	{
		OutError = FText::Format(NSLOCTEXT("MRBNN", "BakeConfigParseFailed", "Failed to parse MRBNN config during bake: {0}"), FText::FromString(ConfigPath));
		return false;
	}

	return true;
}

bool TryMakeRelativeToRoot(const FString& AbsolutePath, const FString& RootPath, FString& OutRelativePath)
{
	OutRelativePath = AbsolutePath;
	FString MutableRelativePath = OutRelativePath;
	FString MutableRootPath = RootPath;
	FPaths::NormalizeDirectoryName(MutableRootPath);
	if (FPaths::MakePathRelativeTo(MutableRelativePath, *MutableRootPath))
	{
		OutRelativePath = MutableRelativePath;
		FPaths::NormalizeFilename(OutRelativePath);
		return true;
	}

	OutRelativePath = FPaths::GetCleanFilename(AbsolutePath);
	return false;
}

bool ShouldSkipBakedRelativeFile(const FString& RelativePath, bool bIncludeSkyboxBakingData)
{
	FString NormalizedRelativePath = RelativePath;
	FPaths::NormalizeFilename(NormalizedRelativePath);
	return !bIncludeSkyboxBakingData && NormalizedRelativePath.StartsWith(TEXT("skybox/"), ESearchCase::IgnoreCase);
}

FString MakeSafeSceneDirectoryName(const FString& SceneName, const FString& FallbackPath)
{
	FString Result = SceneName.TrimStartAndEnd();
	Result.ReplaceInline(TEXT("\\"), TEXT("/"));
	Result = FPaths::GetCleanFilename(Result);
	Result.ReplaceInline(TEXT(".."), TEXT(""));
	return Result.IsEmpty() ? FPaths::GetCleanFilename(FallbackPath) : Result;
}
}

bool UMRBNNBlueprintLibrary::ValidateMRBNNBakedVolumeData(UMRBNNBakedVolumeData* BakedData, FText& OutError)
{
	if (!BakedData)
	{
		OutError = NSLOCTEXT("MRBNN", "NullBakedData", "MRBNN baked data asset is null.");
		return false;
	}

	return BakedData->ValidateData(OutError);
}

FString UMRBNNBlueprintLibrary::GetExpectedMRBNNBridgePath()
{
	return GetPrimaryBridgePath();
}

bool UMRBNNBlueprintLibrary::IsMRBNNBridgeAvailable(FString& OutBridgePath, FText& OutError)
{
	OutBridgePath = GetPrimaryBridgePath();
	if (OutBridgePath.IsEmpty())
	{
		OutError = NSLOCTEXT("MRBNN", "PluginMissing", "MRBNN plugin directory could not be resolved.");
		return false;
	}

	if (!FPaths::FileExists(OutBridgePath))
	{
		OutError = FText::Format(NSLOCTEXT("MRBNN", "BridgeMissingAtExpectedPath", "MRBNNBridge.dll is missing: {0}"), FText::FromString(OutBridgePath));
		return false;
	}

	const FString ExternalTCNNPath = FPaths::Combine(FPaths::GetPath(OutBridgePath), TEXT("ExternalTCNN.dll"));
	if (!FPaths::FileExists(ExternalTCNNPath))
	{
		OutError = FText::Format(NSLOCTEXT("MRBNN", "ExternalTCNNMissing", "ExternalTCNN.dll is missing next to MRBNNBridge.dll: {0}"), FText::FromString(ExternalTCNNPath));
		return false;
	}

	OutError = FText::GetEmpty();
	return true;
}

FString UMRBNNBlueprintLibrary::ResolveMRBNNPath(const FString& PathWithTokens)
{
	return UMRBNNProjectSettings::ResolveMRBNNPath(PathWithTokens);
}

UMRBNNBakedVolumeData* UMRBNNBlueprintLibrary::CreateMRBNNBakedVolumeDataFromProjectSettings(UObject* Outer, FText& OutError)
{
	return UMRBNNProjectSettings::Get()->CreateTransientDefaultBakedData(Outer, OutError);
}

bool UMRBNNBlueprintLibrary::BakeMRBNNDataToPluginData(UMRBNNBakedVolumeData* SourceData, const FString& SceneName, bool bIncludeSkyboxHDRI, bool bIncludeSkyboxBakingData, FText& OutError)
{
	if (!SourceData)
	{
		OutError = NSLOCTEXT("MRBNN", "BakeNullSourceData", "MRBNN bake source data is null.");
		return false;
	}

	FText ValidationError;
	if (!SourceData->ValidateData(ValidationError))
	{
		OutError = ValidationError;
		return false;
	}

	FString SourceWorkingDirectory;
	FString SourceRepositoryRoot;
	if (!SourceData->ResolvePaths(SourceWorkingDirectory, SourceRepositoryRoot, OutError))
	{
		return false;
	}

	const UMRBNNProjectSettings* Settings = UMRBNNProjectSettings::Get();
	const FString DestinationRepositoryRoot = NormalizePath(UMRBNNProjectSettings::ResolveMRBNNPath(
		Settings->BakeDestinationRepositoryRoot.Path.IsEmpty() ? FString(TEXT("$(PluginDir)")) : Settings->BakeDestinationRepositoryRoot.Path));
	const FString SafeSceneName = MakeSafeSceneDirectoryName(SceneName, SourceWorkingDirectory);
	const FString DestinationWorkingDirectory = NormalizePath(FPaths::Combine(DestinationRepositoryRoot, TEXT("Data"), SafeSceneName));

	IFileManager& FileManager = IFileManager::Get();
	FileManager.MakeDirectory(*DestinationWorkingDirectory, true);

	TArray<FString> SourceFiles;
	FileManager.FindFilesRecursive(SourceFiles, *SourceWorkingDirectory, TEXT("*.*"), true, false);
	for (const FString& SourceFile : SourceFiles)
	{
		FString RelativePath = SourceFile;
		FPaths::MakePathRelativeTo(RelativePath, *SourceWorkingDirectory);
		FPaths::NormalizeFilename(RelativePath);
		if (ShouldSkipBakedRelativeFile(RelativePath, bIncludeSkyboxBakingData))
		{
			continue;
		}

		if (!CopyFileEnsuringDirectory(SourceFile, FPaths::Combine(DestinationWorkingDirectory, RelativePath), OutError))
		{
			return false;
		}
	}

	TSharedPtr<FJsonObject> Config;
	if (!ReadConfigJson(SourceWorkingDirectory, Config, OutError))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* VolumeObject = nullptr;
	if (Config->TryGetObjectField(TEXT("volume"), VolumeObject) && VolumeObject && VolumeObject->IsValid())
	{
		FString VolumePath;
		if ((*VolumeObject)->TryGetStringField(TEXT("path"), VolumePath) && !VolumePath.IsEmpty())
		{
			const FString SourceVolumePath = NormalizePath(FPaths::IsRelative(VolumePath) ? FPaths::Combine(SourceRepositoryRoot, VolumePath) : VolumePath);
			FString RelativeVolumePath;
			TryMakeRelativeToRoot(SourceVolumePath, SourceRepositoryRoot, RelativeVolumePath);
			if (FPaths::IsRelative(VolumePath))
			{
				RelativeVolumePath = VolumePath;
			}

			if (!IsPathInsideRoot(SourceVolumePath, SourceRepositoryRoot) || !IsSafeRelativePath(RelativeVolumePath))
			{
				OutError = FText::Format(NSLOCTEXT("MRBNN", "BakeUnsafeVolumePath", "MRBNN volume path is outside the source repository or is not a safe relative path: {0}"), FText::FromString(VolumePath));
				return false;
			}

			const FString DestinationVolumePath = NormalizePath(FPaths::Combine(DestinationRepositoryRoot, RelativeVolumePath));
			if (!IsPathInsideRoot(DestinationVolumePath, DestinationRepositoryRoot))
			{
				OutError = FText::Format(NSLOCTEXT("MRBNN", "BakeVolumeDestinationOutsideRoot", "MRBNN volume destination is outside the plugin data root: {0}"), FText::FromString(DestinationVolumePath));
				return false;
			}

			if (!CopyFileEnsuringDirectory(SourceVolumePath, DestinationVolumePath, OutError))
			{
				return false;
			}
		}
	}

	if (bIncludeSkyboxHDRI)
	{
		FString SourceSkyboxPath;
		if (!SourceData->ResolveSkyboxPath(SourceSkyboxPath, OutError))
		{
			return false;
		}

		if (!SourceSkyboxPath.IsEmpty())
		{
			FString RelativeSkyboxPath;
			TryMakeRelativeToRoot(SourceSkyboxPath, SourceRepositoryRoot, RelativeSkyboxPath);
			if (!IsPathInsideRoot(SourceSkyboxPath, SourceRepositoryRoot) || !IsSafeRelativePath(RelativeSkyboxPath))
			{
				OutError = FText::Format(NSLOCTEXT("MRBNN", "BakeUnsafeSkyboxPath", "MRBNN skybox path is outside the source repository or is not a safe relative path: {0}"), FText::FromString(SourceSkyboxPath));
				return false;
			}

			const FString DestinationSkyboxPath = NormalizePath(FPaths::Combine(DestinationRepositoryRoot, RelativeSkyboxPath));
			if (!IsPathInsideRoot(DestinationSkyboxPath, DestinationRepositoryRoot))
			{
				OutError = FText::Format(NSLOCTEXT("MRBNN", "BakeSkyboxDestinationOutsideRoot", "MRBNN skybox destination is outside the plugin data root: {0}"), FText::FromString(DestinationSkyboxPath));
				return false;
			}

			if (!CopyFileEnsuringDirectory(SourceSkyboxPath, DestinationSkyboxPath, OutError))
			{
				return false;
			}
		}
	}

	const FString ManifestText = FString::Printf(
		TEXT("{\n  \"scene\": \"%s\",\n  \"source_working_directory\": \"%s\",\n  \"source_repository_root\": \"%s\",\n  \"destination_repository_root\": \"%s\"\n}\n"),
		*SafeSceneName,
		*SourceWorkingDirectory.Replace(TEXT("\\"), TEXT("/")),
		*SourceRepositoryRoot.Replace(TEXT("\\"), TEXT("/")),
		*DestinationRepositoryRoot.Replace(TEXT("\\"), TEXT("/")));
	FFileHelper::SaveStringToFile(ManifestText, *FPaths::Combine(DestinationWorkingDirectory, TEXT("mrbnn_ue_manifest.json")));

	OutError = FText::GetEmpty();
	return true;
}
