# Original edition (casos\orig, isolated copy): opens with a given video DLL and the BETA 1.2 Original exe,
# goes Title -> Main menu -> Exhibition -> fight with the virtual pad, stays in the fight -Segundos, closes.
# -EnvList "NAME=value;NAME=value" sets extra environment variables. Restores the DLL and exe.
# -Amostra <dir>: at the end of the fight, sample every thread for 10 s (HtSamplerAll) for simbolizar2.py.
param([Parameter(Mandatory)][string]$Dll,[Parameter(Mandatory)][string]$Tag,[int]$Segundos=150,[string]$EnvList='',[string]$Extra='',[string]$Amostra='',[string]$Exe='')
$ErrorActionPreference='Stop'
$h="C:\Users\Monscriser\Documents\Codex\2026-10-27\continue-a-investiga-o-da-faixa\work\dlc-test"; $bt="$h\bin-trace"
$exe=$(if($Exe){$Exe}else{"E:\PROJETOS ANTIGOS\UFC3MONS-trabalho\BETA-1.2\Windows\data\ufc3-original-v48.exe"})
$bk="E:\PROJETOS ANTIGOS\UFC3MONS-trabalho\BETA-1.2\bin-trace-backup"
$bkd="E:\PROJETOS ANTIGOS\UFC3MONS-trabalho\gpu-port-a16\bin-trace-backup\rexgpu-xenos.dll"
$names=@()
try {
  Copy-Item $exe "$bt\ufc3-original-v48.exe" -Force
  Copy-Item "E:\PROJETOS ANTIGOS\UFC3MONS-trabalho\BETA-1.2\Windows\data\librexruntime.dll" "$bt\librexruntime.dll" -Force
  Copy-Item $Dll "$bt\rexgpu-xenos.dll" -Force
  foreach($kv in ($EnvList -split ';' | ? { $_ })){ $n,$v=$kv -split '=',2; Set-Item "Env:$n" $v; $names+=$n }
  $env:UFC_MEASURE='1'; $names+='UFC_MEASURE'
  if($Extra){ $env:UFC_EXTRA_ARGS=$Extra } else { Remove-Item Env:UFC_EXTRA_ARGS -ErrorAction SilentlyContinue }
  Remove-Item "$h\casos\orig\serial.txt" -ErrorAction SilentlyContinue
  & "$h\abrir-dlc.ps1" -Caso orig -Build TRACE -Virtual | Out-Null
  $info=Get-Content (Get-ChildItem "$h\casos\orig\sessao-TRACE-*.json" | Sort LastWriteTime | Select -Last 1).FullName -Raw | ConvertFrom-Json
  Start-Sleep 60
  & "$h\passo.ps1" -Caso orig -Seq '1000:3,10:8,10:8,1000:8,1000:6,1000:5,1000:5' -Foto "$Tag-menu" -Wait 3
  & "$h\passo.ps1" -Caso orig -Seq '1000:4,1000:6,1000:7,1000:3,1000:3,1000:2,1000:2' -Foto "$Tag-luta0" -Wait 25
  Start-Sleep $Segundos
  if($Amostra){
    if(-not ('HtSamplerAll' -as [type])){ Add-Type -Path 'C:\Users\Monscriser\Documents\Codex\2026-10-13\usan\work\Hydro-Thunder-fontes-revisao-7\tools\test\HtSamplerAll.cs' }
    $p=Get-Process -Id $info.pid
    $mods=@($p.Modules | ? { $_.ModuleName -like 'ufc3-*.exe' -or $_.ModuleName -in 'rexgpu-xenos.dll','librexruntime.dll' })
    $b=[long[]]@($mods | % { [long]$_.BaseAddress }); $e=[long[]]@($mods | % { [long]$_.BaseAddress + $_.ModuleMemorySize })
    $keys=@($mods | % { if($_.ModuleName -like '*.exe'){'E'} elseif($_.ModuleName -like 'rexgpu*'){'G'} else {'R'} })
    $lines=[HtSamplerAll]::Run($info.pid,10000,10,24,$b,$e)
    New-Item -ItemType Directory $Amostra -Force | Out-Null
    $lines | % { $f=$_ -split ' '; $o=@($f[0],$f[1]); foreach($x in $f[2..($f.Count-1)]){ $v=[Convert]::ToInt64($x,16); $k=$null; for($i=0;$i -lt $b.Count;$i++){ if($v -ge $b[$i] -and $v -lt $e[$i]){ $k=('{0}+{1:X}' -f $keys[$i],($v-$b[$i])) } }; $o+=$(if($k){$k}else{$x}) }; $o -join ' ' } | Set-Content "$Amostra\amostras.txt"
    $mods | % { $k=$(if($_.ModuleName -like '*.exe'){'E'} elseif($_.ModuleName -like 'rexgpu*'){'G'} else {'R'}); if($k -eq 'E'){"E $exe"}elseif($k -eq 'G'){"G $Dll"}else{"R E:\PROJETOS ANTIGOS\UFC3MONS-trabalho\BETA-1.2\Windows\data\librexruntime.dll"} } | Set-Content "$Amostra\modulos.txt"
  }
  & "$h\passo.ps1" -Caso orig -Foto "$Tag-luta1" -Wait 1
  "$Tag log=$($info.log)"
} finally {
  Get-Process ufc3-original-v48 -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep 3
  foreach($n in $names){ Remove-Item "Env:$n" -ErrorAction SilentlyContinue }
  Remove-Item Env:UFC_EXTRA_ARGS -ErrorAction SilentlyContinue
  Copy-Item "$bk\ufc3-original-v48.exe" "$bt\ufc3-original-v48.exe" -Force
  Copy-Item "$bk\librexruntime.dll" "$bt\librexruntime.dll" -Force
  Copy-Item $bkd "$bt\rexgpu-xenos.dll" -Force
  $u=Get-Item "$bt\user" -ErrorAction SilentlyContinue; if($u -and $u.LinkType){ $u.Delete() }
}
