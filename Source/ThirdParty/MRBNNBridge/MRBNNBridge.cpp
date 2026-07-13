#include "RenderInterface.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define MRBNNBRIDGE_API extern "C" __declspec(dllexport)
#else
#define MRBNNBRIDGE_API extern "C" __attribute__((visibility("default")))
#endif

namespace
{
struct FContext
{
	std::unique_ptr<RenderInterfaceWithTCNN> Renderer;
	EnvironmentMap Skybox;
	bool bHasSkybox = false;
	float* DeviceFrameBuffer = nullptr;
	std::size_t DeviceFloatCount = 0;

	~FContext()
	{
		if (DeviceFrameBuffer)
		{
			cudaFree(DeviceFrameBuffer);
			DeviceFrameBuffer = nullptr;
		}
	}
};

class FSkyboxRestoreGuard
{
public:
	FSkyboxRestoreGuard(FContext& InContext, RenderParameters& InParams, bool bInActive)
		: Context(InContext)
		, Params(InParams)
		, bActive(bInActive)
	{
	}

	~FSkyboxRestoreGuard()
	{
		if (bActive)
		{
			Context.Skybox = std::move(Params.skybox);
			Context.bHasSkybox = Context.Skybox.Valid();
		}
	}

private:
	FContext& Context;
	RenderParameters& Params;
	bool bActive = false;
};

void SetError(char* OutError, int ErrorCapacity, const std::string& Message)
{
	if (!OutError || ErrorCapacity <= 0)
	{
		return;
	}

	const int CopyCount = std::min<int>(static_cast<int>(Message.size()), ErrorCapacity - 1);
	std::memcpy(OutError, Message.data(), CopyCount);
	OutError[CopyCount] = '\0';
}

bool EnsureDeviceFrameBuffer(FContext& Context, std::size_t FloatCount, char* OutError, int ErrorCapacity)
{
	if (Context.DeviceFloatCount == FloatCount && Context.DeviceFrameBuffer)
	{
		return true;
	}

	if (Context.DeviceFrameBuffer)
	{
		cudaFree(Context.DeviceFrameBuffer);
		Context.DeviceFrameBuffer = nullptr;
		Context.DeviceFloatCount = 0;
	}

	if (cudaMalloc(&Context.DeviceFrameBuffer, FloatCount * sizeof(float)) != cudaSuccess)
	{
		SetError(OutError, ErrorCapacity, "cudaMalloc failed for MRBNN frame buffer.");
		return false;
	}

	Context.DeviceFloatCount = FloatCount;
	return true;
}

glm::vec3 MakeVec3(float X, float Y, float Z)
{
	return glm::vec3{ X, Y, Z };
}

bool FileExists(const std::filesystem::path& Path)
{
	std::error_code Error;
	return std::filesystem::is_regular_file(Path, Error);
}

std::string ToUtf8(const std::filesystem::path& Path)
{
	const auto U8 = Path.u8string();
	return std::string(reinterpret_cast<const char*>(U8.c_str()), U8.size());
}

std::filesystem::path GetBridgeDirectory()
{
#if defined(_WIN32)
	HMODULE Module = nullptr;
	if (!GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&GetBridgeDirectory),
			&Module))
	{
		return {};
	}

	wchar_t Buffer[MAX_PATH] = {};
	constexpr DWORD BufferLength = static_cast<DWORD>(sizeof(Buffer) / sizeof(Buffer[0]));
	const DWORD Length = GetModuleFileNameW(Module, Buffer, BufferLength);
	if (Length == 0 || Length >= BufferLength)
	{
		return {};
	}

	return std::filesystem::path(Buffer).parent_path();
#else
	return {};
#endif
}

void SetProcessEnvironment(const char* Name, const std::string& Value)
{
#if defined(_WIN32)
	_putenv_s(Name, Value.c_str());
#else
	setenv(Name, Value.c_str(), 1);
#endif
}

