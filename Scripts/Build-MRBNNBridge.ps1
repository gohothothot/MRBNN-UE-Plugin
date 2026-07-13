param(
    [string]$MRBNNRoot = "",
    [string]$PluginRoot = "",
    [string]$BuildDir = "",
    [string]$CudaRuntimeDir = "",
    [string]$Configuration = "Release",
    [int]$Parallel = 16,
    [string]$CudaArchitectures = "native",
    [switch]$InstallCudaIfMissing,
    [switch]$PersistUserEnvironment,
    [switch]$RunSmokeTest,
    [string]$SmokeTestWorkDir = "",
    [int]$SmokeTestSize = 1024,
    [int]$SmokeTestSamples = 256,
    [switch]$BuildBakeConsole,
    [switch]$RunBakeConsole,
    [string]$BakeConsoleWorkDir = ""
)

$ErrorActionPreference = "Stop"

function Resolve-FullPath([string]$Path) {
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
}

function Resolve-CudaArchitectures([string]$RequestedArchitectures) {
    if ($RequestedArchitectures -and $RequestedArchitectures -ne "native") {
        return $RequestedArchitectures
    }

    $DetectedArchitecture = ""

    try {
        $ComputeCapability = (& nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>$null | Select-Object -First 1).Trim()
        if ($ComputeCapability -match '^(?<Major>\d+)\.(?<Minor>\d+)$') {
            $DetectedArchitecture = "$($Matches.Major)$($Matches.Minor)"
        }
    } catch {
    }

    if (-not $DetectedArchitecture) {
        $DeviceQuery = Join-Path $env:CUDA_PATH "bin\__nvcc_device_query.exe"
        if (Test-Path $DeviceQuery) {
            try {
                $DeviceQueryOutput = (& $DeviceQuery 2>$null | Select-Object -First 1).Trim()
                if ($DeviceQueryOutput -match '^\d+$') {
                    $DetectedArchitecture = $DeviceQueryOutput
                }
            } catch {
            }
        }
    }

    if ($DetectedArchitecture) {
        Write-Host "Resolved CUDA architectures to $DetectedArchitecture"
        return $DetectedArchitecture
    }

    return "native"
}

function Get-InstalledCudaToolkitVersion() {
    if ($env:CUDA_PATH) {
        $VersionJsonPath = Join-Path $env:CUDA_PATH "version.json"
        if (Test-Path $VersionJsonPath) {
            try {
                $VersionJson = Get-Content -LiteralPath $VersionJsonPath -Raw | ConvertFrom-Json
                if ($VersionJson.cuda.version -match '^(?<Major>\d+)\.(?<Minor>\d+)') {
                    return [version]"$($Matches.Major).$($Matches.Minor)"
                }
            } catch {
            }
        }
    }

    try {
        $NvccVersion = & nvcc --version 2>$null
        foreach ($Line in $NvccVersion) {
            if ($Line -match 'release (?<Major>\d+)\.(?<Minor>\d+)') {
                return [version]"$($Matches.Major).$($Matches.Minor)"
            }
        }
    } catch {
    }

    return $null
}

function Get-DriverCudaVersion() {
    try {
        $Output = & nvidia-smi 2>$null
        foreach ($Line in $Output) {
            if ($Line -match 'CUDA Version:\s*(?<Major>\d+)\.(?<Minor>\d+)') {
                return [version]"$($Matches.Major).$($Matches.Minor)"
            }
        }
    } catch {
    }

    return $null
}

function Get-CudaCompatibilityWarning() {
    $ToolkitVersion = Get-InstalledCudaToolkitVersion
    $DriverCudaVersion = Get-DriverCudaVersion
    if (-not $ToolkitVersion -or -not $DriverCudaVersion) {
        return ""
    }

    if ($ToolkitVersion -gt $DriverCudaVersion) {
        return "Installed CUDA toolkit/runtime is $ToolkitVersion but the NVIDIA driver reports CUDA $DriverCudaVersion. MRBNN's TCNN/NVRTC runtime render can fail with CUDA_ERROR_UNSUPPORTED_PTX_VERSION. Install a CUDA toolkit/runtime no newer than the driver support, or update the NVIDIA driver."
    }

    return ""
}

function Find-DefaultMRBNNRoot([string]$PluginRootPath) {
    $Current = Resolve-FullPath $PluginRootPath
    while ($Current) {
        $Candidate = Join-Path $Current "third_party_refs/MRBNN"
        if (Test-Path (Join-Path $Candidate "CMakeLists.txt")) {
            return $Candidate
        }

        $Parent = Split-Path -Parent $Current
        if (-not $Parent -or $Parent -eq $Current) {
            break
        }
        $Current = $Parent
    }

    return Join-Path (Split-Path -Parent $PluginRootPath) "third_party_refs/MRBNN"
}

