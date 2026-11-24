/* SPDX-License-Identifier: MIT */
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static int sfTLSContext(SSL_CTX *ctx) {
    if(!ctx)return 0;
    const char *path=getenv("SF_URLSESSION_CA_FILE");char local[PATH_MAX];
#ifdef SF_NETWORKING_CA_FILE
    if(!path)path=SF_NETWORKING_CA_FILE;
#endif
    if(!path){
        Dl_info info={0};
        if(!dladdr((const void *)sfTLSContext,&info)||!info.dli_fname)return 0;
        const char *slash=strrchr(info.dli_fname,'/');if(!slash)return 0;
        int n=snprintf(local,sizeof(local),"%.*s/../share/certs/ca-bundle.crt",(int)(slash-info.dli_fname),info.dli_fname);
        if(n<0||n>=(int)sizeof(local))return 0;path=local;
    }
    if(SSL_CTX_load_verify_locations(ctx,path,NULL)!=1)return 0;
    SSL_CTX_set_verify(ctx,SSL_VERIFY_PEER,NULL);return 1;
}
static int sfTLSHost(SSL *ssl,const char *host) {
    if(!ssl||!host||!host[0])return 0;
    unsigned char address[16];
    if(inet_pton(AF_INET,host,address)==1||inet_pton(AF_INET6,host,address)==1)
        return X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl),host)==1;
    return SSL_set_tlsext_host_name(ssl,host)==1 && SSL_set1_host(ssl,host)==1;
}