std::string GetProcessEnvironment(const char* Name)
{
	if (const char* Value = std::getenv(Name))
	{
		return Value;
	}
	return {};
}

void SetKernelPathIfAvailable(const char* RepositoryRootUtf8)
{
	if (!GetProcessEnvironment("MRBNN_NETWORK_KERNEL_PATH").empty())
	{
		return;
	}

	const std::filesystem::path BridgeKernelPath = GetBridgeDirectory() / "Network.kernel";
	if (FileExists(BridgeKernelPath))
	{
		SetProcessEnvironment("MRBNN_NETWORK_KERNEL_PATH", ToUtf8(BridgeKernelPath));
		return;
	}

	if (RepositoryRootUtf8 && *RepositoryRootUtf8)
	{
		const std::filesystem::path RepositoryKernelPath = std::filesystem::path(RepositoryRootUtf8) / "external" / "Network.kernel";
		if (FileExists(RepositoryKernelPath))
		{
			SetProcessEnvironment("MRBNN_NETWORK_KERNEL_PATH", ToUtf8(RepositoryKernelPath));
		}
	}
}

void SetRelativePathRoot(const char* RepositoryRootUtf8)
{
	if (!RepositoryRootUtf8 || !*RepositoryRootUtf8)
	{
		return;
	}

	SetProcessEnvironment("MRBNN_RELA_PATH_ROOT", RepositoryRootUtf8);
	SetKernelPathIfAvailable(RepositoryRootUtf8);
}
}

MRBNNBRIDGE_API int MRBNN_Create(const char* WorkDirUtf8, const char* RepositoryRootUtf8, void** OutHandle, char* OutError, int ErrorCapacity)
{
	if (!WorkDirUtf8 || !OutHandle)
	{
		SetError(OutError, ErrorCapacity, "Invalid arguments passed to MRBNN_Create.");
		return 0;
	}

	try
	{
		SetRelativePathRoot(RepositoryRootUtf8);
		auto Context = std::make_unique<FContext>();
		Context->Renderer = std::make_unique<RenderInterfaceWithTCNN>(std::filesystem::path(WorkDirUtf8));
		*OutHandle = Context.release();
		return 1;
	}
	catch (const std::exception& Ex)
	{
		SetError(OutError, ErrorCapacity, Ex.what());
		return 0;
	}
}

MRBNNBRIDGE_API void MRBNN_Destroy(void* Handle)
{
	delete static_cast<FContext*>(Handle);
}

MRBNNBRIDGE_API int MRBNN_SetSkybox(void* Handle, const char* HdriPathUtf8, float Exposure, char* OutError, int ErrorCapacity)
{
	if (!Handle || !HdriPathUtf8 || !*HdriPathUtf8)
	{
		SetError(OutError, ErrorCapacity, "Invalid arguments passed to MRBNN_SetSkybox.");
		return 0;
	}

	try
	{
		auto& Context = *static_cast<FContext*>(Handle);
		Context.Skybox = EnvironmentMap{ std::filesystem::path(HdriPathUtf8), Exposure };
		Context.bHasSkybox = true;
		return 1;
	}
	catch (const std::exception& Ex)
	{
		SetError(OutError, ErrorCapacity, Ex.what());
		return 0;
	}
}

MRBNNBRIDGE_API int MRBNN_SetSkyboxBaking(void* Handle, const char* WorkDirUtf8, char* OutError, int ErrorCapacity)
{
	if (!Handle || !WorkDirUtf8 || !*WorkDirUtf8)
	{
		SetError(OutError, ErrorCapacity, "Invalid arguments passed to MRBNN_SetSkyboxBaking.");
		return 0;
	}

	try
	{
		auto& Context = *static_cast<FContext*>(Handle);
		const std::filesystem::path WorkDir(WorkDirUtf8);
		Context.Renderer->EquipEnvBaking(&WorkDir);
		return 1;
	}
	catch (const std::exception& Ex)
	{
		SetError(OutError, ErrorCapacity, Ex.what());
		return 0;
	}
}

