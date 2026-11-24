import io
import json
from contextlib import redirect_stdout
import unittest
from unittest.mock import patch
from tools import publish_github as p

class PublisherTests(unittest.TestCase):
    def test_plan_is_private_and_no_execution(self):
        with patch.object(p,'run',side_effect=AssertionError('must not execute')):
            output=io.StringIO()
            with redirect_stdout(output):code=p.main([])
        self.assertEqual(code,0);data=json.loads(output.getvalue())
        self.assertTrue(data['private']);self.assertFalse(data['executed'])
    def test_bad_name_rejected(self):
        for name in ['../foo','--public','a/b','name; command']:
            with self.subTest(name=name),self.assertRaises(ValueError):p.plan('chriskearney',name)
    def test_existing_remote_stops_before_gh(self):
        calls=[]
        def run(args):
            calls.append(args)
            return {('git','rev-parse','--show-toplevel'):str(p.ROOT),('git','status','--porcelain'):'',
                    ('git','branch','--show-current'):'main',('git','remote'):'origin'}[tuple(args)]
        with patch.object(p,'run',side_effect=run),self.assertRaises(RuntimeError):p.publish('chriskearney','darwin-lab')
        self.assertFalse(any(c[0]=='gh' for c in calls))
    def test_wrong_owner_never_creates(self):
        values=[str(p.ROOT),'','main','','other-user']
        with patch.object(p,'run',side_effect=values) as mocked,self.assertRaises(RuntimeError):p.publish('chriskearney','darwin-lab')
        self.assertFalse(any('create' in call.args[0] for call in mocked.call_args_list))
    def test_verified_mocked_success(self):
        values=[str(p.ROOT),'','main','','chriskearney','',
                json.dumps({'nameWithOwner':'chriskearney/darwin-lab','isPrivate':True,'url':'https://github.com/chriskearney/darwin-lab'}),
                'a'*40,'a'*40+'\trefs/heads/main']
        with patch.object(p,'run',side_effect=values):r=p.publish('chriskearney','darwin-lab')
        self.assertEqual(r['status'],'PUBLISHED_AND_VERIFIED')
    def test_wrong_visibility_detected(self):
        values=[str(p.ROOT),'','main','','chriskearney','',json.dumps({'nameWithOwner':'chriskearney/darwin-lab','isPrivate':False})]
        with patch.object(p,'run',side_effect=values),self.assertRaises(RuntimeError):p.publish('chriskearney','darwin-lab')
