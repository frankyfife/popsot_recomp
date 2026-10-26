/* POPSoT: protect display/HUD-sized viewports and the tiny bloom tail while
 * budgeting real auxiliary targets, including padded-pitch retail surfaces. */
#include "../../src/kernel/popsot_effect_quality.h"
#include <stdio.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);return 1; } } while(0)
int main(void)
{
    const unsigned percent[]={25,50,75,100,100};
    for(int q=0;q<=POPSOT_EFFECT_ULTRA;q++) {
        CHECK(popsot_effect_percent(q,256,256)==percent[q]);
        CHECK(popsot_effect_percent(q,320,240)==percent[q]);
        CHECK(popsot_effect_percent(q,40,30)==percent[q]);
        CHECK(popsot_effect_percent(q,512,512)==percent[q]);
        CHECK(popsot_effect_percent(q,640,480)==100);
        CHECK(popsot_effect_percent(q,640,240)==100);
        CHECK(popsot_effect_percent(q,20,15)==100);
        CHECK(popsot_effect_percent(q,10,7)==100);
        CHECK(popsot_effect_percent(q,64,16)==100);
        CHECK(popsot_effect_percent(q,320,1024)==100);
    }
    /* Ultra keeps the screen-fraction chains at the guest's size, not the
     * shadow maps or display-sized buffers. */
    CHECK(popsot_effect_screen_chain(320,240));
    CHECK(popsot_effect_screen_chain(160,120));
    CHECK(popsot_effect_screen_chain(40,30));
    CHECK(popsot_effect_screen_chain(20,15));
    CHECK(popsot_effect_screen_chain(10,7));
    CHECK(!popsot_effect_screen_chain(256,256));
    CHECK(!popsot_effect_screen_chain(640,480));
    CHECK(!popsot_effect_screen_chain(512,256));
    /* Independently recorded caster states, not texture allocation addresses. */
    CHECK(popsot_shadow_target(0x08080213,512,0,0,256,256,0,0,0));
    CHECK(popsot_shadow_target(0x08080113,512,0,0,256,256,0,0,0));
    CHECK(!popsot_shadow_target(0x08080118,512,0,0,256,256,0,0,0));
    CHECK(!popsot_shadow_target(0x08080113,512,0,0,256,256,0x10000,0,0));
    CHECK(!popsot_shadow_target(0x08080113,512,0,0,256,256,0,1,0));
    CHECK(!popsot_shadow_target(0x08080113,512,0,0,256,256,0,0,1));
    CHECK(!popsot_shadow_target(0x08080113,512,1,0,256,256,0,0,0));
    CHECK(!popsot_shadow_target(0x08080113,1024,0,0,256,256,0,0,0));
    puts("Auxiliary quality, bloom tail, display protection and caster admission passed");
    return 0;
}
