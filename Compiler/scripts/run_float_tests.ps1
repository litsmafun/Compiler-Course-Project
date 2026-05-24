$ErrorActionPreference = "Stop"

chcp 65001 > $null
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
  $PSNativeCommandUseErrorActionPreference = $false
}

$Root = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $Root "build"
$Compiler = Join-Path $BuildDir "Debug\compiler.exe"
$TestDir = Join-Path $Root "testcases"
$OutputDir = Join-Path $Root "outputs"
$ForbiddenKoopaTokenPattern = '(^|\s)f32(?=\s|,|\)|$)|=\s+(fadd|fsub|fmul|fdiv)\s|(^|\s)(sitofp|fptosi|fcmp)(?=\s|,|\)|$)'

function Get-CMakeCommand {
  $command = Get-Command cmake -ErrorAction SilentlyContinue
  if ($command) {
    return $command.Source
  }
  $defaultPath = "C:\Program Files\CMake\bin\cmake.exe"
  if (Test-Path $defaultPath) {
    return $defaultPath
  }
  throw "cmake was not found in PATH or at $defaultPath"
}

function Invoke-CompilerTest {
  param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [Parameter(Mandatory = $true)][string]$StdoutPath,
    [Parameter(Mandatory = $true)][string]$StderrPath
  )
  Remove-Item -LiteralPath $StdoutPath -Force -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $StderrPath -Force -ErrorAction SilentlyContinue
  $process = Start-Process `
    -FilePath $Compiler `
    -ArgumentList @($InputPath) `
    -NoNewWindow `
    -Wait `
    -PassThru `
    -RedirectStandardOutput $StdoutPath `
    -RedirectStandardError $StderrPath
  return $process.ExitCode
}

function Read-TestOutput {
  param(
    [Parameter(Mandatory = $true)][string]$StdoutPath,
    [Parameter(Mandatory = $true)][string]$StderrPath
  )
  $parts = @()
  if ((Test-Path $StdoutPath) -and ((Get-Item $StdoutPath).Length -gt 0)) {
    $parts += Get-Content -LiteralPath $StdoutPath -Raw -Encoding UTF8
  }
  if ((Test-Path $StderrPath) -and ((Get-Item $StderrPath).Length -gt 0)) {
    $parts += Get-Content -LiteralPath $StderrPath -Raw -Encoding UTF8
  }
  return ($parts -join "")
}

$CMake = Get-CMakeCommand
$script:HadFailure = $false

if (-not (Test-Path $BuildDir)) {
  Write-Host "[BUILD] Configuring project..."
  & $CMake -S $Root -B $BuildDir
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}

Write-Host "[BUILD] Building compiler..."
& $CMake --build $BuildDir --config Debug
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

if (-not (Test-Path $Compiler)) {
  Write-Host "[FAIL] Compiler not found: $Compiler"
  exit 1
}

New-Item -ItemType Directory -Force $OutputDir | Out-Null

$normalTests = @(
  "float_basic.sy",
  "float_var.sy",
  "float_int_convert.sy",
  "float_func.sy",
  "float_compare.sy",
  "float_array.sy",
  "float_array_sum.sy",
  "global_float.sy",
  "global_float_array.sy",
  "const_float.sy",
  "float_array_param.sy",
  "float_short_circuit.sy",
  "float_comprehensive.sy",
  "float_io.sy",
  "float_get_put.sy"
)

$errorTests = @(
  "error_float_mod.sy",
  "error_float_array_index.sy",
  "error_assign_const_float.sy",
  "error_assign_const_float_array.sy",
  "error_array_param_type.sy",
  "error_float_missing_return.sy",
  "error_float_comprehensive.sy",
  "error_putfloat_arg_count.sy",
  "error_getfloat_arg_count.sy"
)

foreach ($testName in $normalTests) {
  $testPath = Join-Path $TestDir $testName
  if (-not (Test-Path $testPath)) {
    Write-Host "[SKIP] missing testcases/$testName"
    continue
  }
  Write-Host "[RUN] testcases/$testName"
  $baseName = [System.IO.Path]::GetFileNameWithoutExtension($testName)
  $outputPath = Join-Path $OutputDir ($baseName + ".koopa")
  $errorPath = Join-Path $OutputDir ($baseName + ".stderr.txt")
  $exitCode = Invoke-CompilerTest -InputPath $testPath -StdoutPath $outputPath -StderrPath $errorPath
  if ($exitCode -ne 0) {
    Write-Host "[FAIL] $testName"
    Write-Host (Read-TestOutput -StdoutPath $outputPath -StderrPath $errorPath)
    $script:HadFailure = $true
    continue
  }
  Remove-Item -LiteralPath $errorPath -Force -ErrorAction SilentlyContinue
  $forbidden = Select-String -LiteralPath $outputPath -Pattern $ForbiddenKoopaTokenPattern -AllMatches
  if ($forbidden) {
    Write-Host "[FAIL] $testName emitted native float Koopa token"
    $forbidden | ForEach-Object { Write-Host $_.Line }
    $script:HadFailure = $true
    continue
  }
  Write-Host "[OK] $testName"
}

foreach ($testName in $errorTests) {
  $testPath = Join-Path $TestDir $testName
  if (-not (Test-Path $testPath)) {
    Write-Host "[SKIP] missing testcases/$testName"
    continue
  }
  Write-Host "[ERROR TEST] testcases/$testName expected to fail"
  $baseName = [System.IO.Path]::GetFileNameWithoutExtension($testName)
  $stdoutPath = Join-Path $OutputDir ($baseName + ".stdout.txt")
  $errorPath = Join-Path $OutputDir ($baseName + ".error.txt")
  $exitCode = Invoke-CompilerTest -InputPath $testPath -StdoutPath $stdoutPath -StderrPath $errorPath
  $output = Read-TestOutput -StdoutPath $stdoutPath -StderrPath $errorPath
  if ($exitCode -ne 0) {
    if ($output.Trim().Length -gt 0) {
      Write-Host $output.Trim()
    }
    Write-Host "[OK] $testName expected failure"
  } else {
    Write-Host "[FAIL] $testName expected failure but succeeded"
    if ($output.Trim().Length -gt 0) {
      Write-Host $output.Trim()
    }
    $script:HadFailure = $true
  }
  Remove-Item -LiteralPath $stdoutPath -Force -ErrorAction SilentlyContinue
}

if ($script:HadFailure) {
  Write-Host "[DONE] Some float tests failed."
  exit 1
}

Write-Host "[DONE] All float tests completed."
exit 0
