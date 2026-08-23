# EVS-2.1 sweep: full vs incremental capture.
# Core grid per contract section 8: copy amplification focus.
$ErrorActionPreference = 'Continue'
Set-Location $PSScriptRoot
$exe = '.\bench_enca.exe'
$results = '..\results\evs2_amplification.csv'

"store,cs_label,workload,size,edits,retention,readers,ok,p50_ms,p90_ms,p99_ms,max_ms,copied_total,meta_total,live_delta,reader_mbs,label,final_hash,logical_bytes,physical_bytes,sharing_ratio,maint_copied,cold_ok,coalesce,locality,edit_size,copy_amp" |
  Out-File $results -Encoding utf8

function Run-Cell ([string]$store, [long]$cs, [string]$wl, [long]$size,
                   [long]$edits, [long]$ret, [int]$es, [string]$loc) {
  $a = @('--store', $store)
  if ($cs -gt 0) { $a += @('--chunk', "$cs", '--cslabel', "${cs}K") } else { $a += @('--cslabel', '-') }
  $a += @('--workload', $wl, '--size', "$size", '--edits', "$edits",
          '--retention', "$ret", '--verify-every', '13',
          '--seed', '42', '--label', 'evs2',
          '--edit-size', "$es", '--locality', $loc)
  $line = (& $exe @a 2>$null | Select-Object -First 1)
  if ($LASTEXITCODE -ne 0) { $line = "$line,FAILRC$LASTEXITCODE" }
  Add-Content -Path $results -Value $line -Encoding utf8
  Write-Host "> $store cs=$cs $wl sz=$size es=$es loc=$loc"
}

# Small Edit / Large Document invariant cells (contract section 8).
foreach ($sz in @(1048576, 10485760, 104857600)) {
  foreach ($es in @(1, 1024)) {
    Run-Cell 'flat' 0 'W6' $sz 100 4 $es 'middle'
    Run-Cell 'chunked' 65536 'W6' $sz 100 4 $es 'middle'
  }
}

# Edit-size sweep on 10MB middle.
foreach ($es in @(1, 10, 100, 1024, 10240, 102400)) {
  Run-Cell 'flat' 0 'W6' 10485760 100 4 $es 'middle'
  Run-Cell 'chunked' 65536 'W6' 10485760 100 4 $es 'middle'
}

# Locality sweep (1KB edits, 10MB).
foreach ($loc in @('append', 'middle', 'random', 'hot')) {
  Run-Cell 'flat' 0 'W6' 10485760 100 4 1024 $loc
  Run-Cell 'chunked' 65536 'W6' 10485760 100 4 1024 $loc
}

Write-Host "EVS-2 sweep complete -> $results"
