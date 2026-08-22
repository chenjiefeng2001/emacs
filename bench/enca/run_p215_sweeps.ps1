# Re-run only the dimension-tagged sweeps into a side CSV.
$ErrorActionPreference = 'Continue'
$here = Split-Path $MyInvocation.MyCommand.Path
Set-Location $here
$exe = '.\bench_enca.exe'
$results = '..\results\p215_sweeps.csv'

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

  $outAll = (& $exe @cellArgs 2>$null)
  $line = $outAll | Select-Object -First 1
  if ($LASTEXITCODE -ne 0) { $line = "$line,FAILRC$LASTEXITCODE" }
  Add-Content -Path $results -Value $line -Encoding utf8
}

"store,cs_label,workload,size,edits,retention,readers,ok,p50_ms,p90_ms,p99_ms,max_ms,copied_total,meta_total,live_delta,reader_mbs,label,final_hash,logical_bytes,physical_bytes,sharing_ratio,maint_copied,cold_ok,coalesce,locality,edit_size" |
  Out-File $results -Encoding utf8

Write-Host '=== edit-size x locality (16MB) ==='
foreach ($es in @(1, 10, 100, 1024, 10240, 102400)) {
  foreach ($loc in @('append', 'middle', 'random', 'hot')) {
    Run-Cell 'chunked' 65536 'W6' 16777216 150 8 0 0 `
      @{ EditSize = $es; Locality = $loc }
    Run-Cell 'flat' 0 'W6' 16777216 150 8 0 0 `
      @{ EditSize = $es; Locality = $loc }
  }
}

Write-Host '=== coalescing comparison (W1@10MB ret16) ==='
Run-Cell 'chunked' 65536 'W1' 10485760 300 16 0 0 @{ Coalesce = 'none' }
Run-Cell 'chunked' 65536 'W1' 10485760 300 16 0 0 @{ Coalesce = 'local' }
foreach ($thr in @(1.25, 2, 4, 8)) {
  Run-Cell 'chunked' 65536 'W1' 10485760 300 16 0 0 `
    @{ Coalesce = 'deferred'; FragThreshold = $thr; MaintEvery = 25 }
}

Write-Host "sweeps complete -> $results"
