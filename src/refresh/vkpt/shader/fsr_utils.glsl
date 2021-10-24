/*
Copyright (C) 2019, NVIDIA CORPORATION. All rights reserved.
Copyright (C) 2021, Frank Richter. All rights reserved.

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

// Assumes spec_hdr constant is defined and that we're included after ffx_fsr1.h

// Transform (potentially) HDR input for FSR
fsr_vec3 hdr_input(fsr_vec3 color)
{
    if (spec_hdr != 0)
        FsrSrtm(color);
    return color;
}

// Transform FSR output to (potentially) HDR
fsr_vec3 hdr_output(fsr_vec3 color)
{
    if (spec_hdr != 0)
        FsrSrtmInv(color);
    return color;
}
