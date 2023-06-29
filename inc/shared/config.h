//
// Q2PRO configuration file for MSVC
//

// expand to generate version string
#define STRING(x) #x
#define _STR(x) STRING(x)
#define VERSION_STRING "" _STR(VERSION_MAJOR) "." _STR(VERSION_MINOR) "." _STR(VERSION_POINT)
#define LONG_VERSION_STRING "" _STR(VERSION_MAJOR) "." _STR(VERSION_MINOR) "." _STR(VERSION_POINT) "-" _STR(VERSION_BRANCH) "-" _STR(VERSION_SHA)

#ifdef _WIN64
#define CPUSTRING "x86_64"
#define BUILDSTRING "Win64"
#elif  _WIN32
#define CPUSTRING "x86"
#define BUILDSTRING "Win32"
#elif __aarch64__
#define CPUSTRING "aarch64"
#define BUILDSTRING "Linux"
#elif __x86_64__
#define CPUSTRING "x86_64"
#define BUILDSTRING "Linux"
#else
#define CPUSTRING "x86"
#define BUILDSTRING "Linux"
#endif

#define BASEGAME "baseq2"
#define DEFGAME ""

#define USE_ICMP 1
#define USE_ZLIB 1
#define USE_SYSCON 1
#define USE_DBGHELP 1

#if USE_CLIENT
//#define VID_REF "gl"
#define VID_MODELIST "640x480 800x600 1024x768 1280x720"
#define VID_GEOMETRY "1280x720"
//#define REF_GL 1
//#define USE_REF REF_GL
#define USE_UI 1
#define USE_PNG 1
#define USE_JPG 1
#define USE_TGA 1
#define USE_MD3 1
//#define USE_DSOUND 1
#define USE_OPENAL 1
#define USE_SNDDMA 1
//#define USE_CURL 0
#define USE_AUTOREPLY 1
#define USE_CLIENT_GTV 1
#endif

#define USE_MVD_SERVER 1
#define USE_MVD_CLIENT 1
#define USE_AC_SERVER USE_SERVER

#if USE_SERVER
#define USE_PACKETDUP 1
#define USE_WINSVC !USE_CLIENT
#endif

#define _USE_MATH_DEFINES
#define inline __inline
#define __func__ __FUNCTION__

#ifdef _WIN64
typedef __int64     ssize_t;
#define SSIZE_MAX   _I64_MAX
#elif _WIN32
typedef __int32     ssize_t;
#define SSIZE_MAX   _I32_MAX
#endif

#if defined(_MSC_VER)
#pragma warning(disable:4018)
#pragma warning(disable:4244)
#pragma warning(disable:4267)
#pragma warning(disable:4305)
#endif
