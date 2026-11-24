#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Isolate Finch networking classes beside the installed Foundation, never duplicate them."""
from pathlib import Path
import argparse,subprocess,sys,tempfile
FILES=['NSURLSession.m','NSURLSessionSockets.m','NSURLCache.m','NSHTTPCookie.m','NSURLCredential.m']
CLASSES={'NSURLRequest':'NSObject','NSMutableURLRequest':'NSURLRequest','NSURLResponse':'NSObject','NSHTTPURLResponse':'NSURLResponse','NSHTTPCookie':'NSObject','NSHTTPCookieStorage':'NSObject','NSCachedURLResponse':'NSObject','NSURLCache':'NSObject','NSURLProtocol':'NSObject','NSURLSessionTaskTransactionMetrics':'NSObject','NSURLSessionTaskMetrics':'NSObject','NSURLSessionConfiguration':'NSObject','NSURLSessionTask':'NSObject','NSURLSessionDataTask':'NSURLSessionTask','NSURLSessionUploadTask':'NSURLSessionDataTask','NSURLSessionDownloadTask':'NSURLSessionTask','NSURLSessionWebSocketMessage':'NSObject','NSURLSessionWebSocketTask':'NSURLSessionTask','NSURLSessionStreamTask':'NSURLSessionTask','NSURLCredential':'NSObject','NSURLProtectionSpace':'NSObject','NSURLAuthenticationChallenge':'NSObject','NSURLCredentialStorage':'NSObject','NSURLSession':'NSObject'}
if __name__=='__main__':
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('source',type=Path);ap.add_argument('output',type=Path);a=ap.parse_args()
 here=Path(__file__).resolve().parent
 with tempfile.TemporaryDirectory() as d:
  narrow=Path(d)
  for f in FILES:(narrow/f).write_text((a.source/f).read_text())
  pre=(here/'types.h').read_text()
  protocols=['NSURLSessionDelegate','NSURLSessionTaskDelegate','NSURLSessionDataDelegate','NSURLSessionDownloadDelegate','NSURLSessionWebSocketDelegate','NSURLSessionStreamDelegate','NSURLProtocolClient','NSURLAuthenticationChallengeSender']
  pre='\n'.join('#define '+name+' SF'+name for name in [*CLASSES,*protocols])+'\n@class '+','.join(CLASSES)+';\n'+pre
  (narrow/'preamble.h').write_text(pre)
  subprocess.run([sys.executable,str(here.parent/'tools/declarations.py'),str(narrow),str(narrow/'preamble.h'),str(a.output),'--categories','--class-map',','.join(k+':'+v for k,v in CLASSES.items())],check=True)

 out=a.output.read_text()
 import re
 out=re.sub(r'^[-+] \([^)]*\)\s*_\w+[^;]*;\n','',out,flags=re.M)
 start=out.index('@interface NSURLSessionConfiguration :')
 end=out.index('@end',start)
 out=out[:end]+(here/'configuration-properties.inc').read_text()+'\n'+out[end:]
 for name in CLASSES:
  start=out.index('@interface '+name+' :');end=out.index('@end',start);body=out[start:end]
  for method in re.findall(r'- \(BOOL\)(is[A-Z]\w*)\s*;',body):
   stem=method[2:];prop=stem if len(stem)>1 and stem[:2].isupper() else stem[0].lower()+stem[1:]
   body+='@property(readonly,getter='+method+') BOOL '+prop+';\n'
  out=out[:start]+body+out[end:]
 a.output.write_text(out)
