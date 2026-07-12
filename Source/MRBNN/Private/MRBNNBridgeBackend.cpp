#include "MRBNNBackend.h"

#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Math/Float16Color.h"
#include "Misc/Paths.h"

namespace
{
using FMRBNNCreateFn = int32 (*)(const char*, const char*, void**, char*, int32);
using FMRBNNDestroyFn = void (*)(void*);
using FMRBNNSetSkyboxFn = int32 (*)(void*, const char*, float, char*, int32);
using FMRBNNSetSkyboxBakingFn = int32 (*)(void*, const char*, char*, int32);
using FMRBNNRenderRGBA32FFn = int32 (*)(
	void*,
	int32,
	int32,
	int32,
	float,
	float,
	float,
	float,
	float,
	float,
	float,
	float,
	float,
	float,
	float,
	float,
	float,
	int32,
	int32,
	int32,
	int32,
	int32,
	int32,
	int32,
	float*,
	int32,
	char*,
	int32);

FString GetBridgeError(const ANSICHAR* ErrorBuffer)
{
	return UTF8_TO_TCHAR(ErrorBuffer ? ErrorBuffer : "");
}

FString FindBridgeLibraryPath()
{
	TArray<FString> Candidates;

	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MRBNN")))
	{
		const FString BaseDir = Plugin->GetBaseDir();
		Candidates.Add(FPaths::Combine(BaseDir, TEXT("Binaries/ThirdParty/MRBNNBridge/Win64/MRBNNBridge.dll")));
		Candidates.Add(FPaths::Combine(BaseDir, TEXT("Binaries/Win64/MRBNNBridge.dll")));
	}

	Candidates.Add(FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/Win64/MRBNNBridge.dll")));

	for (const FString& Candidate : Candidates)
	{
		if (FPaths::FileExists(Candidate))
		{
			return Candidate;
		}
	}

	return Candidates.Num() > 0 ? Candidates[0] : FString();
}

int32 ToneMappingToBridge(EMRBNNToneMapping Value)
{
	return static_cast<int32>(Value);
}

int32 DenoiseToBridge(EMRBNNDenoiseMode Value)
{
	return static_cast<int32>(Value);
}

int32 CompatibilityToBridge(EMRBNNCompatibilityMode Value)
{
	return static_cast<int32>(Value);
}

class FMRBNNBridgeBackend final : public IMRBNNBackend
{
public:
	~FMRBNNBridgeBackend() override
	{
		Shutdown();
		FreeLibrary();
	}

	bool Initialize(const FString& WorkingDirectory, const FString& RepositoryRoot, const FString& SkyboxPath, float SkyboxExposure, const FString& SkyboxBakingDirectory, FText& OutError) override
	{
		if (!LoadLibrary(OutError))
		{
			return false;
		}
		if (!CreateFn || !DestroyFn || !SetSkyboxFn || !SetSkyboxBakingFn || !RenderFn)
		{
			OutError = NSLOCTEXT("MRBNN", "BridgeSymbolsUnavailable", "MRBNN bridge exports are unavailable.");
			return false;
		}

		Shutdown();

		ANSICHAR ErrorBuffer[2048] = {};
		FTCHARToUTF8 WorkDirUtf8(*WorkingDirectory);
		FTCHARToUTF8 RepositoryRootUtf8(*RepositoryRoot);
		if (!CreateFn(WorkDirUtf8.Get(), RepositoryRootUtf8.Get(), &Handle, ErrorBuffer, UE_ARRAY_COUNT(ErrorBuffer)))
		{
			OutError = FText::Format(NSLOCTEXT("MRBNN", "BridgeCreateFailed", "MRBNN bridge failed to create renderer: {0}"), FText::FromString(GetBridgeError(ErrorBuffer)));
			Handle = nullptr;
			return false;
		}

		if (!SkyboxPath.IsEmpty())
		{
			FTCHARToUTF8 SkyboxPathUtf8(*SkyboxPath);
			if (!SetSkyboxFn(Handle, SkyboxPathUtf8.Get(), SkyboxExposure, ErrorBuffer, UE_ARRAY_COUNT(ErrorBuffer)))
			{
				OutError = FText::Format(NSLOCTEXT("MRBNN", "BridgeSetSkyboxFailed", "MRBNN bridge failed to load skybox HDRI: {0}"), FText::FromString(GetBridgeError(ErrorBuffer)));
				Shutdown();
				return false;
			}
		}

		if (!SkyboxBakingDirectory.IsEmpty())
		{
			FTCHARToUTF8 SkyboxBakingDirectoryUtf8(*SkyboxBakingDirectory);
			if (!SetSkyboxBakingFn(Handle, SkyboxBakingDirectoryUtf8.Get(), ErrorBuffer, UE_ARRAY_COUNT(ErrorBuffer)))
			{
				OutError = FText::Format(NSLOCTEXT("MRBNN", "BridgeSetSkyboxBakingFailed", "MRBNN bridge failed to load skybox baking directory: {0}"), FText::FromString(GetBridgeError(ErrorBuffer)));
				Shutdown();
				return false;
			}
		}

		return true;
	}

	void Shutdown() override
	{
		if (Handle && DestroyFn)
		{
			DestroyFn(Handle);
			Handle = nullptr;
		}
	}

	bool Render(int32 Width, int32 Height, int32 FrameIndex, const FMRBNNRenderSettings& Settings, TArray<FFloat16Color>& OutPixels, FText& OutError) override
	{
		if (!Handle || !RenderFn)
		{
			OutError = NSLOCTEXT("MRBNN", "BridgeNotInitialized", "MRBNN bridge is not initialized.");
			return false;
		}

		const int32 PixelCount = Width * Height;
		TArray<float> FloatPixels;
		FloatPixels.SetNumUninitialized(PixelCount * 4);

		const FVector LightDirection = Settings.LightDirection.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.34281, 0.70711, 0.61845));
		ANSICHAR ErrorBuffer[2048] = {};
		const bool bOk = RenderFn(
			Handle,
			Width,
			Height,
			FrameIndex,
			static_cast<float>(Settings.CameraPosition.X),
			static_cast<float>(Settings.CameraPosition.Y),
			static_cast<float>(Settings.CameraPosition.Z),
			static_cast<float>(LightDirection.X),
			static_cast<float>(LightDirection.Y),
			static_cast<float>(LightDirection.Z),
			Settings.LightColor.R,
			Settings.LightColor.G,
			Settings.LightColor.B,
			Settings.Albedo.R,
			Settings.Albedo.G,
			Settings.Albedo.B,
			Settings.PhaseG,
			ToneMappingToBridge(Settings.ToneMapping),
			DenoiseToBridge(Settings.Denoise),
			CompatibilityToBridge(Settings.Compatibility),
			Settings.bExcludeLightEncoding ? 1 : 0,
			Settings.bFastDirectIllumination ? 1 : 0,
			Settings.bEnableSkybox ? 1 : 0,
			Settings.bEnableSkyboxBaking ? 1 : 0,
			FloatPixels.GetData(),
			FloatPixels.Num(),
			ErrorBuffer,
			UE_ARRAY_COUNT(ErrorBuffer)) != 0;

		if (!bOk)
		{
			OutError = FText::Format(NSLOCTEXT("MRBNN", "BridgeRenderFailed", "MRBNN bridge render failed: {0}"), FText::FromString(GetBridgeError(ErrorBuffer)));
			return false;
		}

		OutPixels.SetNumUninitialized(PixelCount);
		for (int32 Index = 0; Index < PixelCount; ++Index)
		{
			const float* Src = FloatPixels.GetData() + Index * 4;
			OutPixels[Index] = FFloat16Color(FLinearColor(Src[0], Src[1], Src[2], Src[3]));
		}

		return true;
	}

private:
	bool LoadLibrary(FText& OutError)
	{
		if (LibraryHandle)
		{
			if (CreateFn && DestroyFn && SetSkyboxFn && SetSkyboxBakingFn && RenderFn)
			{
				return true;
			}

			FreeLibrary();
		}

		LibraryPath = FindBridgeLibraryPath();
		if (LibraryPath.IsEmpty() || !FPaths::FileExists(LibraryPath))
		{
			OutError = FText::Format(NSLOCTEXT("MRBNN", "BridgeMissing", "MRBNNBridge.dll was not found. Expected path: {0}"), FText::FromString(LibraryPath));
			return false;
		}

		const FString LibraryDirectory = FPaths::GetPath(LibraryPath);
		const FString ExternalTCNNPath = FPaths::Combine(LibraryDirectory, TEXT("ExternalTCNN.dll"));
		if (!FPaths::FileExists(ExternalTCNNPath))
		{
			OutError = FText::Format(NSLOCTEXT("MRBNN", "ExternalTCNNMissingForLoad", "ExternalTCNN.dll is missing next to MRBNNBridge.dll: {0}"), FText::FromString(ExternalTCNNPath));
			return false;
		}

		FPlatformProcess::PushDllDirectory(*LibraryDirectory);
		LibraryHandle = FPlatformProcess::GetDllHandle(*LibraryPath);
		FPlatformProcess::PopDllDirectory(*LibraryDirectory);
		if (!LibraryHandle)
		{
			OutError = FText::Format(NSLOCTEXT("MRBNN", "BridgeLoadFailed", "Failed to load MRBNNBridge.dll: {0}"), FText::FromString(LibraryPath));
			return false;
		}

		CreateFn = reinterpret_cast<FMRBNNCreateFn>(FPlatformProcess::GetDllExport(LibraryHandle, TEXT("MRBNN_Create")));
		DestroyFn = reinterpret_cast<FMRBNNDestroyFn>(FPlatformProcess::GetDllExport(LibraryHandle, TEXT("MRBNN_Destroy")));
		SetSkyboxFn = reinterpret_cast<FMRBNNSetSkyboxFn>(FPlatformProcess::GetDllExport(LibraryHandle, TEXT("MRBNN_SetSkybox")));
		SetSkyboxBakingFn = reinterpret_cast<FMRBNNSetSkyboxBakingFn>(FPlatformProcess::GetDllExport(LibraryHandle, TEXT("MRBNN_SetSkyboxBaking")));
		RenderFn = reinterpret_cast<FMRBNNRenderRGBA32FFn>(FPlatformProcess::GetDllExport(LibraryHandle, TEXT("MRBNN_RenderRGBA32F")));

		if (!CreateFn || !DestroyFn || !SetSkyboxFn || !SetSkyboxBakingFn || !RenderFn)
		{
			OutError = FText::Format(NSLOCTEXT("MRBNN", "BridgeSymbolsMissing", "MRBNNBridge.dll is missing one or more required exports: {0}"), FText::FromString(LibraryPath));
			FreeLibrary();
			return false;
		}

		return true;
	}

	void FreeLibrary()
	{
		if (LibraryHandle)
		{
			FPlatformProcess::FreeDllHandle(LibraryHandle);
			LibraryHandle = nullptr;
		}
		CreateFn = nullptr;
		DestroyFn = nullptr;
		SetSkyboxFn = nullptr;
		SetSkyboxBakingFn = nullptr;
		RenderFn = nullptr;
	}

	void* LibraryHandle = nullptr;
	void* Handle = nullptr;
	FString LibraryPath;
	FMRBNNCreateFn CreateFn = nullptr;
	FMRBNNDestroyFn DestroyFn = nullptr;
	FMRBNNSetSkyboxFn SetSkyboxFn = nullptr;
	FMRBNNSetSkyboxBakingFn SetSkyboxBakingFn = nullptr;
	FMRBNNRenderRGBA32FFn RenderFn = nullptr;
};
}

TUniquePtr<IMRBNNBackend> CreateMRBNNBackend()
{
	return MakeUnique<FMRBNNBridgeBackend>();
}
