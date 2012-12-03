#define BLOCK_SIZE  (1 << BLOCK_SHIFT)

void BLOCK_FUNC(void)
{
    int     v, i, b, lightstep, lighttemp, light;
    byte    *psource, *prowdest;

    psource = pbasesource;
    prowdest = prowdestbase;

    for (v = 0 ; v < r_numvblocks ; v++) {
        // FIXME: make these locals?
        // FIXME: use delta rather than both right and left, like ASM?
        lightleft = r_lightptr[0];
        lightright = r_lightptr[1];
        r_lightptr += r_lightwidth;
        lightleftstep = (r_lightptr[0] - lightleft) >> BLOCK_SHIFT;
        lightrightstep = (r_lightptr[1] - lightright) >> BLOCK_SHIFT;

        for (i = 0 ; i < BLOCK_SIZE ; i++) {
            lighttemp = lightleft - lightright;
            lightstep = lighttemp >> BLOCK_SHIFT;

            light = lightright;

            for (b = BLOCK_SIZE - 1; b >= 0; b--) {
                prowdest[b * TEX_BYTES + 0] = (psource[b * TEX_BYTES + 0] * light) >> 16;
                prowdest[b * TEX_BYTES + 1] = (psource[b * TEX_BYTES + 1] * light) >> 16;
                prowdest[b * TEX_BYTES + 2] = (psource[b * TEX_BYTES + 2] * light) >> 16;
                light += lightstep;
            }

            psource += sourcetstep;
            lightright += lightrightstep;
            lightleft += lightleftstep;
            prowdest += surfrowbytes;
        }

        if (psource >= r_sourcemax)
            psource -= r_stepback;
    }
}

#undef BLOCK_FUNC
#undef BLOCK_SIZE
#undef BLOCK_SHIFT
