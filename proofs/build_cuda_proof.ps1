# Build and run the Memory Expansion Fabric real CUDA consumer-gating proof.
# Requires a CUDA toolkit (nvcc) and an NVIDIA GPU. Builds mef_core first, then
# compiles the .cu with nvcc (using the MSVC dev env) and links against mef_core + cudart.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$vcvars = @(
  'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
  'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vcvars) { Write-Error 'vcvars64.bat not found'; exit 1 }

$nvcc = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.9\bin\nvcc.exe'
if (-not (Test-Path $nvcc)) { $nvcc = (Get-Command nvcc -ErrorAction SilentlyContinue).Source }
if (-not $nvcc) { Write-Error 'nvcc not found'; exit 1 }

$cudaRoot = Split-Path (Split-Path $nvcc -Parent) -Parent
$cudaLib = Join-Path $cudaRoot 'lib\x64'
$cfg = 'RelWithDebInfo'
$mefLib = Join-Path $root "build\$cfg\mef_core.lib"
if (-not (Test-Path $mefLib)) { Write-Error "mef_core.lib not found at $mefLib"; exit 1 }

$bat = Join-Path $env:TEMP 'mef_cuda_build.bat'
@"
@echo off
call "$vcvars" >nul
cd /d "$root"
"$nvcc" -c "proofs\cuda_proof.cu" -o "build\cuda_proof.obj" -I "include" -std=c++20 -arch=sm_120 -Xcompiler=/W4 -Xcompiler=/MD
cl /nologo /std:c++20 /EHsc /MD "build\cuda_proof.obj" /Fe:"build\bin\mef_cuda_proof.exe" /link /LIBPATH:"build\$cfg" mef_core.lib /LIBPATH:"$cudaLib" cudart.lib
"@ | Set-Content -Path $bat

& $bat
if ($LASTEXITCODE -ne 0) { Write-Error 'CUDA proof build failed'; exit 1 }

Write-Host 'Running mef_cuda_proof...'
$env:PATH = "$cudaRoot\bin;$env:PATH"
& (Join-Path $root 'build\bin\mef_cuda_proof.exe')
exit $LASTEXITCODE