#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#elif (defined __unix__)
#include <GL/gl.h>
#else
#error Unknown Target OS
#endif

#ifndef APIENTRY
#define APIENTRY
#endif

