#include "MRBNNProjectSettings.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "MRBNNBakedVolumeData.h"
#include "MRBNNBlueprintLibrary.h"
#include "MRBNNVolumeActor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FMRBNNEditorModule"

namespace
{
const TCHAR* LogPrefix = TEXT("MRBNN Bake Console");
const TCHAR* BakeSyncManifestFileName = TEXT("mrbnn_bake_sync_manifest.json");
const TCHAR* BakeConsoleFileName = TEXT("MRBNNBakeConsole.exe");

FString GetMRBNNPluginBaseDir()
{
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MRBNN")))
	{
		return FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir());
	}

	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineDir(), TEXT("Plugins/Experimental/MRBNN")));
}

FString NormalizeLaunchPath(FString Path)
{
	FPaths::NormalizeFilename(Path);
	if (FPaths::IsRelative(Path))
	{
		Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
	}
	FPaths::CollapseRelativeDirectories(Path);
	return Path;
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

bool IsSceneDataRelativePath(const FString& PluginRelativePath, const FString& SafeSceneName)
{
	if (!IsSafeRelativePath(PluginRelativePath))
	{
		return false;
	}

	FString NormalizedPath = PluginRelativePath;
	FPaths::NormalizeFilename(NormalizedPath);
	FString SceneDataRoot = FPaths::Combine(TEXT("Data"), SafeSceneName);
	FPaths::NormalizeFilename(SceneDataRoot);

	if (NormalizedPath.Equals(SceneDataRoot, ESearchCase::IgnoreCase))
	{
		return true;
	}

	if (!SceneDataRoot.EndsWith(TEXT("/")))
	{
		SceneDataRoot += TEXT("/");
	}

	return NormalizedPath.StartsWith(SceneDataRoot, ESearchCase::IgnoreCase);
}

bool ValidateNoSymlinkInExistingPath(const FString& Path, FText& OutError)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformPhysical();
	FString CurrentPath = NormalizeLaunchPath(Path);
	for (;;)
	{
		if (PlatformFile.FileExists(*CurrentPath) || PlatformFile.DirectoryExists(*CurrentPath))
		{
			const ESymlinkResult SymlinkResult = PlatformFile.IsSymlink(*CurrentPath);
			if (SymlinkResult == ESymlinkResult::Symlink)
			{
				OutError = FText::Format(
					LOCTEXT("BakeSyncSymlinkPathRejected", "MRBNN bake sync refuses to copy through a symlink or junction: {0}."),
					FText::FromString(CurrentPath));
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

FString ResolveConfiguredPath(const FString& Path)
{
	return UMRBNNProjectSettings::ResolveMRBNNPath(Path);
}

bool CopyFileEnsuringDirectory(const FString& SourcePath, const FString& DestinationPath, FText& OutError)
{
	if (!FPaths::FileExists(SourcePath))
	{
		OutError = FText::Format(
			LOCTEXT("BakeSyncSourceMissing", "MRBNN bake sync source file is missing: {0}."),
			FText::FromString(SourcePath));
		return false;
	}

	const FString NormalizedSourcePath = NormalizeLaunchPath(SourcePath);
	const FString NormalizedDestinationPath = NormalizeLaunchPath(DestinationPath);
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
		OutError = FText::Format(
			LOCTEXT("BakeSyncCopyFailed", "Failed to copy MRBNN bake sync file from {0} to {1}."),
			FText::FromString(NormalizedSourcePath),
			FText::FromString(NormalizedDestinationPath));
		return false;
	}

	return true;
}

FString MakeSafeSceneDirectoryName(const FString& SceneName, const FString& FallbackPath)
{
	FString Result = SceneName.TrimStartAndEnd();
	Result.ReplaceInline(TEXT("\\"), TEXT("/"));
	Result = FPaths::GetCleanFilename(Result);
	Result.ReplaceInline(TEXT(".."), TEXT(""));
	return Result.IsEmpty() ? FPaths::GetCleanFilename(FallbackPath) : Result;
}

bool TryGetStringField(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName, FString& OutValue)
{
	return JsonObject.IsValid() && JsonObject->TryGetStringField(FieldName, OutValue) && !OutValue.IsEmpty();
}

bool TryGetNestedStringField(const TSharedPtr<FJsonObject>& JsonObject, const FString& ObjectFieldName, const FString& FieldName, FString& OutValue)
{
	const TSharedPtr<FJsonObject>* ChildObject = nullptr;
	if (!JsonObject.IsValid() || !JsonObject->TryGetObjectField(ObjectFieldName, ChildObject) || !ChildObject || !ChildObject->IsValid())
	{
		return false;
	}

	return TryGetStringField(*ChildObject, FieldName, OutValue);
}

FString ResolveManifestPathValue(const FString& Value, const FString& BaseDirectory)
{
	if (Value.IsEmpty())
	{
		return FString();
	}

	FString ResolvedPath = ResolveConfiguredPath(Value);
	if (FPaths::IsRelative(ResolvedPath))
	{
		ResolvedPath = FPaths::Combine(BaseDirectory, ResolvedPath);
	}

	return NormalizeLaunchPath(ResolvedPath);
}

bool LoadBakeSyncManifest(const FString& ManifestPath, TSharedPtr<FJsonObject>& OutManifest, FText& OutError)
{
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *ManifestPath))
	{
		OutError = FText::Format(
			LOCTEXT("BakeSyncManifestReadFailed", "Failed to read MRBNN bake sync manifest: {0}."),
			FText::FromString(ManifestPath));
		return false;
	}

	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, OutManifest) || !OutManifest.IsValid())
	{
		OutError = FText::Format(
			LOCTEXT("BakeSyncManifestParseFailed", "Failed to parse MRBNN bake sync manifest: {0}."),
			FText::FromString(ManifestPath));
		return false;
	}

	return true;
}

