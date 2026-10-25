"""Package a Linux executable and its existing license notices, never game data."""
from pathlib import Path
import argparse, shutil, subprocess, zipfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('binary',type=Path);p.add_argument('output',type=Path)
a=p.parse_args();repo=Path(__file__).resolve().parents[1]
header=a.binary.read_bytes()[:20]
if header[:6] != b'\x7fELF\x02\x01':p.error('Expected a 64-bit little-endian Linux ELF')
arch=int.from_bytes(header[18:20],'little')
if arch not in (62,183):p.error('Supported package targets: x86_64 or AArch64')
name='pokedaisy-linux-'+('aarch64' if arch==183 else 'x86_64')
a.output.mkdir(parents=True,exist_ok=True);dest=a.output/name
dest.mkdir() # Refuse to overwrite an earlier package.
for source,target in [(a.binary,'pokedaisy-linux'),(repo/'linux/launch.sh','launch.sh'),
                      (repo/'linux/README.md','README.md'),
                      (repo/'LICENSE','LICENSE'),(repo/'NOTICE','NOTICE')]:
    shutil.copy2(source,dest/target)
(dest/'pokedaisy-linux').chmod(0o755);(dest/'launch.sh').chmod(0o755)
licenses=dest/'licenses';licenses.mkdir()
for path,license_name in [('LICENSE','mGBA-MPL-2.0.txt'),('src/third-party/blip_buf/license.txt','blip_buf-LGPL-2.1.txt'),('src/third-party/inih/LICENSE.txt','inih-BSD.txt')]:
    shutil.copy2(repo/'third_party/mgba'/path,licenses/license_name)
(licenses/'PixelOperator.txt').write_text('Pixel Operator by Jayvee Enaguas. CC0. See NOTICE.\n')
sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip()
core=subprocess.check_output(['git','rev-parse','HEAD:third_party/mgba'],cwd=repo,text=True).strip()
(dest/'SOURCE.txt').write_text(f'Source: https://github.com/lidor30/pokedaisy\nFork: https://github.com/keeganwhitacre/pokedaisy-brick\nCommit: {sha}\nmGBA: {core}\nBuild: linux/README.md at this commit\n')
archive=a.output/(name+'.zip')
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
    for f in sorted(dest.rglob('*')):
        if f.is_file():z.write(f,f.relative_to(a.output))
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    assert not any(n.lower().endswith(('.gb','.gbc','.gba','.sav','.ss','.srm','.bin')) for n in z.namelist())
print(archive)
