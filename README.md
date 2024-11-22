# bspguy
A tool for view and edit GoldSrc maps, and merging Sven Co-op maps without decompiling.

This fork support multiple bsp formats: BSP2,2PSB,29,bsp30ex,broken clipnodes.

# Usage
To launch the 3D editor, drag and drop a .bsp file onto the executable/window, or "Open with" bspguy, or run `bspguy` without args.

See the [wiki](https://github.com/wootguy/bspguy/wiki) for tutorials.

## BPSGUY Editor Features
- Keyvalue editor with FGD support
- Entity + BSP model creation and duplication
- Easy object movement and scaling
- Vertex manipulation + face splitting
    - Used to make perfectly shaped triggers. A box is often good enough, though.
- BSP model origin movement/alignment
- Optimize + clean commands to prevent overflows
- Hull deletion + redirection + creation
  - clipnode generation is similar to `-cliptype legacy` in the CSG compiler (the _worst_ method)
- Basic face editing
# NEWBPSGUY Editor Updated Features:
- Texture Rotation
- Face Editor Update(better texture support, verts manual editor, etc, but without texture browser)
- Cull leaf faces (for example 0 solid leaf for cleanup)
- Leaf Editor (WIP)
- Export obj, wad, ent, bsp/bspmodel, hlrad files.
- Import wad, ent, bsp(in two modes)
- Render .BSP and .MDL models.
- Render .SPR sprites (WIP).
- Full support for "angle" and "angles" keyvalue.
- Full featured **LightMap Editor** for edit single or multiple faces.
- Updated Entity Report, added search by any parameters and sorting by fgd flags.
- Added "undo/redo" support for many manipulations. (Move ents/origin, etc)
- Added move model(as option for transforming, like move origin)
- Added CRC-Spoofing(now possible to replace original map and play it on any servers)
- Updated controls logic(now can't using hotkeys and manipulation, if any input/window is active)
- Support J.A.C.K fgd files
- Keyvalue editor can be used for edit all selected entities
- Protect map (anti-decompile, WIP)
...

![image](https://user-images.githubusercontent.com/12087544/88471604-1768ac80-cec0-11ea-9ce5-13095e843ce7.png)

**The editor is full of bugs, unstable, and has undo button not works for some cases. 
Save early and often! Make backups before experimenting with anything.**

Requires OpenGL 3.0 or later.

## First-time Setup
1. Click `File` -> `Settings` -> `General`
2. Set the `Game Directory`, then click `Apply Changes`.
3. Click the 'Assets' tab and enter full or relative path to mod directories (cstrike/valve and etc)
    - This will fix the missing textures
4. Click the `FGDs` tab and add the full or relative path to your mod_name.fgd. Click `Apply Changes`.
    - This will give point entities more colorful cubes, and enable the `Attributes` tab in the `Keyvalue editor`.

bspguy saves configuration files to executable folder 


## Command Line
Some functions are only available via the CLI.

```
Usage: bspguy <command> <mapname> [options]

<Commands>
  info      : Show BSP data summary
  merge     : Merges two or more maps together
  noclip    : Delete some clipnodes/nodes from the BSP
  simplify  : Simplify BSP models
  delete    : Delete BSP models
  transform : Apply 3D transformations to the BSP
  unembed   : Deletes embedded texture data
  exportobj : Export bsp geometry to obj [WIP]
  cullfaces : Remove leaf faces from map
  exportlit : Export .lit (Quake) lightdata file
  importlit : Import .lit (Quake) lightdata file to map.
  exportrad : Export RAD.exe .ext & .wa_ files for hlrad.exe
  exportwad   : Export all map textures to .wad file
  importwad   : Import all .wad textures to map

  

Run 'bspguy <command> help' to read about a specific command.
```

# Building the source
### Windows users:
1. Install Visual Studio 2022
    * Visual Studio: Make sure to checkmark "Desktop development with C++" if you're installing for the first time. 
2. Download and extract [the source](https://github.com/UnrealKaraulov/newbspguy/archive/master.zip) somewhere
3. Open vs-project/bspguy.sln

### Linux users:
1. Install Git, CMake, X11, GLEW, and a compiler.
    * Debian: `sudo apt install build-essential git cmake libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev xorg-dev libglfw3-dev libglew-dev`
2. Download the source: `git clone https://github.com/wootguy/bspguy.git`
3. Open a terminal in the `bspguy` folder and run these commands:
    ```
    mkdir build; cd build
    cmake .. -DCMAKE_BUILD_TYPE=RELEASE
    make
    ```
    (a terminal can _usually_ be opened by pressing F4 with the file manager window in focus)
