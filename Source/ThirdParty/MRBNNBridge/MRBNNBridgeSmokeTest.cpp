#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
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
void PrintUsage()
{
	std::cerr << "Usage: MRBNNBridgeSmokeTest <work_dir> <repository_root> <output_ppm> [size] [samples]\n";
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

bool WritePPM(const std::filesystem::path& OutputPath, int Width, int Height, const std::vector<float>& Pixels)
{
	std::ofstream Out(OutputPath, std::ios::binary);
	if (!Out)
	{
		std::cerr << "Failed to open output image: " << OutputPath << "\n";
		return false;
	}

	Out << "P6\n" << Width << " " << Height << "\n255\n";
	for (int Index = 0; Index < Width * Height; ++Index)
	{
		const float* Pixel = Pixels.data() + Index * 4;
		const std::array<unsigned char, 3> RGB{ ToByte(Pixel[0]), ToByte(Pixel[1]), ToByte(Pixel[2]) };
		Out.write(reinterpret_cast<const char*>(RGB.data()), static_cast<std::streamsize>(RGB.size()));
	}

	return static_cast<bool>(Out);
}

void ApplySpatialDenoise(std::vector<float>& Pixels, int Width, int Height, int PassCount)
{
	if (PassCount <= 0 || Width < 3 || Height < 3)
	{
		return;
	}

	std::vector<float> Scratch(Pixels.size());
	for (int PassIndex = 0; PassIndex < PassCount; ++PassIndex)
	{
		Scratch = Pixels;
		for (int Y = 1; Y < Height - 1; ++Y)
		{
			for (int X = 1; X < Width - 1; ++X)
			{
				const int PixelIndex = Y * Width + X;
				for (int Channel = 0; Channel < 3; ++Channel)
				{
					const int Index = PixelIndex * 4 + Channel;
					Pixels[Index] =
						(Scratch[((Y - 1) * Width + X - 1) * 4 + Channel] +
						 Scratch[((Y - 1) * Width + X + 1) * 4 + Channel] +
						 Scratch[((Y + 1) * Width + X - 1) * 4 + Channel] +
						 Scratch[((Y + 1) * Width + X + 1) * 4 + Channel] +
						 (Scratch[((Y - 1) * Width + X) * 4 + Channel] +
						  Scratch[(Y * Width + X - 1) * 4 + Channel] +
						  Scratch[(Y * Width + X + 1) * 4 + Channel] +
						  Scratch[((Y + 1) * Width + X) * 4 + Channel]) * 2.0f +
						 Scratch[Index] * 4.0f) / 16.0f;
				}
			}
		}
	}
}
}

int main(int Argc, char** Argv)
{
	if (Argc < 4)
	{
		PrintUsage();
		return 2;
	}

	const std::filesystem::path WorkDir = Argv[1];
	const std::filesystem::path RepositoryRoot = Argv[2];
	const std::filesystem::path OutputPath = Argv[3];
	const int Size = Argc >= 5 ? std::max(1, std::atoi(Argv[4])) : 64;
	const int Samples = Argc >= 6 ? std::max(1, std::atoi(Argv[5])) : 64;

	char Error[4096] = {};
	void* Renderer = nullptr;
	if (!MRBNN_Create(WorkDir.string().c_str(), RepositoryRoot.string().c_str(), &Renderer, Error, static_cast<int>(sizeof(Error))))
	{
		std::cerr << "MRBNN_Create failed: " << Error << "\n";
		return 1;
	}

	bool bEnableSkyboxBaking = false;
	const std::filesystem::path SkyboxBakingDir = WorkDir / "skybox";
	if (std::filesystem::exists(SkyboxBakingDir))
	{
		if (!MRBNN_SetSkyboxBaking(Renderer, SkyboxBakingDir.string().c_str(), Error, static_cast<int>(sizeof(Error))))
		{
			std::cerr << "MRBNN_SetSkyboxBaking failed: " << Error << "\n";
			MRBNN_Destroy(Renderer);
			return 1;
		}

		bEnableSkyboxBaking = true;
		std::cout << "Using skybox baking directory: " << SkyboxBakingDir << "\n";
	}

	const std::size_t PixelFloatCount = static_cast<std::size_t>(Size) * Size * 4;
	std::vector<float> Pixels(PixelFloatCount, 0.0f);
	std::vector<float> FramePixels(PixelFloatCount);
	for (int SampleIndex = 0; SampleIndex < Samples; ++SampleIndex)
	{
		std::fill(FramePixels.begin(), FramePixels.end(), 0.0f);
		const int Ok = MRBNN_RenderRGBA32F(
			Renderer,
			Size,
			Size,
			SampleIndex,
			0.67085f,
			-0.03808f,
			-0.04856f,
			0.34281f,
			0.70711f,
			0.61845f,
			1.0f,
			1.0f,
			1.0f,
			1.0f / 1.001f,
			1.0f / 1.001f,
			1.0f / 1.001f,
			0.857f,
			2,
			2,
			0,
			1,
			0,
			0,
			bEnableSkyboxBaking ? 1 : 0,
			FramePixels.data(),
			static_cast<int>(FramePixels.size()),
			Error,
			static_cast<int>(sizeof(Error)));

		if (!Ok)
		{
			std::cerr << "MRBNN_RenderRGBA32F failed at sample " << SampleIndex << ": " << Error << "\n";
			MRBNN_Destroy(Renderer);
			return 1;
		}

		const float Weight = 1.0f / static_cast<float>(SampleIndex + 1);
		for (std::size_t Index = 0; Index < Pixels.size(); ++Index)
		{
			Pixels[Index] += (FramePixels[Index] - Pixels[Index]) * Weight;
		}

		if ((SampleIndex + 1) % 16 == 0 || SampleIndex + 1 == Samples)
		{
			std::cout << "Averaged " << (SampleIndex + 1) << "/" << Samples << " smoke-test samples\n";
		}
	}

	MRBNN_Destroy(Renderer);
	ApplySpatialDenoise(Pixels, Size, Size, 2);

	if (!WritePPM(OutputPath, Size, Size, Pixels))
	{
		return 1;
	}

	std::cout << "Wrote smoke-test image: " << OutputPath << "\n";
	return 0;
}
