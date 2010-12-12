static const char gl_prog_warp[] =
    "!!ARBfp1.0\n"

    "TEMP s, t, coord, diffuse;\n"
    "PARAM amp = { 0.05, 0.05 };\n"
    "PARAM phase = { 4, 4 };\n"

    //"ADD coord, fragment.texcoord[0], program.local[0];\n"
    "MUL coord, phase, fragment.texcoord[0];\n"
    "ADD coord, coord, program.local[0];\n"
    "SIN s, coord.y;\n"
    "SIN t, coord.x;\n"
    "MUL coord.x, amp, s;\n"
    "MUL coord.y, amp, t;\n"
    "ADD coord, coord, fragment.texcoord[0];\n"
    "TEX diffuse, coord, texture[0], 2D;\n"
    "MUL result.color, diffuse, fragment.color;\n"
    "END\n"
;

