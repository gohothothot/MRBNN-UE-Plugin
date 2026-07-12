#include "MRBNNBakedVolumeData.h"

#include "Dom/JsonObject.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool FileExistsInDirectory(const FString& Directory, const FString& FileName, FText& OutError)
{
	const FString Path = FPaths::Combine(Directory, FileName);
	if (!FPaths::FileExists(Path))
	{
		OutError = FText::Format(NSLOCTEXT("MRBNN", "MissingFile", "Missing MRBNN file: {0}"), FText::FromString(Path));
		return false;
	}
	return true;
}

bool TryLoadConfig(const FString& WorkingDirectory, TSharedPtr<FJsonObject>& OutConfig, FText& OutError)
{
	FString JsonText;
	const FString ConfigPath = FPaths::Combine(WorkingDirectory, TEXT("config.json"));
	if (!FFileHelper::LoadFileToString(JsonText, *ConfigPath))
	{
		OutError = FText::Format(NSLOCTEXT("MRBNN", "ConfigReadFailed", "Failed to read MRBNN config: {0}"), FText::FromString(ConfigPath));
		return false;
	}

	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, OutConfig) || !OutConfig.IsValid())
	{
		OutError = FText::Format(NSLOCTEXT("MRBNN", "ConfigParseFailed", "Failed to parse MRBNN config: {0}"), FText::FromString(ConfigPath));
		return false;
	}

	return true;
}

FString ExpandPathTokens(const FString& Path)
{
	FString Result = Path;
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MRBNN")))
	{
		const FString PluginBaseDir = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir());
		Result.ReplaceInline(TEXT("$(PluginDir)"), *PluginBaseDir, ESearchCase::IgnoreCase);
		Result.ReplaceInline(TEXT("${PluginDir}"), *PluginBaseDir, ESearchCase::IgnoreCase);
		Result.ReplaceInline(TEXT("%MRBNN_PLUGIN%"), *PluginBaseDir, ESearchCase::IgnoreCase);
	}
	Result.ReplaceInline(TEXT("$(ProjectDir)"), *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), ESearchCase::IgnoreCase);
	Result.ReplaceInline(TEXT("${ProjectDir}"), *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), ESearchCase::IgnoreCase);
	Result.ReplaceInline(TEXT("$(EngineDir)"), *FPaths::ConvertRelativePathToFull(FPaths::EngineDir()), ESearchCase::IgnoreCase);
	Result.ReplaceInline(TEXT("${EngineDir}"), *FPaths::ConvertRelativePathToFull(FPaths::EngineDir()), ESearchCase::IgnoreCase);
	FPaths::NormalizeFilename(Result);
	FPaths::CollapseRelativeDirectories(Result);
	return Result;
}

FString ResolveDirectory(const FString& Path)
{
	FString Result = ExpandPathTokens(Path);
	if (FPaths::IsRelative(Result))
	{
		Result = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Result);
	}
	FPaths::CollapseRelativeDirectories(Result);
	return Result;
}

FString GuessRepositoryRoot(const FString& WorkingDirectory)
{
	FString Candidate = WorkingDirectory;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (FPaths::FileExists(FPaths::Combine(Candidate, TEXT("README.md"))) &&
			FPaths::DirectoryExists(FPaths::Combine(Candidate, TEXT("data"))))
		{
			return Candidate;
		}

		const FString PluginFileName = FPaths::GetCleanFilename(Candidate) + TEXT(".uplugin");
		if (FPaths::FileExists(FPaths::Combine(Candidate, PluginFileName)) &&
			(FPaths::DirectoryExists(FPaths::Combine(Candidate, TEXT("Data"))) || FPaths::DirectoryExists(FPaths::Combine(Candidate, TEXT("data")))))
		{
			return Candidate;
		}

		Candidate = FPaths::GetPath(Candidate);
	}

	return FPaths::GetPath(FPaths::GetPath(WorkingDirectory));
}
}

