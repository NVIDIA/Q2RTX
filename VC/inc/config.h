//
// Q2PRO configuration file for MSVC
//

#define REVISION 1337
#define VERSION "v133.7"

#ifdef _WIN64
#define CPUSTRING "x86_64"
#define BUILDSTRING "Win64"
#else
#define CPUSTRING "x86"
#define BUILDSTRING "Win32"
#endif

#define BASEGAME "baseq2"
#define DEFGAME ""

#define USE_ICMP 1
#define USE_ZLIB 1
#define USE_SYSCON 1
#define USE_DBGHELP 1
#define USE_MAPCHECKSUM 1

#if USE_CLIENT
//#define VID_REF "gl"
#define VID_MODELIST "640x480 800x600 1024x768"
#define VID_GEOMETRY "640x480"
//#define REF_GL 1
//#define USE_REF REF_GL
#define USE_UI 1
#define USE_PNG 1
#define USE_JPG 1
#define USE_TGA 1
#define USE_MD3 0
#define USE_LIGHTSTYLES 1
#define USE_DLIGHTS 1
//#define USE_DINPUT 1
//#define USE_DSOUND 1
//#define USE_OPENAL 1
#define USE_SNDDMA 1
#define USE_CURL 0
#define USE_AUTOREPLY 1
#endif

#if USE_SERVER
#define USE_AC_SERVER !USE_CLIENT
#define USE_MVD_SERVER 1
#define USE_MVD_CLIENT 1
#define USE_PACKETDUP 1
#define USE_WINSVC !USE_CLIENT
#endif

#define _USE_MATH_DEFINES
#define inline __inline
#define __func__ __FUNCTION__

#ifdef _WIN64
typedef __int64     ssize_t;
#define SSIZE_MAX   _I64_MAX
#else
typedef __int32     ssize_t;
#define SSIZE_MAX   _I32_MAX
#endif

#pragma warning(disable:4018)
#pragma warning(disable:4244)
#pragma warning(disable:4267)
#pragma warning(disable:4305)

