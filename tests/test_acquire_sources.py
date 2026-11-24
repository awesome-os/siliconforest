import copy
import io
from contextlib import redirect_stdout, redirect_stderr
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from tools.acquire_sources import (AcquisitionError,git_blob_sha,checked_entry,select_entries,
    source_url,fetch_one,main)

DATA=b'example source\n'
ENTRY={'repository':'owner/project','commit':'a'*40,'path':'src/test.rs','blob_sha':git_blob_sha(DATA)}

class AcquisitionTests(unittest.TestCase):
    def test_known_git_blob(self):
        self.assertEqual(git_blob_sha(b''),'e69de29bb2d1d6434b8b29ae775ad8c2e48c5391')
    def test_validated_url(self):
        self.assertEqual(source_url(ENTRY),'https://raw.githubusercontent.com/owner/project/'+'a'*40+'/src/test.rs')
    def test_reject_unsafe_paths(self):
        for path in ['../x','a/../x','/tmp/x','a//b','a\\b','a?token=x']:
            e=copy.deepcopy(ENTRY);e['path']=path
            with self.subTest(path=path),self.assertRaises(AcquisitionError):checked_entry(e)
    def test_reject_branch_and_bad_sha(self):
        for key,value in [('commit','main'),('blob_sha','unknown'),('repository','../x')]:
            e=copy.deepcopy(ENTRY);e[key]=value
            with self.assertRaises(AcquisitionError):checked_entry(e)
    def test_deduplicate_and_selection(self):
        with self.assertRaises(AcquisitionError):select_entries({'inspected_files':[ENTRY,ENTRY]},[],[])
        with self.assertRaises(AcquisitionError):select_entries({'inspected_files':[ENTRY]},['other/repo'],[])
        self.assertEqual(select_entries({'inspected_files':[ENTRY]},['owner/project'],[]),[ENTRY])
    def test_fetch_and_verified_cache_hit(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); calls=[]
            def download(url):calls.append(url);return DATA
            r=fetch_one(ENTRY,root,download)
            self.assertEqual(r['status'],'MATCHED_PINNED_BYTES');self.assertFalse(r['cache_hit'])
            r=fetch_one(ENTRY,root,download);self.assertTrue(r['cache_hit']);self.assertEqual(len(calls),1)
            self.assertFalse(r['executed'])
    def test_corrupt_bytes_not_written(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(AcquisitionError):fetch_one(ENTRY,Path(tmp),lambda _:b'wrong')
            self.assertFalse(list(Path(tmp).rglob('test.rs')))
    def test_corrupt_cache_not_overwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            r=fetch_one(ENTRY,Path(tmp),lambda _:DATA);p=Path(tmp)/r['cache_path'];p.write_bytes(b'bad')
            with self.assertRaises(AcquisitionError):fetch_one(ENTRY,Path(tmp),lambda _:DATA)
            self.assertEqual(p.read_bytes(),b'bad')
    def test_symlink_destination_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);other=root/'outside';other.mkdir();(root/'owner').symlink_to(other,target_is_directory=True)
            with self.assertRaises(AcquisitionError):fetch_one(ENTRY,root,lambda _:DATA)
    def test_default_plan_no_network(self):
        with patch('tools.acquire_sources.download',side_effect=AssertionError('network forbidden')):
            with redirect_stdout(io.StringIO()):code=main(['plan'])
        self.assertEqual(code,0)
    def test_fetch_needs_consent_flag(self):
        with redirect_stderr(io.StringIO()):code=main(['fetch'])
        self.assertEqual(code,2)
