using System.IO;
using UnrealBuildTool;

public class MRBNN : ModuleRules
{
	public MRBNN(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"DeveloperSettings",
				"Engine"
			});

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"ImageWrapper",
				"Json",
				"JsonUtilities",
				"Projects",
				"RenderCore",
				"RHI"
			});

		RuntimeDependencies.Add("$(PluginDir)/Data/...", StagedFileType.NonUFS);

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			AddOptionalRuntimeDependenciesFromDirectory(Path.Combine(PluginDirectory, "Binaries", "ThirdParty", "MRBNNBridge", "Win64"));
		}
	}

	private void AddOptionalRuntimeDependenciesFromDirectory(string DirectoryPath)
	{
		if (!Directory.Exists(DirectoryPath))
		{
			return;
		}

		foreach (string SourcePath in Directory.EnumerateFiles(DirectoryPath, "*.dll", SearchOption.TopDirectoryOnly))
		{
			string FileName = Path.GetFileName(SourcePath);
			string StagedPath = Path.Combine("$(PluginDir)", "Binaries", "ThirdParty", "MRBNNBridge", "Win64", FileName);
			RuntimeDependencies.Add(StagedPath);
		}
	}
}
