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

namespace
{
constexpr int PathBufferSize = 1024;
constexpr int NameBufferSize = 128;

struct FConsoleState
{
	char WorkDir[PathBufferSize] = {};
	char RepositoryRoot[PathBufferSize] = {};
	char PluginRoot[PathBufferSize] = {};
	char SceneName[NameBufferSize] = "cloud-03";
	char SkyboxHDRI[PathBufferSize] = {};
	char SkyboxBakingDir[PathBufferSize] = {};
	char OutputPath[PathBufferSize] = {};

	int Width = 768;
	int Height = 768;
	int Samples = 32;
	int FrameIndex = 0;
	int ToneMapping = 2;
	int Denoise = 2;
	int Compatibility = 0;
	bool bExcludeLightEncoding = true;
	bool bFastDirectIllumination = false;
	bool bEnableSkybox = false;
	bool bEnableSkyboxBaking = true;
	bool bIncludeSkyboxHDRIWhenPackaging = false;
	bool bIncludeSkyboxBakingWhenPackaging = true;

	float Camera[3] = { 0.67085f, -0.03808f, -0.04856f };
	float LightDirection[3] = { 0.34281f, 0.70711f, 0.61845f };
	float LightColor[3] = { 1.0f, 1.0f, 1.0f };
	float Albedo[3] = { 0.999f, 0.999f, 0.999f };
	float PhaseG = 0.857f;
	float SkyboxExposure = 1.0f;

