"""Package tested cached-compiler output, with fresh D8 Release/API26 output.

This is a local fallback artifact, not a successful Gradle or CI release.
"""
from pathlib import Path
import argparse, hashlib, json, re, subprocess, time, zipfile
import xml.etree.ElementTree as ET

p=argparse.ArgumentParser()
p.add_argument('compiled',type=Path)
p.add_argument('output',type=Path)
p.add_argument('--java',default='F:/Runtimes/Java21/bin/java.exe')
a=p.parse_args()
root=Path(__file__).resolve().parents[2]
import sys
sys.pycache_prefix = str((root / '../builds/steamlink-patches/build/python-cache').resolve())
sys.path.insert(0, str(root / 'tools'))
from build_paths import relocate_classpath
compiled=a.compiled.resolve()
output=a.output.resolve()
assert not output.exists(),output
reports=list((compiled/'test-results').glob('TEST-*.xml'))
assert reports,'Missing JUnit results'
for report in reports:
    suite=ET.parse(report).getroot()
    assert int(suite.get('failures',0))==int(suite.get('errors',0))==0
version=re.search(r'(?m)^version\s*=\s*(\S+)',(root/'gradle.properties').read_text(encoding='utf-8')).group(1)
patcher=re.search(r'morphe-patcher\s*=\s*"([^"]+)"',(root/'gradle/libs.versions.toml').read_text(encoding='utf-8')).group(1)
entries={}
for source,classes_only in [(compiled/'classes',True),(root/'patches/src/main/resources',False),(compiled/'resources',False)]:
    for path in sorted(source.rglob('*')):
        if not path.is_file() or (classes_only and path.suffix not in ('.class','.kotlin_module')): continue
        name=path.relative_to(source).as_posix()
        assert name not in entries,name
        entries[name]=path.read_bytes()
classes=compiled/'d8-input.jar'
with zipfile.ZipFile(classes,'w',zipfile.ZIP_DEFLATED) as z:
    for name,data in entries.items():
        if name.endswith('.class'): z.writestr(name,data)
dex=compiled/'d8'
dex.mkdir(exist_ok=False)
cp=relocate_classpath((compiled/'runtime-classpath.txt').read_text(encoding='utf-8').strip(), root)
command=[a.java,'-Xmx1g','-cp',str(root/'../builds/steamlink-patches/build/tooling/r8-9.4.17.jar'),'com.android.tools.r8.D8',
         '--release','--min-api','26','--lib',str(root/'.android-sdk/platforms/android-33/android.jar')]
for entry in cp.split(';'):
    if entry.endswith('.jar'): command+=['--classpath',entry]
command+=['--output',str(dex),str(classes)]
with (compiled/'d8.log').open('w',encoding='utf-8') as log:
    subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
for path in sorted(dex.glob('*.dex')): entries[path.name]=path.read_bytes()
assert entries['classes.dex'].startswith(b'dex\n')
for name in ('extension.mpe','minimal-extension.mpe','battery-extension.mpe'):
    assert entries[f'extensions/{name}'].startswith(b'dex\n')
fields={'Manifest-Version':'1.0','Name':'Steam Link GalaxyXR Patches',
        'Description':'Exact 2.0.20/5001812 and 2.0.21/5001968 local adaptation',
        'Version':version,'Timestamp':str(int(time.time()*1000)),
        'Source':'https://github.com/AngelDark92/steamlink-patches','Author':'AngelDark92',
        'Contact':'na','Website':'na','License':'GPLv3','Patcher-Version':patcher}
lines=[]
for key,value in fields.items():
    line=f'{key}: {value}'
    while len(line)>70: lines.append(line[:70]);line=' '+line[70:]
    lines.append(line)
entries['META-INF/MANIFEST.MF']=('\r\n'.join(lines)+'\r\n\r\n').encode('ascii')
output.parent.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED) as z:
    for name,data in entries.items():z.writestr(name,data)
receipt=dict(path=str(output),sha256=hashlib.sha256(output.read_bytes()).hexdigest(),version=version,
             entries=len(entries),dexBytes=len(entries['classes.dex']),build='Cached Kotlin + D8 Release/API26; not Gradle/CI release')
(compiled/'bundle.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
# Use the archive itself as the class/resource source for subsequent Morphe audits.
dependencies=[e for e in cp.split(';') if e.endswith('.jar')]
(compiled/'archive-classpath.txt').write_text(';'.join([str(output),*dependencies]),encoding='utf-8')
print(json.dumps(receipt,indent=2))
