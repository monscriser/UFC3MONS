param([string]$Tag="m1",[int]$Segundos=70,[string]$Dll="",[string]$Extra="")
# Luta Reborn minimizada (controle virtual) com uma DLL de video; restaura a DLL do harness no fim.
$T="C:\Users\Monscriser\Documents\Codex\2026-10-27\continue-a-investiga-o-da-faixa\work\dlc-test"
$H=$PSScriptRoot; $bk="$H\bin-trace-original-rexgpu-xenos.dll"
if(!(Test-Path $bk)){ Copy-Item "$T\bin-trace\rexgpu-xenos.dll" $bk }
if(-not $Dll){ $Dll="$H\out-win\rexgpu-xenos.dll" }
$env:UFC_MEASURE='1'; $env:UFC_MEASURE_DRAW='1'
try {
  Copy-Item $Dll "$T\bin-trace\rexgpu-xenos.dll" -Force
  "dll=$((Get-FileHash "$T\bin-trace\rexgpu-xenos.dll").Hash.Substring(0,16))"
  & "$T\luta-abrir.ps1" -Caso repro2x -Build TRACE -Segundos $Segundos -Foto "dm-$Tag" -Extra $Extra | Out-Null
  for($k=0;$k -lt 30 -and (Get-Process ufc3-launcher-l1 -ErrorAction SilentlyContinue);$k++){ Start-Sleep 1 }
  $L=(Get-ChildItem "$T\casos\repro2x\game\.ufc-reborn\logs\TRACE-*.log" | Sort LastWriteTime | Select -Last 1).FullName
  Copy-Item $L "$H\$Tag.log"
  "log=$L"
} finally {
  Remove-Item Env:UFC_MEASURE,Env:UFC_MEASURE_DRAW -ErrorAction SilentlyContinue
  Start-Sleep 3; Copy-Item $bk "$T\bin-trace\rexgpu-xenos.dll" -Force
}
