/* POPSoT: shared internal pixel scaling; guest coordinates remain unchanged. */
#ifndef POPSOT_NATIVE_SCALE_H
#define POPSOT_NATIVE_SCALE_H
#include <stdint.h>
static uint32_t popsot_internal_extent(uint32_t guest_pixels, uint32_t percent)
{
    return (guest_pixels*percent+50u)/100u;
}
#endif
