# UFC3MONS R8 build: starts from the exact R7 snapshots/commands in ../parity-full.
#   python3 build_r8.py control   -> relink R7 objects unchanged, compare with R7 hashes
#   python3 build_r8.py build     -> R8: input lock + present-on-guest-frame
# Writes only inside this folder. Reads parity-full, de/ work objects and E:/UFC3-RexGlue-Lab/build.
from pathlib import Path
import json, subprocess, hashlib, sys, shutil
root = Path(__file__).resolve().parent
r7 = root.parent / 'parity-full'
mode = sys.argv[1]
r7_hash = {(r['platform'], r['edition']): r['sha256'] for r in json.loads((r7 / 'apps-build-results.json').read_text())}
CWD = {('linux', 'reborn'): '/mnt/e/UFC3-RexGlue-Lab/build/native-linux-v29',
       ('linux', 'original'): '/mnt/e/UFC3-RexGlue-Lab/build/native-linux-v29',
       ('windows', 'reborn'): '/mnt/e/UFC3-RexGlue-Lab/build/native-r9-sdk',
       ('windows', 'original'): '/mnt/e/UFC3-RexGlue-Lab/build/original-v48'}
NEW_WRAPS = ['__imp__XamInputGetCapabilities', '__imp__XamInputSetState', '__imp__XamInputGetKeystrokeEx']

def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def run(cmd, label, cwd, folder):
    (folder / (label + '.command.json')).write_text(json.dumps(cmd, indent=2))
    with (folder / (label + '.log')).open('w') as log:
        subprocess.run(cmd, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=True)
    print(label, 'OK', flush=True)

def patch_main(text):
    # The window gets its presenter after SetupOverlays/OnCreateDialogs, so attach
    # after the base SetupPresentation (game mode only: the configurator has no
    # guest frames and needs the continuous UI repaint).
    a = ' protected:\n  void OnCreateDialogs('
    assert text.count(a) == 1
    text = text.replace(a, ' protected:\n  bool SetupPresentation() override {\n'
                        '    const bool ok = rex::ReXApp::SetupPresentation();\n'
                        '    if (ok && fps_overlay_) UfcAttachOverlayToGuestFrames(imgui_drawer(), UfcWindowPresenter(window()));\n'
                        '    return ok;\n  }\n  void OnCreateDialogs(')
    b = 'fps_overlay_.reset();'
    assert text.count(b) == 1
    text = text.replace(b, 'UfcDetachOverlayFromGuestFrames();\n    ' + b)
    c = '#include "fps_overlay.h"'
    assert text.count(c) == 1
    text = text.replace(c, c + '\n#include <rex/ui/presenter.h>\n#include <rex/ui/window.h>\n'
                        'void UfcAttachOverlayToGuestFrames(rex::ui::ImGuiDrawer*, rex::ui::Presenter*);\n'
                        'void UfcDetachOverlayFromGuestFrames();\n'
                        '// Window::presenter() is protected; read it through a derived-class member pointer.\n'
                        'struct UfcWindowPeek : rex::ui::Window {\n'
                        '  static rex::ui::Presenter* Get(rex::ui::Window* w) { return w ? (w->*(&UfcWindowPeek::presenter))() : nullptr; }\n'
                        '};\n'
                        'static rex::ui::Presenter* UfcWindowPresenter(rex::ui::Window* w) { return UfcWindowPeek::Get(w); }')
    return text

def patch_test_input(text):
    a = ' __real___imp__XamInputGetState(ctx,base);\n static const auto file'
    assert text.count(a) == 1
    text = text.replace(a, ' UfcInputLockedCall(__real___imp__XamInputGetState,ctx,base,"GetState");\n static const auto file')
    d = 'extern "C" void __real___imp__XamInputGetState(PPCContext&,uint8_t*);'
    assert text.count(d) == 1
    return text.replace(d, d + '\nvoid UfcInputLockedCall(void(*)(PPCContext&,uint8_t*),PPCContext&,uint8_t*,const char*);')

def compile_part(platform, edition, part, source, folder, suffix):
    cmd = json.loads((r7 / f'{platform}-{edition}-{part}.command.json').read_text())
    cmd.insert(1, '-I' + str(root / 'src'))
    cmd[cmd.index('-c') + 1] = str(source)
    obj = folder / (source.stem + suffix)
    cmd[cmd.index('-o') + 1] = str(obj)
    run(cmd, f'{platform}-{edition}-{source.stem}', CWD[(platform, edition)], folder)
    return obj

report = []
for platform in ['windows', 'linux']:
    for edition in ['reborn', 'original']:
        folder = root / mode / platform / edition
        if folder.exists():
            shutil.move(str(folder), str(root / '_to_delete' / f'{mode}-{platform}-{edition}-{len(list((root / "_to_delete").glob("*"))) if (root / "_to_delete").exists() else 0}'))
        folder.mkdir(parents=True)
        suffix = '.obj' if platform == 'windows' else '.o'
        link = json.loads((r7 / f'{platform}-{edition}-link.command.json').read_text())
        binary = 'ufc3-original-v48' if edition == 'original' else 'ufc3-launcher-l1'
        exe = folder / (binary + ('.exe' if platform == 'windows' else ''))
        link[link.index('-o') + 1] = str(exe)
        if mode == 'build':
            r7dir = r7 / platform / edition
            main = folder / 'main.cpp'; main.write_text(patch_main(r7dir.joinpath('main.cpp').read_text()))
            tin = folder / 'ufc_test_input.cpp'; tin.write_text(patch_test_input(r7dir.joinpath('ufc_test_input.cpp').read_text()))
            fix = folder / 'ufc_r8_fixes.cpp'; fix.write_bytes((root / 'src' / 'ufc_r8_fixes.cpp').read_bytes())
            main_obj = compile_part(platform, edition, 'main', main, folder, suffix)
            tin_obj = compile_part(platform, edition, 'ufc_test_input', tin, folder, suffix)
            fix_obj = compile_part(platform, edition, 'ufc_test_input', fix, folder, suffix)
            old_main = str(r7dir / ('main' + suffix)); old_tin = str(r7dir / ('ufc_test_input' + suffix))
            assert link.count(old_main) == 1 and link.count(old_tin) == 1
            link[link.index(old_main)] = str(main_obj)
            link.insert(link.index(str(main_obj)) + 1, str(fix_obj))
            link[link.index(old_tin)] = str(tin_obj)
            anchor = '-Wl,--wrap=__imp__XamInputGetState'
            assert link.count(anchor) == 1
            for w in reversed(NEW_WRAPS):
                link.insert(link.index(anchor) + 1, '-Wl,--wrap=' + w)
        run(link, f'{platform}-{edition}-link', CWD[(platform, edition)], folder)
        h = sha(exe)
        entry = {'platform': platform, 'edition': edition, 'file': str(exe), 'sha256': h, 'r7_sha256': r7_hash[(platform, edition)]}
        if mode == 'control':
            entry['matches_r7'] = h == r7_hash[(platform, edition)]
        report.append(entry)
        print(json.dumps(entry), flush=True)
(root / f'{mode}-results.json').write_text(json.dumps(report, indent=2))
