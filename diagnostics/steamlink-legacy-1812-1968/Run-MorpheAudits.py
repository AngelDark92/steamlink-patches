"""Run each exact-base Morphe case in its own JVM; retain logs and output hashes.

Inputs are copied into a task-owned fixture directory. Original APKs are never
patched in place. Outputs are unsigned evidence, not deployment artifacts.
"""
from pathlib import Path
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import shutil
import subprocess

p = argparse.ArgumentParser()
p.add_argument('classpath', type=Path, help='File containing the compiled/archive runtime classpath')
p.add_argument('output', type=Path, help='Fresh output directory')
p.add_argument('--java', default='F:/Runtimes/Java21/bin/java.exe')
p.add_argument('--bundles-only', action='store_true')
args = p.parse_args()
root = Path(__file__).resolve().parents[2]
import sys
sys.pycache_prefix = str((root / '../builds/steamlink-patches/build/python-cache').resolve())
sys.path.insert(0, str(root / 'tools'))
from build_paths import relocate_classpath
out = args.output.resolve()
assert not out.exists(), f'Refusing to reuse {out}'
out.mkdir(parents=True)
fixtures = out / 'fixtures'
fixtures.mkdir()
cp = relocate_classpath(args.classpath.read_text().strip(), root)
versions = [('2.0.20', '5001712'), ('2.0.22', '5002244'), ('2.0.23', '5002363'),
            ('2.0.20', '5001812'), ('2.0.21', '5001968')]
inputs = []
for version, code in versions:
    name = f'decoded-apk-android-steamlinkvr-release-base-{version}-{code}.apk'
    original = (root.parent / 'Best Apks' / f'android-steamlinkvr-release-{version}-{code}.apk'
                if code in ('5001812', '5001968') else root / '../builds/steamlink-patches/build/decoded-fixture-apks' / name)
    assert original.is_file(), original
    shutil.copyfile(original, fixtures / name)
    inputs.append(dict(version=version, code=code, source=str(original),
                       sha256=hashlib.sha256(original.read_bytes()).hexdigest(),
                       provenance='original signed APK' if code in ('5001812', '5001968') else 'decoded fixture'))
(out / 'inputs.json').write_text(json.dumps(inputs, indent=2)+'\n')
cases = []
def decoded(kind, index):
    name=f'{kind}-{index}'
    cases.append((name, ['util.DecodedSteamLinkPatchAudit', str(fixtures), str(out / name), kind, str(index)]))
for i in range(5):
    decoded('recommended', i)
if not args.bundles_only:
    for i in range(20,60):
        decoded('public', i)
    for i in range(5):
        decoded('high-resolution', i)
    decoded('startup-excluded', 0)
    for version, code in versions[3:]:
        apk=fixtures/f'decoded-apk-android-steamlinkvr-release-base-{version}-{code}.apk'
        for depth,gamma in [('off','1.30'),('8-bit','1.02'),('10-bit','1.30')]:
            name=f'vd-{code}-{depth}'
            cases.append((name,['util.VdSdrMorpheAudit',str(apk),str(out/name),depth,gamma]))
        for depth in ['8-bit','10-bit']:
            for layer,oled,order,gamma in [('fovea','false','blue-first','1.00'),
                                          ('background','false','blue-first','1.00'),
                                          ('both-fovea-first','true','blue-first','1.02'),
                                          ('both-background-first','true','oled-first','1.30')]:
                name=f'blue-{code}-{depth}-{layer}'
                cases.append((name,['util.BlueNoiseMorpheAudit',str(apk),str(out/name),depth,oled,order,layer,gamma]))
def run(case):
    name, arguments=case
    log=out/f'{name}.log'
    with log.open('w',encoding='utf-8') as stream:
        result=subprocess.run([args.java,'-Xmx1g','-cp',cp,*arguments],cwd=root,stdout=stream,stderr=subprocess.STDOUT)
    outputs=[dict(path=str(f.relative_to(out)),size=f.stat().st_size,sha256=hashlib.sha256(f.read_bytes()).hexdigest())
             for f in (out/name).rglob('*unsigned.apk')]
    row=dict(case=name,exitCode=result.returncode,log=str(log.relative_to(out)),outputs=outputs)
    print(('PASS' if result.returncode==0 else 'FAIL')+' '+name,flush=True)
    return row
with ThreadPoolExecutor(max_workers=2) as pool:
    results=list(pool.map(run,cases))
(out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
for source in inputs:
    assert hashlib.sha256(Path(source['source']).read_bytes()).hexdigest()==source['sha256'],source
print(f'{sum(r["exitCode"]==0 for r in results)}/{len(results)} Morphe cases passed; source hashes unchanged',flush=True)
raise SystemExit(0 if all(r['exitCode']==0 for r in results) else 1)
