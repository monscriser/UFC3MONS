# UFC3MONS R8.1 = R8 + ufc_spin_backoff ON by default (both editions, both platforms).
# The guest waits for GPU fences in sub_82374768 (cctpl / 8x db16cyc / cctpm spin step,
# identical in Reborn and Original). Recompiled, that step is a no-op, so the waiting
# thread burns a whole core; on the ROG Ally CPU and GPU share one power budget.
# The existing hook (ufc_caf_diagnostics.cpp) pauses/yields/sleeps 200 us between checks
# and always runs the original routine. UFC_V60_ufc_spin_backoff=false restores R8.
from pathlib import Path
import json, shutil, sys
root = Path(__file__).resolve().parent
sys.argv = [sys.argv[0], 'build81']
g ={'__file__': str(root / 'build_r8.py')}
exec(compile((root / 'build_r8.py').read_text().split('report = []')[0], 'build_r8_helpers', 'exec'), g)
r7, CWD, run, sha = g['r7'], g['CWD'], g['run'], g['sha']

def patch_spin(text):
    old = '    rex::cvar::SetFlagByName("ufc_spin_backoff","false");\n'
    if text.count(old) == 1:  # Reborn: keeps its UFC_V60_ufc_spin_backoff override and log
        return text.replace(old, '    // R8.1: bounded backoff in the guest GPU-fence spin (sub_82374768), on by default.\n'
                                 '    rex::cvar::SetFlagByName("ufc_spin_backoff","true");\n')
    # Original never set it (cvar default false). Same routine at the same address.
    anchor = '    if(!REXCVAR_GET(ufc_configurator)) rex::cvar::SetFlagByName("readback_resolve","none");\n'
    assert text.count(anchor) == 1 and 'ufc_spin_backoff' not in text
    return text.replace(anchor, anchor +
        '    // R8.1: bounded backoff in the guest GPU-fence spin (sub_82374768), on by default.\n'
        '    {\n'
        '      const char* v=std::getenv("UFC_V60_ufc_spin_backoff");\n'
        '      rex::cvar::SetFlagByName("ufc_spin_backoff",(v&&std::string_view(v)=="false")?"false":"true");\n'
        '      REXLOG_INFO("[ufc-v60-effective] ufc_spin_backoff={}",rex::cvar::GetFlagByName("ufc_spin_backoff"));\n'
        '    }\n')

report = []
for platform in ['windows', 'linux']:
    suffix = '.obj' if platform == 'windows' else '.o'
    for edition in ['reborn', 'original']:
        folder = root / 'build81' / platform / edition
        if folder.exists():
            dest = root / '_to_delete' / f'build81-{platform}-{edition}-{len(list((root / "_to_delete").glob("*")))}'
            dest.parent.mkdir(exist_ok=True); shutil.move(str(folder), str(dest))
        folder.mkdir(parents=True)
        r8dir = root / 'build' / platform / edition
        link = json.loads((r8dir / f'{platform}-{edition}-link.command.json').read_text())
        binary = 'ufc3-original-v48' if edition == 'original' else 'ufc3-launcher-l1'
        exe = folder / (binary + ('.exe' if platform == 'windows' else ''))
        link[link.index('-o') + 1] = str(exe)
        main = folder / 'main.cpp'; main.write_text(patch_spin((r8dir / 'main.cpp').read_text()))
        cmd = json.loads((r8dir / f'{platform}-{edition}-main.command.json').read_text())
        cmd[cmd.index('-c') + 1] = str(main)
        obj = folder / ('main' + suffix); cmd[cmd.index('-o') + 1] = str(obj)
        run(cmd, f'{platform}-{edition}-main', CWD[(platform, edition)], folder)
        old = str(r8dir / ('main' + suffix)); assert link.count(old) == 1
        link[link.index(old)] = str(obj)
        run(link, f'{platform}-{edition}-link', CWD[(platform, edition)], folder)
        report.append({'platform': platform, 'edition': edition, 'file': str(exe), 'sha256': sha(exe)})
        print(json.dumps(report[-1]), flush=True)
(root / 'build81-results.json').write_text(json.dumps(report, indent=2))
