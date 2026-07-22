#include <cuda_runtime_api.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>

namespace
{
void SetError(char* OutError, int ErrorCapacity, const std::string& Message)
{
	if (!OutError || ErrorCapacity <= 0)
	{
		return;
	}

	const int CopyCount = std::min<int>(static_cast<int>(Message.size()), ErrorCapacity - 1);
	std::memcpy(OutError, Message.data(), static_cast<std::size_t>(CopyCount));
	OutError[CopyCount] = '\0';
}

const char* CudaErrorText(cudaError_t Error)
{
	return cudaGetErrorString(Error);
}

__device__ int ClampInt(int Value, int Min, int Max)
{
	return min(max(Value, Min), Max);
}

__device__ float ReadDensity(const float* Density, int SizeX, int SizeY, int SizeZ, int X, int Y, int Z)
{
	X = ClampInt(X, 0, SizeX - 1);
	Y = ClampInt(Y, 0, SizeY - 1);
	Z = ClampInt(Z, 0, SizeZ - 1);
	const std::size_t Index =
		(static_cast<std::size_t>(Z) * static_cast<std::size_t>(SizeY) + static_cast<std::size_t>(Y)) *
			static_cast<std::size_t>(SizeX) +
		static_cast<std::size_t>(X);
	return Density[Index];
}

__device__ float Lerp(float A, float B, float Alpha)
{
	return A + (B - A) * Alpha;
}

__device__ float SampleDensityTrilinear(const float* Density, int SizeX, int SizeY, int SizeZ, float X, float Y, float Z)
{
	const int X0 = static_cast<int>(floorf(X));
	const int Y0 = static_cast<int>(floorf(Y));
	const int Z0 = static_cast<int>(floorf(Z));
	const int X1 = X0 + 1;
	const int Y1 = Y0 + 1;
	const int Z1 = Z0 + 1;
	const float Fx = X - static_cast<float>(X0);
	const float Fy = Y - static_cast<float>(Y0);
	const float Fz = Z - static_cast<float>(Z0);

	const float C000 = ReadDensity(Density, SizeX, SizeY, SizeZ, X0, Y0, Z0);
	const float C100 = ReadDensity(Density, SizeX, SizeY, SizeZ, X1, Y0, Z0);
	const float C010 = ReadDensity(Density, SizeX, SizeY, SizeZ, X0, Y1, Z0);
	const float C110 = ReadDensity(Density, SizeX, SizeY, SizeZ, X1, Y1, Z0);
	const float C001 = ReadDensity(Density, SizeX, SizeY, SizeZ, X0, Y0, Z1);
	const float C101 = ReadDensity(Density, SizeX, SizeY, SizeZ, X1, Y0, Z1);
	const float C011 = ReadDensity(Density, SizeX, SizeY, SizeZ, X0, Y1, Z1);
	const float C111 = ReadDensity(Density, SizeX, SizeY, SizeZ, X1, Y1, Z1);

	const float C00 = Lerp(C000, C100, Fx);
	const float C10 = Lerp(C010, C110, Fx);
	const float C01 = Lerp(C001, C101, Fx);
	const float C11 = Lerp(C011, C111, Fx);
	const float C0 = Lerp(C00, C10, Fy);
	const float C1 = Lerp(C01, C11, Fy);
	return Lerp(C0, C1, Fz);
}

__global__ void BakeCloudInfoKernel(
	const float* SourceDensity,
	int SourceX,
	int SourceY,
	int SourceZ,
	int BakeX,
	int BakeY,
	int BakeZ,
	float DensityScale,
	float OccupancyThreshold,
	float* OutRGBA)
{
	const std::size_t LinearIndex = static_cast<std::size_t>(blockIdx.x) * static_cast<std::size_t>(blockDim.x) +
		static_cast<std::size_t>(threadIdx.x);
	const std::size_t TotalVoxels = static_cast<std::size_t>(BakeX) * static_cast<std::size_t>(BakeY) * static_cast<std::size_t>(BakeZ);
	if (LinearIndex >= TotalVoxels)
	{
		return;
	}

	const int X = static_cast<int>(LinearIndex % static_cast<std::size_t>(BakeX));
	const int Y = static_cast<int>((LinearIndex / static_cast<std::size_t>(BakeX)) % static_cast<std::size_t>(BakeY));
	const int Z = static_cast<int>(LinearIndex / (static_cast<std::size_t>(BakeX) * static_cast<std::size_t>(BakeY)));

	const float SourceMaxX = static_cast<float>(max(SourceX - 1, 0));
	const float SourceMaxY = static_cast<float>(max(SourceY - 1, 0));
	const float SourceMaxZ = static_cast<float>(max(SourceZ - 1, 0));
	const float SourcePosX = BakeX <= 1 ? 0.0f : (static_cast<float>(X) / static_cast<float>(BakeX - 1)) * SourceMaxX;
	const float SourcePosY = BakeY <= 1 ? 0.0f : (static_cast<float>(Y) / static_cast<float>(BakeY - 1)) * SourceMaxY;
	const float SourcePosZ = BakeZ <= 1 ? 0.0f : (static_cast<float>(Z) / static_cast<float>(BakeZ - 1)) * SourceMaxZ;

	const float Density = SampleDensityTrilinear(SourceDensity, SourceX, SourceY, SourceZ, SourcePosX, SourcePosY, SourcePosZ);
	const float DensityNormalized = fminf(fmaxf(Density * DensityScale, 0.0f), 1.0f);

	const float Dx =
		SampleDensityTrilinear(SourceDensity, SourceX, SourceY, SourceZ, SourcePosX + 1.0f, SourcePosY, SourcePosZ) -
		SampleDensityTrilinear(SourceDensity, SourceX, SourceY, SourceZ, SourcePosX - 1.0f, SourcePosY, SourcePosZ);
	const float Dy =
		SampleDensityTrilinear(SourceDensity, SourceX, SourceY, SourceZ, SourcePosX, SourcePosY + 1.0f, SourcePosZ) -
		SampleDensityTrilinear(SourceDensity, SourceX, SourceY, SourceZ, SourcePosX, SourcePosY - 1.0f, SourcePosZ);
	const float Dz =
		SampleDensityTrilinear(SourceDensity, SourceX, SourceY, SourceZ, SourcePosX, SourcePosY, SourcePosZ + 1.0f) -
		SampleDensityTrilinear(SourceDensity, SourceX, SourceY, SourceZ, SourcePosX, SourcePosY, SourcePosZ - 1.0f);
	const float GradientMagnitude = fminf(sqrtf(Dx * Dx + Dy * Dy + Dz * Dz) * 0.5f * DensityScale, 1.0f);

	const std::size_t OutIndex = LinearIndex * 4;
	OutRGBA[OutIndex + 0] = Density;
	OutRGBA[OutIndex + 1] = DensityNormalized;
	OutRGBA[OutIndex + 2] = GradientMagnitude;
	OutRGBA[OutIndex + 3] = DensityNormalized >= OccupancyThreshold ? 1.0f : 0.0f;
}
}

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
	int ErrorCapacity)
{
	if (!HostDensity || !HostRGBA || SourceX <= 0 || SourceY <= 0 || SourceZ <= 0 || BakeX <= 0 || BakeY <= 0 || BakeZ <= 0)
	{
		SetError(OutError, ErrorCapacity, "Invalid cloud-info bake arguments.");
		return 0;
	}

	const std::size_t SourceVoxelCount =
		static_cast<std::size_t>(SourceX) * static_cast<std::size_t>(SourceY) * static_cast<std::size_t>(SourceZ);
	const std::size_t BakeVoxelCount =
		static_cast<std::size_t>(BakeX) * static_cast<std::size_t>(BakeY) * static_cast<std::size_t>(BakeZ);
	const std::size_t RequiredFloatCount = BakeVoxelCount * 4;
	if (OutFloatCount < 0 || static_cast<std::size_t>(OutFloatCount) < RequiredFloatCount)
	{
		SetError(OutError, ErrorCapacity, "Cloud-info bake output buffer is too small.");
		return 0;
	}

	float* DeviceDensity = nullptr;
	float* DeviceRGBA = nullptr;
	cudaError_t Result = cudaMalloc(reinterpret_cast<void**>(&DeviceDensity), SourceVoxelCount * sizeof(float));
	if (Result != cudaSuccess)
	{
		SetError(OutError, ErrorCapacity, std::string("cudaMalloc failed for source density: ") + CudaErrorText(Result));
		return 0;
	}

	Result = cudaMalloc(reinterpret_cast<void**>(&DeviceRGBA), RequiredFloatCount * sizeof(float));
	if (Result != cudaSuccess)
	{
		cudaFree(DeviceDensity);
		SetError(OutError, ErrorCapacity, std::string("cudaMalloc failed for cloud-info output: ") + CudaErrorText(Result));
		return 0;
	}

	Result = cudaMemcpy(DeviceDensity, HostDensity, SourceVoxelCount * sizeof(float), cudaMemcpyHostToDevice);
	if (Result != cudaSuccess)
	{
		cudaFree(DeviceRGBA);
		cudaFree(DeviceDensity);
		SetError(OutError, ErrorCapacity, std::string("cudaMemcpy failed for source density: ") + CudaErrorText(Result));
		return 0;
	}

	const int ThreadsPerBlock = 256;
	const int Blocks = static_cast<int>((BakeVoxelCount + static_cast<std::size_t>(ThreadsPerBlock) - 1) / static_cast<std::size_t>(ThreadsPerBlock));
	BakeCloudInfoKernel<<<Blocks, ThreadsPerBlock>>>(
		DeviceDensity,
		SourceX,
		SourceY,
		SourceZ,
		BakeX,
		BakeY,
		BakeZ,
		DensityScale,
		OccupancyThreshold,
		DeviceRGBA);

	Result = cudaGetLastError();
	if (Result == cudaSuccess)
	{
		Result = cudaDeviceSynchronize();
	}
	if (Result != cudaSuccess)
	{
		cudaFree(DeviceRGBA);
		cudaFree(DeviceDensity);
		SetError(OutError, ErrorCapacity, std::string("CUDA cloud-info bake kernel failed: ") + CudaErrorText(Result));
		return 0;
	}

	Result = cudaMemcpy(HostRGBA, DeviceRGBA, RequiredFloatCount * sizeof(float), cudaMemcpyDeviceToHost);
	cudaFree(DeviceRGBA);
	cudaFree(DeviceDensity);
	if (Result != cudaSuccess)
	{
		SetError(OutError, ErrorCapacity, std::string("cudaMemcpy failed for cloud-info output: ") + CudaErrorText(Result));
		return 0;
	}

	return 1;
}
