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

$CMake = Get-CMakeCommand
$script:HadFailure = $false

function Invoke-CompilerTest {
  param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [Parameter(Mandatory = $true)][string]$StdoutPath,
    [Parameter(Mandatory = $true)][string]$StderrPath
  )

  if (Test-Path $StdoutPath) {
    Remove-Item -LiteralPath $StdoutPath -Force
  }
  if (Test-Path $StderrPath) {
    Remove-Item -LiteralPath $StderrPath -Force
  }

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

$normalTests = Get-ChildItem $TestDir -Filter "*.sy" |
  Where-Object { $_.Name -notlike "error_*" } |
  Sort-Object Name

foreach ($test in $normalTests) {
  Write-Host "[RUN] testcases/$($test.Name)"
  $outputPath = Join-Path $OutputDir ($test.BaseName + ".koopa")
  $errorPath = Join-Path $OutputDir ($test.BaseName + ".stderr.txt")
  $exitCode = Invoke-CompilerTest `
    -InputPath $test.FullName `
    -StdoutPath $outputPath `
    -StderrPath $errorPath
  if ($exitCode -ne 0) {
    Write-Host "[FAIL] $($test.Name)"
    Write-Host (Read-TestOutput -StdoutPath $outputPath -StderrPath $errorPath)
    $script:HadFailure = $true
    continue
  }
  if (Test-Path $errorPath) {
    Remove-Item -LiteralPath $errorPath -Force
  }
  Write-Host "[OK] $($test.Name)"
}

$errorTests = Get-ChildItem $TestDir -Filter "error_*.sy" | Sort-Object Name

foreach ($test in $errorTests) {
  Write-Host "[ERROR TEST] testcases/$($test.Name) expected to fail"
  $stdoutPath = Join-Path $OutputDir ($test.BaseName + ".stdout.txt")
  $errorPath = Join-Path $OutputDir ($test.BaseName + ".error.txt")
  $exitCode = Invoke-CompilerTest `
    -InputPath $test.FullName `
    -StdoutPath $stdoutPath `
    -StderrPath $errorPath
  $output = Read-TestOutput -StdoutPath $stdoutPath -StderrPath $errorPath
  if ($exitCode -ne 0) {
    if ($output.Trim().Length -gt 0) {
      Write-Host $output.Trim()
    }
    Write-Host "[OK] $($test.Name) expected failure"
  } else {
    Write-Host "[FAIL] $($test.Name) expected failure but succeeded"
    if ($output.Trim().Length -gt 0) {
      Write-Host $output.Trim()
    }
    $script:HadFailure = $true
  }
  if (Test-Path $stdoutPath) {
    Remove-Item -LiteralPath $stdoutPath -Force
  }
}

if ($script:HadFailure) {
  Write-Host "[DONE] Some tests failed."
  exit 1
}

Write-Host "All normal tests passed."
Write-Host "All error tests behaved as expected."
Write-Host "[DONE] All tests completed."
exit 0
