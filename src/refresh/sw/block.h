#define BLOCK_SIZE  (1 << BLOCK_SHIFT)

static void BLOCK_FUNC(void)
{
    int     lightleft[3], lightright[3];
    int     lightleftstep[3], lightrightstep[3];
    int     v, i, b, lightstep[3], light[3];
    byte    *psource, *prowdest;

    psource = pbasesource;
    prowdest = prowdestbase;

    for (v = 0 ; v < r_numvblocks ; v++) {
        // FIXME: use delta rather than both right and left, like ASM?
        lightleft[0] = r_lightptr[0 * LIGHTMAP_BYTES + 0];
        lightleft[1] = r_lightptr[0 * LIGHTMAP_BYTES + 1];
        lightleft[2] = r_lightptr[0 * LIGHTMAP_BYTES + 2];
        lightright[0] = r_lightptr[1 * LIGHTMAP_BYTES + 0];
        lightright[1] = r_lightptr[1 * LIGHTMAP_BYTES + 1];
        lightright[2] = r_lightptr[1 * LIGHTMAP_BYTES + 2];
        r_lightptr += r_lightwidth;
        lightleftstep[0] = (r_lightptr[0 * LIGHTMAP_BYTES + 0] - lightleft[0]) >> BLOCK_SHIFT;
        lightleftstep[1] = (r_lightptr[0 * LIGHTMAP_BYTES + 1] - lightleft[1]) >> BLOCK_SHIFT;
        lightleftstep[2] = (r_lightptr[0 * LIGHTMAP_BYTES + 2] - lightleft[2]) >> BLOCK_SHIFT;
        lightrightstep[0] = (r_lightptr[1 * LIGHTMAP_BYTES + 0] - lightright[0]) >> BLOCK_SHIFT;
        lightrightstep[1] = (r_lightptr[1 * LIGHTMAP_BYTES + 1] - lightright[1]) >> BLOCK_SHIFT;
        lightrightstep[2] = (r_lightptr[1 * LIGHTMAP_BYTES + 2] - lightright[2]) >> BLOCK_SHIFT;

        for (i = 0 ; i < BLOCK_SIZE ; i++) {
            lightstep[0] = (lightleft[0] - lightright[0]) >> BLOCK_SHIFT;
            lightstep[1] = (lightleft[1] - lightright[1]) >> BLOCK_SHIFT;
            lightstep[2] = (lightleft[2] - lightright[2]) >> BLOCK_SHIFT;

            light[0] = lightright[0];
            light[1] = lightright[1];
            light[2] = lightright[2];

            for (b = BLOCK_SIZE - 1; b >= 0; b--) {
                prowdest[b * TEX_BYTES + 0] = (psource[b * TEX_BYTES + 0] * light[0]) >> 16;
                prowdest[b * TEX_BYTES + 1] = (psource[b * TEX_BYTES + 1] * light[1]) >> 16;
                prowdest[b * TEX_BYTES + 2] = (psource[b * TEX_BYTES + 2] * light[2]) >> 16;
                light[0] += lightstep[0];
                light[1] += lightstep[1];
                light[2] += lightstep[2];
            }

            psource += sourcetstep;
            lightright[0] += lightrightstep[0];
            lightright[1] += lightrightstep[1];
            lightright[2] += lightrightstep[2];
            lightleft[0] += lightleftstep[0];
            lightleft[1] += lightleftstep[1];
            lightleft[2] += lightleftstep[2];
            prowdest += surfrowbytes;
        }

        if (psource >= r_sourcemax)
            psource -= r_stepback;
    }
}

#undef BLOCK_FUNC
#undef BLOCK_SIZE
#undef BLOCK_SHIFT
