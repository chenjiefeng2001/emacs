# EVS-2.3 A/B runner: full vs incremental capture on the real slice.
# Syncs ENCA sources from the Windows checkout into the WSL Emacs
# build tree, rebuilds, then runs evs23-bench.el once per arm.
$ErrorActionPreference = 'Continue'
$repo = 'C:\Users\14977\source\repos\emacs'
$wslTree = '~/enca-p11'
$results = '..\results\evs23_ab.csv'

"mode,scenario,size_kb,es,edits,n,p50_ms,p95_ms,p99_ms,max_ms,cap_avg_ms,wasted" |
  Out-File $results -Encoding utf8

Write-Host "== sync sources =="
wsl -e bash -lc @"
set -e
cd $wslTree
cp /mnt/c/Users/14977/source/repos/emacs/src/enca-evs.c src/
cp /mnt/c/Users/14977/source/repos/emacs/src/enca/snapshot/snapshot.c src/enca/snapshot/
cp /mnt/c/Users/14977/source/repos/emacs/src/enca/snapshot/snapshot.h src/enca/snapshot/
make -j20 > build.log 2>&1 && echo BUILD_OK || { echo BUILD_FAIL; grep -E 'error:' build.log | head -20; exit 1; }
"@

if ($LASTEXITCODE -ne 0) { exit 1 }

foreach ($mode in @('full', 'incr')) {
  Write-Host "== arm: $mode =="
  $env:EVS23_MODES = $mode
  if ($args -contains '--smoke') { $env:EVS23_MAX_MB = '10' }
  $out = wsl -e bash -lc "cd $wslTree && EVS23_MODES=$mode ./src/emacs --batch -l /mnt/c/Users/14977/source/repos/emacs/test/enca/evs23-bench.el 2>&1"
  $out | Write-Host
  foreach ($line in $out) {
    # SUMMARY|MODE|NAME|extra|n|p50|p95|p99|max
    if ($line -match '^SUMMARY\|([^|]+)\|([^|]+)\|([^|]*)\|(\d+)\|([\d.]+)\|([\d.]+)\|([\d.]+)\|([\d.]+)$') {
      $sz = 0; $es = 0; $edits = 0; $cap = 0; $wasted = 0
      if ($Matches[2] -match 'E4-(\d+)KB-es(\d+)') {
        $sz = $Matches[1]; $es = $Matches[2]; $edits = 12
      }
      Add-Content -Path $results -Value (
        "$($Matches[1]),$($Matches[2]),$sz,$es,$edits,$($Matches[4])," +
        "$($Matches[5]),$($Matches[6]),$($Matches[7]),$($Matches[8]),$cap,$wasted"
      ) -Encoding utf8
    }
    # CELL|MODE|size_kb|es|edits|cap_avg_ms|wasted
    elseif ($line -match '^CELL\|([^|]+)\|(\d+)\|(\d+)\|(\d+)\|([\d.]+)\|(\d+)$') {
      $csvLine = "$($Matches[1]),E4-cap,$($Matches[2]),$($Matches[3]),$($Matches[4]),,,,$(0),$($Matches[5]),$($Matches[6])"
      Add-Content -Path $results -Value $csvLine -Encoding utf8
    }
  }
}
Remove-Item Env:\EVS23_MODES, Env:\EVS23_MAX_MB -ErrorAction SilentlyContinue
Write-Host "== done: $results =="