MRBNNBRIDGE_API int MRBNN_RenderRGBA32F(
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
	int ErrorCapacity)
{
	if (!Handle || !OutRGBA || Width <= 0 || Height <= 0)
	{
		SetError(OutError, ErrorCapacity, "Invalid arguments passed to MRBNN_RenderRGBA32F.");
		return 0;
	}

	const std::size_t PixelCount = static_cast<std::size_t>(Width) * Height;
	const std::size_t RgbaFloatCount = PixelCount * 4;
	const std::size_t RgbFloatCount = PixelCount * 3;
	if (OutFloatCount < static_cast<int>(RgbaFloatCount))
	{
		SetError(OutError, ErrorCapacity, "Output buffer is too small for MRBNN_RenderRGBA32F.");
		return 0;
	}

	auto& Context = *static_cast<FContext*>(Handle);
	if (!Context.Renderer)
	{
		SetError(OutError, ErrorCapacity, "MRBNN renderer is not initialized.");
		return 0;
	}

	try
	{
		if (!EnsureDeviceFrameBuffer(Context, RgbFloatCount, OutError, ErrorCapacity))
		{
			return 0;
		}

		RenderParameters Params;
		Params.frameBuffer.size = glm::u32vec2{ static_cast<unsigned int>(Width), static_cast<unsigned int>(Height) };
		Params.frameBuffer.bufferPtr = Context.DeviceFrameBuffer;
		Params.frameBuffer.channelNum = 3;
		Params.frameNum = FrameIndex;
		Params.cameraPos = MakeVec3(CameraX, CameraY, CameraZ);
		Params.lightDir = glm::normalize(MakeVec3(LightX, LightY, LightZ));
		Params.lightColor = MakeVec3(LightR, LightG, LightB);
		Params.albedo = MakeVec3(AlbedoR, AlbedoG, AlbedoB);
		Params.g = PhaseG;
		Params.toneMapping = static_cast<RenderParameters::ToneMapping>(ToneMapping);
		Params.denoise = static_cast<RenderParameters::Denoise>(Denoise);
		Params.compatibility = static_cast<RenderParameters::Compatibility>(Compatibility);
		Params.excludeLightEncoding = ExcludeLightEncoding != 0;
		Params.fastDirectIllum = FastDirectIllumination != 0;
		Params.enableSkybox = EnableSkybox != 0 && Context.bHasSkybox && Context.Skybox.Valid();
		Params.enableSkyboxBaking = EnableSkyboxBaking != 0;
		if (Params.enableSkybox)
		{
			Params.skybox = std::move(Context.Skybox);
			Context.bHasSkybox = false;
		}
		FSkyboxRestoreGuard SkyboxRestoreGuard(Context, Params, Params.enableSkybox);

		Context.Renderer->Render(Params);
		if (cudaDeviceSynchronize() != cudaSuccess)
		{
			SetError(OutError, ErrorCapacity, "cudaDeviceSynchronize failed after MRBNN render.");
			return 0;
		}

		std::vector<float> HostRGB(RgbFloatCount);
		if (cudaMemcpy(HostRGB.data(), Context.DeviceFrameBuffer, RgbFloatCount * sizeof(float), cudaMemcpyDeviceToHost) != cudaSuccess)
		{
			SetError(OutError, ErrorCapacity, "cudaMemcpy failed after MRBNN render.");
			return 0;
		}

		for (std::size_t Index = 0; Index < PixelCount; ++Index)
		{
			OutRGBA[Index * 4 + 0] = HostRGB[Index * 3 + 0];
			OutRGBA[Index * 4 + 1] = HostRGB[Index * 3 + 1];
			OutRGBA[Index * 4 + 2] = HostRGB[Index * 3 + 2];
			OutRGBA[Index * 4 + 3] = 1.0f;
		}

		return 1;
	}
	catch (const std::exception& Ex)
	{
		SetError(OutError, ErrorCapacity, Ex.what());
		return 0;
	}
}
