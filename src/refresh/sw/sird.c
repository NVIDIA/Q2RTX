/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "sw.h"

/*
** Start Added by Lewey
**
** This is where the real SIRDS code is
*/

//width of the repeating pattern. Increasing this will
//increase the quality of the SIRD by giving it more
//height levels.
//
//Make sure: ((R_SIRDw % 3) == 0)
//          && (((R_SIRDw / 3) % R_SIRDExponents) == 0)
#define R_SIRDw 144

//height of the repeating pattern (not really important)
#define R_SIRDh 50

//maximum offset. This is the max number of pixels
//an item can be moved due to it's height, this is
//is also obviously then the number of different
//height layers you can have. A large R_SIRDw will
//make it harder and harder to see the image, a larger
//ratio of R_SIRDw (i.e. less than 3) will eventually
//cause your eyes to be unable to see the pattern.
#define R_SIRDmaxDiff (R_SIRDw / 3)

//the number of lower powers to ignore
#define R_SIRDIgnoreExponents   5

//the number of exponents (after ignored ones) to have different
//height values (ones after it are rounded to the max difference)
#define R_SIRDExponents         6

//the number of height levels each exponent is given
#define R_SIRDstepsPerExponent (R_SIRDmaxDiff / R_SIRDExponents)

//this is the z value of the sky, which logically should be 0, but
//for implimentation reasons is made very high. Not my doing by the
//way. If you move to a different platform, you may need to change this
#define R_SIRD_ZofSky 0x8ccc

//this is the number of random numbers
//defined in "rand1k.h"
#define R_SIRDnumRand 103

//this hold the background pattern
static byte r_SIRDBackground[R_SIRDw * R_SIRDh * VID_BYTES];

//these are the actual random numbers
static const byte r_SIRDrandValues[] = {
#include "rand1k.h"
};


//Only used if id386 is false, this acts as a
// reverse bit-scanner, and uses a sort of binary
// search to find the index of the highest set bit.
//You could also expand the loop 4 times to remove
// the 'while'
#if !id386 || !(defined _MSC_VER)
static int UShortLog(int val)
{
    int mask = 0xff00;
    int p = 0;
    int b = 8;
    while (b) {
        if (val & mask) {
            p += b;
            b >>= 1;
            mask &= (mask << b);
        } else {
            mask &= (mask << (b >> 1));
            mask >>= b;
            b >>= 1;
        }
    }
    return p;
}
#endif

static int R_SIRDZFunc(int sub)
{
    int e;

    //special case the sky.
    if (sub ==  R_SIRD_ZofSky)
        return 0;

#if id386 && (defined _MSC_VER)
    e = sub;
    //calculate the log (base 2) of the number. In other
    //words the index of the highest set bit. bsr is undefined
    //if it's input is 0, so special case that.
    if (e != 0) {
        __asm {
            mov ebx, e
            bsr eax, ebx
            mov e, eax
        }
    }
#else
    e = UShortLog(sub);
#endif

    //clip the exponent
    if (e < R_SIRDIgnoreExponents)
        return 0;

    // based on the power, shift the z so that
    // it's as high as it can get while still staying
    // under 0x100
    if (e > 8) {
        sub >>= (e - 8);
    } else {
        if (e < 8) {
            sub <<= (8 - e);
        }
    }

    // Lower the power of the number, this helps scaling and removes
    // small z values.
    e -= R_SIRDIgnoreExponents;

    // contruct the height value. The power is used as the primary calculator,
    // and then the extra bits are used to offset. In this way you
    // get more detail than just the log of the z value, and it works
    // as a pretty good approximation of it.
    e *= R_SIRDstepsPerExponent;
    e += ((sub * R_SIRDstepsPerExponent) >> 8);

    //make sure we stay under maximum height.
    return ((e <= R_SIRDmaxDiff) ? e : R_SIRDmaxDiff);
}

