"""Check the real native/cross package and keep its architecture-specific name."""
from pathlib import Path
import subprocess,sys,tempfile,zipfile
binary=Path(sys.argv[1]).resolve()
arch=int.from_bytes(binary.read_bytes()[18:20],'little')
name='pokedaisy-linux-'+{62:'x86_64',183:'aarch64'}[arch]
script=Path(__file__).with_name('package.py')
with tempfile.TemporaryDirectory(prefix='pd-linux-package-') as d:
    result=subprocess.run([sys.executable,str(script),str(binary),d],capture_output=True,text=True)
    assert result.returncode==0,result.stderr
    archive=Path(d)/(name+'.zip')
    assert archive.exists() and Path(result.stdout.strip())==archive
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        names=z.namelist();assert len(names)==10,names
        assert all(n.startswith(name+'/') for n in names)
        assert z.read(name+'/pokedaisy-linux')==binary.read_bytes()
        assert z.getinfo(name+'/launch.sh').external_attr>>16 & 0o111
        assert z.read(name+'/SOURCE.txt').startswith(b'Source: https://github.com/lidor30/pokedaisy')
        assert not any(n.lower().endswith(('.gb','.gbc','.gba','.sav','.ss','.srm','.bin')) for n in names)
    before=archive.read_bytes()
    result=subprocess.run([sys.executable,str(script),str(binary),d],capture_output=True,text=True)
    assert result.returncode!=0 and archive.read_bytes()==before
print('PASS architecture-specific package, actual executable, license/source records and overwrite refusal')