	void* Renderer = nullptr;
	std::vector<float> Pixels;
	std::vector<float> FramePixels;
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
	std::filesystem::path WorkDir = Args.size() > 1 ? MakePath(Args[1].c_str()) : std::filesystem::path();
	std::filesystem::path RepositoryRoot = Args.size() > 2 ? MakePath(Args[2].c_str()) : std::filesystem::path();
	std::filesystem::path PluginRoot = Args.size() > 3 ? MakePath(Args[3].c_str()) : std::filesystem::path();

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
}

void DrawPathInput(const char* Label, char* Buffer)
{
	ImGui::InputText(Label, Buffer, PathBufferSize);
}

void DrawMainPanel(FConsoleState& State)
{
	ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(520.0f, 720.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("MRBNN Bake Console");
	ImGui::TextWrapped("CUDA reference renderer, UE packaging helper, and bake/debug control surface.");
	ImGui::Separator();

	if (ImGui::CollapsingHeader("Dataset", ImGuiTreeNodeFlags_DefaultOpen))
	{
		DrawPathInput("Work Dir", State.WorkDir);
		DrawPathInput("Repository Root", State.RepositoryRoot);
		DrawPathInput("Plugin Root", State.PluginRoot);
		ImGui::InputText("Scene Name", State.SceneName, NameBufferSize);
		DrawPathInput("Skybox HDRI", State.SkyboxHDRI);
		DrawPathInput("Skybox Baking Dir", State.SkyboxBakingDir);
	}

	if (ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::InputInt("Width", &State.Width);
		ImGui::InputInt("Height", &State.Height);
		ImGui::InputInt("Samples", &State.Samples);
		ImGui::InputInt("Frame Index", &State.FrameIndex);
		State.Width = std::clamp(State.Width, 1, 4096);
		State.Height = std::clamp(State.Height, 1, 4096);
		State.Samples = std::clamp(State.Samples, 1, 4096);

		ImGui::DragFloat3("Camera", State.Camera, 0.002f);
		ImGui::DragFloat3("Light Direction", State.LightDirection, 0.002f);
		ImGui::ColorEdit3("Light Color", State.LightColor);
		ImGui::ColorEdit3("Albedo", State.Albedo);
		ImGui::SliderFloat("Phase G", &State.PhaseG, -0.95f, 0.95f);

		const char* ToneMappingItems[] = { "None", "Reinhard", "ACES" };
		const char* DenoiseItems[] = { "None", "Fast", "Visual Plausible" };
		const char* CompatibilityItems[] = { "Normal", "Compatibility" };
		ImGui::Combo("Tone Mapping", &State.ToneMapping, ToneMappingItems, IM_ARRAYSIZE(ToneMappingItems));
		ImGui::Combo("Denoise", &State.Denoise, DenoiseItems, IM_ARRAYSIZE(DenoiseItems));
		ImGui::Combo("Compatibility", &State.Compatibility, CompatibilityItems, IM_ARRAYSIZE(CompatibilityItems));
		ImGui::Checkbox("Exclude Light Encoding", &State.bExcludeLightEncoding);
		ImGui::Checkbox("Fast Direct Illumination", &State.bFastDirectIllumination);
		ImGui::Checkbox("Enable Skybox", &State.bEnableSkybox);
		ImGui::SameLine();
		ImGui::DragFloat("Exposure", &State.SkyboxExposure, 0.01f, 0.0f, 8.0f);
		ImGui::Checkbox("Enable Skybox Baking", &State.bEnableSkyboxBaking);
	}

	if (ImGui::CollapsingHeader("Actions", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (!State.bCudaRuntimeCompatible)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.28f, 1.0f), "%s", State.CudaCompatibilityWarning.c_str());
		}

		if (!State.bCudaRuntimeCompatible)
		{
			ImGui::BeginDisabled();
		}

		if (ImGui::Button(State.Renderer ? "Reinitialize CUDA Renderer" : "Initialize CUDA Renderer"))
		{
			InitializeRenderer(State);
		}
		ImGui::SameLine();
		if (ImGui::Button("Release Renderer"))
		{
			DestroyRenderer(State);
			AddLog(State, "Renderer released.");
		}

		if (ImGui::Button("Render 1 Sample"))
		{
			RenderSamples(State, 1);
		}
		ImGui::SameLine();
		if (ImGui::Button("Render Average"))
		{
			RenderSamples(State, State.Samples);
		}

		if (!State.bCudaRuntimeCompatible)
		{
			ImGui::EndDisabled();
		}

		DrawPathInput("Output PPM", State.OutputPath);
		if (ImGui::Button("Save PPM + RGBA32F"))
		{
			SaveOutputs(State);
		}

		ImGui::Checkbox("Package Skybox HDRI", &State.bIncludeSkyboxHDRIWhenPackaging);
		ImGui::SameLine();
		ImGui::Checkbox("Package Skybox Baking", &State.bIncludeSkyboxBakingWhenPackaging);
		if (ImGui::Button("Package Current Data To Plugin Data"))
		{
			PackageDataToPlugin(State);
		}
	}

	ImGui::Separator();
	ImGui::TextWrapped("Status: %s", State.Status.c_str());
	ImGui::End();
}

void DrawPreviewPanel(FConsoleState& State)
{
	ImGui::SetNextWindowPos(ImVec2(548.0f, 12.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(880.0f, 640.0f), ImGuiCond_FirstUseEver);
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
	ImGui::SetNextWindowPos(ImVec2(548.0f, 668.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(880.0f, 220.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Log");
	if (ImGui::Button("Clear"))
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

void GlfwErrorCallback(int Error, const char* Description)
{
	std::fprintf(stderr, "GLFW error %d: %s\n", Error, Description ? Description : "");
}
}

int main(int Argc, char** Argv)
{
	FConsoleState State;
	SetDefaultPaths(State, GetUtf8CommandLineArguments(Argc, Argv));
	CheckCudaRuntimeCompatibility(State);

	glfwSetErrorCallback(GlfwErrorCallback);
	if (!glfwInit())
	{
		return 1;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	GLFWwindow* Window = glfwCreateWindow(1440, 900, "MRBNN Bake Console", nullptr, nullptr);
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
	ImGui::StyleColorsDark();
	ImGui_ImplGlfw_InitForOpenGL(Window, true);
	ImGui_ImplOpenGL3_Init("#version 430");

	AddLog(State, "Ready.");
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
