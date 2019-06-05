# Quake II RTX

**Quake II RTX** is NVIDIA's attempt at implemeting a fully functional 
version of Id Software's 1997 hit game **Quake II** with RTX path-traced 
global illumination.

**Quake II RTX** builds upon the [Q2VKPT](http://brechpunkt.de/q2vkpt) 
branch of the Quake II open source engine. Q2VKPT was created by former 
NVIDIA intern Christoph Schied, a Ph.D. student at the Karlsruhe Institute 
of Technology in Germany.

Q2VKPT, in turn, builds upon [Q2PRO](https://skuller.net/q2pro/), which is a 
modernized version of the Quake II engine. Consequently, many of the settings 
and console variables that work for Q2PRO also work for Quake II RTX. The 
[client](https://skuller.net/q2pro/nightly/client.html) and 
[server](https://skuller.net/q2pro/nightly/server.html) manuals are particularly useful.

## License

**Quake II RTX** is licensed under the terms of the **GPL v.2** (GNU General Public License).
You can find the entire license in the license.txt file.

The **Quake II** game data files remain copyrighted and licensed under the
original id Software terms, so you cannot redistribute the pak files from the
original game.

## Features

**Quake II RTX** introduces the following features:
  - Caustics approximation
  - Cylindrical projection mode
  - Dynamic lighting for items such as blinking lights, signs, switches, elevators and moving objects
  - Dynamic real-time "time of day" lighting
  - Flare gun and other high-detail weapons
  - High-quality screenshot mode
  - Improved denoising technology
  - Multi-GPU (SLI) support
  - Multiplayer modes (deathmatch and cooperative)
  - Optional two-bounce indirect illumination
  - Particles, laser beams, and new explosion sprites
  - Physically based materials, including roughness, metallic, emissive, and normal maps
  - Player avatar (casting shadows, visible in reflections)
  - Reflections and refractions on water and glass, reflective chrome and screen surfaces
  - Procedural environments (sky, mountains, clouds that react to lighting; also space)
  - Sunlight with direct and indirect illumination
  - Volumetric lighting (god-rays)

You can download functional builds of the game from [NVIDIA](https://www.geforce.com/quakeiirtx/)
or [Steam](https://store.steampowered.com/).

## Additional Information

  * [Announcement Article](https://www.nvidia.com/en-us/geforce/news/quake-ii-rtx-ray-tracing-vulkan-vkray-geforce-rtx/)
  * [Ray-Tracing Deep Dive](https://www.nvidia.com/en-us/geforce/news/geforce-gtx-dxr-ray-tracing-available-now/)
  * [Launch Trailer Video](https://www.youtube.com/watch?v=unGtBbhaPeU)
  * [Path Tracer Overview Video](https://www.youtube.com/watch?v=BOltWXdV2XY)
  * [GDC 2019 Presentation](https://www.gdcvault.com/play/1026185/)

## Forum

  * https://forums.geforce.com/default/topic/1119082/geforce-rtx-20-series/quake-ii-rtx-installation-guide/

## System Requirements

In order to build **Quake II RTX** you will need the following software
installed on your computer (with at least the specified versions or more 
recent ones).

### Operating System

|             | Windows    | Linux     |
|-------------|------------|-----------|
| Min Version | Win 7 x64  | Ubuntu 16 |

Note: only the Windows 10 version has been extensively tested.

Note: distributions that are binary compatible with Ubuntu 16 should work as well.

### Software

|                                                     | min Version |
|-----------------------------------------------------|-------------|
| NVIDIA driver <br> https://www.geforce.com/drivers  | 430         |
| git <br> https://git-scm.com/downloads              | 2.15        |
| CMake <br> https://cmake.org/download/              | 3.8         |
| Vulkan SDK <br> https://www.lunarg.com/vulkan-sdk/  | 1.1.92      |

## Submodules

* [zlib](https://github.com/madler/zlib)
* [curl](https://github.com/curl/curl)
* [SDL2](https://github.com/spurious/SDL-mirror)

## Build Instructions

  1. Clone the repository and its submodules from git :

     ```git clone --recursive https://github.com/NVIDIA/q2rtx.git ```

  2. Create a build folder named 'build' under the repository root (q2rtx/build)     

     Note: this is required by the shader build rules.

  3. Copy (or create a symbolic link) to the game assets folder (q2rtx/baseq2) 

     Note: the asset packages from the binary build of Quake II RTX are required for the engine to run.
     Specifically, the `blue_noise.pkz` and `q2rtx_media.pkz` files or their extracted contents.

  4. Configure CMake with either the GUI or the command line and make sure to
     point the build at the 'build' folder created in step 1.

  5. Build with Visual Studio on Windows, make on Linux, or the CMake command
     line:

     ```cmake --build . ```

## MIDI Controller Support

The Quake II console can be remote operated through a UDP connection, which
allows users to control in-game effects from input peripherals such as MIDI controllers. This is 
useful for tuning various graphics parameters such as position of the sun, intensities of lights, 
material parameters, filter settings, etc.

You can find a compatible MIDI controller driver [here](https://github.com/NVIDIA/korgi)

To enable remote access to your Quake II RTX client, you will need to set the following 
console variables _before_ starting the game, i.e. in the config file or through the command line:
```
 rcon_password "<password>"
 backdoor "1"
```

Note: the password set here should match the password specified in the korgi configuration file.

Note 2: enabling the rcon backdoor allows other people to issue console commands to your game from 
other computers, so choose a good password.