function Copy-MatchingFiles([string]$SourceDirectory, [string[]]$Patterns, [string[]]$Destinations) {
    if (-not $SourceDirectory -or -not (Test-Path $SourceDirectory)) {
        return
    }

    foreach ($Pattern in $Patterns) {
        foreach ($File in Get-ChildItem $SourceDirectory -Filter $Pattern -File -ErrorAction SilentlyContinue) {
            foreach ($Destination in $Destinations | Where-Object { $_ }) {
                New-Item -ItemType Directory -Force -Path $Destination | Out-Null
                Copy-Item -LiteralPath $File.FullName -Destination (Join-Path $Destination $File.Name) -Force
            }
        }
    }
}

function Convert-PpmToPng([string]$PpmPath, [string]$PngPath) {
    $Magick = Get-Command magick -ErrorAction SilentlyContinue
    if ($Magick) {
        & $Magick.Source $PpmPath $PngPath
        if ($LASTEXITCODE -eq 0 -and (Test-Path $PngPath)) {
            return
        }
    }

    Add-Type -AssemblyName System.Drawing

    $Bytes = [System.IO.File]::ReadAllBytes($PpmPath)
    $Position = 0

    function Read-PpmToken([byte[]]$Data, [ref]$PositionRef) {
        while ($PositionRef.Value -lt $Data.Length) {
            $Byte = $Data[$PositionRef.Value]
            if ($Byte -eq 35) {
                while ($PositionRef.Value -lt $Data.Length -and $Data[$PositionRef.Value] -ne 10) {
                    $PositionRef.Value++
                }
                continue
            }
            if ($Byte -gt 32) {
                break
            }
            $PositionRef.Value++
        }

        $Start = $PositionRef.Value
        while ($PositionRef.Value -lt $Data.Length -and $Data[$PositionRef.Value] -gt 32) {
            $PositionRef.Value++
        }

        $Token = [System.Text.Encoding]::ASCII.GetString($Data, $Start, $PositionRef.Value - $Start)
        if ($PositionRef.Value -lt $Data.Length -and $Data[$PositionRef.Value] -le 32) {
            $PositionRef.Value++
        }
        return $Token
    }

    $Magic = Read-PpmToken $Bytes ([ref]$Position)
    if ($Magic -ne "P6") {
        throw "Unsupported PPM format in $PpmPath. Expected P6, got $Magic."
    }

    $Width = [int](Read-PpmToken $Bytes ([ref]$Position))
    $Height = [int](Read-PpmToken $Bytes ([ref]$Position))
    $MaxValue = [int](Read-PpmToken $Bytes ([ref]$Position))
    if ($MaxValue -ne 255) {
        throw "Unsupported PPM max value in $PpmPath. Expected 255, got $MaxValue."
    }

    $ExpectedBytes = $Width * $Height * 3
    if ($Bytes.Length - $Position -lt $ExpectedBytes) {
        throw "PPM pixel data is truncated: $PpmPath"
    }

    $Bitmap = New-Object System.Drawing.Bitmap($Width, $Height, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    try {
        $DataIndex = $Position
        for ($Y = 0; $Y -lt $Height; $Y++) {
            for ($X = 0; $X -lt $Width; $X++) {
                $R = $Bytes[$DataIndex]
                $G = $Bytes[$DataIndex + 1]
                $B = $Bytes[$DataIndex + 2]
                $DataIndex += 3
                $Bitmap.SetPixel($X, $Y, [System.Drawing.Color]::FromArgb($R, $G, $B))
            }
        }

        $Bitmap.Save($PngPath, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $Bitmap.Dispose()
    }
}

$SetupScript = Join-Path $PSScriptRoot "Setup-MRBNNBuildEnv.ps1"
if (-not (Test-Path $SetupScript)) {
    throw "Setup script is missing: $SetupScript"
}

. $SetupScript -InstallCudaIfMissing:$InstallCudaIfMissing -PersistUserEnvironment:$PersistUserEnvironment -Quiet | Out-Null

if (-not $PluginRoot) {
    $PluginRoot = Split-Path -Parent $PSScriptRoot
}

$PluginRoot = Resolve-FullPath $PluginRoot

if (-not $MRBNNRoot) {
    $MRBNNRoot = Find-DefaultMRBNNRoot $PluginRoot
}

$MRBNNRoot = Resolve-FullPath $MRBNNRoot
$BridgeSource = Join-Path $PluginRoot "Source/ThirdParty/MRBNNBridge"
if (-not $BuildDir) {
    $BuildDir = Join-Path $PluginRoot "Intermediate/MRBNNBridge"
}
$BuildDir = Resolve-FullPath $BuildDir
$DeployDir = Join-Path $PluginRoot "Binaries/ThirdParty/MRBNNBridge/Win64"

if (-not (Test-Path (Join-Path $MRBNNRoot "CMakeLists.txt"))) {
    throw "MRBNNRoot does not look like the Extra-Creativity/MRBNN repository: $MRBNNRoot"
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake was not found on PATH."
}

if (-not (Get-Command nvcc -ErrorAction SilentlyContinue)) {
    throw "nvcc was not found on PATH after environment setup. Run Setup-MRBNNBuildEnv.ps1 first or rerun this script with -InstallCudaIfMissing."
}

$CudaCompatibilityWarning = Get-CudaCompatibilityWarning
if ($CudaCompatibilityWarning) {
    Write-Warning $CudaCompatibilityWarning
    if ($RunSmokeTest) {
        throw "Smoke test was not run because the installed CUDA toolkit/runtime is newer than the NVIDIA driver-supported CUDA version."
    }
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $DeployDir | Out-Null

$ResolvedCudaArchitectures = Resolve-CudaArchitectures $CudaArchitectures
$CMakeBuildBakeConsole = if ($BuildBakeConsole -or $RunBakeConsole) { "ON" } else { "OFF" }
$ConfigureArgs = @(
    "-S", $BridgeSource,
    "-B", $BuildDir,
    "-DMRBNN_ROOT=$MRBNNRoot",
    "-DCMAKE_CUDA_ARCHITECTURES=$ResolvedCudaArchitectures",
    "-DTCNN_CUDA_ARCHITECTURES=$ResolvedCudaArchitectures",
    "-DMRBNN_BUILD_BAKE_CONSOLE=$CMakeBuildBakeConsole",
    "-DCMAKE_BUILD_TYPE=$Configuration"
)

& cmake @ConfigureArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE."
}

$BuildTargets = @("MRBNNBridge")
if ($RunSmokeTest) {
    $BuildTargets += "MRBNNBridgeSmokeTest"
}
if ($BuildBakeConsole -or $RunBakeConsole) {
    $BuildTargets += "MRBNNBakeConsole"
}
$BuildTargets = $BuildTargets | Select-Object -Unique

foreach ($BuildTarget in $BuildTargets) {
    $BuildArgs = @(
        "--build", $BuildDir,
        "--config", $Configuration,
        "--target", $BuildTarget,
        "-j", $Parallel
    )

    & cmake @BuildArgs
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed for target $BuildTarget with exit code $LASTEXITCODE."
    }
}

$BridgeDll = Get-ChildItem $BuildDir -Recurse -Filter "MRBNNBridge.dll" | Where-Object { $_.FullName -match "\\$Configuration\\" -or $_.DirectoryName -eq $BuildDir } | Select-Object -First 1
$TCNNDll = Get-ChildItem $BuildDir -Recurse -Filter "ExternalTCNN.dll" | Where-Object { $_.FullName -match "\\$Configuration\\" -or $_.DirectoryName -eq $BuildDir } | Select-Object -First 1
$NetworkKernel = Join-Path $MRBNNRoot "external/Network.kernel"

if (-not $BridgeDll) {
    throw "MRBNNBridge.dll was not found under $BuildDir after build."
}
if (-not $TCNNDll) {
    throw "ExternalTCNN.dll was not found under $BuildDir after build."
}
if (-not (Test-Path $NetworkKernel)) {
    throw "Network.kernel was not found under the MRBNN repository: $NetworkKernel"
}

Copy-Item -LiteralPath $BridgeDll.FullName -Destination (Join-Path $DeployDir "MRBNNBridge.dll") -Force
Copy-Item -LiteralPath $TCNNDll.FullName -Destination (Join-Path $DeployDir "ExternalTCNN.dll") -Force
Copy-Item -LiteralPath $NetworkKernel -Destination (Join-Path $DeployDir "Network.kernel") -Force
Copy-Item -LiteralPath $NetworkKernel -Destination (Join-Path $BridgeDll.DirectoryName "Network.kernel") -Force

$ConsoleExe = $null
if ($BuildBakeConsole -or $RunBakeConsole) {
    $ConsoleExe = Get-ChildItem $BuildDir -Recurse -Filter "MRBNNBakeConsole.exe" | Where-Object { $_.FullName -match "\\$Configuration\\" -or $_.DirectoryName -eq $BuildDir } | Select-Object -First 1
    if (-not $ConsoleExe) {
        throw "MRBNNBakeConsole.exe was not found under $BuildDir after build."
    }

    Copy-Item -LiteralPath $ConsoleExe.FullName -Destination (Join-Path $DeployDir "MRBNNBakeConsole.exe") -Force
}

if ($CudaRuntimeDir) {
    $CudaRuntimeDir = Resolve-FullPath $CudaRuntimeDir
} else {
    $CudaRuntimeDir = Join-Path $env:CUDA_PATH "bin\x64"
}
Copy-MatchingFiles $CudaRuntimeDir @("nvrtc*.dll", "nvrtc-builtins*.dll", "nvJitLink*.dll", "cudart64_*.dll", "curand64_*.dll") @($DeployDir, $BridgeDll.DirectoryName)

Write-Host "Deployed MRBNNBridge.dll, ExternalTCNN.dll, and Network.kernel to $DeployDir"
if ($ConsoleExe) {
    Write-Host "Deployed MRBNNBakeConsole.exe to $DeployDir"
}

if ($RunSmokeTest) {
    if (-not $SmokeTestWorkDir) {
        $SmokeTestWorkDir = Join-Path $MRBNNRoot "data/cloud-03"
    }

    $SmokeTestWorkDir = Resolve-FullPath $SmokeTestWorkDir
    if (-not (Test-Path (Join-Path $SmokeTestWorkDir "config.json"))) {
        throw "SmokeTestWorkDir does not contain config.json: $SmokeTestWorkDir"
    }

    $SmokeExe = Get-ChildItem $BuildDir -Recurse -Filter "MRBNNBridgeSmokeTest.exe" | Where-Object { $_.FullName -match "\\$Configuration\\" -or $_.DirectoryName -eq $BuildDir } | Select-Object -First 1
    if (-not $SmokeExe) {
        throw "MRBNNBridgeSmokeTest.exe was not found under $BuildDir after build."
    }

    $SmokeOutput = Join-Path $DeployDir "MRBNNBridgeSmokeTest.ppm"
    Copy-MatchingFiles $CudaRuntimeDir @("nvrtc*.dll", "nvrtc-builtins*.dll", "nvJitLink*.dll", "cudart64_*.dll", "curand64_*.dll") @($SmokeExe.DirectoryName)
    Copy-Item -LiteralPath $NetworkKernel -Destination (Join-Path $SmokeExe.DirectoryName "Network.kernel") -Force
    & $SmokeExe.FullName $SmokeTestWorkDir $MRBNNRoot $SmokeOutput $SmokeTestSize $SmokeTestSamples
    if ($LASTEXITCODE -ne 0) {
        throw "MRBNNBridgeSmokeTest failed with exit code $LASTEXITCODE."
    }

    $SmokePreviewPng = Join-Path $DeployDir "MRBNNBridgeSmokeTestPreview.png"
    Convert-PpmToPng $SmokeOutput $SmokePreviewPng

    Write-Host "Smoke test image: $SmokeOutput"
    Write-Host "Smoke test preview: $SmokePreviewPng"
}

if ($RunBakeConsole) {
    if (-not $BakeConsoleWorkDir) {
        if ($SmokeTestWorkDir) {
            $BakeConsoleWorkDir = $SmokeTestWorkDir
        } else {
            $BakeConsoleWorkDir = Join-Path $MRBNNRoot "data/cloud-03"
        }
    }

    $BakeConsoleWorkDir = Resolve-FullPath $BakeConsoleWorkDir
    if (-not (Test-Path (Join-Path $BakeConsoleWorkDir "config.json"))) {
        throw "BakeConsoleWorkDir does not contain config.json: $BakeConsoleWorkDir"
    }

    $DeployedConsoleExe = Join-Path $DeployDir "MRBNNBakeConsole.exe"
    if (-not (Test-Path $DeployedConsoleExe)) {
        throw "MRBNNBakeConsole.exe was not deployed to $DeployedConsoleExe"
    }

    $env:MRBNN_NETWORK_KERNEL_PATH = Join-Path $DeployDir "Network.kernel"
    $ConsoleArguments = @(
        "`"$BakeConsoleWorkDir`"",
        "`"$MRBNNRoot`"",
        "`"$PluginRoot`""
    )
    Start-Process -FilePath $DeployedConsoleExe -ArgumentList $ConsoleArguments -WorkingDirectory $DeployDir
    Write-Host "Launched MRBNNBakeConsole.exe with work dir $BakeConsoleWorkDir"
}
