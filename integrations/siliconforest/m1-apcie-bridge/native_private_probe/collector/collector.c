/* Original bounded collector: export actual bytes before SELF-REPORT classify. */
#include "collector.h"
#include <string.h>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/sha.h>
#endif
static int parse_nonce(const char *s,uint8_t out[24])
{
    if(!s || strlen(s)!=48)return 0;
    for(unsigned i=0;i<24;i++) {
        unsigned v=0;
        for(unsigned j=0;j<2;j++) {
            char c=s[2*i+j];if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return 0;
            v=v*16+(unsigned)(c<='9'?c-'0':c-'a'+10);
        }
        out[i]=(uint8_t)v;
    }
    return 1;
}
static int emit(FILE *out,const char *nonce,const uint8_t *raw,size_t n)
{
    const char hex[]="0123456789abcdef";unsigned char digest[32];char text[65],chunk[129];
#ifdef __APPLE__
    if(!CC_SHA256(raw,(CC_LONG)n,digest))return 1;
#else
    if(!SHA256(raw,n,digest))return 1;
#endif
    for(unsigned i=0;i<32;i++) {text[2*i]=hex[digest[i]>>4];text[2*i+1]=hex[digest[i]&15];}text[64]=0;
    fprintf(out,"DL32_PRIVATE_RECORD_BEGIN %s " BCP_PROPERTY " bytes=%zu chunk_bytes=64\n",nonce,n);
    size_t count=0;
    for(size_t at=0;at<n;at+=64) {
        size_t bytes=n-at<64?n-at:64;
        for(size_t i=0;i<bytes;i++){chunk[2*i]=hex[raw[at+i]>>4];chunk[2*i+1]=hex[raw[at+i]&15];}chunk[2*bytes]=0;
        fprintf(out,"DL32_PRIVATE_RECORD_HEX %s " BCP_PROPERTY " index=%zu %s\n",nonce,count++,chunk);
    }
    fprintf(out,"DL32_PRIVATE_RECORD_END %s " BCP_PROPERTY " bytes=%zu chunks=%zu sha256=%s\n",nonce,n,count,text);
    return fflush(out)||ferror(out)?1:0;
}
int bcpc_run(int argc,char **argv,bcpc_getter get,void *context,FILE *out)
{
    uint8_t nonce[24],raw[BCPC_CAP]={0};size_t n=0;int32_t detail=0,cleanup=0;
    if(argc!=2 || !argv || !parse_nonce(argv[1],nonce) || !get || !out)return 2;
    int fetched=get(context,raw,sizeof raw,&n,&detail,&cleanup),rc=1;
    const char *verdict="RPC_ERROR";unsigned bound=0,result=3,stage=0,attempts=0;int32_t error=0;
    if(fetched==BCPC_OK && n<=BCPC_CAP) {
        if(emit(out,argv[1],raw,n))return 1;
        rc=cleanup?1:bcp_validate(raw,n,nonce);
        verdict=cleanup?"CLEANUP_ERROR":rc==0?"CONFIG_AND_PRIVATE_LIFECYCLE_SELF_REPORT":rc==77?"GATED_NO_ATTEMPT":"FAILED_OR_INVALID";
        if(n==BCP_BYTES) {
            const struct bcp_record *r=(const struct bcp_record *)(const void *)raw;
            result=bcp_word(r,BCP_RESULT_AT);stage=bcp_word(r,BCP_STAGE_AT);attempts=bcp_word(r,BCP_ATTEMPTS_AT);
            error=(int32_t)bcp_word(r,BCP_ERROR_AT);
            bound=memcmp(raw+BCP_IDENTITY_AT,nonce,24)==0 && result!=BCP_GATED;
        }
    } else {
        verdict=fetched==BCPC_MISSING?"MISSING":"RPC_ERROR";
        fprintf(out,"DL32_PRIVATE_COPY %s status=%s detail=%d cleanup=%d\n",argv[1],fetched==BCPC_MISSING?"missing":"rpc_error",detail,cleanup);
    }
    fprintf(out,"DL32_PRIVATE_VERDICT %s verdict=%s result=%u stage=%u attempts=%u error=%d record_nonce_bound=%s data_readback=false effect_commands=false trusted_host_labels=true\n",
        argv[1],verdict,result,stage,attempts,error,bound?"true":"false");
    return fflush(out)||ferror(out)?1:rc;
}
