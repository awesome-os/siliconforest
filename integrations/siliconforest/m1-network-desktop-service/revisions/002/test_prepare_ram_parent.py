"""Generated local ext4 fixtures only; no device, target, mount or old image writes."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import prepare_ram_parent as subject
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def debug(image,command):return subprocess.run(['/usr/bin/debugfs','-w','-R',command,str(image)],capture_output=True,check=True)
class BrokenRunner(subject.Runner):
 def run(self,argv,**kw):
  if '-w' in argv:
   script=Path(argv[argv.index('-f')+1]);script.write_text(script.read_text()+'mkdir /private/var/run\n')
  return super().run(argv,**kw)
class ParentPreparation(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.shared=tempfile.TemporaryDirectory(prefix='dnds-ext4-fixtures-');cls.base=Path(cls.shared.name)/'base.img'
  with cls.base.open('xb') as f:f.truncate(32*1024*1024)
  subprocess.run(['/usr/bin/mke2fs','-q','-t','ext4','-F',str(cls.base)],capture_output=True,check=True)
  debug(cls.base,'mkdir /private');debug(cls.base,'mkdir /private/var');cls.base_hash=sha(cls.base)
 @classmethod
 def tearDownClass(cls):cls.shared.cleanup()
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory(prefix='dnds-prepare-test-');self.addCleanup(self.temp.cleanup);self.directory=Path(self.temp.name)
  self.image=self.directory/'input.img';shutil.copyfile(self.base,self.image)
  self.output_dir=self.directory/'prepared';self.output_dir.mkdir(mode=0o700);self.output=self.output_dir/'prepared.img'
 def refused(self,runner=None,expected=None):
  before=sha(self.image)
  with self.assertRaises((ValueError,OSError)):subject.prepare(self.image,self.output,expected_sha256=expected or before,runner=runner)
  self.assertEqual(sha(self.image),before);self.assertFalse(self.output.exists())
  self.assertFalse(any(p.name.startswith('.prepare-run-') for p in self.output_dir.iterdir()))
 def test_positive_sparse_copy_exact_directory_records_and_input_unchanged(self):
  before=sha(self.image);self.image.chmod(0o444);record=subject.prepare(self.image,self.output,expected_sha256=before)
  self.assertEqual(sha(self.image),before);self.assertEqual(record['input_sha256'],before)
  self.assertNotEqual(sha(self.output),before);self.assertEqual(sha(self.output),record['output_sha256']);self.assertEqual(self.output.stat().st_mode&0o777,0o444)
  self.assertEqual(record['new_directory']['type'],'directory');self.assertEqual(record['new_directory']['mode'],0o755)
  self.assertEqual(record['new_directory']['uid'],0);self.assertEqual(record['new_directory']['gid'],0);self.assertEqual(record['new_directory']['nlink'],2)
  self.assertTrue(record['input_unchanged']);self.assertEqual(record['e2fsck_exit_code'],0);self.assertEqual(record['commands'],list(subject.COMMANDS))
  self.assertLess(self.output.stat().st_blocks*512,self.output.stat().st_size)
  log=Path(str(self.output)+'.filesystem.log').read_bytes();self.assertEqual(hashlib.sha256(log).hexdigest(),record['tool_log_sha256'])
  self.assertTrue(Path(str(self.output)+'.record.json').is_file());self.assertEqual(sha(self.base),self.base_hash)
 def test_existing_run_directory_refuses_before_mutation(self):
  debug(self.image,'mkdir /private/var/run');runner=subject.Runner();self.refused(runner);self.assertNotIn(b'"-w"',runner.log)
 def test_existing_run_regular_and_symlink_refuse(self):
  payload=self.directory/'payload';payload.write_text('x')
  for command in (f'write {payload} /private/var/run','symlink /private/var/run /nonexistent'):
   shutil.copyfile(self.base,self.image);debug(self.image,command);self.refused()
   for p in self.output_dir.iterdir():p.unlink()
 def test_parent_wrong_type_mode_owner_and_symlink_refuse(self):
  payload=self.directory/'payload';payload.write_text('x')
  variants=[['set_inode_field /private mode 040777'],['set_inode_field /private/var mode 040775'],['set_inode_field /private/var uid 1'],['set_inode_field /private/var gid 1'],['rmdir /private/var',f'write {payload} /private/var'],['rmdir /private/var','symlink /private/var /elsewhere']]
  for commands in variants:
   with self.subTest(commands=commands):
    shutil.copyfile(self.base,self.image)
    for command in commands:debug(self.image,command)
    self.refused()
    for p in self.output_dir.iterdir():p.unlink()
 def test_actual_debugfs_zero_exit_command_error_is_refused(self):
  runner=BrokenRunner();self.refused(runner);self.assertIn(b'Ext2 directory already exists',runner.log);self.assertIn(b'RETURN 0',runner.log);self.assertTrue(Path(str(self.output)+'.failed.log').is_file())
 def test_existing_output_and_unknown_symlink_no_overwrite(self):
  self.output.write_text('preserve');before=self.output.read_bytes();self.refused_existing();self.assertEqual(self.output.read_bytes(),before)
  self.output.unlink();self.output.symlink_to('missing');self.refused_existing();self.assertTrue(self.output.is_symlink())
 def refused_existing(self):
  before=sha(self.image)
  with self.assertRaises((ValueError,OSError)):subject.prepare(self.image,self.output,expected_sha256=before)
  self.assertEqual(sha(self.image),before)
 def test_hash_mismatch_and_output_parent_metadata(self):
  self.refused(expected='0'*64);self.output_dir.chmod(0o755);self.refused()
 def test_input_symlink_not_followed(self):
  link=self.directory/'source-link';link.symlink_to(self.image)
  with self.assertRaises((ValueError,OSError)):subject.prepare(link,self.output,expected_sha256=sha(self.image))
  self.assertFalse(self.output.exists())
 def test_s2_strict_mutation_validator_rejects_all_extra_or_missing_output(self):
  valid=b'debugfs 1.47.4 (6-Mar-2025)\n'+b''.join(b'debugfs: '+c.encode()+b'\n' for c in subject.COMMANDS);subject.validate_mutation(valid)
  for mutant in [valid+b'write: unexpected error\n',valid+b'debugfs: bad_command\n',valid.replace(b'040755',b'040777'),valid.replace(b'debugfs 1.47.4 (6-Mar-2025)',b'debugfs arbitrary unchecked warning'),valid+valid,valid.replace(b'mkdir /private/var/run',b'mkdir /elsewhere'),valid+b'\0']:
   with self.subTest(mutant=mutant):
    with self.assertRaises(ValueError):subject.validate_mutation(mutant)
 def test_output_parent_symlink_refuses(self):
  other=self.directory/'alias';other.symlink_to(self.output_dir,target_is_directory=True)
  with self.assertRaises((ValueError,OSError)):subject.prepare(self.image,other/'prepared.img',expected_sha256=sha(self.image))
  self.assertFalse(self.output.exists())
if __name__=='__main__':unittest.main()