bool UMRBNNBakedVolumeData::ResolvePaths(FString& OutWorkingDirectory, FString& OutRepositoryRoot, FText& OutError) const
{
	if (WorkingDirectory.Path.IsEmpty())
	{
		OutError = NSLOCTEXT("MRBNN", "NoWorkingDirectory", "MRBNN working directory is empty.");
		return false;
	}

	OutWorkingDirectory = ResolveDirectory(WorkingDirectory.Path);
	if (!FPaths::DirectoryExists(OutWorkingDirectory))
	{
		OutError = FText::Format(NSLOCTEXT("MRBNN", "WorkingDirectoryMissing", "MRBNN working directory does not exist: {0}"), FText::FromString(OutWorkingDirectory));
		return false;
	}

	OutRepositoryRoot = RepositoryRoot.Path.IsEmpty() ? GuessRepositoryRoot(OutWorkingDirectory) : ResolveDirectory(RepositoryRoot.Path);
	if (!FPaths::DirectoryExists(OutRepositoryRoot))
	{
		OutError = FText::Format(NSLOCTEXT("MRBNN", "RepositoryRootMissing", "MRBNN repository root does not exist: {0}"), FText::FromString(OutRepositoryRoot));
		return false;
	}

	return true;
}

bool UMRBNNBakedVolumeData::ResolveSkyboxPath(FString& OutSkyboxPath, FText& OutError) const
{
	OutSkyboxPath.Reset();
	if (SkyboxHDRI.FilePath.IsEmpty())
	{
		return true;
	}

	FString WorkingDir;
	FString RootDir;
	if (!ResolvePaths(WorkingDir, RootDir, OutError))
	{
		return false;
	}

	OutSkyboxPath = SkyboxHDRI.FilePath;
	OutSkyboxPath = ExpandPathTokens(OutSkyboxPath);
	if (FPaths::IsRelative(OutSkyboxPath))
	{
		OutSkyboxPath = FPaths::Combine(RootDir, OutSkyboxPath);
	}
	FPaths::CollapseRelativeDirectories(OutSkyboxPath);

	if (!FPaths::FileExists(OutSkyboxPath))
	{
		OutError = FText::Format(NSLOCTEXT("MRBNN", "SkyboxMissing", "MRBNN skybox HDRI does not exist: {0}"), FText::FromString(OutSkyboxPath));
		return false;
	}

	return true;
}

bool UMRBNNBakedVolumeData::ResolveSkyboxBakingDirectory(FString& OutSkyboxBakingDirectory, FText& OutError) const
{
	OutSkyboxBakingDirectory.Reset();
	if (SkyboxBakingDirectory.Path.IsEmpty())
	{
		return true;
	}

	FString WorkingDir;
	FString RootDir;
	if (!ResolvePaths(WorkingDir, RootDir, OutError))
	{
		return false;
	}

	OutSkyboxBakingDirectory = SkyboxBakingDirectory.Path;
	OutSkyboxBakingDirectory = ExpandPathTokens(OutSkyboxBakingDirectory);
	if (FPaths::IsRelative(OutSkyboxBakingDirectory))
	{
		OutSkyboxBakingDirectory = FPaths::Combine(RootDir, OutSkyboxBakingDirectory);
	}
	FPaths::CollapseRelativeDirectories(OutSkyboxBakingDirectory);

	if (!FPaths::DirectoryExists(OutSkyboxBakingDirectory))
	{
		OutError = FText::Format(NSLOCTEXT("MRBNN", "SkyboxBakingMissing", "MRBNN skybox baking directory does not exist: {0}"), FText::FromString(OutSkyboxBakingDirectory));
		return false;
	}

	TSharedPtr<FJsonObject> Config;
	if (!TryLoadConfig(OutSkyboxBakingDirectory, Config, OutError))
	{
		return false;
	}

	int32 StepNum = 0;
	if (!Config->TryGetNumberField(TEXT("step_num"), StepNum) || StepNum <= 0)
	{
		OutError = NSLOCTEXT("MRBNN", "InvalidSkyboxStepNum", "MRBNN skybox baking config.json must contain a positive step_num.");
		return false;
	}

	const FString RequiredFiles[] = {
		TEXT("config.json"),
		TEXT("base.bin"),
		TEXT("attr0.bin"),
		TEXT("attr1.bin"),
		TEXT("attr2.bin"),
		TEXT("tcnn.final_mlp.bin")
	};

	for (const FString& FileName : RequiredFiles)
	{
		if (!FileExistsInDirectory(OutSkyboxBakingDirectory, FileName, OutError))
		{
			return false;
		}
	}

	for (int32 Index = 0; Index < 9; ++Index)
	{
		if (!FileExistsInDirectory(OutSkyboxBakingDirectory, FString::Printf(TEXT("ms%d.bin"), Index), OutError))
		{
			return false;
		}
	}

	for (int32 Index = 0; Index < StepNum; ++Index)
	{
		if (!FileExistsInDirectory(OutSkyboxBakingDirectory, FString::Printf(TEXT("tcnn.mlp%d.bin"), Index), OutError))
		{
			return false;
		}
	}

	return true;
}

