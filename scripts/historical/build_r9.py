# UFC3MONS R9 release apps (Windows + Linux, Reborn + Original) = R8.1 (input lock, present on
# guest frame, spin backoff, vsync on) + command-thread cuts validated on the Ally in R8.3–R8.5:
# ufc_cp_fast_registers=true, ht_float_constant_runs=true. No measurement code (no forced
# UFC_WAIT_DIAG, no thread/hot samplers). Plugin/runtime are packaged separately.
from pathlib import Path
import json, shutil, sys
root = Path(__file__).resolve().parent
sys.argv = [sys.argv[0], 'build9final']
g = {'__file__': str(root / 'build_r8.py')}
exec(compile((root / 'build_r8.py').read_text().split('report = []')[0], 'build_r8_helpers', 'exec'), g)
CWD, run, sha = g['CWD'], g['run'], g['sha']

BLOCK = ('    // R9: command-thread CPU cuts already in the UFC plugin (Hydro 9.0.0 / 8.8 ports);\n'
         '    // validated on the ROG Ally (R8.3-R8.5). UFC_R9_CP_CUTS=false restores R8.1.\n'
         '    {\n'
         '      const char* v=std::getenv("UFC_R9_CP_CUTS");\n'
         '      const char* on=(v&&std::string_view(v)=="false")?"false":"true";\n'
         '      rex::cvar::SetFlagByName("ufc_cp_fast_registers",on);\n'
         '      rex::cvar::SetFlagByName("ht_float_constant_runs",on);\n'
         '      REXLOG_INFO("[ufc-cp-cpu] fast_registers={} float_constant_runs={}",rex::cvar::GetFlagByName("ufc_cp_fast_registers"),rex::cvar::GetFlagByName("ht_float_constant_runs"));\n'
         '    }\n')

def patch(text):
    for anchor in ['    rex::cvar::SetFlagByName("ufc_spin_backoff","true");\n',
                   '      REXLOG_INFO("[ufc-v60-effective] ufc_spin_backoff={}",rex::cvar::GetFlagByName("ufc_spin_backoff"));\n    }\n']:
        if text.count(anchor) == 1:
            return text.replace(anchor, anchor + BLOCK)
    raise AssertionError('anchor')

report = []
for platform in ['windows', 'linux']:
    suffix = '.obj' if platform == 'windows' else '.o'
    for edition in ['reborn', 'original']:
        folder = root / 'build9final' / platform / edition
        if folder.exists():
            dest = root / '_to_delete' / f'build9final-{platform}-{edition}-{len(list((root / "_to_delete").glob("*")))}'
            dest.parent.mkdir(exist_ok=True); shutil.move(str(folder), str(dest))
        folder.mkdir(parents=True)
        src = root / 'build81' / platform / edition
        link = json.loads((src / f'{platform}-{edition}-link.command.json').read_text())
        binary = 'ufc3-original-v48' if edition == 'original' else 'ufc3-launcher-l1'
        exe = folder / (binary + ('.exe' if platform == 'windows' else ''))
        link[link.index('-o') + 1] = str(exe)
        main = folder / 'main.cpp'; main.write_text(patch((src / 'main.cpp').read_text()))
        cmd = json.loads((src / f'{platform}-{edition}-main.command.json').read_text())
        cmd[cmd.index('-c') + 1] = str(main)
        obj = folder / ('main' + suffix); cmd[cmd.index('-o') + 1] = str(obj)
        run(cmd, f'{platform}-{edition}-main', CWD[(platform, edition)], folder)
        old = str(src / ('main' + suffix)); assert link.count(old) == 1
        link[link.index(old)] = str(obj)
        run(link, f'{platform}-{edition}-link', CWD[(platform, edition)], folder)
        report.append({'platform': platform, 'edition': edition, 'file': str(exe), 'sha256': sha(exe)})
        print(json.dumps(report[-1]), flush=True)
(root / 'build9final-results.json').write_text(json.dumps(report, indent=2))
