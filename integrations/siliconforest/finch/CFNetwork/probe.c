/* SPDX-License-Identifier: MIT */
/* Independent assertions of CFNetwork's value/parser APIs; no external hosts. */
#include <CFNetwork/CFNetwork.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"failed: %s\n",#x); return 1; } } while (0)
int main(void)
{
    const char wire[]="HTTP/1.1 200 OK\r\nContent-Length: 5\r\nX-Test: value\r\n\r\nhello";
    CFHTTPMessageRef message=CFHTTPMessageCreateEmpty(NULL,false);
    CHECK(message && CFGetTypeID(message)==CFHTTPMessageGetTypeID());
    CHECK(CFHTTPMessageAppendBytes(message,(const UInt8 *)wire,12));
    CHECK(!CFHTTPMessageIsHeaderComplete(message));
    CHECK(CFHTTPMessageAppendBytes(message,(const UInt8 *)wire+12,sizeof wire-1-12));
    CHECK(CFHTTPMessageIsHeaderComplete(message));
    CHECK(CFHTTPMessageGetResponseStatusCode(message)==200);
    CFStringRef header=CFHTTPMessageCopyHeaderFieldValue(message,CFSTR("x-test"));
    CHECK(header && CFEqual(header,CFSTR("value")));CFRelease(header);
    CFDataRef body=CFHTTPMessageCopyBody(message);
    CHECK(body && CFDataGetLength(body)==5 && !memcmp(CFDataGetBytePtr(body),"hello",5));CFRelease(body);
    CFHTTPMessageRef copy=CFHTTPMessageCreateCopy(NULL,message);
    CHECK(copy && CFHTTPMessageGetResponseStatusCode(copy)==200);CFRelease(copy);CFRelease(message);
    CFURLRef url=CFURLCreateWithString(NULL,CFSTR("http://127.0.0.1/path"),NULL);
    CFHTTPMessageRef request=CFHTTPMessageCreateRequest(NULL,CFSTR("GET"),url,kCFHTTPVersion1_1);
    CHECK(request && CFHTTPMessageIsRequest(request));
    CHECK(CFHTTPMessageAddAuthentication(request,NULL,CFSTR("user"),CFSTR("pass"),kCFHTTPAuthenticationSchemeBasic,false));
    header=CFHTTPMessageCopyHeaderFieldValue(request,CFSTR("Authorization"));
    CHECK(header && CFEqual(header,CFSTR("Basic dXNlcjpwYXNz")));CFRelease(header);
    CFRelease(request);CFRelease(url);
    const UInt8 bytes[]={1,2,3};
    CFDataRef value=CFDataCreate(NULL,bytes,3);
    const void *keys[]={CFSTR("key")}, *values[]={value};
    CFDictionaryRef dict=CFDictionaryCreate(NULL,keys,values,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    CFDataRef txt=CFNetServiceCreateTXTDataWithDictionary(NULL,dict);
    CHECK(txt && CFDataGetLength(txt)==8);
    CFDictionaryRef decoded=CFNetServiceCreateDictionaryWithTXTData(NULL,txt);
    CHECK(decoded && CFEqual(dict,decoded));
    CFRelease(decoded);CFRelease(txt);CFRelease(dict);CFRelease(value);
    const char listing[]="-rw-r--r-- 1 owner group 123 Jan 02 2020 test.txt\r\n";
    CFDictionaryRef parsed=NULL;
    CHECK(CFFTPCreateParsedResourceListing(NULL,(const UInt8 *)listing,sizeof listing-1,&parsed)>0);
    CHECK(parsed && CFEqual(CFDictionaryGetValue(parsed,kCFFTPResourceName),CFSTR("test.txt")));
    CFRelease(parsed);
    puts("PASS CFNetwork HTTP incremental parse/copy, Basic auth, TXT round trip, FTP listing");
    return 0;
}
