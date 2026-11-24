#!/usr/bin/env python3
"""Create only /private/var/run in a NEW local ext4 image; never mount a device."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import tempfile

COMMANDS=('mkdir /private/var/run','set_inode_field /private/var/run mode 040755',
          'set_inode_field /private/var/run uid 0','set_inode_field /private/var/run gid 0')
BANNER=re.compile(r'debugfs [0-9]+(?:\.[0-9]+)+ \([0-9]{1,2}-[A-Za-z]{3}-[0-9]{4}\)')
FSCK_BANNER=re.compile(r'e2fsck [0-9]+(?:\.[0-9]+)+ \([0-9]{1,2}-[A-Za-z]{3}-[0-9]{4}\)')
STAT_FIRST=re.compile(r'Inode: ([1-9][0-9]*)\s+Type: ([a-z]+)\s+Mode:\s+([0-7]{3,4})\s+Flags: 0x[0-9a-fA-F]+')
STAT_OTHER=[re.compile(p) for p in (
 r'Generation: [0-9]+\s+Version: 0x[0-9a-fA-F]+:[0-9a-fA-F]+',
 r'User:\s+[0-9]+\s+Group:\s+[0-9]+\s+Project:\s+[0-9]+\s+Size: [0-9]+',
 r'File ACL: [0-9]+',r'Links: [0-9]+\s+Blockcount: [0-9]+',
 r'Fragment:\s+Address: [0-9]+\s+Number: [0-9]+\s+Size: [0-9]+',
 r'\s*(?:ctime|atime|mtime|crtime): 0x[0-9a-fA-F]+(?::[0-9a-fA-F]+)? -- [ -~]+',
 r'Size of extra inode fields: [0-9]+',r'Inode checksum: 0x[0-9a-fA-F]+',
 r'EXTENTS:',r'\([0-9]+(?:-[0-9]+)?\):[0-9]+(?:-[0-9]+)?(?:, \([0-9]+(?:-[0-9]+)?\):[0-9]+(?:-[0-9]+)?)*')]

class PrepareError(ValueError):pass

def _text(data):
    if type(data) is not bytes or len(data)>2*1024*1024 or not data.isascii() or b'\0' in data or b'\r' in data:
        raise PrepareError('unexpected tool output bytes/cap')
    return data.decode('ascii')

def validate_stat(stdout,stderr,*,absent=False):
    out,err=_text(stdout),_text(stderr)
    errlines=err.splitlines()
    if absent:
        if out or len(errlines)!=2 or not BANNER.fullmatch(errlines[0]) or errlines[1].rstrip()!='/private/var/run: File not found by ext2_lookup':
            raise PrepareError('run must be exactly absent; existing/unknown state refused')
        return None
    if len(errlines)!=1 or not BANNER.fullmatch(errlines[0]):raise PrepareError('debugfs stat stderr')
    lines=out.splitlines()
    if not lines:raise PrepareError('missing debugfs stat')
    first=STAT_FIRST.fullmatch(lines[0])
    if not first or any(line and not any(p.fullmatch(line) for p in STAT_OTHER) for line in lines[1:]):
        raise PrepareError('unexpected debugfs stat stdout')
    owners=re.findall(r'^User:\s+([0-9]+)\s+Group:\s+([0-9]+)\s+Project:\s+[0-9]+\s+Size: ([0-9]+)$',out,re.M)
    links=re.findall(r'^Links: ([0-9]+)\s+Blockcount: [0-9]+$',out,re.M)
    if len(owners)!=1 or len(links)!=1:raise PrepareError('duplicate/missing stat metadata')
    return dict(inode=int(first[1]),type=first[2],mode=int(first[3],8),uid=int(owners[0][0]),gid=int(owners[0][1]),size=int(owners[0][2]),nlink=int(links[0]))

def validate_mutation(data):
    lines=_text(data).splitlines()
    headers=[line for line in lines if BANNER.fullmatch(line)]
    echoes=[line[len('debugfs: '):] for line in lines if line.startswith('debugfs: ')]
    if len(headers)!=1 or echoes!=list(COMMANDS) or any(line and not BANNER.fullmatch(line) and not line.startswith('debugfs: ') for line in lines):
        raise PrepareError('debugfs mutation batch reported unexpected result')

def validate_fsck(data):
    lines=_text(data).splitlines()
    if len(lines)!=7 or not FSCK_BANNER.fullmatch(lines[0]):raise PrepareError('unexpected e2fsck framing')
    expected=('Pass 1: Checking inodes, blocks, and sizes','Pass 2: Checking directory structure',
              'Pass 3: Checking directory connectivity','Pass 4: Checking reference counts','Pass 5: Checking group summary information')
    if tuple(lines[1:6])!=expected or not re.fullmatch(r'[^\r\n]+: [0-9]+/[0-9]+ files \([0-9.]+% non-contiguous\), [0-9]+/[0-9]+ blocks',lines[6]):
        raise PrepareError('e2fsck did not report exactly clean complete passes')

def _fingerprint(s):return (s.st_dev,s.st_ino,s.st_mode,s.st_uid,s.st_gid,s.st_nlink,s.st_size,s.st_mtime_ns,s.st_ctime_ns)
def _digest_fd(fd,size):
    h=hashlib.sha256();offset=0
    while offset<size:
        b=os.pread(fd,min(1024*1024,size-offset),offset)
        if not b:raise PrepareError('input changed while hashing')
        h.update(b);offset+=len(b)
    return h.hexdigest()

def _copy_sparse(source,dest,size):
    offset=0;written=0;zero=bytes(1024*1024)
    while offset<size:
        b=os.pread(source,min(len(zero),size-offset),offset)
        if not b:raise PrepareError('input changed while copying')
        if b!=zero[:len(b)]:
            os.lseek(dest,offset,os.SEEK_SET);position=0
            while position<len(b):
                n=os.write(dest,b[position:])
                if n<=0:raise PrepareError('short sparse copy write')
                position+=n
            written+=len(b)
        offset+=len(b)
    os.ftruncate(dest,size);os.fsync(dest);return written

class Runner:
    def __init__(self):self.log=bytearray()
    def run(self,argv,*,fds=(),merged=False):
        self.log.extend(('ARGV '+json.dumps(argv,separators=(',',':'))+'\n').encode())
        try:r=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT if merged else subprocess.PIPE,pass_fds=fds,timeout=45)
        except subprocess.TimeoutExpired as e:raise PrepareError('tool deadline') from e
        self.log.extend(b'STDOUT\n'+r.stdout+b'\nSTDERR\n'+(r.stderr or b'')+b'\nRETURN '+str(r.returncode).encode()+b'\n')
        if len(self.log)>4*1024*1024:raise PrepareError('tool log cap')
        if r.returncode:raise PrepareError('tool nonzero '+str(r.returncode))
        return r

def prepare(input_path,output_path,*,expected_sha256,runner=None):
    runner=runner or Runner();source=Path(input_path).absolute();output=Path(output_path).absolute()
    if output.name in ('','.','..') or any(c in str(output) for c in ('\0','\n','\r')):raise PrepareError('output name')
    if type(expected_sha256) is not str or not re.fullmatch('[0-9a-f]{64}',expected_sha256):raise PrepareError('exact expected input SHA256 required')
    named=source.lstat()
    if not stat.S_ISREG(named.st_mode):raise PrepareError('input must be a real regular image')
    source_fd=os.open(source,os.O_RDONLY|os.O_NOFOLLOW|os.O_CLOEXEC|os.O_NONBLOCK)
    parent_fd=-1;published=[];sidecars=[output.name,output.name+'.record.json',output.name+'.filesystem.log',output.name+'.failed.log']
    try:
        before=os.fstat(source_fd)
        if (before.st_dev,before.st_ino)!=(named.st_dev,named.st_ino):raise PrepareError('input name changed')
        if not stat.S_ISREG(before.st_mode) or not 16*1024*1024<=before.st_size<=1024*1024*1024 or before.st_size%4096:
            raise PrepareError('input must be bounded regular ext4 image')
        if os.pread(source_fd,2,1080)!=b'\x53\xef' or not int.from_bytes(os.pread(source_fd,4,1120),'little')&0x40:
            raise PrepareError('input is not selected ext4 extent filesystem')
        parent_fd=os.open(output.parent,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW|os.O_CLOEXEC)
        parent=os.fstat(parent_fd)
        if parent.st_uid!=os.geteuid() or stat.S_IMODE(parent.st_mode)!=0o700:raise PrepareError('output parent must be owned private0700 directory')
        for name in sidecars:
            try:os.stat(name,dir_fd=parent_fd,follow_symlinks=False)
            except FileNotFoundError:continue
            raise PrepareError('output or sidecar already exists')
        source_digest=_digest_fd(source_fd,before.st_size)
        if source_digest!=expected_sha256:raise PrepareError('input SHA256 mismatch')
        source_name=f'/proc/self/fd/{source_fd}'
        r=runner.run(['/usr/bin/e2fsck','-fn',source_name],fds=(source_fd,),merged=True);validate_fsck(r.stdout)
        parents={}
        for name in ('/private','/private/var'):
            r=runner.run(['/usr/bin/debugfs','-R','stat '+name,source_name],fds=(source_fd,))
            v=validate_stat(r.stdout,r.stderr)
            if v['type']!='directory' or v['uid'] or v['gid'] or v['mode']&0o022:raise PrepareError('private/var must be real nonwritable root directories')
            parents[name]=v
        r=runner.run(['/usr/bin/debugfs','-R','stat /private/var/run',source_name],fds=(source_fd,));validate_stat(r.stdout,r.stderr,absent=True)
        # Only now allocate and mutate an unpublished copy in the caller's private dir.
        with tempfile.TemporaryDirectory(prefix='.prepare-run-',dir=f'/proc/self/fd/{parent_fd}') as workname:
            work=Path(workname);image=work/'image.img';fd=os.open(image,os.O_RDWR|os.O_CREAT|os.O_EXCL|os.O_CLOEXEC,0o600)
            try:copy_bytes=_copy_sparse(source_fd,fd,before.st_size)
            finally:os.close(fd)
            batch=work/'commands';batch.write_text('\n'.join(COMMANDS)+'\n');batch.chmod(0o600)
            r=runner.run(['/usr/bin/debugfs','-w','-f',str(batch),str(image)],fds=(parent_fd,),merged=True);validate_mutation(r.stdout)
            r=runner.run(['/usr/bin/debugfs','-R','stat /private/var/run',str(image)],fds=(parent_fd,));new=validate_stat(r.stdout,r.stderr)
            if new['type']!='directory' or new['mode']!=0o755 or new['uid'] or new['gid'] or new['nlink']!=2 or new['inode'] in [x['inode'] for x in parents.values()]:raise PrepareError('new run directory final metadata')
            r=runner.run(['/usr/bin/e2fsck','-fn',str(image)],fds=(parent_fd,),merged=True);validate_fsck(r.stdout)
            if _fingerprint(os.fstat(source_fd))!=_fingerprint(before) or _fingerprint(source.lstat())!=_fingerprint(before) or _digest_fd(source_fd,before.st_size)!=source_digest:raise PrepareError('input changed')
            current_parent=output.parent.lstat()
            if not stat.S_ISDIR(current_parent.st_mode) or (current_parent.st_dev,current_parent.st_ino,current_parent.st_uid,stat.S_IMODE(current_parent.st_mode))!=(parent.st_dev,parent.st_ino,os.geteuid(),0o700):raise PrepareError('output parent identity changed')
            with image.open('rb') as image_stream:output_digest=hashlib.file_digest(image_stream,'sha256').hexdigest()
            record={'schema_version':1,'status':'FIXED_RAM_PARENT_PREPARED_NOT_BOOTED','input':str(source),'input_sha256':source_digest,'input_bytes':before.st_size,'input_unchanged':True,'output':str(output),'output_sha256':output_digest,'output_bytes':image.stat().st_size,'new_directory':{'path':'/private/var/run',**new},'validated_parents':parents,'commands':list(COMMANDS),'sparse_copy_nonzero_chunk_bytes':copy_bytes,'e2fsck_exit_code':0,'tool_log_sha256':hashlib.sha256(runner.log).hexdigest(),'host_mount_performed':False,'target_accessed':False}
            rec=work/'record.json';rec.write_text(json.dumps(record,indent=2)+'\n');rec.chmod(0o600)
            log=work/'filesystem.log';log.write_bytes(runner.log);log.chmod(0o600);image.chmod(0o444)
            # Publish image last; a successful image always has both exact sidecars.
            for path,name in ((rec,sidecars[1]),(log,sidecars[2]),(image,sidecars[0])):
                os.link(path,name,dst_dir_fd=parent_fd,follow_symlinks=False);published.append((name,path.stat().st_ino))
        os.fsync(parent_fd)
        return record
    except BaseException:
        # Remove only our own partial publication, never a replacement or existing name.
        if parent_fd>=0:
            for name,inode in reversed(published):
                try:
                    if os.stat(name,dir_fd=parent_fd,follow_symlinks=False).st_ino==inode:os.unlink(name,dir_fd=parent_fd)
                except FileNotFoundError:pass
            if runner.log:
                try:
                    fd=os.open(sidecars[3],os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW|os.O_CLOEXEC,0o600,dir_fd=parent_fd)
                    try:
                        with os.fdopen(fd,'wb',closefd=False) as f:f.write(runner.log);f.flush();os.fsync(fd)
                    finally:os.close(fd)
                except FileExistsError:pass
        raise
    finally:
        if parent_fd>=0:os.close(parent_fd)
        os.close(source_fd)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',type=Path,required=True);p.add_argument('--expected-sha256',required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    try:record=prepare(args.input,args.output,expected_sha256=args.expected_sha256)
    except (OSError,ValueError,subprocess.SubprocessError) as e:p.exit(1,'prepare_ram_parent: '+str(e)+'\n')
    print(json.dumps(record,indent=2))
if __name__=='__main__':main()
