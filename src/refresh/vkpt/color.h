/*
Copyright (C) 2019, NVIDIA CORPORATION. All rights reserved.

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

#ifndef COLOR_H_
#define COLOR_H_

static inline float decode_srgb(byte pix)
{
	float x = (float)pix / 255.f;
	
	if (x < 0.04045f)
		return x / 12.92f;

	return powf((x + 0.055f) / 1.055f, 2.4f);
}

static inline byte encode_srgb(float x)
{
    if (x <= 0.0031308f)
        x *= 12.92f;
    else
        x = 1.055f * powf(x, 1.f / 2.4f) - 0.055f;
     
    x = max(0.f, min(1.f, x));

    return (byte)roundf(x * 255.f);
}

#endif // COLOR_H_
