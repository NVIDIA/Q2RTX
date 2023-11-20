/*
Copyright (C) 2018 Andrey Nazarov

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

#define QGLAPI
#include "gl.h"

typedef struct {
    const char *name;
    void *dest;
} glfunction_t;

typedef struct {
    uint16_t ver_gl;
    uint16_t ver_es;
    uint16_t excl_gl;
    uint16_t excl_es;
    uint32_t caps;
    const char *extension;
    const glfunction_t *functions;
} glsection_t;

#define QGL_FN(x)   { "gl"#x, (void*)&qgl##x }

static const glsection_t sections[] = {
    // GL 1.1
    {
        .ver_gl = QGL_VER(1, 1),
        .ver_es = QGL_VER(1, 0),
        .functions = (const glfunction_t []) {
            QGL_FN(BindTexture),
            QGL_FN(BlendFunc),
            QGL_FN(Clear),
            QGL_FN(ClearColor),
            QGL_FN(ClearStencil),
            QGL_FN(CullFace),
            QGL_FN(DeleteTextures),
            QGL_FN(DepthFunc),
            QGL_FN(DepthMask),
            QGL_FN(Disable),
            QGL_FN(DrawArrays),
            QGL_FN(DrawElements),
            QGL_FN(Enable),
            QGL_FN(Finish),
            QGL_FN(FrontFace),
            QGL_FN(GenTextures),
            QGL_FN(GetError),
            QGL_FN(GetFloatv),
            QGL_FN(GetIntegerv),
            QGL_FN(GetString),
            QGL_FN(IsEnabled),
            QGL_FN(LineWidth),
            QGL_FN(PolygonOffset),
            QGL_FN(ReadPixels),
            QGL_FN(Scissor),
            QGL_FN(StencilFunc),
            QGL_FN(StencilOp),
            QGL_FN(TexImage2D),
            QGL_FN(TexParameterf),
            QGL_FN(TexParameteri),
            QGL_FN(TexSubImage2D),
            QGL_FN(Viewport),
            { NULL }
        }
    },

    // GL 1.1, compat
    {
        .ver_gl = QGL_VER(1, 1),
        .ver_es = QGL_VER(1, 0),
        .excl_gl = QGL_VER(3, 1),
        .excl_es = QGL_VER(2, 0),
        .caps = QGL_CAP_LEGACY,
        .functions = (const glfunction_t []) {
            QGL_FN(AlphaFunc),
            QGL_FN(Color4f),
            QGL_FN(ColorPointer),
            QGL_FN(DisableClientState),
            QGL_FN(EnableClientState),
            QGL_FN(LoadIdentity),
            QGL_FN(LoadMatrixf),
            QGL_FN(MatrixMode),
            QGL_FN(Scalef),
            QGL_FN(ShadeModel),
            QGL_FN(TexCoordPointer),
            QGL_FN(TexEnvf),
            QGL_FN(Translatef),
            QGL_FN(VertexPointer),
            { NULL }
        }
    },

    // GL 1.1, not ES
    {
        .ver_gl = QGL_VER(1, 1),
        .functions = (const glfunction_t []) {
            QGL_FN(ClearDepth),
            QGL_FN(DepthRange),
            { NULL }
        }
    },

    // GL 1.1, not ES, compat
    {
        .ver_gl = QGL_VER(1, 1),
        .excl_gl = QGL_VER(3, 1),
        .caps = QGL_CAP_TEXTURE_BITS,
        .functions = (const glfunction_t []) {
            QGL_FN(PolygonMode),
            { NULL }
        }
    },

    // ES 1.1
    {
        .ver_es = QGL_VER(1, 1),
        .caps = QGL_CAP_TEXTURE_CLAMP_TO_EDGE,
    },

    // GL 1.2
    {
        .ver_gl = QGL_VER(1, 2),
        .caps = QGL_CAP_TEXTURE_CLAMP_TO_EDGE | QGL_CAP_TEXTURE_MAX_LEVEL,
    },

    // GL 1.3
    // GL_ARB_multitexture
    {
        .extension = "GL_ARB_multitexture",
        .ver_gl = QGL_VER(1, 3),
        .ver_es = QGL_VER(1, 0),
        .functions = (const glfunction_t []) {
            QGL_FN(ActiveTexture),
            { NULL }
        }
    },

    // GL 1.3, compat
    // GL_ARB_multitexture
    {
        .extension = "GL_ARB_multitexture",
        .ver_gl = QGL_VER(1, 3),
        .ver_es = QGL_VER(1, 0),
        .excl_gl = QGL_VER(3, 1),
        .excl_es = QGL_VER(2, 0),
        .functions = (const glfunction_t []) {
            QGL_FN(ClientActiveTexture),
            { NULL }
        }
    },

    // GL 1.4, compat
    {
        .ver_gl = QGL_VER(1, 4),
        .excl_gl = QGL_VER(3, 1),
        .caps = QGL_CAP_TEXTURE_LOD_BIAS,
    },

    // GL 1.5
    // GL_ARB_vertex_buffer_object
    {
        .extension = "GL_ARB_vertex_buffer_object",
        .ver_gl = QGL_VER(1, 5),
        .ver_es = QGL_VER(1, 1),
        .functions = (const glfunction_t []) {
            QGL_FN(BindBuffer),
            QGL_FN(BufferData),
            QGL_FN(BufferSubData),
            QGL_FN(DeleteBuffers),
            QGL_FN(GenBuffers),
            { NULL }
        }
    },

    // GL 2.0
    {
        .ver_gl = QGL_VER(2, 0),
        .ver_es = QGL_VER(2, 0),
        .functions = (const glfunction_t []) {
            QGL_FN(AttachShader),
            QGL_FN(BindAttribLocation),
            QGL_FN(CompileShader),
            QGL_FN(CreateProgram),
            QGL_FN(CreateShader),
            QGL_FN(DeleteProgram),
            QGL_FN(DeleteShader),
            QGL_FN(DisableVertexAttribArray),
            QGL_FN(EnableVertexAttribArray),
            QGL_FN(GetProgramInfoLog),
            QGL_FN(GetProgramiv),
            QGL_FN(GetShaderInfoLog),
            QGL_FN(GetShaderiv),
            QGL_FN(GetUniformLocation),
            QGL_FN(LinkProgram),
            QGL_FN(ShaderSource),
            QGL_FN(Uniform1i),
            QGL_FN(UseProgram),
            QGL_FN(VertexAttrib4f),
            QGL_FN(VertexAttribPointer),
            { NULL }
        }
    },

    // GL 3.0, ES 2.0
    {
        .ver_gl = QGL_VER(3, 0),
        .ver_es = QGL_VER(2, 0),
        .functions = (const glfunction_t []) {
            QGL_FN(GenerateMipmap),
            { NULL }
        }
    },

    // GL 3.0, ES 3.0
    {
        .ver_gl = QGL_VER(3, 0),
        .ver_es = QGL_VER(3, 0),
        .caps = QGL_CAP_TEXTURE_MAX_LEVEL | QGL_CAP_TEXTURE_NON_POWER_OF_TWO,
        .functions = (const glfunction_t []) {
            QGL_FN(GetStringi),
            { NULL }
        }
    },

    // GL 3.1
    // GL_ARB_uniform_buffer_object
    {
        .extension = "GL_ARB_uniform_buffer_object",
        .ver_gl = QGL_VER(3, 1),
        .ver_es = QGL_VER(3, 0),
        .caps = QGL_CAP_SHADER,
        .functions = (const glfunction_t []) {
            QGL_FN(BindBufferBase),
            QGL_FN(GetUniformBlockIndex),
            QGL_FN(GetActiveUniformBlockiv),
            QGL_FN(UniformBlockBinding),
            { NULL }
        }
    },

    // GL 4.1
    {
        .ver_gl = QGL_VER(4, 1),
        .ver_es = QGL_VER(1, 0),
        .functions = (const glfunction_t []) {
            QGL_FN(ClearDepthf),
            QGL_FN(DepthRangef),
            { NULL }
        }
    },

    // GL 4.3
    // GL_KHR_debug
    {
        .extension = "GL_KHR_debug",
        .ver_gl = QGL_VER(4, 3),
        .ver_es = QGL_VER(3, 2),
        .functions = (const glfunction_t []) {
            QGL_FN(DebugMessageCallback),
            { NULL }
        }
    },

    // GL 4.6
    // GL_EXT_texture_filter_anisotropic
    {
        .extension = "GL_EXT_texture_filter_anisotropic",
        .ver_gl = QGL_VER(4, 6),
        .caps = QGL_CAP_TEXTURE_ANISOTROPY
    },

    // GL_ARB_fragment_program
    {
        .extension = "GL_ARB_fragment_program",
        .functions = (const glfunction_t []) {
            QGL_FN(BindProgramARB),
            QGL_FN(DeleteProgramsARB),
            QGL_FN(GenProgramsARB),
            QGL_FN(ProgramLocalParameter4fvARB),
            QGL_FN(ProgramStringARB),
            { NULL }
        }
    },

    // GL_EXT_compiled_vertex_array
    {
        .extension = "GL_EXT_compiled_vertex_array",
        .functions = (const glfunction_t []) {
            QGL_FN(LockArraysEXT),
            QGL_FN(UnlockArraysEXT),
            { NULL }
        }
    },
};

static bool parse_version(void)
{
    const char *s;
    int major, minor, ver;
    bool gl_es = false;

    qglGetString = vid.get_proc_addr("glGetString");
    if (!qglGetString)
        return false;

    Com_DPrintf("GL_VENDOR: %s\n", qglGetString(GL_VENDOR));
    Com_DPrintf("GL_RENDERER: %s\n", qglGetString(GL_RENDERER));

    // get version string
    s = (const char *)qglGetString(GL_VERSION);
    if (!s || !*s)
        return false;

    Com_DPrintf("GL_VERSION: %s\n", s);

    // parse ES profile prefix
    if (!strncmp(s, "OpenGL ES", 9)) {
        s += 9;
        if (s[0] == '-' && s[1] && s[2] && s[3] == ' ')
            s += 4;
        else if (s[0] == ' ')
            s += 1;
        else
            return false;
        gl_es = true;
    }

    // parse version
    if (sscanf(s, "%d.%d", &major, &minor) < 2)
        return false;

    if (major < 1 || minor < 0 || minor > 99)
        return false;

    ver = QGL_VER(major, minor);
    if (gl_es)
        gl_config.ver_es = ver;
    else
        gl_config.ver_gl = ver;

    return true;
}

static bool parse_glsl_version(void)
{
    const char *s;
    int major, minor;

    if (gl_config.ver_gl < QGL_VER(2, 0) && gl_config.ver_es < QGL_VER(2, 0))
        return true;

    s = (const char *)qglGetString(GL_SHADING_LANGUAGE_VERSION);
    if (!s || !*s)
        return false;

    Com_DPrintf("GL_SHADING_LANGUAGE_VERSION: %s\n", s);

    if (gl_config.ver_es && !strncmp(s, "OpenGL ES GLSL ES ", 18))
        s += 18;

    if (sscanf(s, "%d.%d", &major, &minor) < 2)
        return false;

    if (major < 1 || minor < 0 || minor > 99)
        return false;

    gl_config.ver_sl = QGL_VER(major, minor);
    return true;
}

static bool extension_blacklisted(const char *search)
{
    cvar_t *var = Cvar_FindVar(search);

    if (!var) {
        char buffer[MAX_QPATH];
        Q_strlcpy(buffer, search, sizeof(buffer));
        Q_strlwr(buffer);
        var = Cvar_FindVar(buffer);
    }

    return var && !strcmp(var->string, "0");
}

static bool extension_present(const char *search)
{
    const char *s, *p;
    size_t len;

    if (!search || !*search)
        return false;

    if (qglGetStringi) {
        GLint count = 0;
        qglGetIntegerv(GL_NUM_EXTENSIONS, &count);

        for (int i = 0; i < count; i++) {
            s = (const char *)qglGetStringi(GL_EXTENSIONS, i);
            if (s && !strcmp(s, search))
                return true;
        }

        return false;
    }

    s = (const char *)qglGetString(GL_EXTENSIONS);
    if (!s)
        return false;

    len = strlen(search);
    while (*s) {
        p = Q_strchrnul(s, ' ');
        if (p - s == len && !memcmp(s, search, len))
            return true;
        if (!*p)
            break;
        s = p + 1;
    }

    return false;
}

void QGL_Shutdown(void)
{
    for (int i = 0; i < q_countof(sections); i++) {
        const glsection_t *sec = &sections[i];
        const glfunction_t *func;

        if (sec->functions)
            for (func = sec->functions; func->name; func++)
                *(void **)func->dest = NULL;
    }
}

bool QGL_Init(void)
{
    if (!parse_version()) {
        Com_EPrintf("OpenGL returned invalid version string\n");
        return false;
    }

    if (!parse_glsl_version()) {
        Com_EPrintf("OpenGL returned invalid GLSL version string\n");
        return false;
    }

    if (gl_config.ver_gl >= QGL_VER(3, 0) || gl_config.ver_es >= QGL_VER(3, 0)) {
        qglGetStringi = vid.get_proc_addr("glGetStringi");
        qglGetIntegerv = vid.get_proc_addr("glGetIntegerv");
        if (!qglGetStringi || !qglGetIntegerv) {
            Com_EPrintf("Required OpenGL entry points not found\n");
            return false;
        }
    }

    bool compatible = false;
    if (gl_config.ver_gl >= QGL_VER(3, 2)) {
        // Profile is correctly set by Mesa, but can be 0 for compatibility
        // context on NVidia. Thus only check for core bit.
        GLint profile = 0;
        qglGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
        compatible = !(profile & GL_CONTEXT_CORE_PROFILE_BIT);
    } else if (gl_config.ver_gl == QGL_VER(3, 1)) {
        compatible = extension_present("GL_ARB_compatibility");
    }

    if (gl_config.ver_es) {
        Com_DPrintf("Detected OpenGL ES %d.%d\n",
                    QGL_UNPACK_VER(gl_config.ver_es));
    } else if (gl_config.ver_gl >= QGL_VER(3, 2)) {
        Com_DPrintf("Detected OpenGL %d.%d (%s profile)\n",
                    QGL_UNPACK_VER(gl_config.ver_gl),
                    compatible ? "compatibility" : "core");
    } else {
        Com_DPrintf("Detected OpenGL %d.%d\n",
                    QGL_UNPACK_VER(gl_config.ver_gl));
    }

    for (int i = 0; i < q_countof(sections); i++) {
        const glsection_t *sec = &sections[i];
        const glfunction_t *func;
        bool core;

        if (sec->excl_gl && gl_config.ver_gl >= sec->excl_gl && !compatible)
            continue;
        if (sec->excl_es && gl_config.ver_es >= sec->excl_es)
            continue;

        core  = sec->ver_gl && gl_config.ver_gl >= sec->ver_gl;
        core |= sec->ver_es && gl_config.ver_es >= sec->ver_es;

        if (!core) {
            if (!extension_present(sec->extension))
                continue;
            if (extension_blacklisted(sec->extension)) {
                Com_Printf("Blacklisted extension %s\n", sec->extension);
                continue;
            }
        }

        if (sec->functions) {
            for (func = sec->functions; func->name; func++) {
                const char *name = func->name;
                void *addr = vid.get_proc_addr(name);

                // try with XYZ suffix if this is a GL_XYZ_extension
                if (!addr && !core)  {
                    char suf[4];
                    memcpy(suf, sec->extension + 3, 3);
                    suf[3] = 0;
                    if (!strstr(name, suf)) {
                        name = va("%s%s", name, suf);
                        addr = vid.get_proc_addr(name);
                    }
                }

                if (!addr) {
                    Com_EPrintf("Couldn't get entry point: %s\n", name);
                    break;
                }

                *(void **)func->dest = addr;
            }

            if (func->name) {
                if (core) {
                    Com_EPrintf("Required OpenGL entry points not found\n");
                    return false;
                }

                // NULL out all functions
                for (func = sec->functions; func->name; func++)
                    *(void **)func->dest = NULL;

                Com_EPrintf("Couldn't load extension %s\n", sec->extension);
                continue;
            }
        }

        if (!core)
            Com_DPrintf("Loaded extension %s\n", sec->extension);

        gl_config.caps |= sec->caps;
    }

    if (gl_config.ver_es) {
        // don't ever attempt to use shaders with GL ES < 3.0
        if (gl_config.ver_es < QGL_VER(3, 0) || gl_config.ver_sl < QGL_VER(3, 0))
            gl_config.caps &= ~QGL_CAP_SHADER;

        // GL ES 3.0+ deprecates, but still supports client vertex arrays, thus
        // pure QGL_CAP_SHADER mode will work.
        if (!(gl_config.caps & (QGL_CAP_LEGACY | QGL_CAP_SHADER))) {
            Com_EPrintf("Unsupported OpenGL ES version\n");
            return false;
        }
    } else {
        // don't ever attempt to use shaders with GL < 3.0
        if (gl_config.ver_gl < QGL_VER(3, 0) || gl_config.ver_sl < QGL_VER(1, 30))
            gl_config.caps &= ~QGL_CAP_SHADER;

        // MUST have QGL_CAP_LEGACY bit, because Q2PRO still uses client vertex
        // arrays removed by GL 3.1+.
        if (!(gl_config.caps & QGL_CAP_LEGACY)) {
            Com_EPrintf("Unsupported OpenGL version/profile\n");
            return false;
        }
    }

    return true;
}
