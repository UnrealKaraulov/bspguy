# bspguy
A tool for view and edit GoldSrc maps, and merging Sven Co-op maps without decompiling.

This fork support multiple bsp formats: BSP2,2PSB,29,bsp30ex,broken clipnodes.

# Usage
To launch the 3D editor, drag and drop a .bsp file onto the executable/window, or "Open with" bspguy, or run `bspguy` without args.

See the [wiki](https://github.com/wootguy/bspguy/wiki) for tutorials.

## Editor Features
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

Added new features in this fork:
- Texture Rotation
- Face Editor Update(better texture support, verts manual editor, etc, but without texture browser)
- Export obj, wad, ent, bsp(like "valve hammer prefabs", model with working collision, can be used in any entity instead of MDL models)
- Import wad, ent, bsp(in two modes)
- Render bsp and mdl models(MDL with no rendermodes/lightings support)
- Full support for "angle" and "angles" keyvalue.
- Render ents and models using these keyvalues.
- Full featured LightMap Editor.
- Updated Entity Report, added search by any parameters and sorting by fgd flags.
- Added "undo/redo" for any manipulation. (Move ents/origin, etc)
- Added move model(as option for transforming, like move origin)
- Added CRC-Spoofing(now possible to replace original map and play it on any servers)
- Updated controls logic(now can't using hotkeys and manipulation, if any input/window is active)
- Replaced and edited many functions(using static analysis proposals, compiler warnings)
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

bspguy saves configuration files to executable folder or in '%APPDATA%/bspguy` if not found.


## Command Line
Some functions are only available via the CLI.

```
Usage: bspguy <command> <mapname> [options]

<Commands>
  info      : Show BSP data summary
  merge     : Merges two or more maps together
  noclip    : Delete some clipnodes/nodes from the BSP
  delete    : Delete BSP models
  simplify  : Simplify BSP models
  transform : Apply 3D transformations to the BSP

Run 'bspguy <command> help' to read about a specific command.
```

# Building the source
### Windows users:
1. Install Visual Studio 2022
    * Visual Studio: Make sure to checkmark "Desktop development with C++" if you're installing for the first time. 
2. Download and extract [the source](https://github.com/UnrealKaraulov/newbspguy/archive/master.zip) somewhere
3. Open vs-project/bspguy.sln

### Linux users:
1. Install Git, CMake, X11, GLFW, GLEW, and a compiler.
    * Debian: `sudo apt install build-essential git cmake libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev xorg-dev libglfw3-dev libglew-dev`
2. Download the source: `git clone https://github.com/wootguy/bspguy.git`
3. Download [Dear ImGui](https://github.com/ocornut/imgui/releases/tag/v1.81) and extract next to the `src` folder. Rename to `imgui`.
4. Open a terminal in the `bspguy` folder and run these commands:
    ```
    mkdir build; cd build
    cmake .. -DCMAKE_BUILD_TYPE=RELEASE
    make
    ```
    (a terminal can _usually_ be opened by pressing F4 with the file manager window in focus)
