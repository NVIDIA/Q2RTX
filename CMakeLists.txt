cmake_minimum_required (VERSION 3.8)

include("cmake/HunterGate.cmake")
# ---------- Startup hunter -------------------------
HunterGate(
    URL "https://github.com/ruslo/hunter/archive/v0.20.7.tar.gz"
    SHA1 "a376088a679fbfbe3a2c330746801f1d47e51a37"
)

PROJECT(quake2-pt)

OPTION(CONFIG_GL_RENDERER "Enable GL renderer")
OPTION(CONFIG_VKPT_RENDERER "Enable VKPT renderer" ON)
OPTION(CONFIG_USE_OPTIX "Use optix for tracing rays")
SET(CONFIG_OPTIX_DIR "" CACHE PATH "Optix SDK directory")

# ---------- Setup output Directories -------------------------
SET (CMAKE_LIBRARY_OUTPUT_DIRECTORY
   ${PROJECT_BINARY_DIR}/Bin
   CACHE PATH
   "Single Directory for all Libraries"
   )

# --------- Setup the Executable output Directory -------------
SET (CMAKE_RUNTIME_OUTPUT_DIRECTORY
   ${PROJECT_BINARY_DIR}/Bin
   CACHE PATH
   "Single Directory for all Executables."
   )
   
# --------- Dependencies -------------------------------------
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set( IS_64_BIT 1 )
else ()
endif ()

# --------- Dependencies -------------------------------------
hunter_add_package(zlib)
hunter_add_package(png)
hunter_add_package(jpeg)
hunter_add_package(sdl2)
#hunter_add_package(curl)

find_package(ZLIB CONFIG REQUIRED)
find_package(PNG CONFIG REQUIRED)
find_package(JPEG CONFIG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
#find_package(Vulkan)
#find_library(Vulkan_LIBRARY NAMES vulkan-1 vulkan PATHS "C:/VulkanSDK/1.1.85.0")

link_directories(.)

# Compatibility mode
find_package(ZLIB REQUIRED)
string(COMPARE EQUAL "${ZLIB_INCLUDE_DIRS}" "" is_empty)
if(is_empty)
  message(FATAL_ERROR "Expected non-empty")
endif()


SET(CMAKE_INSTALL_PREFIX ${PROJECT_BINARY_DIR}/install)


ADD_SUBDIRECTORY(src)
