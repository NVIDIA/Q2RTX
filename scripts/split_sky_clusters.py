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

# Split a monolithic sky_clusters.txt file into per-map sky cluster files.

import argparse
import os

arguments = argparse.ArgumentParser()
arguments.add_argument('gamedir', help='path to game directory')
arg_values = arguments.parse_args()

header = """# This file is part of the Q2RTX lighting system.
# For this map, it lists BSP clusters with skybox and lava polygons
# that have to be converted to analytic area lights.
# For more information, see comments in the `path_tracer.h` file
# in Q2RTX source code.

"""
gamedir = arg_values.gamedir

line_iter = iter(open(os.path.join(gamedir, "sky_clusters.txt"), "r"))

def next_line():
    try:
        line = next(line_iter)
        line = line.rstrip()
        comment_pos = line.find('#')
        if comment_pos != -1:
            line_no_comment = line[:comment_pos].rstrip()
        else:
            line_no_comment = line
        return line, line_no_comment
    except StopIteration:
        return None, None # Indicate EOF

# Track current per-map sky file
current_sky_file = None

line, line_no_comment = next_line()
while line is not None:
    if line_no_comment.isidentifier(): # recognize map names by first char being a letter
        # We have a map file name, open sky file
        map_name = line_no_comment
        current_sky_file = open(os.path.join(gamedir, "maps", "sky", f"{map_name}.txt"), "w")
        current_sky_file.write(header)
    elif current_sky_file is not None:
        # Write non-empty lines to sky file
        if len(line) > 0:
            print(line, file=current_sky_file)
    line, line_no_comment = next_line()

# Close current per-map sky file
current_sky_file = None
