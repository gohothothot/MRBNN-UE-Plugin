param(
    [string]$MRBNNRoot = "",
    [string]$PluginRoot = "",
    [string]$Configuration = "Release",
    [int]$Parallel = 16,
    [string]$CudaArchitectures = "native",
    [switch]$InstallCudaIfMissing,
    [switch]$PersistUserEnvironment,
    [switch]$RunSmokeTest,
    [string]$SmokeTestWorkDir = "",
    [int]$SmokeTestSize = 1024,
    [int]$SmokeTestSamples = 256
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
    $WorkspaceRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PluginRoot)))
    $MRBNNRoot = Join-Path $WorkspaceRoot "third_party_refs/MRBNN"
}

$MRBNNRoot = Resolve-FullPath $MRBNNRoot
$BridgeSource = Join-Path $PluginRoot "Source/ThirdParty/MRBNNBridge"
$BuildDir = Join-Path $PluginRoot "Intermediate/MRBNNBridge"
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

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $DeployDir | Out-Null

$ResolvedCudaArchitectures = Resolve-CudaArchitectures $CudaArchitectures
$ConfigureArgs = @(
    "-S", $BridgeSource,
    "-B", $BuildDir,
    "-DMRBNN_ROOT=$MRBNNRoot",
    "-DCMAKE_CUDA_ARCHITECTURES=$ResolvedCudaArchitectures",
    "-DTCNN_CUDA_ARCHITECTURES=$ResolvedCudaArchitectures",
    "-DCMAKE_BUILD_TYPE=$Configuration"
)

& cmake @ConfigureArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE."
}

$BuildTarget = if ($RunSmokeTest) { "MRBNNBridgeSmokeTest" } else { "MRBNNBridge" }
$BuildArgs = @(
    "--build", $BuildDir,
    "--config", $Configuration,
    "--target", $BuildTarget,
    "-j", $Parallel
)

& cmake @BuildArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE."
}

$BridgeDll = Get-ChildItem $BuildDir -Recurse -Filter "MRBNNBridge.dll" | Where-Object { $_.FullName -match "\\$Configuration\\" -or $_.DirectoryName -eq $BuildDir } | Select-Object -First 1
$TCNNDll = Get-ChildItem $BuildDir -Recurse -Filter "ExternalTCNN.dll" | Where-Object { $_.FullName -match "\\$Configuration\\" -or $_.DirectoryName -eq $BuildDir } | Select-Object -First 1

if (-not $BridgeDll) {
    throw "MRBNNBridge.dll was not found under $BuildDir after build."
}
if (-not $TCNNDll) {
    throw "ExternalTCNN.dll was not found under $BuildDir after build."
}

Copy-Item -LiteralPath $BridgeDll.FullName -Destination (Join-Path $DeployDir "MRBNNBridge.dll") -Force
Copy-Item -LiteralPath $TCNNDll.FullName -Destination (Join-Path $DeployDir "ExternalTCNN.dll") -Force

$CudaRuntimeDir = Join-Path $env:CUDA_PATH "bin\x64"
Copy-MatchingFiles $CudaRuntimeDir @("nvrtc*.dll", "nvrtc-builtins*.dll", "nvJitLink*.dll", "cudart64_*.dll") @($DeployDir, $BridgeDll.DirectoryName)

Write-Host "Deployed MRBNNBridge.dll and ExternalTCNN.dll to $DeployDir"

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
    Copy-MatchingFiles $CudaRuntimeDir @("nvrtc*.dll", "nvrtc-builtins*.dll", "nvJitLink*.dll", "cudart64_*.dll") @($SmokeExe.DirectoryName)
    & $SmokeExe.FullName $SmokeTestWorkDir $MRBNNRoot $SmokeOutput $SmokeTestSize $SmokeTestSamples
    if ($LASTEXITCODE -ne 0) {
        throw "MRBNNBridgeSmokeTest failed with exit code $LASTEXITCODE."
    }

    $SmokePreviewPng = Join-Path $DeployDir "MRBNNBridgeSmokeTestPreview.png"
    Convert-PpmToPng $SmokeOutput $SmokePreviewPng

    Write-Host "Smoke test image: $SmokeOutput"
    Write-Host "Smoke test preview: $SmokePreviewPng"
}
