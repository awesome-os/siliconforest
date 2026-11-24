/* SPDX-License-Identifier: MIT */
#include "sf_atc_tunables.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    const unsigned char good[12]={0,0,0,32,1,0,0,0,1,0,0,0};
    /* The actual native adapter's validator: a present zero-byte common
     * group is valid; absent common data and empty lane data are distinct. */
    assert(!sf_atc_tunable_observation_validate(1,NULL,0,0x4000,1));
    assert(!sf_atc_tunable_observation_validate(1,good,0,0x4000,1));
    assert(sf_atc_tunable_observation_validate(0,NULL,0,0x4000,1)==ENOENT);
    assert(sf_atc_tunable_observation_validate(1,NULL,0,0x1000,0)==ENOENT);
    for(int allow=0;allow<=1;allow++){
        assert(!sf_atc_tunable_observation_validate(1,good,sizeof(good),0x1000,allow));
        assert(sf_atc_tunable_observation_validate(0,good,sizeof(good),0x1000,allow)==ENOENT);
        assert(sf_atc_tunable_observation_validate(1,NULL,sizeof(good),0x1000,allow)==EINVAL);
        assert(sf_atc_tunable_observation_validate(1,good,11,0x1000,allow)==EINVAL);
        unsigned char bad[12];for(unsigned i=0;i<12;i++)bad[i]=good[i];bad[0]=1;
        assert(sf_atc_tunable_observation_validate(1,bad,sizeof(bad),0x1000,allow)==EINVAL);
        bad[0]=0;bad[1]=0x10;
        assert(sf_atc_tunable_observation_validate(1,bad,sizeof(bad),0x1000,allow)==EINVAL);
    }
    puts("ATC observation: actual native validator accepts present empty common groups, rejects missing/empty lane data and malformed/out-of-window records");
}
