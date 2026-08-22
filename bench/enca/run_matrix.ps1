# P2.1 representative experiment matrix runner.
# Full sweep variables are documented in bench/REPORT.md section 11;
# this script executes the core grid and appends one CSV per cell to
# bench/results/p21_storage_study.csv.
$ErrorActionPreference = 'Stop'
$here = Split-Path $MyInvocation.MyCommand.Path
Set-Location $here
$exe = '.\bench_enca.exe'
if (-not (Test-Path $exe)) { throw 'build bench_enca.exe first' }

$results = '..\results\p21_storage_study.csv'
"store,cs_label,workload,size,edits,retention,readers,ok,p50_ms,p90_ms,p99_ms,max_ms,copied_total,meta_total,live_delta,reader_mbs,label" |
  Out-File $results -Encoding utf8

function Run-Cell ($store, $cs, $wl, $size, $edits, $ret, $readers, $rms) {
  $args = @('--store', $store, '--workload', $wl, '--size', "$size",
            '--edits', "$edits", '--retention', "$ret",
            '--verify-every', '7', '--seed', '42',
            '--label', "p21")
  if ($cs -gt 0) { $args += @('--chunk', "$cs", '--cslabel', "${cs}K") }
  else { $args += @('--cslabel', '-') }
  if ($readers -gt 0) { $args += @('--readers', "$readers", '--read-ms', "$rms") }
  $line = (& $exe @args | Select-Object -First 1)
  if ($LASTEXITCODE -ne 0) { $line = $line + ",FAIL" }
  Add-Content -Path $results -Value $line -Encoding utf8
  Write-Host $line
}

foreach ($sz in @(65536, 1048576)) {
  $ed = 3000
  foreach ($wl in @('W1', 'W2')) {
    Run-Cell 'flat'    0 $wl $sz $ed 1  0 0
    Run-Cell 'chunked' 4096  $wl $sz $ed 1  0 0
    Run-Cell 'chunked' 65536 $wl $sz $ed 1  0 0
    Run-Cell 'chunked' 262144 $wl $sz $ed 1 0 0
  }
}

foreach ($sz in @(1048576, 10485760)) {
  $ed = 300
  foreach ($wl in @('W1', 'W3')) {
    foreach ($ret in @(1, 16)) {
      Run-Cell 'flat'    0      $wl $sz $ed $ret 0 0
      Run-Cell 'chunked' 65536  $wl $sz $ed $ret 0 0
    }
  }
}

# Reader concurrency on the big local-edit workload.
Run-Cell 'flat'    0      'W5' 10485760 100 16 8 200
Run-Cell 'chunked' 65536  'W5' 10485760 100 16 8 200

# Chunk-size sweep on the large-file local-edit workload.
foreach ($cs in @(4096, 16384, 65536, 262144)) {
  Run-Cell 'chunked' $cs 'W5' 104857600 60 4 0 0
}
Run-Cell 'flat' 0 'W5' 104857600 60 4 0 0

Write-Host "matrix complete -> $results"
