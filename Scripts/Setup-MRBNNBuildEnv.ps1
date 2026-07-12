param(
    [switch]$InstallCudaIfMissing,
    [string]$CudaPackageVersion = "13.3.1",
    [switch]$PersistUserEnvironment,
    [switch]$Quiet,
    [switch]$PassThru
)

$ErrorActionPreference = "Stop"

function Write-Status([string]$Message) {
    if (-not $Quiet) {
        Write-Host $Message
    }
}

function Add-UniquePathEntry([string]$Entry) {
    if (-not $Entry -or -not (Test-Path $Entry)) {
        return
    }

    $Segments = @($env:Path -split ';' | Where-Object { $_ })
    if ($Segments -contains $Entry) {
        return
    }

    $env:Path = "$Entry;$($env:Path)"
}

function Add-UniquePersistedUserPathEntry([string]$Entry) {
    if (-not $Entry -or -not (Test-Path $Entry)) {
        return
    }

    $Current = [Environment]::GetEnvironmentVariable("Path", "User")
    $Segments = @($Current -split ';' | Where-Object { $_ })
    if ($Segments -contains $Entry) {
        return
    }

    $NewValue = if ([string]::IsNullOrWhiteSpace($Current)) { $Entry } else { "$Entry;$Current" }
    [Environment]::SetEnvironmentVariable("Path", $NewValue, "User")
}

function Get-VcVarsPath() {
    $Candidates = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    )

    foreach ($Candidate in $Candidates) {
        if (Test-Path $Candidate) {
            return $Candidate
        }
    }

    return $null
}

function Import-VcVarsEnvironment([string]$VcVarsPath) {
    $Command = "`"$VcVarsPath`" amd64 >nul && set"
    $Lines = & cmd.exe /s /c $Command
    foreach ($Line in $Lines) {
        $SeparatorIndex = $Line.IndexOf('=')
        if ($SeparatorIndex -le 0) {
            continue
        }

        $Name = $Line.Substring(0, $SeparatorIndex)
        $Value = $Line.Substring($SeparatorIndex + 1)
        Set-Item -Path "Env:$Name" -Value $Value
    }
}

function Get-CudaRoot() {
    $Candidates = @()
    if ($env:CUDA_PATH) {
        $Candidates += $env:CUDA_PATH
    }

    $CudaBaseDir = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA"
    if (Test-Path $CudaBaseDir) {
        $Candidates += @(Get-ChildItem $CudaBaseDir -Directory | Sort-Object Name -Descending | ForEach-Object { $_.FullName })
    }

    foreach ($Candidate in $Candidates | Where-Object { $_ } | Select-Object -Unique) {
        if (Test-Path (Join-Path $Candidate "bin\nvcc.exe")) {
            return [string]$Candidate
        }
    }

    return $null
}

function Get-CudaVersionedVariableName([string]$CudaRoot) {
    if (-not $CudaRoot) {
        return $null
    }

    $LeafName = Split-Path $CudaRoot -Leaf
    if ($LeafName -match '^v(?<Major>\d+)\.(?<Minor>\d+)$') {
        return "CUDA_PATH_V{0}_{1}" -f $Matches.Major, $Matches.Minor
    }

    return $null
}

function Ensure-CudaInstalled() {
    $CudaRoot = Get-CudaRoot
    if ($CudaRoot) {
        return $CudaRoot
    }

    if (-not $InstallCudaIfMissing) {
        throw "CUDA toolkit was not found. Run Setup-MRBNNBuildEnv.ps1 -InstallCudaIfMissing or install CUDA 12.6+ manually."
    }

    $Choco = Get-Command choco -ErrorAction SilentlyContinue
    if (-not $Choco) {
        throw "Chocolatey is not available, so CUDA cannot be installed automatically. Install CUDA 12.6+ manually."
    }

    Write-Status "Installing CUDA toolkit $CudaPackageVersion with Chocolatey..."
    choco install cuda --version=$CudaPackageVersion -y --no-progress --limit-output | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Chocolatey failed to install CUDA toolkit. Exit code: $LASTEXITCODE"
    }

    $CudaRoot = Get-CudaRoot
    if (-not $CudaRoot) {
        throw "CUDA installation completed but nvcc.exe was still not found."
    }

    return $CudaRoot
}

$VcVarsPath = Get-VcVarsPath
if (-not $VcVarsPath) {
    throw "Visual Studio 2022 C++ build tools were not found. Expected vcvars64.bat under a VS 2022 install."
}

Import-VcVarsEnvironment $VcVarsPath
Write-Status "Loaded Visual Studio build environment from $VcVarsPath"

$CudaRoot = Ensure-CudaInstalled
$CudaVersionedVariableName = Get-CudaVersionedVariableName $CudaRoot
$env:CUDA_PATH = $CudaRoot
$env:CUDAToolkit_ROOT = $CudaRoot
$env:CudaToolkitDir = "$CudaRoot\"
if ($CudaVersionedVariableName) {
    Set-Item -Path "Env:$CudaVersionedVariableName" -Value $CudaRoot
}
Add-UniquePathEntry (Join-Path $CudaRoot "bin")
Add-UniquePathEntry (Join-Path $CudaRoot "bin\x64")
Add-UniquePathEntry (Join-Path $CudaRoot "libnvvp")

if ($PersistUserEnvironment) {
    [Environment]::SetEnvironmentVariable("CUDA_PATH", $CudaRoot, "User")
    [Environment]::SetEnvironmentVariable("CUDAToolkit_ROOT", $CudaRoot, "User")
    [Environment]::SetEnvironmentVariable("CudaToolkitDir", "$CudaRoot\", "User")
    if ($CudaVersionedVariableName) {
        [Environment]::SetEnvironmentVariable($CudaVersionedVariableName, $CudaRoot, "User")
    }
    Add-UniquePersistedUserPathEntry (Join-Path $CudaRoot "bin")
    Add-UniquePersistedUserPathEntry (Join-Path $CudaRoot "bin\x64")
    Add-UniquePersistedUserPathEntry (Join-Path $CudaRoot "libnvvp")
}

$Result = [pscustomobject]@{
    VisualStudioVcVars = $VcVarsPath
    CudaRoot = $CudaRoot
    NvccPath = (Get-Command nvcc -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source)
    ClPath = (Get-Command cl -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source)
    CMakePath = (Get-Command cmake -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source)
}

if (-not $Result.CMakePath) {
    throw "cmake was not found on PATH after environment setup."
}
if (-not $Result.ClPath) {
    throw "cl.exe was not found on PATH after Visual Studio environment setup."
}
if (-not $Result.NvccPath) {
    throw "nvcc.exe was not found on PATH after CUDA environment setup."
}

Write-Status "CUDA root: $($Result.CudaRoot)"
Write-Status "nvcc: $($Result.NvccPath)"
Write-Status "cl.exe: $($Result.ClPath)"
Write-Status "cmake: $($Result.CMakePath)"

if ($PassThru) {
    return $Result
}
