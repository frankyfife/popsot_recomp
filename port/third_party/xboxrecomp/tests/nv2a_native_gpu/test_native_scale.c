/* POPSoT: fractional internal sizes must share scissor edges exactly. */
#include "../../src/kernel/nv2a_native_scale.h"
#include <stdio.h>
int main(void)
{
    const uint32_t percent[]={50,75,100,150,225,300};
    const uint32_t height[]={240,360,480,720,1080,1440};
    for (unsigned i=0;i<6;i++) {
        uint32_t p=percent[i], full=popsot_internal_extent(853,p);
        if (popsot_internal_extent(480,p)!=height[i]) return 1;
        if (popsot_internal_extent(0,p)!=0) return 2;
        for (uint32_t split=0;split<=853;split++) {
            uint32_t left=popsot_internal_extent(split,p);
            uint32_t right=full-left;
            if (left+right!=full || left>full) return 3;
            if (split && left<popsot_internal_extent(split-1,p)) return 4;
        }
    }
    puts("Android internal sizes and shared scissor edges passed");
    return 0;
}