void R_ApplySIRDAlgorithum(void)
{
    short* curz, *oldz;
    short cz = 0, lastz = 0;
    byte* curp;
    byte* curbp, j = 0;
    int x, y, i, zinc, k;
    int mode = sw_drawsird->integer;

    //note of interest: I've made this static so that
    //if you like you could make it not static and see
    //what would happen if you didn't change the background
    static int ji = 0;

    //create the background image to tile
    //basically done by shifting the values around
    //each time and xoring them with a randomly
    //selected pixel
    for (i = 0; i < R_SIRDw * R_SIRDh * VID_BYTES; i++) {
        if ((i % R_SIRDnumRand) == 0) {
            ji++;
            ji %= R_SIRDnumRand;
            j = r_SIRDrandValues[r_SIRDrandValues[ji] % R_SIRDnumRand];
        }
        r_SIRDBackground[i] = r_SIRDrandValues[(i % R_SIRDnumRand) ] ^ j;
    }

    //if we are under water:
    if ((r_dowarp) && (vid.width != WARP_WIDTH)) {
        //the rendering is only in the top left
        //WARP_WIDTH by WARP_HEIGHT area, so scale the z-values
        //to span over the whole screen


        //why are we going backwards? so that we don't write over the
        //values before we read from them

        zinc = ((WARP_WIDTH * 0x10000) / vid.width);
        for (y = vid.height - 1; y >= 0; y--) {
            curz = (d_pzbuffer + (vid.width * y));
            oldz = (d_pzbuffer + (vid.width * ((y * WARP_HEIGHT) / vid.height)));
            k = (zinc * (vid.width - 1));

            for (x = vid.width - 1; x >= 0; x--) {
                curz[x] = oldz[k >> 16];
                k -= zinc;
            }
        }
    }


    //SIRDify each line
    for (y = 0; y < vid.height; y++) {
        curp = (vid.buffer + (vid.rowbytes * y));
        curz = (d_pzbuffer + (vid.width * y));

        if (mode != 3) {
            // draw the SIRD

            // copy the background into the left most column
            curbp = &(r_SIRDBackground[ R_SIRDw * (y % R_SIRDh) ]);
            for (x = 0; x < R_SIRDw; x++) {
                curp[0] = curbp[0];
                curp[1] = curbp[1];
                curp[2] = curbp[2];

                curp += VID_BYTES;
                curbp += VID_BYTES;
            }

            lastz = 0;
            cz = 0;
            curz += R_SIRDw;
            curbp = curp - R_SIRDw * VID_BYTES;

            // now calculate the SIRD
            for (x = R_SIRDw; x < vid.width; x++) {
                //only call the z-function with a new
                //value, it is slow so this saves quite
                //some time.
                if (lastz != *curz) {
                    lastz = *curz;

                    //convert from z to height offset
                    cz = (mode == 2) ? R_SIRDmaxDiff - R_SIRDZFunc(lastz) : R_SIRDZFunc(lastz);

                    //the "height offset" used in making SIRDS
                    //can be considered an adjustment of the
                    //frequency of repetition in the pattern.
                    //so here we are copying from bp to p, and so
                    //it simply increases or decreases the distance
                    //between the two.
                    curbp = (curp - R_SIRDw * VID_BYTES + cz * VID_BYTES);
                }

                curp[0] = curbp[0];
                curp[1] = curbp[1];
                curp[2] = curbp[2];

                curp += VID_BYTES;
                curbp += VID_BYTES;
                curz++;
            }
        } else {
            //if we are just drawing the height map
            //this lets you see which layers are used to
            //create the SIRD
            //
            //NOTE: even though it may sort of look like
            //a grey-scale height map, that is merely a
            //coincidence because of how the colours are
            //organized in the pallette.

            for (x = 0; x < vid.width; x++) {
                if (lastz != *curz) {
                    lastz = *curz;
                    cz = R_SIRDZFunc(*curz);
                }

                curp[0] = cz;
                curp[1] = cz;
                curp[2] = cz;

                curp += VID_BYTES;
                curz++;
            }
        }
    }
}

/*
** End Added by Lewey
*/
