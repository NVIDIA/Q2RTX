/*static const char gl_prog_warp[] =
    "!!ARBfp1.0\n"

    "TEMP tmp, s, t, coord, diffuse;\n"
    "PARAM scale = { 0.31830988618379067154, 0.31830988618379067154, 0.31830988618379067154, 1.0 };\n"

    "ADD tmp, fragment.texcoord[0], program.local[0];\n"
    "MUL tmp, scale, tmp;\n"
    "SIN t, tmp.x;\n"
    "SIN s, tmp.y;\n"
    "ADD coord.x, s, fragment.texcoord[0];\n"
    "ADD coord.y, t, fragment.texcoord[0];\n"
    "TEX diffuse, coord, texture[0], 2D;\n"

    "MUL result.color, diffuse, fragment.color;\n"
    "END\n"
;*/

static const char gl_prog_warp[] =
    "!!ARBfp1.0\n"

    "TEMP offset, diffuse;\n"
    "TEX offset, fragment.texcoord[1], texture[1], 2D;\n"
    "ADD offset, offset, program.local[0];\n"
    "ADD offset, offset, fragment.texcoord[0];\n"
    "TEX diffuse, offset, texture[0], 2D;\n"
//    "TEX diffuse, fragment.texcoord[0], texture[0], 2D;\n"
    "MUL result.color, diffuse, fragment.color;\n"
    "END\n"
;

static const char gl_prog_light[] =
    "!!ARBfp1.0\n"

    "TEMP light, diffuse;\n"
    "TEX light, fragment.texcoord[1], texture[1], 2D;\n"
    "TEX diffuse, fragment.texcoord[0], texture[0], 2D;\n"
    "ADD light, light, fragment.color;\n"
    "MUL result.color, diffuse, light;\n"
    "END\n"
;
