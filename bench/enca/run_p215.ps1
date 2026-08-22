# P2.1.5 Storage Decision Closure sweeps.
# Writes one CSV row per cell to bench/results/p215_storage_closure.csv.
$ErrorActionPreference = 'Continue'
$here = Split-Path $MyInvocation.MyCommand.Path
Set-Location $here
$exe = '.\bench_enca.exe'
$results = '..\results\p215_storage_closure.csv'

function Run-Cell ([string]$store, [long]$cs, [string]$wl, [long]$size,
                   [long]$edits, [long]$ret, [int]$readers, [int]$rms,
                   [hashtable]$opt) {
  $cellArgs = @('--store', $store)
  if ($cs -gt 0) {
    $cellArgs += @('--chunk', "$cs", '--cslabel', "${cs}K")
  } else {
    $cellArgs += @('--cslabel', '-')
  }
  if ($opt -and $opt.Coalesce) { $cellArgs += @('--coalesce', $opt.Coalesce) }
  $cellArgs += @('--workload', $wl,
                 '--size', "$size",
                 '--edits', "$edits",
                 '--retention', "$ret",
                 '--verify-every', '11',
                 '--seed', '42',
                 '--label', 'p215')
  if ($opt -and $opt.FragThreshold) {
    $cellArgs += @('--frag-threshold', "$($opt.FragThreshold)")
  }
  if ($opt -and $opt.MaintEvery) {
    $cellArgs += @('--maint-every', "$($opt.MaintEvery)")
  }
  if ($opt -and $opt.EditSize) {
    $cellArgs += @('--edit-size', "$($opt.EditSize)")
  }
  if ($opt -and $opt.Locality) {
    $cellArgs += @('--locality', $opt.Locality)
  }
  if ($opt -and $opt.ColdHoldAt) {
    $cellArgs += @('--cold-hold-at', "$($opt.ColdHoldAt)")
  }
  if ($readers -gt 0) {
    $cellArgs += @('--readers', "$readers", '--read-ms', "$rms")
  }

  # Let the process run to completion (early pipeline close would
  # kill it mid-write and skew the exit code).
  $outAll = (& $exe @cellArgs 2>$null)
  $line = $outAll | Select-Object -First 1
  if ($LASTEXITCODE -ne 0) { $line = "$line,FAILRC$LASTEXITCODE" }
  Add-Content -Path $results -Value $line -Encoding utf8
  Write-Host ("{0,-8} {1,-7} {2,-3} {3,-12} sz={4} ed={5} ret={6} r={7}{8}" -f `
    $store, $cs, ($(if ($opt -and $opt.Coalesce) { $opt.Coalesce } else { '-' })), `
    $wl, $size, $edits, $ret, $readers,
    ($(if ($opt -and $opt.EditSize) { " es=" + $opt.EditSize } else { "" })))
}

"store,cs_label,workload,size,edits,retention,readers,ok,p50_ms,p90_ms,p99_ms,max_ms,copied_total,meta_total,live_delta,reader_mbs,label,final_hash,logical_bytes,physical_bytes,sharing_ratio,maint_copied,cold_ok,coalesce,locality,edit_size" |
  Out-File $results -Encoding utf8

Write-Host '=== [3] cross-check: flat vs chunked final hash ==='
Run-Cell 'flat' 0 'W1' 1048576 300 16 0 0 @{}
Run-Cell 'chunked' 65536 'W1' 1048576 300 16 0 0 @{ Coalesce = 'none' }
Run-Cell 'chunked' 65536 'W1' 1048576 300 16 0 0 @{ Coalesce = 'local' }
Run-Cell 'chunked' 65536 'W1' 1048576 300 16 0 0 @{ Coalesce = 'deferred'; MaintEvery = 25 }

Write-Host '=== [4] Tier A sizes (crossover hunt) ==='
foreach ($sz in @(1024, 4096, 16384, 65536, 262144)) {
  foreach ($wl in @('W1', 'W2')) {
    foreach ($ret in @(1, 4, 16)) {
      Run-Cell 'flat' 0 $wl $sz 2000 $ret 0 0 @{}
      Run-Cell 'chunked' 4096 $wl $sz 2000 $ret 0 0 @{}
      Run-Cell 'chunked' 65536 $wl $sz 2000 $ret 0 0 @{}
    }
  }
}

Write-Host '=== [5] Tier B sizes ==='
foreach ($sz in @(1048576, 4194304, 16777216)) {
  foreach ($wl in @('W1', 'W5')) {
    foreach ($ret in @(1, 8, 64)) {
      Run-Cell 'flat' 0 $wl $sz 300 $ret 0 0 @{}
      Run-Cell 'chunked' 65536 $wl $sz 300 $ret 0 0 @{}
    }
  }
}

Write-Host '=== [6] Tier C large docs ==='
foreach ($sz in @(268435456, 1073741824)) {
  foreach ($wl in @('W5', 'W3')) {
    $edits = 20; if ($wl -eq 'W3') { $edits = 12 }
    Run-Cell 'chunked' 65536 $wl $sz $edits 8 0 0 @{}
    Run-Cell 'chunked' 262144 $wl $sz $edits 8 0 0 @{}
  }
  Run-Cell 'flat' 0 'W5' $sz 10 1 0 0 @{}
}

Write-Host '=== [7] reader scaling ==='
foreach ($r in @(1, 2, 4, 8, 16, 32)) {
  Run-Cell 'flat' 0 'W5' 10485760 100 16 $r 250 @{}
  Run-Cell 'chunked' 65536 'W5' 10485760 100 16 $r 250 @{}
}

Write-Host '=== [8] edit-size x locality sweep (16MB doc) ==='
foreach ($es in @(1, 10, 100, 1024, 10240, 102400)) {
  foreach ($loc in @('append', 'middle', 'random', 'hot')) {
    Run-Cell 'chunked' 65536 'W6' 16777216 150 8 0 0 `
      @{ EditSize = $es; Locality = $loc }
    Run-Cell 'flat' 0 'W6' 16777216 150 8 0 0 `
      @{ EditSize = $es; Locality = $loc }
  }
}

Write-Host '=== [9] coalescing comparison (W1@10MB) ==='
Run-Cell 'chunked' 65536 'W1' 10485760 300 16 0 0 @{ Coalesce = 'none' }
Run-Cell 'chunked' 65536 'W1' 10485760 300 16 0 0 @{ Coalesce = 'local' }
foreach ($thr in @(1.25, 2, 4, 8)) {
  Run-Cell 'chunked' 65536 'W1' 10485760 300 16 0 0 `
    @{ Coalesce = 'deferred'; FragThreshold = $thr; MaintEvery = 25 }
}

Write-Host '=== [10] cold snapshot ==='
Run-Cell 'chunked' 65536 'W1' 1048576 300 128 0 0 @{ ColdHoldAt = 5 }
Run-Cell 'flat' 0 'W1' 1048576 300 128 0 0 @{ ColdHoldAt = 5 }

Write-Host '=== [11] sharing ratio sweep (ret 1..128) ==='
foreach ($ret in @(1, 4, 8, 16, 32, 64, 128)) {
  Run-Cell 'chunked' 65536 'W1' 1048576 300 $ret 0 0 @{}
  Run-Cell 'flat' 0 'W1' 1048576 300 $ret 0 0 @{}
}

Write-Host "closure matrix complete -> $results"
