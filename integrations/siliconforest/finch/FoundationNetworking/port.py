#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Apply reviewable TLS/CFNetwork adaptations to the pinned source in a build tree."""
from pathlib import Path
import sys
root=Path(sys.argv[1]);p=root/'NSURLSession.m';s=p.read_text()
s=s.replace('#import <Security/Security.h>','#include "sf-tls.h"')
a=s.index('@interface NSURLProtectionSpace (FinchServerTrust)')
b=s.index('/* Load a request, following up to 16 redirects',a)
s=s[:a]+'''/* Strict CA+hostname verification has already completed during TLS handshake.
 * Trust-delegate bypass is deliberately unavailable in this isolated port. */
static NSInteger conn_server_trust(Connection *c, NSURL *url) {
    (void)url;
    return c->ssl && SSL_get_verify_result(c->ssl)!=X509_V_OK ? NSURLErrorServerCertificateUntrusted : 0;
}

'''+s[b:]
s=s.replace('c->ctx = SSL_CTX_new(TLS_client_method());','c->ctx = SSL_CTX_new(TLS_client_method());\n    if (!c->ctx) return NSURLErrorUnknown;')
s=s.replace('BIO *bio = BIO_new(fd_bio_method());','BIO *bio = BIO_new(fd_bio_method());\n    if (!bio) return NSURLErrorUnknown;')
s=s.replace('    SSL_CTX_set_verify(c->ctx, SSL_VERIFY_NONE, NULL);','    if (!sfTLSContext(c->ctx)) return NSURLErrorServerCertificateUntrusted;')
s=s.replace('        SSL_CTX_set_verify(c->ctx, SSL_VERIFY_NONE, NULL);','        if (!sfTLSContext(c->ctx)) return NSURLErrorServerCertificateUntrusted;')
s=s.replace('    SSL_set_tlsext_host_name(c->ssl, [host UTF8String]);','    if (!sfTLSHost(c->ssl,[host UTF8String])) return NSURLErrorServerCertificateUntrusted;')
s=s.replace('        SSL_set_tlsext_host_name(c->ssl, [host UTF8String]);','        if (!sfTLSHost(c->ssl,[host UTF8String])) return NSURLErrorServerCertificateUntrusted;')
old='    BOOL wroteHead = conn_write(&c, [headData bytes], [headData length]);'
new='''    CFHTTPMessageRef wire=CFHTTPMessageCreateEmpty(NULL,true);
    BOOL valid=wire && CFHTTPMessageAppendBytes(wire,[headData bytes],[headData length]) && CFHTTPMessageIsHeaderComplete(wire);
    CFDataRef encoded=valid?CFHTTPMessageCopySerializedMessage(wire):NULL;
    BOOL wroteHead=encoded && conn_write(&c,CFDataGetBytePtr(encoded),CFDataGetLength(encoded));
    if(encoded)CFRelease(encoded);if(wire)CFRelease(wire);'''
assert old in s;s=s.replace(old,new)
assert 'SSL_VERIFY_NONE' not in s and 'SecTrustEvaluate' not in s
p.write_text(s)

p=root/'NSURLSessionSockets.m';s=p.read_text()
old='        end = [in rangeOfData:[NSData dataWithBytes:"\\r\\n\\r\\n" length:4] options:0 range:NSMakeRange(0, [in length])];'
new='        const uint8_t *bytes=[in bytes];NSUInteger length=[in length];\n        for(NSUInteger n=0;n+4<=length;n++)if(!memcmp(bytes+n,"\\r\\n\\r\\n",4)){end=NSMakeRange(n,4);break;}'
assert old in s;s=s.replace(old,new);p.write_text(s)

for name in ['NSURLCache.m','NSHTTPCookie.m']:
 p=root/name;s=p.read_text()
 s=s.replace('[NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"]','[[[NSLocale alloc] initWithLocaleIdentifier:@"en_US_POSIX"] autorelease]')
 s=s.replace('u.requiresSecureCoding = NO;','if([u respondsToSelector:@selector(setRequiresSecureCoding:)])[u setRequiresSecureCoding:NO];')
 p.write_text(s)

# Use the actual retained CF date parser instead of declaring an absent setter.
p=root/'NSHTTPCookie.m';s=p.read_text()
s=s.replace('#import <Foundation/Foundation.h>','#import <Foundation/Foundation.h>\n#include "sf-http-date.h"')
a=s.index('static NSDate *\nhttp_date(NSString *s)');b=s.index('\n@implementation NSHTTPCookie',a)
s=s[:a]+'static NSDate *\nhttp_date(NSString *s)\n{\n    return sfHTTPDate(s,@[@"EEE, dd MMM yyyy HH:mm:ss zzz",@"EEE, dd-MMM-yyyy HH:mm:ss zzz",@"EEEE, dd-MMM-yy HH:mm:ss zzz",@"EEE MMM d HH:mm:ss yyyy",@"EEE, dd-MMM-yy HH:mm:ss zzz",@"EEE, dd MMM yy HH:mm:ss zzz"]);\n}\n'+s[b:];p.write_text(s)
p=root/'NSURLCache.m';s=p.read_text()
s=s.replace('#import <Foundation/Foundation.h>','#import <Foundation/Foundation.h>\n#include "sf-http-date.h"')
a=s.index('        NSDateFormatter *f = [[NSDateFormatter new] autorelease];');b=s.index('        return d &&',a)
s=s[:a]+'        NSDate *d=sfHTTPDate(expires,@[@"EEE, dd MMM yyyy HH:mm:ss zzz"]);\n'+s[b:];p.write_text(s)

p=root/'NSURLCredential.m';s=p.read_text().replace('_credentials[space].count','[(NSDictionary *)_credentials[space] count]');p.write_text(s)

# Native M5 counts payload bytes, not Finch's arbitrary per-entry overhead.
p=root/'NSURLCache.m';s=p.read_text();old='return r.data.length + 256;';assert s.count(old)==1;s=s.replace(old,'return r.data.length;');p.write_text(s)
