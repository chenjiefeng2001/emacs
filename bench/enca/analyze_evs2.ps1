$rows = Import-CSV "C:\Users\14977\source\repos\emacs\bench\results\evs2_amplification.csv"
Write-Output "=== Small-Edit/Large-Document (1B edit, middle) ==="
$rows | Where-Object { $_.edit_size -eq '1' -and $_.locality -eq 'middle' } | ForEach-Object { "{0,-8} sz={1,10} copied={2,12} amp={3,14} p50={4,8}ms" -f $_.store,$_.size,$_.copied_total,[math]::Round([double]$_.copy_amp),[math]::Round([double]$_.p50_ms,4) }
Write-Output ""
Write-Output "=== 1KB edit amplification by size ==="
$rows | Where-Object { $_.edit_size -eq '1024' -and $_.locality -eq 'middle' } | ForEach-Object { "{0,-8} sz={1,10} copied={2,12} amp={3,12}" -f $_.store,$_.size,$_.copied_total,[math]::Round([double]$_.copy_amp,1) }
Write-Output ""
Write-Output "=== Locality sweep (chunked 64K, 1KB edits @10MB) ==="
$rows | Where-Object { $_.store -eq 'chunked' -and $_.edit_size -eq '1024' } | ForEach-Object { "{0,-8} copied={1,8} p50={2,8}ms" -f $_.locality,$_.copied_total,[math]::Round([double]$_.p50_ms,4) }
Write-Output ""
$okCount = ($rows | Where-Object { $_.ok -eq 'True' }).Count
Write-Output "all-ok=$($okCount -eq $rows.Count) ($okCount/$($rows.Count))"
