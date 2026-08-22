param(
    [string]$OutputDir = "bench/results"
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$out = Join-Path $OutputDir "baseline-$ts.txt"

$emacsVersion = "unknown"
$acInit = Select-String -Path "configure.ac" -Pattern '^AC_INIT\(\[GNU Emacs\], \[\s*([0-9.]+)' |
    Select-Object -First 1
if ($acInit) { $emacsVersion = $acInit.Matches[0].Groups[1].Value }

$gitRev = git rev-parse HEAD

$gitDescribe = "unknown"
try { $d = git describe --always --dirty 2>$null; if ($LASTEXITCODE -eq 0) { $gitDescribe = $d } } catch { }

$clangCmd = Get-Command clang -ErrorAction SilentlyContinue
$gccCmd = Get-Command gcc -ErrorAction SilentlyContinue
if ($clangCmd) { $compiler = (clang --version)[0] }
elseif ($gccCmd) { $compiler = (gcc --version)[0] }
else { $compiler = "unknown" }

$cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1).Name

$cfgOpt = $env:ENCA_CONFIGURE_OPTIONS
if (-not $cfgOpt)
  {
    if (Test-Path "config.log")
      {
        $m = Select-String -Path "config.log" -Pattern '^\$ (.*)' |
          Select-Object -First 1
        if ($m) { $cfgOpt = $m.Matches[0].Groups[1].Value }
      }
    if (-not $cfgOpt) { $cfgOpt = "not-configured" }
  }

$buildFlags = $env:ENCA_BUILD_FLAGS
if (-not $buildFlags)
  {
    $mkCmd = Get-Command make -ErrorAction SilentlyContinue
    if (-not $mkCmd)
      { $mkCmd = Get-Command mingw32-make -ErrorAction SilentlyContinue }
    if ($mkCmd)
      {
        $dry = & $mkCmd.Source --no-print-directory -C test/enca -n gcc-check `
          2>$null
        $cmd = (($dry -join " ") -replace "[\t\\]+", " ")`
          -replace "\s+", " "
        if ($cmd -match "^(.*?) -o (\S+) (.*)$")
          {
            $buildFlags = ("{0} -o {1} <sources per test/enca/Makefile>" -f
                           $Matches[1], $Matches[2])
          }
      }
    if (-not $buildFlags) { $buildFlags = "<fill from actual build>" }
  }

$gcSettings = if ($env:ENCA_GC_SETTINGS) { $env:ENCA_GC_SETTINGS } else { "default" }

@(
    "enca-baseline-record"
    "date: $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))"
    "emacs-version: $emacsVersion"
    "git-revision: $gitRev"
    "git-describe: $gitDescribe"
    "compiler-c: $compiler"
    "configure-options: $cfgOpt"
    "cpu: $cpu"
    "os: $([System.Environment]::OSVersion.VersionString)"
    "build-flags: $buildFlags"
    "gc-settings: $gcSettings"
) | Set-Content -Path $out

Write-Host "wrote $out"
