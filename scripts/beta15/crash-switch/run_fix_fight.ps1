param([string]$Tag='fix-luta',[int]$Segundos=90)
# Luta Reborn minimizada (controle virtual) com os binarios da BETA 1.5 corrigida (exe Reborn + runtime + DLL de video).
# Faz backup dos binarios do harness e restaura no fim.
$T="C:\Users\Monscriser\Documents\Codex\2026-10-27\continue-a-investiga-o-da-faixa\work\dlc-test"
$H=$PSScriptRoot; $P='E:\PROJETOS ANTIGOS\UFC3MONS-trabalho\BETA-1.5\Windows\data'
$files='ufc3-launcher-l1.exe','librexruntime.dll','rexgpu-xenos.dll'
$bk="$H\harness-backup"; New-Item -ItemType Directory $bk -Force | Out-Null
foreach($f in $files){ if(!(Test-Path "$bk\$f")){ Copy-Item "$T\bin-trace\$f" "$bk\$f" } }
try {
  foreach($f in $files){ Copy-Item "$P\$f" "$T\bin-trace\$f" -Force }
  & "$T\luta-abrir.ps1" -Caso repro2x -Build TRACE -Segundos $Segundos -Foto $Tag | Out-Null
  for($k=0;$k -lt 30 -and (Get-Process ufc3-launcher-l1 -ErrorAction SilentlyContinue);$k++){ Start-Sleep 1 }
  $L=(Get-ChildItem "$T\casos\repro2x\game\.ufc-reborn\logs\TRACE-*.log" | Sort LastWriteTime | Select -Last 1).FullName
  Copy-Item $L "$H\$Tag.log"; "log=$L"
} finally {
  Start-Sleep 3
  foreach($f in $files){ Copy-Item "$bk\$f" "$T\bin-trace\$f" -Force }
  foreach($f in $files){ "{0} restaurado={1}" -f $f, ((Get-FileHash "$bk\$f").Hash -eq (Get-FileHash "$T\bin-trace\$f").Hash) }
}
