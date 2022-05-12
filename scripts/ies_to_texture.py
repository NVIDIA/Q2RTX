#!python3

# Copyright (C) 2022, Frank Richter. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

# This is a simple IES file parser that outputs a PNG file usable
# as a spotlight emission profile in Q2RTX.

import argparse
import math
import numpngw
import numpy
import scipy.interpolate

arguments = argparse.ArgumentParser()
arguments.add_argument('iesfile', help='input IES file name')
arguments.add_argument('outfile', help='output PNG file name')
arguments.add_argument('--width', '-W', type=int, default=256, help='output image width')
arg_values = arguments.parse_args()

class IesFile:
    def __init__(self):
        self.keyword_lines = None
        self.horz_angles = None
        self.vert_angles = None
        self.light_values = None

    def read(self, f):
        # Helper method to get a number of floats from the file
        current_values = [] # Hold values from the currently parsed line
        def get_floats(num):
            nonlocal current_values
            result = []
            n = 0
            while n < num:
                if len(current_values) == 0:
                    line = f.readline().strip()
                    if len(line) == 0: return
                    current_values = line.split()
                result.append(float(current_values[0]))
                current_values = current_values[1:]
                n += 1
            return result

        header_line = f.readline().strip()
        if not header_line in ["IESNA91", "IESNA:LM-63-1995", "IESNA:LM-63-2002"]:
            raise Exception(f"Unexpected header line: {header_line}")

        # Collect IES keywords for inclusion in .png, to allow identification of source data later
        self.keyword_lines = []

        TILT = None
        while not TILT:
            line = f.readline()
            if len(line) == 0: break
            line = line.strip()
            if len(line) > 0:
                self.keyword_lines.append(line)
            if line.startswith("TILT="):
                TILT = line[5:]
                break

        if TILT is None:
            raise Exception("TILT= line missing!")
        elif TILT != "NONE":
            raise Exception("Unsupported TILT value: {TILT}")

        lamps_line = f.readline().strip()
        num_lamps, lumens_per_lamp, candela_mul, num_vert_angles, num_horz_angles, photometric_type, units_type, width, length, height = map(lambda s: float(s), lamps_line.split())
        self.num_vert_angles = int(num_vert_angles)
        self.num_horz_angles = int(num_horz_angles)

        if num_lamps != 1:
            raise Exception(f"Unsupported number of lamps: {num_lamps}")

        if photometric_type != 1:
            raise Exception(f"Unsupported photometric type: {photometric_type}")

        # "ballast factor", "future use", "input watts": skip
        f.readline()

        self.vert_angles = get_floats(self.num_vert_angles)
        self.horz_angles = get_floats(self.num_horz_angles)
        self.light_values = []
        for _  in range(0, self.num_horz_angles):
            self.light_values.append(get_floats(self.num_vert_angles))

ies_file = IesFile()
ies_file.read(open(arg_values.iesfile, "r"))

if ies_file.num_horz_angles != 1:
    raise Exception("Only 1 horizontal angle is currently supported")
    # An alternative could be to just average over multiple horizontal angles,
    # might work for lights which are almost, but not quite symmetric

max_angle = ies_file.vert_angles[-1]
if max_angle > 180:
    raise Exception(f"Unexpected last vertical angle {max_angle}")
elif max_angle > 90:
    print(f"Last vertical angle {max_angle} is > 90 deg, values beyond that will be ignored")

if ies_file.horz_angles[0] != 0:
    raise Exception(f"Unexpected horizontal angle: {ies_file.horz_angles[0]}")

# Output resolution
res = arg_values.width

# Q2RTX looks up the emission factor by using the cosine of the angle as the coordinate,
# so interpolate for these from the per-angle light values
angle_function = scipy.interpolate.interp1d(ies_file.vert_angles, ies_file.light_values[0], kind='cubic')
output_values = numpy.zeros(res)
for x in range(0, res):
    angle = (x / (res - 1)) * 90
    if angle <= max_angle:
        interp = angle_function(angle)
    else:
        interp = 0
    output_values[x] = interp

# Write image: normalized light values, 16bpc gray scale
max_value = max(output_values)
values_ui16 = ((65535 / max_value) * output_values).astype(numpy.uint16)
values_ui16 = numpy.reshape(values_ui16, (1, -1))
numpngw.write_png(arg_values.outfile, values_ui16, text_list=[('ies_keywords', '\n'.join(ies_file.keyword_lines))])
