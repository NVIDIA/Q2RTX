# Copyright (C) 2019, NVIDIA CORPORATION. All rights reserved.
# SPDX-License-Identifier: GPL-2.0-or-later
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

import os
import sys
import glob

#
# scan textures to build materials index
#
def scan_textures(q2_path, paks_path):

    # scan bsp mesh textures in 'textures' folder
    bsp_tex = set()
    for fmt in [".wal", ".pcx"]:
        folder = os.path.join(paks_path, 'textures')
        for filename in glob.iglob(folder+'/**/*'+fmt, recursive=True):
            filename = os.path.relpath(filename, paks_path)
            filename = os.path.splitext(filename)[0]
            bsp_tex.add(filename)

    # scan model textures in both base paks & expanded baseq2 folders
    # to catch added md2 / md3 items & weapons
    model_tex = set()
    for path in [paks_path, q2_path]:
        for folder in ['models', 'players', 'sprites']:
            for fmt in [".tga", ".png", ".jgg", ".wal", ".pcx"]:
                for filename in glob.iglob(os.path.join(path, folder)+'/**/*'+fmt, recursive=True):
                    filename = os.path.relpath(filename, path)
                    filename = os.path.splitext(filename)[0]
                    # skip normal / emissive textures
                    if (filename.endswith('_n') or filename.endswith('_light')):
                        continue
                    model_tex.add(filename)

    return sorted(list(bsp_tex | model_tex), key=lambda s: s.lower())

#
# build materials csv file from list of texture names
#
def build_csv(textures):

    print('"key", "bump scale", "rough scale", "spec scale", "emit scale", "chrome flg", "invisible flg", "light flg"')

    for tex in textures:
        print('"%s", 1.0, 1.0, 1.0, 1.0, 0, 0, 0'%(tex))

#
# main
#

#
# usage : build_materials <path to base2> <path to expanded paks> > <output file>
#

q2_path = None
if (len(sys.argv) > 1):
    if (not os.path.isabs(sys.argv[1])):
        q2_path = os.path.join(os.getcwd(), sys.argv[1])
    else:
        q2_path = sys.argv[1]

paks_path = None
if (len(sys.argv) > 2):
    if (not os.path.isabs(sys.argv[2])):
        paks_path = os.path.join(os.getcwd(), sys.argv[2])
    else:
        paks_path = sys.argv[2]


textures = scan_textures(q2_path, paks_path)

build_csv(textures)
