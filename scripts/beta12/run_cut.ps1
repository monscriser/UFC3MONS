# One 2x fight with a given DLL and extra cvars; optional draw trace frame and visible window. Restores the DLL.
param([string]$Dll,[string]$Tag,[string]$Extra='',[int]$Segundos=70,[string]$TraceFrame='',[switch]$Visible,[switch]$Gpu)
$T="C:\Users\Monscriser\Documents\Codex\2026-10-27\continue-a-investiga-o-da-faixa\work\dlc-test"
$bk="E:\PROJETOS ANTIGOS\UFC3MONS-trabalho\gpu-port-a16\bin-trace-backup\rexgpu-xenos.dll"
if($TraceFrame){ $env:UFC_DRAW_TRACE_FRAME=$TraceFrame }
if($Visible){ $env:UFC_TEST_VISIBLE='1' }
if($Gpu){ $env:UFC_MEASURE='1'; $env:UFC_MEASURE_GPU='1' }
try {
  Copy-Item $Dll "$T\bin-trace\rexgpu-xenos.dll" -Force
  & "$T\luta-abrir.ps1" -Caso repro2x -Build TRACE -Segundos $Segundos -Extra $Extra -Foto "cut-$Tag" | Out-Null
  for($k=0;$k -lt 30 -and (Get-Process ufc3-launcher-l1 -ErrorAction SilentlyContinue);$k++){ Start-Sleep 1 }
} finally {
  Remove-Item Env:UFC_DRAW_TRACE_FRAME,Env:UFC_TEST_VISIBLE,Env:UFC_MEASURE,Env:UFC_MEASURE_GPU -ErrorAction SilentlyContinue
  Start-Sleep 3; Copy-Item $bk "$T\bin-trace\rexgpu-xenos.dll" -Force
}
$L=(Get-ChildItem "$T\casos\repro2x\game\.ufc-reborn\logs\TRACE-*.log" | Sort LastWriteTime | Select -Last 1).FullName
"$Tag log=$L"
