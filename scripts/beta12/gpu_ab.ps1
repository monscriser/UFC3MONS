# GPU A/B: average real GPU time per frame (UFC_GPU_TIMING, gpu_sum_submission_ms) over the fight at the
# normal 60 FPS cap, for one video DLL. Reports mean/median of the last N frames logged.
param([string]$Dll,[string]$Tag,[string]$Extra='',[int]$Segundos=70)
$T="C:\Users\Monscriser\Documents\Codex\2026-10-27\continue-a-investiga-o-da-faixa\work\dlc-test"
$bk="E:\PROJETOS ANTIGOS\UFC3MONS-trabalho\gpu-port-a16\bin-trace-backup\rexgpu-xenos.dll"
$env:UFC_MEASURE='1'; $env:UFC_MEASURE_GPU='1'
try {
  Copy-Item $Dll "$T\bin-trace\rexgpu-xenos.dll" -Force
  & "$T\luta-abrir.ps1" -Caso repro2x -Build TRACE -Segundos $Segundos -Extra $Extra -Foto "gpuab-$Tag" | Out-Null
  for($k=0;$k -lt 30 -and (Get-Process ufc3-launcher-l1 -ErrorAction SilentlyContinue);$k++){ Start-Sleep 1 }
  $L=(Get-ChildItem "$T\casos\repro2x\game\.ufc-reborn\logs\TRACE-*.log" | Sort LastWriteTime | Select -Last 1).FullName
  $v=Select-String -Path $L -Pattern 'gpu_sum_submission_ms=([\d.]+)' | ForEach-Object { [double]$_.Matches[0].Groups[1].Value }
  $lk=Select-String -Path $L -Pattern 'ufc-texture-set-cache\] lookups=(\d+)' | ForEach-Object { [double]$_.Matches[0].Groups[1].Value }
  $sw=Select-String -Path $L -Pattern 'guest_swaps=(\d+)' | ForEach-Object { [double]$_.Matches[0].Groups[1].Value }
  $lkpf= if($sw.Count -gt 60){ ($lk[-1]-$lk[[int]($lk.Count*0.5)]) / ($sw[-1]-$sw[[int]($sw.Count*0.5)]) } else { 0 }
  $last=@($v | Select -Last 1800)
  $sorted=$last | Sort-Object
  $avg=($last|Measure -Average).Average
  "{0}: quadros={1} gpu_media_ms={2:N3} mediana_ms={3:N3} lookups_por_quadro~{4:N0} gpu_us_por_lookup={5:N3} log={6}" -f $Tag,$last.Count,$avg,$sorted[[int]($sorted.Count/2)],$lkpf,($avg*1000/[Math]::Max($lkpf,1)),(Split-Path $L -Leaf)
} finally {
  Remove-Item Env:UFC_MEASURE,Env:UFC_MEASURE_GPU -ErrorAction SilentlyContinue
  Start-Sleep 3; Copy-Item $bk "$T\bin-trace\rexgpu-xenos.dll" -Force
}
