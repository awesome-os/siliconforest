// SPDX-License-Identifier: MIT
#include <linux/kernel.h>
#include <crypto/sha2.h>
#include <crypto/utils.h>
#include <linux/uuid.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
static void read_exact(void *p,size_t n) { assert(fread(p,1,n,stdin)==n); }
int main(void)
{
 unsigned cases=0;u32 lengths[2];
 while(fread(lengths,sizeof(lengths),1,stdin)==1) {
  size_t nk=lengths[0],nd=lengths[1];assert(nk<65536 && nd<1048577);
  size_t skew=cases%8;u8 *key=malloc(nk+8),*data=malloc(nd+8);assert(key && data);
  read_exact(key+skew,nk);read_exact(data+skew,nd);u8 expected[120];read_exact(expected,sizeof(expected));
  u8 out[64];sha256(data+skew,nd,out+skew);assert(!memcmp(out+skew,expected,32));
  sha224(data+skew,nd,out+skew);assert(!memcmp(out+skew,expected+32,28));
  hmac_sha256_usingrawkey(key+skew,nk,data+skew,nd,out+skew);assert(!memcmp(out+skew,expected+60,32));
  hmac_sha224_usingrawkey(key+skew,nk,data+skew,nd,out+skew);assert(!memcmp(out+skew,expected+92,28));
  struct hmac_sha256_ctx ctx;hmac_sha256_init_usingrawkey(&ctx,key+skew,nk);
  size_t step=1+cases%137;for(size_t i=0;i<nd;i+=step)hmac_sha256_update(&ctx,data+skew+i,nd-i<step?nd-i:step);
  hmac_sha256_final(&ctx,out+skew);assert(!memcmp(out+skew,expected+60,32));
  for(size_t i=0;i<sizeof(ctx);i++)assert(!((u8 *)&ctx)[i]);
  assert(!crypto_memneq(expected,expected,sizeof(expected)));
  for(unsigned i=0;i<sizeof(expected);i++) { u8 copy[120];memcpy(copy,expected,sizeof(copy));copy[i]^=1;assert(crypto_memneq(expected,copy,sizeof(copy))); }
  for(unsigned n=0;n<40;n++) { u8 copy[120];memcpy(copy,expected,sizeof(copy));assert(!crypto_memneq(expected+skew,copy+skew,n));if(n){copy[skew+n-1]^=1;assert(crypto_memneq(expected+skew,copy+skew,n));} }
  free(key);free(data);++cases;
 }
 uuid_t parsed;guid_t guid;const char text[]="00112233-4455-6677-8899-aabbccddeeff";
 assert(!uuid_parse(text,&parsed));assert(!guid_parse(text,&guid));
 for(unsigned i=0;i<16;i++)assert(parsed.b[i]==i*17);
 assert(guid.b[0]==0x33 && guid.b[4]==0x55 && guid.b[6]==0x77);
 for(unsigned i=0;i<36;i++) { char bad[37];memcpy(bad,text,sizeof(bad));bad[i]='z';assert(uuid_parse(bad,&parsed)<0); }
 printf("pinned SHA224/256/HMAC: %u Python hashlib/hmac cases, unaligned and incremental/zeroized contexts; memneq differences/bounds and UUID byte order/malformed parsing PASS\n",cases);
}
