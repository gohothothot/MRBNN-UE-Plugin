#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "nlohmann/json.hpp"

#include <cuda_runtime_api.h>
#include <nvrtc.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
extern "C"
{
	__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#define MRBNNBRIDGE_IMPORT extern "C" __declspec(dllimport)
#else
#define MRBNNBRIDGE_IMPORT extern "C"
#endif

MRBNNBRIDGE_IMPORT int MRBNN_Create(const char* WorkDirUtf8, const char* RepositoryRootUtf8, void** OutHandle, char* OutError, int ErrorCapacity);
MRBNNBRIDGE_IMPORT void MRBNN_Destroy(void* Handle);
MRBNNBRIDGE_IMPORT int MRBNN_SetSkybox(void* Handle, const char* HdriPathUtf8, float Exposure, char* OutError, int ErrorCapacity);
MRBNNBRIDGE_IMPORT int MRBNN_SetSkyboxBaking(void* Handle, const char* WorkDirUtf8, char* OutError, int ErrorCapacity);
MRBNNBRIDGE_IMPORT int MRBNN_RenderRGBA32F(
	void* Handle,
	int Width,
	int Height,
	int FrameIndex,
	float CameraX,
	float CameraY,
	float CameraZ,
	float LightX,
	float LightY,
	float LightZ,
	float LightR,
	float LightG,
	float LightB,
	float AlbedoR,
	float AlbedoG,
	float AlbedoB,
	float PhaseG,
	int ToneMapping,
	int Denoise,
	int Compatibility,
	int ExcludeLightEncoding,
	int FastDirectIllumination,
	int EnableSkybox,
	int EnableSkyboxBaking,
	float* OutRGBA,
	int OutFloatCount,
	char* OutError,
	int ErrorCapacity);

extern "C" int MRBNN_BakeCloudInfoVolumeRGBA32F(
	const float* HostDensity,
	int SourceX,
	int SourceY,
	int SourceZ,
	int BakeX,
	int BakeY,
	int BakeZ,
	float DensityScale,
	float OccupancyThreshold,
	float* HostRGBA,
	int OutFloatCount,
	char* OutError,
	int ErrorCapacity);

namespace
{
constexpr int PathBufferSize = 1024;
constexpr int NameBufferSize = 128;

enum class ECloudShapeGenerator : int
{
	SourceVolume = 0,
	CumulusCore = 1,
	AnvilTower = 2,
	LayerBank = 3,
	WispyStreaks = 4
};

struct FConsoleState
{
	char WorkDir[PathBufferSize] = {};
	char RepositoryRoot[PathBufferSize] = {};
	char PluginRoot[PathBufferSize] = {};
	char SceneName[NameBufferSize] = "cloud-03";
	char SkyboxHDRI[PathBufferSize] = {};
	char SkyboxBakingDir[PathBufferSize] = {};
	char OutputPath[PathBufferSize] = {};
	char CloudInfoOutputDir[PathBufferSize] = {};
	char LastCloudInfoRawPath[PathBufferSize] = {};
	char LastCloudInfoManifestPath[PathBufferSize] = {};
	char LastBakeSyncManifestPath[PathBufferSize] = {};

	int Width = 768;
	int Height = 768;
	int Samples = 32;
	int FrameIndex = 0;
	int CloudInfoBakeResolution = 96;
	int Language = 0;
	int CloudShapeGenerator = static_cast<int>(ECloudShapeGenerator::SourceVolume);
	int CloudShapeSeed = 1337;
	int ToneMapping = 2;
	int Denoise = 2;
	int Compatibility = 0;
	bool bExcludeLightEncoding = true;
	bool bFastDirectIllumination = false;
	bool bEnableSkybox = false;
	bool bEnableSkyboxBaking = true;
	bool bIncludeSkyboxHDRIWhenPackaging = false;
	bool bIncludeSkyboxBakingWhenPackaging = true;
	bool bCloudInfoAutoDensityScale = true;
	bool bCopySyncManifestToWorkDir = true;

	float Camera[3] = { 0.67085f, -0.03808f, -0.04856f };
	float LightDirection[3] = { 0.34281f, 0.70711f, 0.61845f };
	float LightColor[3] = { 1.0f, 1.0f, 1.0f };
	float Albedo[3] = { 0.999f, 0.999f, 0.999f };
	float PhaseG = 0.857f;
	float SkyboxExposure = 1.0f;
	float CloudInfoDensityScale = 1.0f;
	float CloudInfoOccupancyThreshold = 0.02f;
	float CloudShapeCoverage = 0.72f;
	float CloudShapeBaseHeight = 0.28f;
	float CloudShapeThickness = 0.58f;
	float CloudShapeEdgeSoftness = 0.22f;
	float CloudShapeDetail = 0.45f;
	float CloudShapeWindShear = 0.18f;

	void* Renderer = nullptr;
	std::vector<float> Pixels;
	std::vector<float> FramePixels;
	std::vector<float> CloudInfoRGBA;
	std::vector<unsigned char> PreviewBytes;
	GLuint PreviewTexture = 0;
	int PreviewWidth = 0;
	int PreviewHeight = 0;
	std::string Status = "Idle.";
	std::vector<std::string> Log;
	bool bCudaRuntimeCompatible = true;
	int CudaDriverVersion = 0;
	int CudaRuntimeVersion = 0;
	int NvrtcMajorVersion = 0;
	int NvrtcMinorVersion = 0;
	std::string CudaCompatibilityWarning;
};

struct FCloudInfoVolumeConfig
{
	std::filesystem::path ConfigPath;
	std::filesystem::path WorkDir;
	std::filesystem::path RepositoryRoot;
	std::filesystem::path VolumePath;
	std::string ConfigVolumePath;
	std::array<int, 3> SourceResolution = { 0, 0, 0 };
	std::array<float, 3> BoundMin = { -0.5f, -0.5f, -0.5f };
	std::array<float, 3> BoundMax = { 0.5f, 0.5f, 0.5f };
	std::size_t SkipByteCount = 0;
	int MipmapLevel = 0;
	bool bHasExplicitBounds = false;
};

const char* Tr(const FConsoleState& State, const char* English, const char* Chinese)
{
	return State.Language == 1 ? Chinese : English;
}

void CopyString(char* Destination, int DestinationSize, const std::string& Value)
{
	if (!Destination || DestinationSize <= 0)
	{
		return;
	}

	std::snprintf(Destination, static_cast<size_t>(DestinationSize), "%s", Value.c_str());
}

std::string ToUtf8(const std::filesystem::path& Path)
{
	const auto U8 = Path.u8string();
	return std::string(reinterpret_cast<const char*>(U8.c_str()), U8.size());
}

std::filesystem::path MakePath(const char* Text)
{
	if (!Text || !*Text)
	{
		return std::filesystem::path();
	}

#if defined(_WIN32)
	const char8_t* Begin = reinterpret_cast<const char8_t*>(Text);
	return std::filesystem::path(std::u8string(Begin, Begin + std::strlen(Text)));
#else
	return std::filesystem::path(Text);
#endif
}

#if defined(_WIN32)
std::string WideToUtf8(const std::wstring& Value)
{
	if (Value.empty())
	{
		return {};
	}

	const int RequiredBytes = WideCharToMultiByte(CP_UTF8, 0, Value.data(), static_cast<int>(Value.size()), nullptr, 0, nullptr, nullptr);
	if (RequiredBytes <= 0)
	{
		return {};
	}

	std::string Result(static_cast<size_t>(RequiredBytes), '\0');
	WideCharToMultiByte(CP_UTF8, 0, Value.data(), static_cast<int>(Value.size()), Result.data(), RequiredBytes, nullptr, nullptr);
	return Result;
}
#endif

std::vector<std::string> GetUtf8CommandLineArguments(int Argc, char** Argv)
{
#if defined(_WIN32)
	int WideArgc = 0;
	LPWSTR* WideArgv = CommandLineToArgvW(GetCommandLineW(), &WideArgc);
	if (WideArgv)
	{
		std::vector<std::string> Args;
		Args.reserve(static_cast<size_t>(WideArgc));
		for (int Index = 0; Index < WideArgc; ++Index)
		{
			Args.emplace_back(WideToUtf8(WideArgv[Index]));
		}
		LocalFree(WideArgv);
		return Args;
	}
#endif

	std::vector<std::string> Args;
	Args.reserve(static_cast<size_t>(std::max(Argc, 0)));
	for (int Index = 0; Index < Argc; ++Index)
	{
		Args.emplace_back(Argv[Index] ? Argv[Index] : "");
	}
	return Args;
}

void AddLog(FConsoleState& State, const char* Format, ...)
{
	char Buffer[2048] = {};
	va_list Args;
	va_start(Args, Format);
	std::vsnprintf(Buffer, sizeof(Buffer), Format, Args);
	va_end(Args);
	State.Status = Buffer;
	State.Log.emplace_back(Buffer);
	if (State.Log.size() > 256)
	{
		State.Log.erase(State.Log.begin(), State.Log.begin() + static_cast<std::ptrdiff_t>(State.Log.size() - 256));
	}
}

std::string FormatCudaVersion(int Version)
{
	if (Version <= 0)
	{
		return "unknown";
	}

	const int Major = Version / 1000;
	const int Minor = (Version % 1000) / 10;
	return std::to_string(Major) + "." + std::to_string(Minor);
}

bool IsCudaRuntimeNewerThanDriver(int RuntimeVersion, int DriverVersion)
{
	const int RuntimeMajor = RuntimeVersion / 1000;
	const int RuntimeMinor = (RuntimeVersion % 1000) / 10;
	const int DriverMajor = DriverVersion / 1000;
	const int DriverMinor = (DriverVersion % 1000) / 10;
	return RuntimeMajor > DriverMajor || (RuntimeMajor == DriverMajor && RuntimeMinor > DriverMinor);
}

bool IsCudaVersionPairNewerThanDriver(int Major, int Minor, int DriverVersion)
{
	const int DriverMajor = DriverVersion / 1000;
	const int DriverMinor = (DriverVersion % 1000) / 10;
	return Major > DriverMajor || (Major == DriverMajor && Minor > DriverMinor);
}

std::string FormatVersionPair(int Major, int Minor)
{
	if (Major <= 0)
	{
		return "unknown";
	}

	return std::to_string(Major) + "." + std::to_string(Minor);
}

void CheckCudaRuntimeCompatibility(FConsoleState& State)
{
	const cudaError_t DriverResult = cudaDriverGetVersion(&State.CudaDriverVersion);
	const cudaError_t RuntimeResult = cudaRuntimeGetVersion(&State.CudaRuntimeVersion);
	const nvrtcResult NvrtcResult = nvrtcVersion(&State.NvrtcMajorVersion, &State.NvrtcMinorVersion);
	if (DriverResult != cudaSuccess || RuntimeResult != cudaSuccess || NvrtcResult != NVRTC_SUCCESS)
	{
		State.bCudaRuntimeCompatible = false;
		State.CudaCompatibilityWarning = "CUDA driver/runtime/NVRTC version query failed. CUDA render controls are disabled.";
		AddLog(State, "%s", State.CudaCompatibilityWarning.c_str());
		return;
	}

	if (IsCudaRuntimeNewerThanDriver(State.CudaRuntimeVersion, State.CudaDriverVersion) ||
		IsCudaVersionPairNewerThanDriver(State.NvrtcMajorVersion, State.NvrtcMinorVersion, State.CudaDriverVersion))
	{
		State.bCudaRuntimeCompatible = false;
		State.CudaCompatibilityWarning =
			"CUDA runtime " + FormatCudaVersion(State.CudaRuntimeVersion) +
			" / NVRTC " + FormatVersionPair(State.NvrtcMajorVersion, State.NvrtcMinorVersion) +
			" is newer than driver CUDA " + FormatCudaVersion(State.CudaDriverVersion) +
			". Install a compatible toolkit/runtime or update the NVIDIA driver before rendering.";
		AddLog(State, "%s", State.CudaCompatibilityWarning.c_str());
		return;
	}

	AddLog(State, "CUDA driver/runtime/NVRTC check passed: driver %s, runtime %s, NVRTC %s.",
		FormatCudaVersion(State.CudaDriverVersion).c_str(),
		FormatCudaVersion(State.CudaRuntimeVersion).c_str(),
		FormatVersionPair(State.NvrtcMajorVersion, State.NvrtcMinorVersion).c_str());
}

bool FileExists(const std::filesystem::path& Path)
{
	std::error_code Error;
	return std::filesystem::is_regular_file(Path, Error);
}

bool DirectoryExists(const std::filesystem::path& Path)
{
	std::error_code Error;
	return std::filesystem::is_directory(Path, Error);
}

std::filesystem::path NormalizePath(const std::filesystem::path& Path)
{
	std::error_code Error;
	const std::filesystem::path Absolute = std::filesystem::absolute(Path, Error);
	return Error ? Path.lexically_normal() : Absolute.lexically_normal();
}

std::string SanitizeSceneName(std::string SceneName, const std::filesystem::path& FallbackPath)
{
	std::replace(SceneName.begin(), SceneName.end(), '\\', '/');
	while (SceneName.find("..") != std::string::npos)
	{
		SceneName.erase(SceneName.find(".."), 2);
	}

	const std::filesystem::path ScenePath(SceneName);
	SceneName = ScenePath.filename().string();
	if (SceneName.empty())
	{
		SceneName = FallbackPath.filename().string();
	}
	return SceneName.empty() ? std::string("mrbnn_scene") : SceneName;
}

std::string GeneratorTypeName(int Generator)
{
	switch (static_cast<ECloudShapeGenerator>(Generator))
	{
	case ECloudShapeGenerator::SourceVolume:
		return "source_volume";
	case ECloudShapeGenerator::CumulusCore:
		return "cumulus_core";
	case ECloudShapeGenerator::AnvilTower:
		return "anvil_tower";
	case ECloudShapeGenerator::LayerBank:
		return "layer_bank";
	case ECloudShapeGenerator::WispyStreaks:
		return "wispy_streaks";
	default:
		return "source_volume";
	}
}

int ParseGeneratorType(std::string Text)
{
	std::transform(Text.begin(), Text.end(), Text.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
	if (Text == "cumulus" || Text == "cumulus_core")
	{
		return static_cast<int>(ECloudShapeGenerator::CumulusCore);
	}
	if (Text == "anvil" || Text == "anvil_tower")
	{
		return static_cast<int>(ECloudShapeGenerator::AnvilTower);
	}
	if (Text == "layer" || Text == "layer_bank")
	{
		return static_cast<int>(ECloudShapeGenerator::LayerBank);
	}
	if (Text == "wispy" || Text == "wispy_streaks")
	{
		return static_cast<int>(ECloudShapeGenerator::WispyStreaks);
	}
	return static_cast<int>(ECloudShapeGenerator::SourceVolume);
}

void ApplyCloudShapePreset(FConsoleState& State, ECloudShapeGenerator Generator)
{
	State.CloudShapeGenerator = static_cast<int>(Generator);
	switch (Generator)
	{
	case ECloudShapeGenerator::SourceVolume:
		State.CloudShapeCoverage = 1.0f;
		State.CloudShapeBaseHeight = 0.0f;
		State.CloudShapeThickness = 1.0f;
		State.CloudShapeEdgeSoftness = 0.08f;
		State.CloudShapeDetail = 0.0f;
		State.CloudShapeWindShear = 0.0f;
		break;
	case ECloudShapeGenerator::CumulusCore:
		State.CloudShapeCoverage = 0.72f;
		State.CloudShapeBaseHeight = 0.24f;
		State.CloudShapeThickness = 0.58f;
		State.CloudShapeEdgeSoftness = 0.22f;
		State.CloudShapeDetail = 0.45f;
		State.CloudShapeWindShear = 0.18f;
		break;
	case ECloudShapeGenerator::AnvilTower:
		State.CloudShapeCoverage = 0.68f;
		State.CloudShapeBaseHeight = 0.16f;
		State.CloudShapeThickness = 0.76f;
		State.CloudShapeEdgeSoftness = 0.18f;
		State.CloudShapeDetail = 0.34f;
		State.CloudShapeWindShear = 0.28f;
		break;
	case ECloudShapeGenerator::LayerBank:
		State.CloudShapeCoverage = 0.92f;
		State.CloudShapeBaseHeight = 0.30f;
		State.CloudShapeThickness = 0.34f;
		State.CloudShapeEdgeSoftness = 0.24f;
		State.CloudShapeDetail = 0.28f;
		State.CloudShapeWindShear = 0.08f;
		break;
	case ECloudShapeGenerator::WispyStreaks:
		State.CloudShapeCoverage = 0.58f;
		State.CloudShapeBaseHeight = 0.34f;
		State.CloudShapeThickness = 0.42f;
		State.CloudShapeEdgeSoftness = 0.30f;
		State.CloudShapeDetail = 0.72f;
		State.CloudShapeWindShear = 0.46f;
		break;
	}
}

std::string FormatUtcTimestamp()
{
	const std::time_t Now = std::time(nullptr);
	std::tm UtcTime = {};
#if defined(_WIN32)
	gmtime_s(&UtcTime, &Now);
#else
	gmtime_r(&Now, &UtcTime);
#endif
	char Buffer[32] = {};
	std::strftime(Buffer, sizeof(Buffer), "%Y-%m-%dT%H:%M:%SZ", &UtcTime);
	return Buffer;
}

float SmoothStep(float Edge0, float Edge1, float Value)
{
	if (Edge0 == Edge1)
	{
		return Value < Edge0 ? 0.0f : 1.0f;
	}

	const float T = std::clamp((Value - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
	return T * T * (3.0f - 2.0f * T);
}

float HashNoise01(int X, int Y, int Z, int Seed)
{
	std::uint32_t H = static_cast<std::uint32_t>(Seed);
	H ^= static_cast<std::uint32_t>(X) * 374761393u;
	H ^= static_cast<std::uint32_t>(Y) * 668265263u;
	H ^= static_cast<std::uint32_t>(Z) * 2147483647u;
	H = (H ^ (H >> 13u)) * 1274126177u;
	H ^= H >> 16u;
	return static_cast<float>(H & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

float ComputeCloudShapeMask(const FConsoleState& State, float X, float Y, float Z, int Xi, int Yi, int Zi)
{
	const ECloudShapeGenerator Generator = static_cast<ECloudShapeGenerator>(State.CloudShapeGenerator);
	if (Generator == ECloudShapeGenerator::SourceVolume)
	{
		return 1.0f;
	}

	const float Coverage = std::clamp(State.CloudShapeCoverage, 0.0f, 1.0f);
	const float Thickness = std::clamp(State.CloudShapeThickness, 0.02f, 1.0f);
	const float Edge = std::clamp(State.CloudShapeEdgeSoftness, 0.01f, 0.5f);
	const float Detail = std::clamp(State.CloudShapeDetail, 0.0f, 1.0f);
	const float Shear = State.CloudShapeWindShear;
	const float ShearedX = std::clamp(X + (Z - 0.5f) * Shear, 0.0f, 1.0f);
	const float CenterX = ShearedX - 0.5f;
	const float CenterY = Y - 0.5f;
	const float BaseZ = std::clamp(State.CloudShapeBaseHeight, 0.0f, 1.0f);
	const float HeightZ = Z - BaseZ;
	const float Noise = HashNoise01(Xi / 4, Yi / 4, Zi / 4, State.CloudShapeSeed);
	float Mask = 1.0f;

	switch (Generator)
	{
	case ECloudShapeGenerator::CumulusCore:
	{
		const float RadiusX = 0.16f + Coverage * 0.38f;
		const float RadiusY = 0.12f + Coverage * 0.30f;
		const float RadiusZ = 0.10f + Thickness * 0.46f;
		const float Ellipsoid =
			(CenterX * CenterX) / std::max(RadiusX * RadiusX, 1.0e-4f) +
			(CenterY * CenterY) / std::max(RadiusY * RadiusY, 1.0e-4f) +
			((HeightZ - RadiusZ * 0.45f) * (HeightZ - RadiusZ * 0.45f)) / std::max(RadiusZ * RadiusZ, 1.0e-4f);
		Mask = 1.0f - SmoothStep(1.0f - Edge, 1.0f + Edge, Ellipsoid);
		break;
	}
	case ECloudShapeGenerator::AnvilTower:
	{
		const float Tower = 1.0f - SmoothStep(0.08f + Coverage * 0.16f, 0.18f + Coverage * 0.24f, std::sqrt(CenterX * CenterX + CenterY * CenterY));
		const float Shelf = 1.0f - SmoothStep(0.24f + Coverage * 0.20f, 0.38f + Coverage * 0.28f, std::sqrt(CenterX * CenterX + CenterY * CenterY * 1.8f));
		const float Vertical = SmoothStep(0.0f, Edge + 0.04f, HeightZ) * (1.0f - SmoothStep(Thickness, Thickness + Edge + 0.08f, HeightZ));
		const float Anvil = SmoothStep(Thickness * 0.56f, Thickness * 0.78f, HeightZ) * Shelf;
		Mask = std::max(Tower * Vertical, Anvil * 0.82f);
		break;
	}
	case ECloudShapeGenerator::LayerBank:
	{
		const float DeckCenter = BaseZ + Thickness * 0.35f;
		const float DeckDistance = std::abs(Z - DeckCenter);
		const float Deck = 1.0f - SmoothStep(Thickness * 0.20f, Thickness * 0.52f + Edge, DeckDistance);
		const float Footprint = 1.0f - SmoothStep(0.38f + Coverage * 0.22f, 0.52f + Coverage * 0.28f, std::abs(CenterY) + std::abs(CenterX) * 0.28f);
		Mask = Deck * Footprint;
		break;
	}
	case ECloudShapeGenerator::WispyStreaks:
	{
		const float Wave = std::sin((ShearedX * 8.0f + Y * 3.5f + static_cast<float>(State.CloudShapeSeed % 17)) * 3.14159265f);
		const float Streak = 1.0f - SmoothStep(0.12f + Coverage * 0.08f, 0.34f + Coverage * 0.18f, std::abs(Wave) + std::abs(Y - 0.5f) * 0.55f);
		const float Vertical = 1.0f - SmoothStep(Thickness * 0.22f, Thickness * 0.62f + Edge, std::abs(Z - (BaseZ + Thickness * 0.34f)));
		Mask = Streak * Vertical;
		break;
	}
	case ECloudShapeGenerator::SourceVolume:
	default:
		Mask = 1.0f;
		break;
	}

	const float DetailLift = 1.0f - Detail * 0.45f + Noise * Detail * 0.55f;
	return std::clamp(Mask * DetailLift, 0.0f, 1.0f);
}

float ComputeMaxDensity(const std::vector<float>& Density)
{
	float MaxDensity = 0.0f;
	for (const float Value : Density)
	{
		if (std::isfinite(Value))
		{
			MaxDensity = std::max(MaxDensity, Value);
		}
	}
	return MaxDensity;
}

void ApplyCloudShapeGenerator(FConsoleState& State, const FCloudInfoVolumeConfig& Config, std::vector<float>& Density)
{
	if (static_cast<ECloudShapeGenerator>(State.CloudShapeGenerator) == ECloudShapeGenerator::SourceVolume)
	{
		return;
	}

	const int SizeX = Config.SourceResolution[0];
	const int SizeY = Config.SourceResolution[1];
	const int SizeZ = Config.SourceResolution[2];
	for (int Z = 0; Z < SizeZ; ++Z)
	{
		const float NormalizedZ = SizeZ <= 1 ? 0.0f : static_cast<float>(Z) / static_cast<float>(SizeZ - 1);
		for (int Y = 0; Y < SizeY; ++Y)
		{
			const float NormalizedY = SizeY <= 1 ? 0.0f : static_cast<float>(Y) / static_cast<float>(SizeY - 1);
			for (int X = 0; X < SizeX; ++X)
			{
				const float NormalizedX = SizeX <= 1 ? 0.0f : static_cast<float>(X) / static_cast<float>(SizeX - 1);
				const std::size_t Index =
					static_cast<std::size_t>(X) +
					static_cast<std::size_t>(Y) * static_cast<std::size_t>(SizeX) +
					static_cast<std::size_t>(Z) * static_cast<std::size_t>(SizeX) * static_cast<std::size_t>(SizeY);
				Density[Index] *= ComputeCloudShapeMask(State, NormalizedX, NormalizedY, NormalizedZ, X, Y, Z);
			}
		}
	}

	AddLog(State, "Applied cloud generator '%s' with seed %d.", GeneratorTypeName(State.CloudShapeGenerator).c_str(), State.CloudShapeSeed);
}

bool StartsWithSkyboxDirectory(std::filesystem::path RelativePath)
{
	RelativePath = RelativePath.lexically_normal();
	auto It = RelativePath.begin();
	if (It == RelativePath.end())
	{
		return false;
	}

	std::string FirstPart = It->string();
	std::transform(FirstPart.begin(), FirstPart.end(), FirstPart.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
	return FirstPart == "skybox";
}

std::filesystem::path MakeRelativeOrFilename(const std::filesystem::path& Path, const std::filesystem::path& Root)
{
	std::error_code Error;
	std::filesystem::path Relative = std::filesystem::relative(Path, Root, Error);
	const std::string RelativeText = Relative.generic_string();
	if (!Error && !Relative.empty() && RelativeText != ".." && RelativeText.rfind("../", 0) != 0)
	{
		return Relative;
	}
	return Path.filename();
}

bool CopyFileEnsuringDirectory(const std::filesystem::path& Source, const std::filesystem::path& Destination, std::string& OutError)
{
	if (!FileExists(Source))
	{
		OutError = "Missing source file: " + Source.string();
		return false;
	}

	std::error_code Error;
	if (std::filesystem::exists(Destination, Error) && !Error)
	{
		std::error_code EquivalentError;
		if (std::filesystem::equivalent(Source, Destination, EquivalentError) && !EquivalentError)
		{
			return true;
		}
	}

	Error.clear();
	std::filesystem::create_directories(Destination.parent_path(), Error);
	if (Error)
	{
		OutError = "Could not create directory: " + Destination.parent_path().string();
		return false;
	}

	std::filesystem::copy_file(Source, Destination, std::filesystem::copy_options::overwrite_existing, Error);
	if (Error)
	{
		OutError = "Copy failed from " + Source.string() + " to " + Destination.string() + ": " + Error.message();
		return false;
	}

	return true;
}

bool WriteRawRGBA32F(const std::filesystem::path& OutputPath, const std::vector<float>& Pixels);

std::array<int, 3> ReadJsonResolution(const nlohmann::json& Value)
{
	if (!Value.is_array() || Value.size() != 3)
	{
		throw std::runtime_error("Expected a 3-element volume resolution.");
	}

	return { Value[0].get<int>(), Value[1].get<int>(), Value[2].get<int>() };
}

std::array<float, 3> ReadJsonFloat3(const nlohmann::json& Value)
{
	if (!Value.is_array() || Value.size() != 3)
	{
		throw std::runtime_error("Expected a 3-element float vector.");
	}

	return { Value[0].get<float>(), Value[1].get<float>(), Value[2].get<float>() };
}

std::filesystem::path ResolveMRBNNDataPath(const std::filesystem::path& Path, const std::filesystem::path& RepositoryRoot)
{
	return NormalizePath(Path.is_relative() ? RepositoryRoot / Path : Path);
}

bool ReadCloudInfoVolumeConfig(FConsoleState& State, FCloudInfoVolumeConfig& OutConfig)
{
	OutConfig = {};
	OutConfig.WorkDir = NormalizePath(MakePath(State.WorkDir));
	OutConfig.RepositoryRoot = NormalizePath(MakePath(State.RepositoryRoot));
	OutConfig.ConfigPath = OutConfig.WorkDir / "config.json";

	if (!FileExists(OutConfig.ConfigPath))
	{
		AddLog(State, "Cloud-info bake requires Work Dir/config.json.");
		return false;
	}
	if (!DirectoryExists(OutConfig.RepositoryRoot))
	{
		AddLog(State, "Cloud-info bake requires a valid Repository Root.");
		return false;
	}

	try
	{
		std::ifstream ConfigFile(OutConfig.ConfigPath);
		const nlohmann::json ConfigJson = nlohmann::json::parse(ConfigFile);
		const nlohmann::json& VolumeJson = ConfigJson.at("volume");
		OutConfig.ConfigVolumePath = VolumeJson.at("path").get<std::string>();
		OutConfig.SourceResolution = ReadJsonResolution(VolumeJson.at("resolution"));
		OutConfig.SkipByteCount = VolumeJson.value("skip_byte_num", std::size_t{ 0 });
		OutConfig.MipmapLevel = VolumeJson.value("mipmap_level", 0);
		if (VolumeJson.contains("bound"))
		{
			const nlohmann::json& BoundsJson = VolumeJson.at("bound");
			if (!BoundsJson.is_array() || BoundsJson.size() != 2)
			{
				throw std::runtime_error("volume.bound must be a two-corner array.");
			}
			OutConfig.BoundMin = ReadJsonFloat3(BoundsJson[0]);
			OutConfig.BoundMax = ReadJsonFloat3(BoundsJson[1]);
			OutConfig.bHasExplicitBounds = true;
		}
		OutConfig.VolumePath = ResolveMRBNNDataPath(MakePath(OutConfig.ConfigVolumePath.c_str()), OutConfig.RepositoryRoot);
	}
	catch (const std::exception& Exception)
	{
		AddLog(State, "Failed to parse cloud-info bake volume config: %s", Exception.what());
		return false;
	}

	if (!FileExists(OutConfig.VolumePath))
	{
		AddLog(State, "Cloud-info source volume is missing: %s", OutConfig.VolumePath.string().c_str());
		return false;
	}
	for (int Axis = 0; Axis < 3; ++Axis)
	{
		if (OutConfig.SourceResolution[Axis] <= 0)
		{
			AddLog(State, "Cloud-info source resolution must be positive.");
			return false;
		}
	}

	return true;
}

bool ReadDensityVolume(FConsoleState& State, const FCloudInfoVolumeConfig& Config, std::vector<float>& OutDensity, float& OutMaxDensity)
{
	const std::size_t VoxelCount =
		static_cast<std::size_t>(Config.SourceResolution[0]) *
		static_cast<std::size_t>(Config.SourceResolution[1]) *
		static_cast<std::size_t>(Config.SourceResolution[2]);
	OutDensity.assign(VoxelCount, 0.0f);
	OutMaxDensity = 0.0f;

	std::ifstream VolumeFile(Config.VolumePath, std::ios::binary);
	if (!VolumeFile)
	{
		AddLog(State, "Could not open source volume: %s", Config.VolumePath.string().c_str());
		return false;
	}

	VolumeFile.seekg(static_cast<std::streamoff>(Config.SkipByteCount), std::ios::beg);
	VolumeFile.read(reinterpret_cast<char*>(OutDensity.data()), static_cast<std::streamsize>(OutDensity.size() * sizeof(float)));
	if (VolumeFile.gcount() != static_cast<std::streamsize>(OutDensity.size() * sizeof(float)))
	{
		AddLog(State, "Source volume is smaller than config resolution requires.");
		return false;
	}

	for (const float Value : OutDensity)
	{
		if (std::isfinite(Value))
		{
			OutMaxDensity = std::max(OutMaxDensity, Value);
		}
	}

	return true;
}

bool WriteJsonFile(FConsoleState& State, const std::filesystem::path& OutputPath, const nlohmann::json& Json, const char* Label)
{
	std::error_code Error;
	std::filesystem::create_directories(OutputPath.parent_path(), Error);
	if (Error)
	{
		AddLog(State, "Could not create %s directory: %s", Label, Error.message().c_str());
		return false;
	}

	std::ofstream File(OutputPath);
	if (!File)
	{
		AddLog(State, "Could not write %s: %s", Label, OutputPath.string().c_str());
		return false;
	}
	File << Json.dump(2) << "\n";
	if (!File)
	{
		AddLog(State, "Failed while writing %s: %s", Label, OutputPath.string().c_str());
		return false;
	}
	return true;
}

bool WriteCloudInfoManifest(
	FConsoleState& State,
	const FCloudInfoVolumeConfig& Config,
	const std::filesystem::path& ManifestPath,
	const std::filesystem::path& RawPath,
	int BakeResolution,
	float SourceMaxDensity,
	float DensityScale)
{
	const std::filesystem::path RawRelativePath = MakeRelativeOrFilename(RawPath, ManifestPath.parent_path());
	const nlohmann::json ManifestJson = {
		{ "artifact_version", 1 },
		{ "artifact_type", "mrbnn_cloud_info_rgba32f" },
		{ "scene", SanitizeSceneName(State.SceneName, Config.WorkDir) },
		{ "source", {
			{ "config_json", Config.ConfigPath.generic_string() },
			{ "working_directory", Config.WorkDir.generic_string() },
			{ "repository_root", Config.RepositoryRoot.generic_string() },
			{ "volume_path_from_config", Config.ConfigVolumePath },
			{ "resolved_volume_path", Config.VolumePath.generic_string() },
			{ "source_resolution_xyz", Config.SourceResolution },
			{ "skip_byte_num", Config.SkipByteCount },
			{ "mipmap_level", Config.MipmapLevel }
		} },
		{ "bake", {
			{ "output_file", RawRelativePath.generic_string() },
			{ "format", "raw little-endian float32 RGBA, x-fastest linear order" },
			{ "bake_resolution_xyz", std::array<int, 3>{ BakeResolution, BakeResolution, BakeResolution } },
			{ "voxel_count", BakeResolution * BakeResolution * BakeResolution },
			{ "bounds", {
				{ "source_bound_min", Config.BoundMin },
				{ "source_bound_max", Config.BoundMax },
				{ "has_explicit_source_bounds", Config.bHasExplicitBounds },
				{ "crop_enabled", false },
				{ "crop_min_voxel_xyz", std::array<int, 3>{ 0, 0, 0 } },
				{ "crop_max_voxel_xyz", std::array<int, 3>{ Config.SourceResolution[0] - 1, Config.SourceResolution[1] - 1, Config.SourceResolution[2] - 1 } }
			} }
		} },
		{ "axis_mapping", {
			{ "source_resolution_order", "config volume.resolution [x,y,z]" },
			{ "source_volume_linear_order", "x fastest, then y, then z after skip_byte_num" },
			{ "output_linear_order", "x fastest, then y, then z" },
			{ "applied_swizzle", "none" },
			{ "original_mrbnn_gpu_density_sample_order_note", "VolumeKernelData::GetDensityRaw_ samples MRBNN textures as z,y,x internally; this bake stores canonical config x,y,z order for later UE ingestion." }
		} },
		{ "normalization", {
			{ "raw_density_channel", "R" },
			{ "normalized_density_channel", "G" },
			{ "formula", "G = saturate(R * density_scale)" },
			{ "auto_density_scale", State.bCloudInfoAutoDensityScale },
			{ "source_max_density", SourceMaxDensity },
			{ "density_scale", DensityScale },
			{ "occupancy_threshold", State.CloudInfoOccupancyThreshold },
			{ "occupancy_formula", "A = (G >= occupancy_threshold) ? 1.0 : 0.0" },
			{ "gradient_formula", "B = saturate(length(central_difference_raw_density) * 0.5 * density_scale)" }
		} },
		{ "generator", {
			{ "type", GeneratorTypeName(State.CloudShapeGenerator) },
			{ "seed", State.CloudShapeSeed },
			{ "shape_params", {
				{ "coverage", State.CloudShapeCoverage },
				{ "base_height", State.CloudShapeBaseHeight },
				{ "thickness", State.CloudShapeThickness },
				{ "edge_softness", State.CloudShapeEdgeSoftness },
				{ "detail", State.CloudShapeDetail },
				{ "wind_shear", State.CloudShapeWindShear }
			} }
		} },
		{ "channel_layout", nlohmann::json::array({
			{ { "channel", "R" }, { "name", "raw_density" }, { "value_space", "raw MRBNN density after CUDA trilinear resampling" } },
			{ { "channel", "G" }, { "name", "normalized_density" }, { "value_space", "normalized 0..1 by manifest normalization formula" } },
			{ { "channel", "B" }, { "name", "normalized_gradient_magnitude" }, { "value_space", "normalized 0..1 edge/detail proxy" } },
			{ { "channel", "A" }, { "name", "occupancy" }, { "value_space", "binary 0/1 from normalized density threshold" } }
		}) },
		{ "future_original_tcnn_buffer_export_todo", {
			{ "status", "not exported in this bounded first iteration" },
			{ "owner_code", "RenderInterfaceWithTCNN::Render" },
			{ "candidate_buffers", nlohmann::json::array({ "featureBuffer", "radianceBuffer", "positionBuffer", "viewDirBuffer" }) },
			{ "candidate_metadata", nlohmann::json::array({ "stepNum", "featureDim", "paddedSize", "frameBuffer.size" }) },
			{ "source_anchor", "third_party_refs/MRBNN/src/render/RenderInterface.cpp TCNNData and DispatchTCNNRenderKernel path" }
		} }
	};

	return WriteJsonFile(State, ManifestPath, ManifestJson, "cloud-info manifest");
}

bool WriteBakeSyncManifest(
	FConsoleState& State,
	const FCloudInfoVolumeConfig& Config,
	const std::filesystem::path& SyncManifestPath,
	const std::filesystem::path& RawPath,
	const std::filesystem::path& CloudInfoManifestPath,
	const std::filesystem::path& OutputDirectory,
	int BakeResolution)
{
	const std::filesystem::path PluginRoot = NormalizePath(MakePath(State.PluginRoot));
	const std::filesystem::path RawRelativeToOutput = MakeRelativeOrFilename(RawPath, OutputDirectory);
	const std::filesystem::path CloudInfoManifestRelativeToOutput = MakeRelativeOrFilename(CloudInfoManifestPath, OutputDirectory);
	const std::filesystem::path SyncRelativeToOutput = MakeRelativeOrFilename(SyncManifestPath, OutputDirectory);

	nlohmann::json UeCopyTargets = nlohmann::json::object();
	std::error_code Error;
	const std::filesystem::path OutputRelativeToPlugin = std::filesystem::relative(OutputDirectory, PluginRoot, Error);
	const std::string OutputRelativeText = OutputRelativeToPlugin.generic_string();
	if (!Error && !OutputRelativeToPlugin.empty() && OutputRelativeText != ".." && OutputRelativeText.rfind("../", 0) != 0)
	{
		UeCopyTargets = {
			{ "plugin_relative_output_directory", OutputRelativeToPlugin.generic_string() },
			{ "plugin_relative_cloudInfo_rgba32f", (OutputRelativeToPlugin / RawPath.filename()).generic_string() },
			{ "plugin_relative_cloudInfo_manifest", (OutputRelativeToPlugin / CloudInfoManifestPath.filename()).generic_string() },
			{ "plugin_relative_sync_manifest", (OutputRelativeToPlugin / SyncManifestPath.filename()).generic_string() }
		};
	}

	const nlohmann::json SyncJson = {
		{ "schema_version", 1 },
		{ "schema_name", "mrbnn_bake_sync_manifest" },
		{ "scene_name", SanitizeSceneName(State.SceneName, Config.WorkDir) },
		{ "source_working_directory", Config.WorkDir.generic_string() },
		{ "plugin_root", PluginRoot.generic_string() },
		{ "output_directory", OutputDirectory.generic_string() },
		{ "sync_manifest_path", SyncManifestPath.generic_string() },
		{ "cloudInfo", {
			{ "rgba32f_path", RawPath.generic_string() },
			{ "manifest_path", CloudInfoManifestPath.generic_string() },
			{ "rgba32f_relative_to_output", RawRelativeToOutput.generic_string() },
			{ "manifest_relative_to_output", CloudInfoManifestRelativeToOutput.generic_string() },
			{ "format", "raw little-endian float32 RGBA, x-fastest linear order" }
		} },
		{ "generator", {
			{ "type", GeneratorTypeName(State.CloudShapeGenerator) },
			{ "seed", State.CloudShapeSeed },
			{ "shape_params", {
				{ "coverage", State.CloudShapeCoverage },
				{ "base_height", State.CloudShapeBaseHeight },
				{ "thickness", State.CloudShapeThickness },
				{ "edge_softness", State.CloudShapeEdgeSoftness },
				{ "detail", State.CloudShapeDetail },
				{ "wind_shear", State.CloudShapeWindShear }
			} }
		} },
		{ "bake_resolution_xyz", std::array<int, 3>{ BakeResolution, BakeResolution, BakeResolution } },
		{ "density_source", {
			{ "path", Config.VolumePath.generic_string() },
			{ "path_from_config", Config.ConfigVolumePath },
			{ "resolution_xyz", Config.SourceResolution },
			{ "skip_byte_num", Config.SkipByteCount },
			{ "mipmap_level", Config.MipmapLevel }
		} },
		{ "timestamps", {
			{ "created_utc", FormatUtcTimestamp() }
		} },
		{ "ueCopyTargets", UeCopyTargets },
		{ "work_dir_copy_enabled", State.bCopySyncManifestToWorkDir },
		{ "sync_manifest_relative_to_output", SyncRelativeToOutput.generic_string() }
	};

	if (!WriteJsonFile(State, SyncManifestPath, SyncJson, "bake sync manifest"))
	{
		return false;
	}

	if (State.bCopySyncManifestToWorkDir)
	{
		const std::filesystem::path WorkDirCopy = Config.WorkDir / "mrbnn_bake_sync_manifest.json";
		if (NormalizePath(WorkDirCopy) != NormalizePath(SyncManifestPath))
		{
			std::string CopyError;
			if (!CopyFileEnsuringDirectory(SyncManifestPath, WorkDirCopy, CopyError))
			{
				AddLog(State, "%s", CopyError.c_str());
				return false;
			}
		}
	}

	CopyString(State.LastBakeSyncManifestPath, PathBufferSize, ToUtf8(SyncManifestPath));
	return true;
}

bool BakeCloudInfoVolume(FConsoleState& State)
{
	if (!State.bCudaRuntimeCompatible)
	{
		AddLog(State, "%s", State.CudaCompatibilityWarning.c_str());
		return false;
	}

	FCloudInfoVolumeConfig Config;
	if (!ReadCloudInfoVolumeConfig(State, Config))
	{
		return false;
	}

	State.CloudInfoBakeResolution = std::clamp(State.CloudInfoBakeResolution, 4, 256);
	State.CloudInfoOccupancyThreshold = std::clamp(State.CloudInfoOccupancyThreshold, 0.0f, 1.0f);
	std::vector<float> Density;
	float SourceMaxDensity = 0.0f;
	if (!ReadDensityVolume(State, Config, Density, SourceMaxDensity))
	{
		return false;
	}
	ApplyCloudShapeGenerator(State, Config, Density);
	SourceMaxDensity = ComputeMaxDensity(Density);

	float DensityScale = State.CloudInfoDensityScale;
	if (State.bCloudInfoAutoDensityScale)
	{
		DensityScale = SourceMaxDensity > 0.0f ? 1.0f / SourceMaxDensity : 1.0f;
		State.CloudInfoDensityScale = DensityScale;
	}
	DensityScale = std::max(DensityScale, 0.0f);

	const int BakeResolution = State.CloudInfoBakeResolution;
	const std::size_t OutputFloatCount =
		static_cast<std::size_t>(BakeResolution) *
		static_cast<std::size_t>(BakeResolution) *
		static_cast<std::size_t>(BakeResolution) *
		4;
	State.CloudInfoRGBA.assign(OutputFloatCount, 0.0f);

	char Error[4096] = {};
	const auto StartTime = std::chrono::steady_clock::now();
	const int Ok = MRBNN_BakeCloudInfoVolumeRGBA32F(
		Density.data(),
		Config.SourceResolution[0],
		Config.SourceResolution[1],
		Config.SourceResolution[2],
		BakeResolution,
		BakeResolution,
		BakeResolution,
		DensityScale,
		State.CloudInfoOccupancyThreshold,
		State.CloudInfoRGBA.data(),
		static_cast<int>(State.CloudInfoRGBA.size()),
		Error,
		static_cast<int>(sizeof(Error)));
	if (!Ok)
	{
		AddLog(State, "Cloud-info CUDA bake failed: %s", Error);
		return false;
	}

	std::filesystem::path OutputDirectory = MakePath(State.CloudInfoOutputDir);
	if (OutputDirectory.empty())
	{
		OutputDirectory = NormalizePath(MakePath(State.PluginRoot) / "Binaries/ThirdParty/MRBNNBridge/Win64/CloudInfoBakes");
	}
	OutputDirectory = NormalizePath(OutputDirectory);
	const std::string SafeSceneName = SanitizeSceneName(State.SceneName, Config.WorkDir);
	const std::filesystem::path RawPath = OutputDirectory / (SafeSceneName + "_cloud_info.rgba32f");
	const std::filesystem::path ManifestPath = OutputDirectory / (SafeSceneName + "_cloud_info_manifest.json");
	const std::filesystem::path SyncManifestPath = OutputDirectory / "mrbnn_bake_sync_manifest.json";

	std::error_code FsError;
	std::filesystem::create_directories(OutputDirectory, FsError);
	if (FsError)
	{
		AddLog(State, "Could not create cloud-info output directory: %s", FsError.message().c_str());
		return false;
	}

	if (!WriteRawRGBA32F(RawPath, State.CloudInfoRGBA))
	{
		AddLog(State, "Failed to write cloud-info RGBA32F: %s", RawPath.string().c_str());
		return false;
	}
	if (!WriteCloudInfoManifest(State, Config, ManifestPath, RawPath, BakeResolution, SourceMaxDensity, DensityScale))
	{
		return false;
	}
	if (!WriteBakeSyncManifest(State, Config, SyncManifestPath, RawPath, ManifestPath, OutputDirectory, BakeResolution))
	{
		return false;
	}

	CopyString(State.LastCloudInfoRawPath, PathBufferSize, ToUtf8(RawPath));
	CopyString(State.LastCloudInfoManifestPath, PathBufferSize, ToUtf8(ManifestPath));

	const double ElapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - StartTime).count();
	AddLog(State, "Baked cloud info %d^3 to %s with sync manifest %s in %.2fs.",
		BakeResolution,
		RawPath.string().c_str(),
		SyncManifestPath.string().c_str(),
		ElapsedSeconds);
	return true;
}

unsigned char ToByte(float Value)
{
	if (!std::isfinite(Value))
	{
		return 0;
	}

	Value = std::clamp(Value, 0.0f, 1.0f);
	return static_cast<unsigned char>(std::lround(Value * 255.0f));
}

float PreviewToneMap(float Value)
{
	if (!std::isfinite(Value))
	{
		return 0.0f;
	}

	Value = std::max(Value, 0.0f);
	Value = Value * (2.51f * Value + 0.03f) / std::max(Value * (2.43f * Value + 0.59f) + 0.14f, 1.0e-3f);
	Value = std::pow(std::clamp(Value, 0.0f, 1.0f), 1.0f / 2.2f);
	return Value;
}

bool WritePPM(const std::filesystem::path& OutputPath, int Width, int Height, const std::vector<float>& Pixels)
{
	std::ofstream Out(OutputPath, std::ios::binary);
	if (!Out)
	{
		return false;
	}

	Out << "P6\n" << Width << " " << Height << "\n255\n";
	for (int Index = 0; Index < Width * Height; ++Index)
	{
		const float* Pixel = Pixels.data() + static_cast<size_t>(Index) * 4;
		const std::array<unsigned char, 3> RGB{ ToByte(Pixel[0]), ToByte(Pixel[1]), ToByte(Pixel[2]) };
		Out.write(reinterpret_cast<const char*>(RGB.data()), static_cast<std::streamsize>(RGB.size()));
	}

	return static_cast<bool>(Out);
}

bool WriteRawRGBA32F(const std::filesystem::path& OutputPath, const std::vector<float>& Pixels)
{
	std::ofstream Out(OutputPath, std::ios::binary);
	if (!Out)
	{
		return false;
	}

	Out.write(reinterpret_cast<const char*>(Pixels.data()), static_cast<std::streamsize>(Pixels.size() * sizeof(float)));
	return static_cast<bool>(Out);
}

void EnsurePreviewTexture(FConsoleState& State, int Width, int Height)
{
	if (State.PreviewTexture == 0)
	{
		glGenTextures(1, &State.PreviewTexture);
	}

	if (State.PreviewWidth != Width || State.PreviewHeight != Height)
	{
		State.PreviewWidth = Width;
		State.PreviewHeight = Height;
		glBindTexture(GL_TEXTURE_2D, State.PreviewTexture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, Width, Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	}
}

void UploadPreviewTexture(FConsoleState& State)
{
	if (State.Pixels.empty() || State.Width <= 0 || State.Height <= 0)
	{
		return;
	}

	EnsurePreviewTexture(State, State.Width, State.Height);
	State.PreviewBytes.resize(static_cast<size_t>(State.Width) * State.Height * 4);
	for (int Index = 0; Index < State.Width * State.Height; ++Index)
	{
		const float* Source = State.Pixels.data() + static_cast<size_t>(Index) * 4;
		unsigned char* Destination = State.PreviewBytes.data() + static_cast<size_t>(Index) * 4;
		Destination[0] = ToByte(PreviewToneMap(Source[0]));
		Destination[1] = ToByte(PreviewToneMap(Source[1]));
		Destination[2] = ToByte(PreviewToneMap(Source[2]));
		Destination[3] = 255;
	}

	glBindTexture(GL_TEXTURE_2D, State.PreviewTexture);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, State.Width, State.Height, GL_RGBA, GL_UNSIGNED_BYTE, State.PreviewBytes.data());
}

void DestroyRenderer(FConsoleState& State)
{
	if (State.Renderer)
	{
		MRBNN_Destroy(State.Renderer);
		State.Renderer = nullptr;
	}
}

bool InitializeRenderer(FConsoleState& State)
{
	if (!State.bCudaRuntimeCompatible)
	{
		AddLog(State, "%s", State.CudaCompatibilityWarning.c_str());
		return false;
	}

	DestroyRenderer(State);

	const std::filesystem::path WorkDir = MakePath(State.WorkDir);
	const std::filesystem::path RepositoryRoot = MakePath(State.RepositoryRoot);
	if (!DirectoryExists(WorkDir) || !FileExists(WorkDir / "config.json"))
	{
		AddLog(State, "Work Dir must contain config.json.");
		return false;
	}
	if (!DirectoryExists(RepositoryRoot))
	{
		AddLog(State, "Repository Root does not exist.");
		return false;
	}

	char Error[4096] = {};
	if (!MRBNN_Create(ToUtf8(WorkDir).c_str(), ToUtf8(RepositoryRoot).c_str(), &State.Renderer, Error, static_cast<int>(sizeof(Error))))
	{
		AddLog(State, "MRBNN_Create failed: %s", Error);
		return false;
	}

	if (State.bEnableSkybox && State.SkyboxHDRI[0] != '\0')
	{
		if (!MRBNN_SetSkybox(State.Renderer, State.SkyboxHDRI, State.SkyboxExposure, Error, static_cast<int>(sizeof(Error))))
		{
			AddLog(State, "MRBNN_SetSkybox failed: %s", Error);
			DestroyRenderer(State);
			return false;
		}
	}

	if (State.bEnableSkyboxBaking && State.SkyboxBakingDir[0] != '\0')
	{
		if (!MRBNN_SetSkyboxBaking(State.Renderer, State.SkyboxBakingDir, Error, static_cast<int>(sizeof(Error))))
		{
			AddLog(State, "MRBNN_SetSkyboxBaking failed: %s", Error);
			DestroyRenderer(State);
			return false;
		}
	}

	AddLog(State, "Renderer initialized.");
	return true;
}

bool RenderSamples(FConsoleState& State, int SampleCount)
{
	State.Width = std::max(State.Width, 1);
	State.Height = std::max(State.Height, 1);
	SampleCount = std::max(SampleCount, 1);

	if (!State.Renderer && !InitializeRenderer(State))
	{
		return false;
	}

	const size_t FloatCount = static_cast<size_t>(State.Width) * State.Height * 4;
	State.Pixels.assign(FloatCount, 0.0f);
	State.FramePixels.assign(FloatCount, 0.0f);

	char Error[4096] = {};
	const auto StartTime = std::chrono::steady_clock::now();
	for (int SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		std::fill(State.FramePixels.begin(), State.FramePixels.end(), 0.0f);
		const int Frame = State.FrameIndex + SampleIndex;
		const int Ok = MRBNN_RenderRGBA32F(
			State.Renderer,
			State.Width,
			State.Height,
			Frame,
			State.Camera[0],
			State.Camera[1],
			State.Camera[2],
			State.LightDirection[0],
			State.LightDirection[1],
			State.LightDirection[2],
			State.LightColor[0],
			State.LightColor[1],
			State.LightColor[2],
			State.Albedo[0],
			State.Albedo[1],
			State.Albedo[2],
			State.PhaseG,
			State.ToneMapping,
			State.Denoise,
			State.Compatibility,
			State.bExcludeLightEncoding ? 1 : 0,
			State.bFastDirectIllumination ? 1 : 0,
			State.bEnableSkybox ? 1 : 0,
			State.bEnableSkyboxBaking ? 1 : 0,
			State.FramePixels.data(),
			static_cast<int>(State.FramePixels.size()),
			Error,
			static_cast<int>(sizeof(Error)));

		if (!Ok)
		{
			AddLog(State, "Render failed at sample %d: %s", SampleIndex, Error);
			return false;
		}

		const float Weight = 1.0f / static_cast<float>(SampleIndex + 1);
		for (size_t Index = 0; Index < State.Pixels.size(); ++Index)
		{
			State.Pixels[Index] += (State.FramePixels[Index] - State.Pixels[Index]) * Weight;
		}
	}

	UploadPreviewTexture(State);
	State.FrameIndex += SampleCount;

	const double ElapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - StartTime).count();
	AddLog(State, "Rendered %d sample(s) at %dx%d in %.2fs.", SampleCount, State.Width, State.Height, ElapsedSeconds);
	return true;
}

bool SaveOutputs(FConsoleState& State)
{
	if (State.Pixels.empty())
	{
		AddLog(State, "Render before saving outputs.");
		return false;
	}

	std::filesystem::path OutputPath = MakePath(State.OutputPath);
	if (OutputPath.empty())
	{
		OutputPath = NormalizePath(MakePath(State.PluginRoot) / "Binaries/ThirdParty/MRBNNBridge/Win64/MRBNNBakeConsolePreview.ppm");
	}

	std::error_code Error;
	std::filesystem::create_directories(OutputPath.parent_path(), Error);
	if (Error)
	{
		AddLog(State, "Could not create output directory: %s", Error.message().c_str());
		return false;
	}

	const std::filesystem::path RawPath = OutputPath.string() + ".rgba32f";
	if (!WriteRawRGBA32F(RawPath, State.Pixels))
	{
		AddLog(State, "Failed to write raw output.");
		return false;
	}

	if (!WritePPM(OutputPath, State.Width, State.Height, State.Pixels))
	{
		AddLog(State, "Failed to write PPM output.");
		return false;
	}

	AddLog(State, "Saved %s and %s.", OutputPath.string().c_str(), RawPath.string().c_str());
	return true;
}

bool PackageDataToPlugin(FConsoleState& State)
{
	const std::filesystem::path SourceWorkingDirectory = NormalizePath(MakePath(State.WorkDir));
	const std::filesystem::path SourceRepositoryRoot = NormalizePath(MakePath(State.RepositoryRoot));
	const std::filesystem::path DestinationRepositoryRoot = NormalizePath(MakePath(State.PluginRoot));
	const std::string SafeSceneName = SanitizeSceneName(State.SceneName, SourceWorkingDirectory);
	const std::filesystem::path DestinationWorkingDirectory = DestinationRepositoryRoot / "Data" / SafeSceneName;

	if (!DirectoryExists(SourceWorkingDirectory) || !FileExists(SourceWorkingDirectory / "config.json"))
	{
		AddLog(State, "Cannot package: Work Dir must contain config.json.");
		return false;
	}
	if (!DirectoryExists(DestinationRepositoryRoot))
	{
		AddLog(State, "Cannot package: Plugin Root does not exist.");
		return false;
	}

	std::string CopyError;
	std::error_code Error;
	std::filesystem::create_directories(DestinationWorkingDirectory, Error);
	if (Error)
	{
		AddLog(State, "Could not create destination working directory: %s", Error.message().c_str());
		return false;
	}

	for (const std::filesystem::directory_entry& Entry : std::filesystem::recursive_directory_iterator(SourceWorkingDirectory, Error))
	{
		if (Error)
		{
			AddLog(State, "Failed to scan work dir: %s", Error.message().c_str());
			return false;
		}
		if (!Entry.is_regular_file())
		{
			continue;
		}

		const std::filesystem::path RelativePath = std::filesystem::relative(Entry.path(), SourceWorkingDirectory, Error);
		if (Error)
		{
			continue;
		}
		if (!State.bIncludeSkyboxBakingWhenPackaging && StartsWithSkyboxDirectory(RelativePath))
		{
			continue;
		}
		if (!CopyFileEnsuringDirectory(Entry.path(), DestinationWorkingDirectory / RelativePath, CopyError))
		{
			AddLog(State, "%s", CopyError.c_str());
			return false;
		}
	}

	try
	{
		std::ifstream ConfigFile(SourceWorkingDirectory / "config.json");
		const nlohmann::json Config = nlohmann::json::parse(ConfigFile);
		if (Config.contains("volume") && Config["volume"].contains("path"))
		{
			const std::filesystem::path ConfigVolumePath = Config["volume"]["path"].get<std::string>();
			const std::filesystem::path SourceVolumePath = ConfigVolumePath.is_relative()
				? NormalizePath(SourceRepositoryRoot / ConfigVolumePath)
				: NormalizePath(ConfigVolumePath);
			const std::filesystem::path RelativeVolumePath = ConfigVolumePath.is_relative()
				? ConfigVolumePath
				: MakeRelativeOrFilename(SourceVolumePath, SourceRepositoryRoot);
			if (!CopyFileEnsuringDirectory(SourceVolumePath, DestinationRepositoryRoot / RelativeVolumePath, CopyError))
			{
				AddLog(State, "%s", CopyError.c_str());
				return false;
			}
		}
	}
	catch (const std::exception& Exception)
	{
		AddLog(State, "Failed to read volume path from config.json: %s", Exception.what());
		return false;
	}

	if (State.bIncludeSkyboxHDRIWhenPackaging && State.SkyboxHDRI[0] != '\0')
	{
		const std::filesystem::path SourceSkyboxPath = NormalizePath(MakePath(State.SkyboxHDRI));
		const std::filesystem::path RelativeSkyboxPath = MakeRelativeOrFilename(SourceSkyboxPath, SourceRepositoryRoot);
		if (!CopyFileEnsuringDirectory(SourceSkyboxPath, DestinationRepositoryRoot / RelativeSkyboxPath, CopyError))
		{
			AddLog(State, "%s", CopyError.c_str());
			return false;
		}
	}

	const nlohmann::json ManifestJson = {
		{ "scene", SafeSceneName },
		{ "source_working_directory", SourceWorkingDirectory.generic_string() },
		{ "source_repository_root", SourceRepositoryRoot.generic_string() },
		{ "destination_repository_root", DestinationRepositoryRoot.generic_string() }
	};

	std::ofstream Manifest(DestinationWorkingDirectory / "mrbnn_ue_manifest.json");
	if (!Manifest)
	{
		AddLog(State, "Could not write manifest in %s.", DestinationWorkingDirectory.string().c_str());
		return false;
	}
	Manifest << ManifestJson.dump(2) << "\n";
	if (!Manifest)
	{
		AddLog(State, "Failed while writing manifest in %s.", DestinationWorkingDirectory.string().c_str());
		return false;
	}

	AddLog(State, "Packaged MRBNN data to %s.", DestinationWorkingDirectory.string().c_str());
	return true;
}

void SetDefaultPaths(FConsoleState& State, const std::vector<std::string>& Args)
{
	std::vector<std::string> PositionalArgs;
	for (size_t Index = 1; Index < Args.size() && Args[Index].rfind("-", 0) != 0; ++Index)
	{
		PositionalArgs.push_back(Args[Index]);
	}

	std::filesystem::path WorkDir = PositionalArgs.size() > 0 ? MakePath(PositionalArgs[0].c_str()) : std::filesystem::path();
	std::filesystem::path RepositoryRoot = PositionalArgs.size() > 1 ? MakePath(PositionalArgs[1].c_str()) : std::filesystem::path();
	std::filesystem::path PluginRoot = PositionalArgs.size() > 2 ? MakePath(PositionalArgs[2].c_str()) : std::filesystem::path();

	const std::filesystem::path Current = std::filesystem::current_path();
	if (PluginRoot.empty())
	{
		PluginRoot = Current;
	}
	if (RepositoryRoot.empty())
	{
		RepositoryRoot = NormalizePath(PluginRoot / "../../../../third_party_refs/MRBNN");
	}
	if (WorkDir.empty())
	{
		WorkDir = NormalizePath(PluginRoot / "Data/cloud-03");
	}

	CopyString(State.WorkDir, PathBufferSize, ToUtf8(NormalizePath(WorkDir)));
	CopyString(State.RepositoryRoot, PathBufferSize, ToUtf8(NormalizePath(RepositoryRoot)));
	CopyString(State.PluginRoot, PathBufferSize, ToUtf8(NormalizePath(PluginRoot)));
	CopyString(State.SkyboxHDRI, PathBufferSize, ToUtf8(NormalizePath(MakePath(State.PluginRoot) / "Data/qwantani_sunset_puresky_4k.hdr")));
	CopyString(State.SkyboxBakingDir, PathBufferSize, ToUtf8(NormalizePath(MakePath(State.WorkDir) / "skybox")));
	CopyString(State.OutputPath, PathBufferSize, ToUtf8(NormalizePath(MakePath(State.PluginRoot) / "Binaries/ThirdParty/MRBNNBridge/Win64/MRBNNBakeConsolePreview.ppm")));
	CopyString(State.CloudInfoOutputDir, PathBufferSize, ToUtf8(NormalizePath(MakePath(State.PluginRoot) / "Binaries/ThirdParty/MRBNNBridge/Win64/CloudInfoBakes")));
}

bool ApplyCommandLineOptions(FConsoleState& State, const std::vector<std::string>& Args)
{
	bool bBakeCloudInfoAndExit = false;
	for (size_t Index = 1; Index < Args.size(); ++Index)
	{
		const std::string& Arg = Args[Index];
		if (Arg == "--bake-cloud-info")
		{
			bBakeCloudInfoAndExit = true;
		}
		else if (Arg == "--cloud-info-output-dir" && Index + 1 < Args.size())
		{
			CopyString(State.CloudInfoOutputDir, PathBufferSize, Args[++Index]);
		}
		else if (Arg == "--cloud-info-resolution" && Index + 1 < Args.size())
		{
			State.CloudInfoBakeResolution = std::atoi(Args[++Index].c_str());
		}
		else if (Arg == "--cloud-info-density-scale" && Index + 1 < Args.size())
		{
			State.bCloudInfoAutoDensityScale = false;
			State.CloudInfoDensityScale = static_cast<float>(std::atof(Args[++Index].c_str()));
		}
		else if (Arg == "--cloud-info-occupancy-threshold" && Index + 1 < Args.size())
		{
			State.CloudInfoOccupancyThreshold = static_cast<float>(std::atof(Args[++Index].c_str()));
		}
		else if (Arg == "--cloud-shape-generator" && Index + 1 < Args.size())
		{
			ApplyCloudShapePreset(State, static_cast<ECloudShapeGenerator>(ParseGeneratorType(Args[++Index])));
		}
		else if (Arg == "--cloud-shape-seed" && Index + 1 < Args.size())
		{
			State.CloudShapeSeed = std::atoi(Args[++Index].c_str());
		}
		else if (Arg == "--cloud-shape-coverage" && Index + 1 < Args.size())
		{
			State.CloudShapeCoverage = static_cast<float>(std::atof(Args[++Index].c_str()));
		}
		else if (Arg == "--cloud-shape-base-height" && Index + 1 < Args.size())
		{
			State.CloudShapeBaseHeight = static_cast<float>(std::atof(Args[++Index].c_str()));
		}
		else if (Arg == "--cloud-shape-thickness" && Index + 1 < Args.size())
		{
			State.CloudShapeThickness = static_cast<float>(std::atof(Args[++Index].c_str()));
		}
		else if (Arg == "--cloud-shape-edge-softness" && Index + 1 < Args.size())
		{
			State.CloudShapeEdgeSoftness = static_cast<float>(std::atof(Args[++Index].c_str()));
		}
		else if (Arg == "--cloud-shape-detail" && Index + 1 < Args.size())
		{
			State.CloudShapeDetail = static_cast<float>(std::atof(Args[++Index].c_str()));
		}
		else if (Arg == "--cloud-shape-wind-shear" && Index + 1 < Args.size())
		{
			State.CloudShapeWindShear = static_cast<float>(std::atof(Args[++Index].c_str()));
		}
		else if (Arg == "--no-workdir-sync-manifest-copy")
		{
			State.bCopySyncManifestToWorkDir = false;
		}
	}
	return bBakeCloudInfoAndExit;
}

void PrintUsage()
{
	std::printf(
		"MRBNN Bake Console\n"
		"\n"
		"Usage:\n"
		"  MRBNNBakeConsole.exe [WorkDir] [RepositoryRoot] [PluginRoot] [options]\n"
		"\n"
		"Options:\n"
		"  --bake-cloud-info                 Run a headless Cloud Info bake and exit.\n"
		"  --cloud-info-output-dir <path>    Output directory for Cloud Info artifacts.\n"
		"  --cloud-info-resolution <n>       Cubic bake resolution.\n"
		"  --cloud-info-density-scale <v>    Manual density normalization scale.\n"
		"  --cloud-info-occupancy-threshold <v>\n"
		"  --cloud-shape-generator <name>    source_volume, cumulus_core, anvil_tower, layer_bank, wispy_streaks.\n"
		"  --cloud-shape-seed <n>\n"
		"  --cloud-shape-coverage <v>\n"
		"  --cloud-shape-base-height <v>\n"
		"  --cloud-shape-thickness <v>\n"
		"  --cloud-shape-edge-softness <v>\n"
		"  --cloud-shape-detail <v>\n"
		"  --cloud-shape-wind-shear <v>\n"
		"  --no-workdir-sync-manifest-copy   Do not copy mrbnn_bake_sync_manifest.json to WorkDir.\n"
		"  -h, --help                        Print this help.\n");
}

bool IsKnownOptionWithValue(const std::string& Arg)
{
	return Arg == "--cloud-info-output-dir" ||
		Arg == "--cloud-info-resolution" ||
		Arg == "--cloud-info-density-scale" ||
		Arg == "--cloud-info-occupancy-threshold" ||
		Arg == "--cloud-shape-generator" ||
		Arg == "--cloud-shape-seed" ||
		Arg == "--cloud-shape-coverage" ||
		Arg == "--cloud-shape-base-height" ||
		Arg == "--cloud-shape-thickness" ||
		Arg == "--cloud-shape-edge-softness" ||
		Arg == "--cloud-shape-detail" ||
		Arg == "--cloud-shape-wind-shear";
}

bool ValidateCommandLineArguments(const std::vector<std::string>& Args, int& OutExitCode, bool& bOutShouldExit)
{
	OutExitCode = 0;
	bOutShouldExit = false;
	for (size_t Index = 1; Index < Args.size(); ++Index)
	{
		const std::string& Arg = Args[Index];
		if (Arg == "-h" || Arg == "--help")
		{
			PrintUsage();
			bOutShouldExit = true;
			return true;
		}

		if (Arg.rfind("--", 0) != 0)
		{
			continue;
		}

		if (Arg == "--bake-cloud-info" || Arg == "--no-workdir-sync-manifest-copy")
		{
			continue;
		}

		if (IsKnownOptionWithValue(Arg))
		{
			if (Index + 1 >= Args.size() || Args[Index + 1].rfind("--", 0) == 0)
			{
				std::fprintf(stderr, "Missing value for %s.\n\n", Arg.c_str());
				PrintUsage();
				OutExitCode = 2;
				bOutShouldExit = true;
				return false;
			}
			++Index;
			continue;
		}

		std::fprintf(stderr, "Unknown option: %s.\n\n", Arg.c_str());
		PrintUsage();
		OutExitCode = 2;
		bOutShouldExit = true;
		return false;
	}

	return true;
}

std::string UiLabel(const FConsoleState& State, const char* English, const char* Chinese, const char* Id)
{
	return std::string(Tr(State, English, Chinese)) + "##" + Id;
}

void DrawSectionTitle(const FConsoleState& State, const char* English, const char* Chinese)
{
	ImGui::Spacing();
	ImGui::TextColored(ImVec4(0.62f, 0.78f, 0.92f, 1.0f), "%s", Tr(State, English, Chinese));
	ImGui::Separator();
}

void DrawPathInput(const FConsoleState& State, const char* English, const char* Chinese, const char* Id, char* Buffer)
{
	ImGui::TextUnformatted(Tr(State, English, Chinese));
	ImGui::SetNextItemWidth(-1.0f);
	const std::string Label = std::string("##") + Id;
	ImGui::InputText(Label.c_str(), Buffer, PathBufferSize);
}

bool DrawPrimaryButton(const char* Label)
{
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.76f, 0.49f, 0.20f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.59f, 0.26f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.62f, 0.38f, 0.14f, 1.0f));
	const bool bClicked = ImGui::Button(Label, ImVec2(-1.0f, 34.0f));
	ImGui::PopStyleColor(3);
	return bClicked;
}

void DrawCloudGeneratorControls(FConsoleState& State)
{
	const char* GeneratorItemsEn[] = { "Source volume", "Cumulus core", "Anvil tower", "Layer bank", "Wispy streaks" };
	const char* GeneratorItemsZh[] = { "源体积", "积云核心", "砧状塔云", "层状云带", "风切云丝" };
	const char* const* GeneratorItems = State.Language == 1 ? GeneratorItemsZh : GeneratorItemsEn;
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::Combo(UiLabel(State, "Generator", "生成器", "CloudShapeGenerator").c_str(), &State.CloudShapeGenerator, GeneratorItems, IM_ARRAYSIZE(GeneratorItemsEn)))
	{
		ApplyCloudShapePreset(State, static_cast<ECloudShapeGenerator>(State.CloudShapeGenerator));
	}

	const float ButtonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
	if (ImGui::Button(Tr(State, "Cumulus", "积云"), ImVec2(ButtonWidth, 0.0f)))
	{
		ApplyCloudShapePreset(State, ECloudShapeGenerator::CumulusCore);
	}
	ImGui::SameLine();
	if (ImGui::Button(Tr(State, "Anvil", "砧云"), ImVec2(ButtonWidth, 0.0f)))
	{
		ApplyCloudShapePreset(State, ECloudShapeGenerator::AnvilTower);
	}
	ImGui::SameLine();
	if (ImGui::Button(Tr(State, "Layer", "层云"), ImVec2(ButtonWidth, 0.0f)))
	{
		ApplyCloudShapePreset(State, ECloudShapeGenerator::LayerBank);
	}
	if (ImGui::Button(Tr(State, "Wispy", "云丝"), ImVec2(ButtonWidth, 0.0f)))
	{
		ApplyCloudShapePreset(State, ECloudShapeGenerator::WispyStreaks);
	}
	ImGui::SameLine();
	if (ImGui::Button(Tr(State, "Source only", "仅源体积"), ImVec2(ButtonWidth, 0.0f)))
	{
		ApplyCloudShapePreset(State, ECloudShapeGenerator::SourceVolume);
	}

	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputInt(UiLabel(State, "Seed", "种子", "CloudShapeSeed").c_str(), &State.CloudShapeSeed);
	ImGui::SliderFloat(UiLabel(State, "Coverage", "覆盖率", "CloudShapeCoverage").c_str(), &State.CloudShapeCoverage, 0.0f, 1.0f, "%.2f");
	ImGui::SliderFloat(UiLabel(State, "Base height", "底部高度", "CloudShapeBaseHeight").c_str(), &State.CloudShapeBaseHeight, 0.0f, 1.0f, "%.2f");
	ImGui::SliderFloat(UiLabel(State, "Thickness", "厚度", "CloudShapeThickness").c_str(), &State.CloudShapeThickness, 0.02f, 1.0f, "%.2f");
	ImGui::SliderFloat(UiLabel(State, "Edge softness", "边缘柔化", "CloudShapeEdgeSoftness").c_str(), &State.CloudShapeEdgeSoftness, 0.01f, 0.5f, "%.2f");
	ImGui::SliderFloat(UiLabel(State, "Detail noise", "细节噪声", "CloudShapeDetail").c_str(), &State.CloudShapeDetail, 0.0f, 1.0f, "%.2f");
	ImGui::SliderFloat(UiLabel(State, "Wind shear", "风切变", "CloudShapeWindShear").c_str(), &State.CloudShapeWindShear, -0.75f, 0.75f, "%.2f");
}

void DrawTopBar(FConsoleState& State)
{
	ImGui::TextColored(ImVec4(0.94f, 0.96f, 0.98f, 1.0f), "%s", Tr(State, "MRBNN Bake Console", "MRBNN 云烘焙控制台"));
	ImGui::SameLine();
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 220.0f);
	const char* LanguageItems[] = { "English", "中文" };
	ImGui::SetNextItemWidth(190.0f);
	ImGui::Combo("##Language", &State.Language, LanguageItems, IM_ARRAYSIZE(LanguageItems));
	ImGui::TextColored(State.bCudaRuntimeCompatible ? ImVec4(0.42f, 0.84f, 0.72f, 1.0f) : ImVec4(1.0f, 0.72f, 0.28f, 1.0f),
		"%s: %s",
		Tr(State, "CUDA", "CUDA 状态"),
		State.bCudaRuntimeCompatible ? Tr(State, "ready", "可用") : State.CudaCompatibilityWarning.c_str());
	ImGui::Separator();
}

void DrawMainPanel(FConsoleState& State)
{
	ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(640.0f, 850.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin(Tr(State, "MRBNN Bake Console", "MRBNN 云烘焙控制台"));
	DrawTopBar(State);

	if (ImGui::BeginTabBar("MRBNNConsoleTabs", ImGuiTabBarFlags_Reorderable))
	{
		if (ImGui::BeginTabItem(Tr(State, "Setup", "设置")))
		{
			DrawSectionTitle(State, "Dataset", "数据集");
			DrawPathInput(State, "Work directory", "工作目录", "WorkDir", State.WorkDir);
			DrawPathInput(State, "Repository root", "仓库根目录", "RepositoryRoot", State.RepositoryRoot);
			DrawPathInput(State, "Plugin root", "插件根目录", "PluginRoot", State.PluginRoot);
			ImGui::TextUnformatted(Tr(State, "Scene name", "场景名称"));
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputText("##SceneName", State.SceneName, NameBufferSize);
			DrawPathInput(State, "Skybox HDRI", "天空盒 HDRI", "SkyboxHDRI", State.SkyboxHDRI);
			DrawPathInput(State, "Skybox baking directory", "天空盒烘焙目录", "SkyboxBakingDir", State.SkyboxBakingDir);
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(Tr(State, "Render", "渲染")))
		{
			DrawSectionTitle(State, "Frame", "帧设置");
			ImGui::InputInt(UiLabel(State, "Width", "宽度", "Width").c_str(), &State.Width);
			ImGui::InputInt(UiLabel(State, "Height", "高度", "Height").c_str(), &State.Height);
			ImGui::InputInt(UiLabel(State, "Samples", "采样数", "Samples").c_str(), &State.Samples);
			ImGui::InputInt(UiLabel(State, "Frame index", "帧序号", "FrameIndex").c_str(), &State.FrameIndex);
		State.Width = std::clamp(State.Width, 1, 4096);
		State.Height = std::clamp(State.Height, 1, 4096);
		State.Samples = std::clamp(State.Samples, 1, 4096);

			DrawSectionTitle(State, "Lighting and camera", "光照与相机");
			ImGui::DragFloat3(UiLabel(State, "Camera", "相机", "Camera").c_str(), State.Camera, 0.002f);
			ImGui::DragFloat3(UiLabel(State, "Light direction", "光照方向", "LightDirection").c_str(), State.LightDirection, 0.002f);
			ImGui::ColorEdit3(UiLabel(State, "Light color", "光照颜色", "LightColor").c_str(), State.LightColor);
			ImGui::ColorEdit3(UiLabel(State, "Albedo", "反照率", "Albedo").c_str(), State.Albedo);
			ImGui::SliderFloat(UiLabel(State, "Phase G", "相函数 G", "PhaseG").c_str(), &State.PhaseG, -0.95f, 0.95f);

		const char* ToneMappingItems[] = { "None", "Reinhard", "ACES" };
		const char* DenoiseItems[] = { "None", "Fast", "Visual Plausible" };
		const char* CompatibilityItems[] = { "Normal", "Compatibility" };
			ImGui::Combo(UiLabel(State, "Tone mapping", "色调映射", "ToneMapping").c_str(), &State.ToneMapping, ToneMappingItems, IM_ARRAYSIZE(ToneMappingItems));
			ImGui::Combo(UiLabel(State, "Denoise", "降噪", "Denoise").c_str(), &State.Denoise, DenoiseItems, IM_ARRAYSIZE(DenoiseItems));
			ImGui::Combo(UiLabel(State, "Compatibility", "兼容模式", "Compatibility").c_str(), &State.Compatibility, CompatibilityItems, IM_ARRAYSIZE(CompatibilityItems));
			ImGui::Checkbox(UiLabel(State, "Exclude light encoding", "排除光照编码", "ExcludeLightEncoding").c_str(), &State.bExcludeLightEncoding);
			ImGui::Checkbox(UiLabel(State, "Fast direct illumination", "快速直接光照", "FastDirectIllumination").c_str(), &State.bFastDirectIllumination);
			ImGui::Checkbox(UiLabel(State, "Enable skybox", "启用天空盒", "EnableSkybox").c_str(), &State.bEnableSkybox);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(160.0f);
			ImGui::DragFloat(UiLabel(State, "Exposure", "曝光", "SkyboxExposure").c_str(), &State.SkyboxExposure, 0.01f, 0.0f, 8.0f);
			ImGui::Checkbox(UiLabel(State, "Enable skybox baking", "启用天空盒烘焙", "EnableSkyboxBaking").c_str(), &State.bEnableSkyboxBaking);
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(Tr(State, "Cloud Bake / Sync", "云烘焙 / 同步")))
		{
			DrawSectionTitle(State, "Generator", "云形生成器");
			DrawCloudGeneratorControls(State);

			DrawSectionTitle(State, "Cloud info bake", "Cloud Info 烘焙");
			DrawPathInput(State, "Cloud info output directory", "Cloud Info 输出目录", "CloudInfoOutputDir", State.CloudInfoOutputDir);
			ImGui::InputInt(UiLabel(State, "Bake resolution", "烘焙分辨率", "CloudInfoBakeResolution").c_str(), &State.CloudInfoBakeResolution);
		State.CloudInfoBakeResolution = std::clamp(State.CloudInfoBakeResolution, 4, 256);
			ImGui::Checkbox(UiLabel(State, "Auto density scale", "自动密度缩放", "AutoDensityScale").c_str(), &State.bCloudInfoAutoDensityScale);
		if (!State.bCloudInfoAutoDensityScale)
		{
				ImGui::DragFloat(UiLabel(State, "Density scale", "密度缩放", "DensityScale").c_str(), &State.CloudInfoDensityScale, 0.001f, 0.0f, 1000.0f, "%.6f");
		}
		else
		{
				ImGui::Text("%s: %.6f", Tr(State, "Density scale", "密度缩放"), State.CloudInfoDensityScale);
		}
			ImGui::SliderFloat(UiLabel(State, "Occupancy threshold", "占用阈值", "OccupancyThreshold").c_str(), &State.CloudInfoOccupancyThreshold, 0.0f, 1.0f, "%.4f");
			ImGui::Checkbox(UiLabel(State, "Copy sync manifest to Work Dir", "同步清单复制到工作目录", "CopySyncManifestToWorkDir").c_str(), &State.bCopySyncManifestToWorkDir);
		if (!State.bCudaRuntimeCompatible)
		{
			ImGui::BeginDisabled();
		}
			if (DrawPrimaryButton(Tr(State, "Bake Cloud Info and write sync manifest", "烘焙 Cloud Info 并写入同步清单")))
		{
			BakeCloudInfoVolume(State);
		}
		if (!State.bCudaRuntimeCompatible)
		{
			ImGui::EndDisabled();
		}
			ImGui::TextWrapped("%s", Tr(State,
				"Writes mrbnn_bake_sync_manifest.json next to the RGBA32F output. Channels: R raw density, G normalized density, B gradient, A occupancy.",
				"在 RGBA32F 输出旁写入 mrbnn_bake_sync_manifest.json。通道: R 原始密度, G 归一化密度, B 梯度, A 占用。"));
			if (State.LastBakeSyncManifestPath[0] != '\0')
			{
				ImGui::TextColored(ImVec4(0.42f, 0.84f, 0.72f, 1.0f), "%s", State.LastBakeSyncManifestPath);
			}
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(Tr(State, "Actions", "操作")))
		{
		if (!State.bCudaRuntimeCompatible)
		{
			ImGui::BeginDisabled();
		}

			if (ImGui::Button(State.Renderer ? Tr(State, "Reinitialize CUDA renderer", "重新初始化 CUDA 渲染器") : Tr(State, "Initialize CUDA renderer", "初始化 CUDA 渲染器")))
		{
			InitializeRenderer(State);
		}
		ImGui::SameLine();
			if (ImGui::Button(Tr(State, "Release renderer", "释放渲染器")))
		{
			DestroyRenderer(State);
			AddLog(State, "Renderer released.");
		}

			if (ImGui::Button(Tr(State, "Render 1 sample", "渲染 1 次采样")))
		{
			RenderSamples(State, 1);
		}
		ImGui::SameLine();
			if (ImGui::Button(Tr(State, "Render average", "渲染平均图")))
		{
			RenderSamples(State, State.Samples);
		}

		if (!State.bCudaRuntimeCompatible)
		{
			ImGui::EndDisabled();
		}

			DrawPathInput(State, "Output PPM", "输出 PPM", "OutputPPM", State.OutputPath);
			if (ImGui::Button(Tr(State, "Save PPM + RGBA32F", "保存 PPM + RGBA32F")))
		{
			SaveOutputs(State);
		}

			DrawSectionTitle(State, "Plugin package helper", "插件打包助手");
			ImGui::Checkbox(UiLabel(State, "Package skybox HDRI", "打包天空盒 HDRI", "PackageSkyboxHDRI").c_str(), &State.bIncludeSkyboxHDRIWhenPackaging);
		ImGui::SameLine();
			ImGui::Checkbox(UiLabel(State, "Package skybox baking", "打包天空盒烘焙", "PackageSkyboxBaking").c_str(), &State.bIncludeSkyboxBakingWhenPackaging);
			if (ImGui::Button(Tr(State, "Package current data to plugin Data", "打包当前数据到插件 Data")))
		{
			PackageDataToPlugin(State);
		}

			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::Separator();
	ImGui::TextWrapped("%s: %s", Tr(State, "Status", "状态"), State.Status.c_str());
	ImGui::End();
}

void DrawPreviewPanel(FConsoleState& State)
{
	ImGui::SetNextWindowPos(ImVec2(668.0f, 12.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(900.0f, 640.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("CUDA Preview");
	if (State.PreviewTexture != 0 && State.PreviewWidth > 0 && State.PreviewHeight > 0)
	{
		const ImVec2 Available = ImGui::GetContentRegionAvail();
		const float Aspect = static_cast<float>(State.PreviewWidth) / static_cast<float>(State.PreviewHeight);
		ImVec2 ImageSize = Available;
		if (ImageSize.x / std::max(ImageSize.y, 1.0f) > Aspect)
		{
			ImageSize.x = ImageSize.y * Aspect;
		}
		else
		{
			ImageSize.y = ImageSize.x / Aspect;
		}
		ImGui::Image((ImTextureID)(intptr_t)State.PreviewTexture, ImageSize);
	}
	else
	{
		ImGui::TextWrapped("Render a sample to create the preview texture.");
	}
	ImGui::End();
}

void DrawLogPanel(FConsoleState& State)
{
	ImGui::SetNextWindowPos(ImVec2(668.0f, 668.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(900.0f, 220.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin(Tr(State, "Log", "日志"));
	if (ImGui::Button(Tr(State, "Clear", "清空")))
	{
		State.Log.clear();
	}
	ImGui::Separator();
	for (const std::string& Line : State.Log)
	{
		ImGui::TextWrapped("%s", Line.c_str());
	}
	ImGui::End();
}

void ApplyConsoleStyle()
{
	ImGui::StyleColorsDark();
	ImGuiStyle& Style = ImGui::GetStyle();
	Style.WindowRounding = 5.0f;
	Style.FrameRounding = 3.0f;
	Style.GrabRounding = 3.0f;
	Style.TabRounding = 4.0f;
	Style.WindowPadding = ImVec2(14.0f, 12.0f);
	Style.FramePadding = ImVec2(9.0f, 6.0f);
	Style.ItemSpacing = ImVec2(9.0f, 8.0f);
	Style.ItemInnerSpacing = ImVec2(8.0f, 5.0f);
	Style.ScrollbarSize = 14.0f;

	ImVec4* Colors = Style.Colors;
	Colors[ImGuiCol_WindowBg] = ImVec4(0.045f, 0.050f, 0.058f, 1.0f);
	Colors[ImGuiCol_ChildBg] = ImVec4(0.060f, 0.066f, 0.078f, 1.0f);
	Colors[ImGuiCol_FrameBg] = ImVec4(0.105f, 0.125f, 0.155f, 1.0f);
	Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.145f, 0.175f, 0.215f, 1.0f);
	Colors[ImGuiCol_FrameBgActive] = ImVec4(0.180f, 0.225f, 0.285f, 1.0f);
	Colors[ImGuiCol_TitleBg] = ImVec4(0.060f, 0.070f, 0.085f, 1.0f);
	Colors[ImGuiCol_TitleBgActive] = ImVec4(0.075f, 0.095f, 0.125f, 1.0f);
	Colors[ImGuiCol_Button] = ImVec4(0.145f, 0.205f, 0.285f, 1.0f);
	Colors[ImGuiCol_ButtonHovered] = ImVec4(0.195f, 0.275f, 0.365f, 1.0f);
	Colors[ImGuiCol_ButtonActive] = ImVec4(0.110f, 0.165f, 0.235f, 1.0f);
	Colors[ImGuiCol_Header] = ImVec4(0.145f, 0.205f, 0.285f, 1.0f);
	Colors[ImGuiCol_HeaderHovered] = ImVec4(0.195f, 0.275f, 0.365f, 1.0f);
	Colors[ImGuiCol_HeaderActive] = ImVec4(0.110f, 0.165f, 0.235f, 1.0f);
	Colors[ImGuiCol_Tab] = ImVec4(0.080f, 0.100f, 0.125f, 1.0f);
	Colors[ImGuiCol_TabHovered] = ImVec4(0.200f, 0.295f, 0.390f, 1.0f);
	Colors[ImGuiCol_TabActive] = ImVec4(0.145f, 0.205f, 0.285f, 1.0f);
	Colors[ImGuiCol_CheckMark] = ImVec4(0.42f, 0.84f, 0.72f, 1.0f);
	Colors[ImGuiCol_SliderGrab] = ImVec4(0.42f, 0.84f, 0.72f, 1.0f);
	Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.76f, 0.49f, 0.20f, 1.0f);
}

void ConfigureFonts(ImGuiIO& IO)
{
#if defined(_WIN32)
	const char* CandidateFonts[] = {
		"C:/Windows/Fonts/msyh.ttc",
		"C:/Windows/Fonts/simhei.ttf",
		"C:/Windows/Fonts/simsun.ttc"
	};
	for (const char* FontPath : CandidateFonts)
	{
		if (FileExists(MakePath(FontPath)))
		{
			IO.Fonts->AddFontFromFileTTF(FontPath, 16.0f, nullptr, IO.Fonts->GetGlyphRangesChineseSimplifiedCommon());
			return;
		}
	}
#endif
	IO.Fonts->AddFontDefault();
}

void GlfwErrorCallback(int Error, const char* Description)
{
	std::fprintf(stderr, "GLFW error %d: %s\n", Error, Description ? Description : "");
}

void CaptureFramebufferPPMIfRequested(int Width, int Height)
{
	const char* CapturePath = std::getenv("MRBNN_BAKE_CONSOLE_CAPTURE_PPM");
	if (!CapturePath || CapturePath[0] == '\0' || Width <= 0 || Height <= 0)
	{
		return;
	}

	std::vector<unsigned char> Pixels(static_cast<size_t>(Width) * static_cast<size_t>(Height) * 3);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, Width, Height, GL_RGB, GL_UNSIGNED_BYTE, Pixels.data());

	std::ofstream Output(CapturePath, std::ios::binary);
	if (!Output)
	{
		return;
	}

	Output << "P6\n" << Width << " " << Height << "\n255\n";
	for (int Y = Height - 1; Y >= 0; --Y)
	{
		const size_t Offset = static_cast<size_t>(Y) * static_cast<size_t>(Width) * 3;
		Output.write(reinterpret_cast<const char*>(Pixels.data() + Offset), static_cast<std::streamsize>(Width) * 3);
	}
}
}

int main(int Argc, char** Argv)
{
	FConsoleState State;
	const std::vector<std::string> Args = GetUtf8CommandLineArguments(Argc, Argv);
	int EarlyExitCode = 0;
	bool bShouldExitEarly = false;
	ValidateCommandLineArguments(Args, EarlyExitCode, bShouldExitEarly);
	if (bShouldExitEarly)
	{
		return EarlyExitCode;
	}

	SetDefaultPaths(State, Args);
	const bool bBakeCloudInfoAndExit = ApplyCommandLineOptions(State, Args);
	CheckCudaRuntimeCompatibility(State);
	if (bBakeCloudInfoAndExit)
	{
		return BakeCloudInfoVolume(State) ? 0 : 1;
	}

	glfwSetErrorCallback(GlfwErrorCallback);
	if (!glfwInit())
	{
		return 1;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	GLFWwindow* Window = glfwCreateWindow(1600, 920, "MRBNN Bake Console / MRBNN Cloud Bake", nullptr, nullptr);
	if (!Window)
	{
		glfwTerminate();
		return 1;
	}

	glfwMakeContextCurrent(Window);
	glfwSwapInterval(1);
	if (!gladLoadGL(glfwGetProcAddress))
	{
		glfwDestroyWindow(Window);
		glfwTerminate();
		return 1;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& IO = ImGui::GetIO();
	IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ConfigureFonts(IO);
	ApplyConsoleStyle();
	ImGui_ImplGlfw_InitForOpenGL(Window, true);
	ImGui_ImplOpenGL3_Init("#version 430");

	AddLog(State, "Ready.");
	bool bCapturedFirstFrame = false;
	int FrameCounter = 0;
	while (!glfwWindowShouldClose(Window))
	{
		glfwPollEvents();
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		DrawMainPanel(State);
		DrawPreviewPanel(State);
		DrawLogPanel(State);

		ImGui::Render();
		int DisplayWidth = 0;
		int DisplayHeight = 0;
		glfwGetFramebufferSize(Window, &DisplayWidth, &DisplayHeight);
		glViewport(0, 0, DisplayWidth, DisplayHeight);
		glClearColor(0.05f, 0.055f, 0.065f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		++FrameCounter;
		if (!bCapturedFirstFrame && FrameCounter >= 5)
		{
			CaptureFramebufferPPMIfRequested(DisplayWidth, DisplayHeight);
			bCapturedFirstFrame = true;
		}
		glfwSwapBuffers(Window);
	}

	DestroyRenderer(State);
	if (State.PreviewTexture != 0)
	{
		glDeleteTextures(1, &State.PreviewTexture);
	}
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(Window);
	glfwTerminate();
	return 0;
}
