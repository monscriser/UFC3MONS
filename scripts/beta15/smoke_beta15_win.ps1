# BETA 1.5 Windows: start the game through UFC3.exe --play (zip contents, separate settings), check the session, close.
param([string]$Case='repro2x',[int]$Seconds=60)
$ErrorActionPreference='Stop'
$O='E:\PROJETOS ANTIGOS\UFC3MONS-trabalho\BETA-1.5'
$W="$O\teste-windows"; if(Test-Path $W){ Move-Item $W "$O\teste-windows-old-$(Get-Date -Format HHmmss)" }
New-Item -ItemType Directory $W | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::ExtractToDirectory("$O\UFC3MONS-BETA1.5-Windows.zip","$W\pkg")
$G="C:\Users\Monscriser\Documents\Codex\2026-10-27\continue-a-investiga-o-da-faixa\work\dlc-test\casos\$Case\game"
Remove-Item "$G\.ufc3mons-p11-camisas\game.lock" -ErrorAction SilentlyContinue
$t0=Get-Date
$env:UFC3_SETTINGS_ROOT="$W\settings"; $env:UFC_FORCE_SESSION_LOGS='1'
try {
  $p=Start-Process "$W\pkg\UFC3.exe" -ArgumentList @('--play','--game',"`"$G`"") -PassThru -WindowStyle Minimized
  Start-Sleep $Seconds
  $game=Get-Process ufc3-launcher-l1,ufc3-original-v48 -ErrorAction SilentlyContinue | ? { $_.Path -like "$W\pkg\*" }
  "game_alive=$([bool]$game) game=$($game.ProcessName) path=$($game.Path)"
} finally {
  Get-Process ufc3-launcher-l1,ufc3-original-v48,UFC3 -ErrorAction SilentlyContinue | ? { $_.Path -like "$W\pkg\*" } | Stop-Process -Force
  Remove-Item Env:UFC3_SETTINGS_ROOT,Env:UFC_FORCE_SESSION_LOGS -ErrorAction SilentlyContinue
}
Start-Sleep 2
$d=Get-ChildItem "$G\.ufc3mons-p11-camisas\logs" -Directory | ? { $_.LastWriteTime -ge $t0 } | Sort LastWriteTime | Select -Last 1
"log_dir=$($d.FullName)"
Select-String -Path "$($d.FullName)\launch.txt" -Pattern '^build_id=|^edition=|rexgpu-xenos.dll=|librexruntime.dll=' | % { $_.Line }
$log="$($d.FullName)\ufc3.log"
"log_lines=$((Get-Content $log).Count) crash=$((Select-String -Path $log -Pattern 'ufc-crash' -SimpleMatch).Count)"
Select-String -Path $log -Pattern '\[ufc-fps\]' | Select -Last 2 | % { $_.Line.Substring([Math]::Max(0,$_.Line.IndexOf('[ufc-fps]'))) }
New-Item -ItemType Directory "$O\provas\smoke-windows-$Case" -Force | Out-Null
Copy-Item "$($d.FullName)\launch.txt","$log" "$O\provas\smoke-windows-$Case\"