bool UMRBNNBakedVolumeData::ValidateData(FText& OutError) const
{
	FString WorkingDir;
	FString RootDir;
	if (!ResolvePaths(WorkingDir, RootDir, OutError))
	{
		return false;
	}

	TSharedPtr<FJsonObject> Config;
	if (!TryLoadConfig(WorkingDir, Config, OutError))
	{
		return false;
	}

	int32 StepNum = 0;
	if (!Config->TryGetNumberField(TEXT("step_num"), StepNum) || StepNum <= 0)
	{
		OutError = NSLOCTEXT("MRBNN", "InvalidStepNum", "MRBNN config.json must contain a positive step_num.");
		return false;
	}

	const FString RequiredRootFiles[] = {
		TEXT("base.bin"),
		TEXT("view.bin"),
		TEXT("light.bin"),
		TEXT("hg.bin"),
		TEXT("albedo.bin"),
		TEXT("final_mlp.weight0.bin"),
		TEXT("final_mlp.weight1.bin"),
		TEXT("tcnn.final_mlp.bin")
	};

	for (const FString& FileName : RequiredRootFiles)
	{
		if (!FileExistsInDirectory(WorkingDir, FileName, OutError))
		{
			return false;
		}
	}

	for (int32 Index = 0; Index < StepNum; ++Index)
	{
		if (!FileExistsInDirectory(WorkingDir, FString::Printf(TEXT("mlp%d.weight0.bin"), Index), OutError) ||
			!FileExistsInDirectory(WorkingDir, FString::Printf(TEXT("mlp%d.weight1.bin"), Index), OutError) ||
			!FileExistsInDirectory(WorkingDir, FString::Printf(TEXT("tcnn.mlp%d.bin"), Index), OutError))
		{
			return false;
		}
	}

	for (int32 Index = 0; Index < 9; ++Index)
	{
		if (!FileExistsInDirectory(WorkingDir, FString::Printf(TEXT("ms%d.bin"), Index), OutError))
		{
			return false;
		}
	}

	const TSharedPtr<FJsonObject>* VolumeObject = nullptr;
	if (Config->TryGetObjectField(TEXT("volume"), VolumeObject) && VolumeObject && VolumeObject->IsValid())
	{
		FString VolumePath;
		if ((*VolumeObject)->TryGetStringField(TEXT("path"), VolumePath))
		{
			const FString ResolvedVolumePath = FPaths::IsRelative(VolumePath) ? FPaths::Combine(RootDir, VolumePath) : VolumePath;
			if (!FPaths::FileExists(ResolvedVolumePath))
			{
				OutError = FText::Format(NSLOCTEXT("MRBNN", "VolumeMissing", "MRBNN volume file does not exist: {0}"), FText::FromString(ResolvedVolumePath));
				return false;
			}
		}
	}

	FString SkyboxPath;
	if (!ResolveSkyboxPath(SkyboxPath, OutError))
	{
		return false;
	}

	FString SkyboxBakingPath;
	if (!ResolveSkyboxBakingDirectory(SkyboxBakingPath, OutError))
	{
		return false;
	}

	OutError = FText::GetEmpty();
	return true;
}