FString ResolveConfiguredBakeSyncManifestPath(const UMRBNNProjectSettings* Settings)
{
	const FString ConfiguredPath = Settings->BakeSyncManifestPath.FilePath;
	if (!ConfiguredPath.IsEmpty())
	{
		const FString ManifestPath = NormalizeLaunchPath(ResolveConfiguredPath(ConfiguredPath));
		if (FPaths::FileExists(ManifestPath))
		{
			return ManifestPath;
		}
	}

	const FString DataRoot = ResolveConfiguredPath(Settings->DefaultDataRoot.Path.IsEmpty() ? TEXT("$(PluginDir)/Data") : Settings->DefaultDataRoot.Path);
	const FString SceneName = Settings->DefaultSceneName.IsEmpty() ? TEXT("cloud-03") : Settings->DefaultSceneName;
	return NormalizeLaunchPath(FPaths::Combine(DataRoot, SceneName, BakeSyncManifestFileName));
}

FString ResolveDestinationPluginRoot(const TSharedPtr<FJsonObject>& Manifest, const UMRBNNProjectSettings* Settings, const FString& ManifestDirectory)
{
	(void)Manifest;
	(void)Settings;
	(void)ManifestDirectory;
	return NormalizeLaunchPath(GetMRBNNPluginBaseDir());
}

