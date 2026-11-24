from pathlib import Path
import json,subprocess,tempfile
HERE=Path(__file__).resolve().parent
def main():
    with tempfile.TemporaryDirectory(prefix='dnds-path-build-') as build:
        build=Path(build)
        source=(HERE/'paths.c').read_text()
        def one(text,name):
            tree=Path(tempfile.mkdtemp(prefix='dnds-path-tree-'))
            try:
                src=build/(name+'.c');exe=build/name;src.write_text(text)
                args=['clang','-std=c11','-D_GNU_SOURCE','-Wall','-Wextra','-Werror','-Wpedantic','-O1',
                      '-fsanitize=address,undefined','-DDNDS_PATH_ROOT='+json.dumps(str(tree)),
                      '-I'+str(HERE),str(src),str(HERE/'test_paths.c'),'-o',str(exe)]
                p=subprocess.run(args,capture_output=True,text=True,timeout=30)
                if p.returncode:raise RuntimeError(p.stderr)
                return subprocess.run(['sudo','-n',str(exe)],capture_output=True,text=True,timeout=30)
            finally:
                p=subprocess.run(['sudo','-n','python3','-c','import shutil,sys;shutil.rmtree(sys.argv[1])',str(tree)],capture_output=True,text=True,timeout=30)
                if p.returncode:raise RuntimeError(p.stderr)
        p=one(source,'baseline')
        if p.returncode:raise RuntimeError(p.stdout+p.stderr)
        mutations={
          'accept-writable-system-parent':('!(s->st_mode&0022)','1'),
          'accept-writable-private-directory':('(s->st_mode&07777)==0700','1'),
          'forget-directory-binding':('a.st_ino!=b.st_ino','0'),
          'follow-symlink':('O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC','O_DIRECTORY|O_CLOEXEC'),
          'drop-cloexec':('O_NOFOLLOW|O_CLOEXEC','O_NOFOLLOW'),
          'ignore-close-error':('if(close(fd))failed(p);','(void)close(fd);'),
        }
        killed=[]
        for name,(a,b) in mutations.items():
            if a not in source:raise RuntimeError('mutation absent '+name)
            p=one(source.replace(a,b),name)
            if p.returncode==0 or 'Assertion' not in p.stderr:raise RuntimeError('mutation survived '+name+' '+p.stderr)
            killed.append(name)
        print(json.dumps({'status':'PASS','real_root_filesystem_scenarios':8,'assertion_killed_mutants':killed}))
if __name__=='__main__':main()