bool IsPathInsideRoot(const FString& CandidatePath, const FString& RootPath)
{
	FString Candidate = NormalizeLaunchPath(CandidatePath);
	FString Root = NormalizeLaunchPath(RootPath);
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

bool IsPathInsideAnyRoot(const FString& CandidatePath, const TArray<FString>& RootPaths)
{
	for (const FString& RootPath : RootPaths)
	{
		if (!RootPath.IsEmpty() && IsPathInsideRoot(CandidatePath, RootPath))
		{
			return true;
		}
	}

	return false;
}

bool CopyManifestFileToPluginData(
	const FString& SourcePath,
	const FString& DestinationPluginRoot,
	const FString& SafeSceneName,
	const FString& RelativeDestination,
	FText& OutError)
{
	if (SourcePath.IsEmpty() || !FPaths::FileExists(SourcePath))
	{
		return true;
	}

	if (!RelativeDestination.IsEmpty() && !IsSafeRelativePath(RelativeDestination))
	{
		OutError = FText::Format(
			LOCTEXT("BakeSyncUnsafeRelativeTarget", "MRBNN bake sync copy target is not a safe plugin-relative path: {0}."),
			FText::FromString(RelativeDestination));
		return false;
	}

	const FString DestinationPath = NormalizeLaunchPath(FPaths::Combine(
		DestinationPluginRoot,
		TEXT("Data"),
		SafeSceneName,
		RelativeDestination.IsEmpty() ? FPaths::GetCleanFilename(SourcePath) : RelativeDestination));
	if (!IsPathInsideRoot(DestinationPath, DestinationPluginRoot))
	{
		OutError = FText::Format(
			LOCTEXT("BakeSyncCopyTargetOutsidePlugin", "MRBNN bake sync copy target is outside the plugin root: {0}."),
			FText::FromString(DestinationPath));
		return false;
	}

	return CopyFileEnsuringDirectory(SourcePath, DestinationPath, OutError);
}

bool CopyBakeSyncSidecars(
	const TSharedPtr<FJsonObject>& Manifest,
	const FString& ManifestPath,
	const FString& DestinationPluginRoot,
	const FString& SafeSceneName,
	const TArray<FString>& ApprovedSourceRoots,
	FText& OutError)
{
	const FString ManifestDirectory = FPaths::GetPath(ManifestPath);
	FString OutputDirectory;
	if (!TryGetStringField(Manifest, TEXT("output_directory"), OutputDirectory))
	{
		OutputDirectory = ManifestDirectory;
	}
	OutputDirectory = ResolveManifestPathValue(OutputDirectory, ManifestDirectory);

	if (!CopyManifestFileToPluginData(ManifestPath, DestinationPluginRoot, SafeSceneName, BakeSyncManifestFileName, OutError))
	{
		return false;
	}

	FString CloudInfoRGBAPath;
	if (TryGetNestedStringField(Manifest, TEXT("cloudInfo"), TEXT("rgba32f_path"), CloudInfoRGBAPath) ||
		TryGetNestedStringField(Manifest, TEXT("cloud_info"), TEXT("rgba32f_path"), CloudInfoRGBAPath))
	{
		CloudInfoRGBAPath = ResolveManifestPathValue(CloudInfoRGBAPath, OutputDirectory);
		if (!IsPathInsideAnyRoot(CloudInfoRGBAPath, ApprovedSourceRoots))
		{
			OutError = FText::Format(
				LOCTEXT("BakeSyncCloudInfoOutsideSourceRoots", "MRBNN Cloud Info source is outside the approved bake roots: {0}."),
				FText::FromString(CloudInfoRGBAPath));
			return false;
		}
		if (!CopyManifestFileToPluginData(CloudInfoRGBAPath, DestinationPluginRoot, SafeSceneName, FPaths::Combine(TEXT("CloudInfoBakes"), FPaths::GetCleanFilename(CloudInfoRGBAPath)), OutError))
		{
			return false;
		}
	}

	FString CloudInfoManifestPath;
	if (TryGetNestedStringField(Manifest, TEXT("cloudInfo"), TEXT("manifest_path"), CloudInfoManifestPath) ||
		TryGetNestedStringField(Manifest, TEXT("cloud_info"), TEXT("manifest_path"), CloudInfoManifestPath))
	{
		CloudInfoManifestPath = ResolveManifestPathValue(CloudInfoManifestPath, OutputDirectory);
		if (!IsPathInsideAnyRoot(CloudInfoManifestPath, ApprovedSourceRoots))
		{
			OutError = FText::Format(
				LOCTEXT("BakeSyncCloudManifestOutsideSourceRoots", "MRBNN Cloud Info manifest source is outside the approved bake roots: {0}."),
				FText::FromString(CloudInfoManifestPath));
			return false;
		}
		if (!CopyManifestFileToPluginData(CloudInfoManifestPath, DestinationPluginRoot, SafeSceneName, FPaths::Combine(TEXT("CloudInfoBakes"), FPaths::GetCleanFilename(CloudInfoManifestPath)), OutError))
		{
			return false;
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* CopyTargets = nullptr;
	if (!Manifest->TryGetArrayField(TEXT("ueCopyTargets"), CopyTargets) || !CopyTargets)
	{
		return true;
	}

	for (const TSharedPtr<FJsonValue>& CopyTargetValue : *CopyTargets)
	{
		if (!CopyTargetValue.IsValid() || CopyTargetValue->Type == EJson::None)
		{
			continue;
		}

		FString SourcePath;
		FString PluginRelativePath;
		if (CopyTargetValue->Type == EJson::String)
		{
			SourcePath = CopyTargetValue->AsString();
		}
		else if (CopyTargetValue->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> CopyTargetObject = CopyTargetValue->AsObject();
			TryGetStringField(CopyTargetObject, TEXT("source_path"), SourcePath) ||
				TryGetStringField(CopyTargetObject, TEXT("source"), SourcePath) ||
				TryGetStringField(CopyTargetObject, TEXT("path"), SourcePath);
			TryGetStringField(CopyTargetObject, TEXT("plugin_relative_path"), PluginRelativePath) ||
				TryGetStringField(CopyTargetObject, TEXT("destination_plugin_relative_path"), PluginRelativePath) ||
				TryGetStringField(CopyTargetObject, TEXT("destination"), PluginRelativePath);
		}

		if (SourcePath.IsEmpty() && !PluginRelativePath.IsEmpty())
		{
			SourcePath = FPaths::Combine(DestinationPluginRoot, PluginRelativePath);
		}
		if (SourcePath.IsEmpty())
		{
			continue;
		}

		SourcePath = ResolveManifestPathValue(SourcePath, OutputDirectory);
		if (PluginRelativePath.IsEmpty())
		{
			PluginRelativePath = FPaths::Combine(TEXT("Data"), SafeSceneName, TEXT("ExternalBakes"), FPaths::GetCleanFilename(SourcePath));
		}
		if (!IsSafeRelativePath(PluginRelativePath))
		{
			OutError = FText::Format(
				LOCTEXT("BakeSyncUnsafeManifestTarget", "MRBNN bake sync manifest target is not a safe plugin-relative path: {0}."),
				FText::FromString(PluginRelativePath));
			return false;
		}
		if (!IsSceneDataRelativePath(PluginRelativePath, SafeSceneName))
		{
			OutError = FText::Format(
				LOCTEXT("BakeSyncManifestTargetOutsideSceneData", "MRBNN bake sync manifest target must stay under Data/{0}: {1}."),
				FText::FromString(SafeSceneName),
				FText::FromString(PluginRelativePath));
			return false;
		}
		if (!IsPathInsideAnyRoot(SourcePath, ApprovedSourceRoots))
		{
			OutError = FText::Format(
				LOCTEXT("BakeSyncSourceOutsideApprovedRoots", "MRBNN bake sync source is outside the approved bake roots: {0}."),
				FText::FromString(SourcePath));
			return false;
		}

		const FString DestinationPath = NormalizeLaunchPath(FPaths::Combine(DestinationPluginRoot, PluginRelativePath));
		if (!IsPathInsideRoot(DestinationPath, DestinationPluginRoot))
		{
			OutError = FText::Format(
				LOCTEXT("BakeSyncManifestTargetOutsidePlugin", "MRBNN bake sync manifest target is outside the plugin root: {0}."),
				FText::FromString(DestinationPath));
			return false;
		}

		if (!CopyFileEnsuringDirectory(SourcePath, DestinationPath, OutError))
		{
			return false;
		}
	}

	return true;
}

FString QuoteLaunchArg(const FString& Value)
{
	return FString::Printf(TEXT("\"%s\""), *Value.Replace(TEXT("\""), TEXT("\\\"")));
}

void ShowLaunchNotification(const FText& Message, SNotificationItem::ECompletionState CompletionState)
{
	FNotificationInfo Info(Message);
	Info.bFireAndForget = true;
	Info.ExpireDuration = 5.0f;

	if (TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Notification->SetCompletionState(CompletionState);
	}
}

bool ResolveBakeConsoleLaunch(FString& OutExecutablePath, FString& OutArguments, FString& OutWorkingDirectory, FText& OutError)
{
	const UMRBNNProjectSettings* Settings = UMRBNNProjectSettings::Get();
	const FString ConfiguredExecutablePath = Settings->BakeConsoleExecutableOverride.FilePath;
	const FString DefaultExecutablePath = FPaths::Combine(GetMRBNNPluginBaseDir(), TEXT("Binaries/ThirdParty/MRBNNBridge/Win64/MRBNNBakeConsole.exe"));
	OutExecutablePath = ConfiguredExecutablePath.IsEmpty()
		? DefaultExecutablePath
		: ResolveConfiguredPath(ConfiguredExecutablePath);
	OutExecutablePath = NormalizeLaunchPath(OutExecutablePath);

	const FString TrustedBinaryDirectory = NormalizeLaunchPath(FPaths::GetPath(DefaultExecutablePath));
	if (!FPaths::GetCleanFilename(OutExecutablePath).Equals(BakeConsoleFileName, ESearchCase::IgnoreCase) ||
		!IsPathInsideRoot(OutExecutablePath, TrustedBinaryDirectory))
	{
		OutError = FText::Format(
			LOCTEXT("BakeConsoleExecutableUntrusted", "MRBNN Bake Console executable must be named MRBNNBakeConsole.exe and live under the plugin bridge binary directory: {0}."),
			FText::FromString(TrustedBinaryDirectory));
		return false;
	}

	if (!FPaths::FileExists(OutExecutablePath))
	{
		OutError = FText::Format(
			LOCTEXT("BakeConsoleExecutableMissing", "MRBNNBakeConsole.exe was not found at {0}. Build it with Scripts/Build-MRBNNBridge.ps1 -BuildBakeConsole, or set an override in Project Settings > Plugins > MRBNN."),
			FText::FromString(OutExecutablePath));
		return false;
	}

	const FString RepositoryRoot = ResolveConfiguredPath(Settings->DefaultRepositoryRoot.Path.IsEmpty() ? TEXT("$(PluginDir)") : Settings->DefaultRepositoryRoot.Path);
	const FString DataRoot = ResolveConfiguredPath(Settings->DefaultDataRoot.Path.IsEmpty() ? TEXT("$(PluginDir)/Data") : Settings->DefaultDataRoot.Path);
	const FString SceneName = Settings->DefaultSceneName.IsEmpty() ? TEXT("cloud-03") : Settings->DefaultSceneName;
	const FString WorkingDirectory = NormalizeLaunchPath(FPaths::Combine(DataRoot, SceneName));

	OutArguments = FString::Printf(
		TEXT("%s %s %s"),
		*QuoteLaunchArg(WorkingDirectory),
		*QuoteLaunchArg(RepositoryRoot),
		*QuoteLaunchArg(GetMRBNNPluginBaseDir()));
	OutWorkingDirectory = FPaths::GetPath(OutExecutablePath);
	return true;
}
}

class FMRBNNEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		if (IsRunningCommandlet())
		{
			return;
		}

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMRBNNEditorModule::RegisterMenus));

		if (UMRBNNProjectSettings::Get()->bAutoOpenBakeConsoleOnEditorStartup)
		{
			if (!UMRBNNProjectSettings::Get()->BakeConsoleExecutableOverride.FilePath.IsEmpty())
			{
				UE_LOG(LogTemp, Warning, TEXT("%s: auto-open skipped because BakeConsoleExecutableOverride is set. Launch it manually from the MRBNN toolbar after verifying the path."), LogPrefix);
				return;
			}

			LaunchBakeConsole(false);
		}
	}

	virtual void ShutdownModule() override
	{
		if (!IsEngineExitRequested() && UObjectInitialized())
		{
			UToolMenus::UnRegisterStartupCallback(this);
			UToolMenus::UnregisterOwner(this);
		}
	}

private:
	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);

		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar.User"));
		FToolMenuSection& Section = ToolbarMenu->FindOrAddSection(TEXT("MRBNN"));

		FToolMenuEntry BakeConsoleEntry = FToolMenuEntry::InitToolBarButton(
			TEXT("MRBNNBakeConsole"),
			FUIAction(FExecuteAction::CreateRaw(this, &FMRBNNEditorModule::OpenBakeConsoleFromToolbar)),
			LOCTEXT("BakeConsoleButtonLabel", "MRBNN"),
			LOCTEXT("BakeConsoleButtonTooltip", "Open the MRBNN ImGui bake/debug console using the configured Project Settings data paths."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Play")));
		BakeConsoleEntry.StyleNameOverride = TEXT("CalloutToolbar");
		Section.AddEntry(BakeConsoleEntry);

		FToolMenuEntry SyncBakeEntry = FToolMenuEntry::InitToolBarButton(
			TEXT("MRBNNSyncBakeOutput"),
			FUIAction(FExecuteAction::CreateRaw(this, &FMRBNNEditorModule::SyncBakeOutputFromToolbar)),
			LOCTEXT("BakeSyncButtonLabel", "Sync MRBNN"),
			LOCTEXT("BakeSyncButtonTooltip", "Import the latest mrbnn_bake_sync_manifest.json bake output into the MRBNN plugin data and refresh MRBNN actors."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Refresh")));
		Section.AddEntry(SyncBakeEntry);
	}

	void OpenBakeConsoleFromToolbar()
	{
		LaunchBakeConsole(true);
	}

	void LaunchBakeConsole(bool bShowSuccessNotification)
	{
		FString ExecutablePath;
		FString Arguments;
		FString WorkingDirectory;
		FText Error;
		if (!ResolveBakeConsoleLaunch(ExecutablePath, Arguments, WorkingDirectory, Error))
		{
			UE_LOG(LogTemp, Warning, TEXT("%s: %s"), LogPrefix, *Error.ToString());
			ShowLaunchNotification(Error, SNotificationItem::CS_Fail);
			return;
		}

		uint32 ProcessId = 0;
		FProcHandle ProcessHandle = FPlatformProcess::CreateProc(
			*ExecutablePath,
			*Arguments,
			true,
			false,
			false,
			&ProcessId,
			0,
			*WorkingDirectory,
			nullptr);

		if (!ProcessHandle.IsValid())
		{
			const FText ErrorMessage = FText::Format(
				LOCTEXT("BakeConsoleLaunchFailed", "MRBNNBakeConsole.exe could not be launched from {0}."),
				FText::FromString(ExecutablePath));
			UE_LOG(LogTemp, Warning, TEXT("%s: %s Args=%s"), LogPrefix, *ErrorMessage.ToString(), *Arguments);
			ShowLaunchNotification(ErrorMessage, SNotificationItem::CS_Fail);
			return;
		}

		FPlatformProcess::CloseProc(ProcessHandle);
		UE_LOG(LogTemp, Display, TEXT("%s: launched %s %s"), LogPrefix, *ExecutablePath, *Arguments);

		if (bShowSuccessNotification)
		{
			ShowLaunchNotification(LOCTEXT("BakeConsoleLaunched", "MRBNN Bake Console launched."), SNotificationItem::CS_Success);
		}
	}

	void SyncBakeOutputFromToolbar()
	{
		FText Error;
		int32 RefreshedActorCount = 0;
		if (!SyncBakeOutputFromManifest(Error, RefreshedActorCount))
		{
			UE_LOG(LogTemp, Warning, TEXT("%s: sync failed: %s"), LogPrefix, *Error.ToString());
			ShowLaunchNotification(Error, SNotificationItem::CS_Fail);
			return;
		}

		const FText Message = FText::Format(
			LOCTEXT("BakeSyncComplete", "MRBNN bake output synced. Refreshed {0} actor(s)."),
			FText::AsNumber(RefreshedActorCount));
		UE_LOG(LogTemp, Display, TEXT("%s: %s"), LogPrefix, *Message.ToString());
		ShowLaunchNotification(Message, SNotificationItem::CS_Success);
	}

	bool SyncBakeOutputFromManifest(FText& OutError, int32& OutRefreshedActorCount)
	{
		OutRefreshedActorCount = 0;

		UMRBNNProjectSettings* Settings = UMRBNNProjectSettings::GetMutable();
		const FString ManifestPath = ResolveConfiguredBakeSyncManifestPath(Settings);
		if (!FPaths::FileExists(ManifestPath))
		{
			OutError = FText::Format(
				LOCTEXT("BakeSyncManifestMissing", "MRBNN bake sync manifest was not found: {0}."),
				FText::FromString(ManifestPath));
			return false;
		}

		TSharedPtr<FJsonObject> Manifest;
		if (!LoadBakeSyncManifest(ManifestPath, Manifest, OutError))
		{
			return false;
		}

		const FString ManifestDirectory = FPaths::GetPath(ManifestPath);
		FString SourceWorkingDirectory;
		if (!TryGetStringField(Manifest, TEXT("source_working_directory"), SourceWorkingDirectory) &&
			!TryGetNestedStringField(Manifest, TEXT("source"), TEXT("working_directory"), SourceWorkingDirectory))
		{
			OutError = LOCTEXT("BakeSyncMissingWorkingDirectory", "MRBNN bake sync manifest is missing source_working_directory.");
			return false;
		}
		SourceWorkingDirectory = ResolveManifestPathValue(SourceWorkingDirectory, ManifestDirectory);

		const FString TrustedRepositoryRoot = NormalizeLaunchPath(ResolveConfiguredPath(Settings->DefaultRepositoryRoot.Path.IsEmpty() ? TEXT("$(PluginDir)") : Settings->DefaultRepositoryRoot.Path));
		const FString SourceRepositoryRoot = TrustedRepositoryRoot;

		FString SceneName;
		TryGetStringField(Manifest, TEXT("scene_name"), SceneName) ||
			TryGetStringField(Manifest, TEXT("scene"), SceneName);
		const FString SafeSceneName = MakeSafeSceneDirectoryName(SceneName, SourceWorkingDirectory);

		const FString DestinationPluginRoot = ResolveDestinationPluginRoot(Manifest, Settings, ManifestDirectory);
		if (!FPaths::DirectoryExists(DestinationPluginRoot))
		{
			OutError = FText::Format(
				LOCTEXT("BakeSyncPluginRootMissing", "MRBNN bake sync plugin root does not exist: {0}."),
				FText::FromString(DestinationPluginRoot));
			return false;
		}

		TArray<FString> ApprovedSourceRoots;
		ApprovedSourceRoots.AddUnique(NormalizeLaunchPath(ManifestDirectory));
		ApprovedSourceRoots.AddUnique(DestinationPluginRoot);
		ApprovedSourceRoots.AddUnique(TrustedRepositoryRoot);

		if (!IsPathInsideAnyRoot(SourceWorkingDirectory, ApprovedSourceRoots))
		{
			OutError = FText::Format(
				LOCTEXT("BakeSyncWorkingDirectoryOutsideApprovedRoots", "MRBNN bake sync working directory is outside the approved bake roots: {0}."),
				FText::FromString(SourceWorkingDirectory));
			return false;
		}
		ApprovedSourceRoots.AddUnique(SourceWorkingDirectory);

		UMRBNNBakedVolumeData* SourceData = NewObject<UMRBNNBakedVolumeData>(GetTransientPackage(), NAME_None, RF_Transient);
		SourceData->WorkingDirectory.Path = SourceWorkingDirectory;
		SourceData->RepositoryRoot.Path = SourceRepositoryRoot;

		Settings->BakeDestinationRepositoryRoot.Path = DestinationPluginRoot;

		FText BakeError;
		if (!UMRBNNBlueprintLibrary::BakeMRBNNDataToPluginData(
			SourceData,
			SafeSceneName,
			Settings->bEnableDefaultSkybox,
			Settings->bEnableDefaultSkyboxBaking,
			BakeError))
		{
			OutError = BakeError;
			return false;
		}

		if (!CopyBakeSyncSidecars(Manifest, ManifestPath, DestinationPluginRoot, SafeSceneName, ApprovedSourceRoots, OutError))
		{
			return false;
		}

		Settings->DefaultRepositoryRoot.Path = DestinationPluginRoot;
		Settings->DefaultDataRoot.Path = NormalizeLaunchPath(FPaths::Combine(DestinationPluginRoot, TEXT("Data")));
		Settings->DefaultSceneName = SafeSceneName;
		Settings->BakeSyncManifestPath.FilePath = NormalizeLaunchPath(FPaths::Combine(DestinationPluginRoot, TEXT("Data"), SafeSceneName, BakeSyncManifestFileName));
		Settings->SaveConfig();

		if (Settings->bRefreshActorsAfterBakeSync)
		{
			RefreshMRBNNActorsFromProjectSettings(OutRefreshedActorCount);
		}

		return true;
	}

	void RefreshMRBNNActorsFromProjectSettings(int32& OutRefreshedActorCount)
	{
		OutRefreshedActorCount = 0;
		if (!GEditor)
		{
			return;
		}

		UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
		if (!EditorWorld)
		{
			return;
		}

		for (TActorIterator<AMRBNNVolumeActor> ActorIt(EditorWorld); ActorIt; ++ActorIt)
		{
			AMRBNNVolumeActor* Actor = *ActorIt;
			if (!Actor)
			{
				continue;
			}

			Actor->Modify();
			Actor->ConfigureFromProjectSettings();
			Actor->RebuildVolumeShader();
			++OutRefreshedActorCount;
		}
	}
};

IMPLEMENT_MODULE(FMRBNNEditorModule, MRBNNEditor)

#undef LOCTEXT_NAMESPACE
