![Title Image](https://github.com/Wouldubeinta/Blobset-Tools/blob/qt6_master/readme/image.jpg?raw=true)

# Blobset Tools

**Extract and Modify BigAnt Games Blobset Files**

![Download Count](https://img.shields.io/github/downloads/Wouldubeinta/Blobset-Tools/total.svg)
![Latest Release](https://img.shields.io/github/v/release/Wouldubeinta/Blobset-Tools)

Blobset Tools opens the blobset archives of BigAnt's games, lets you browse, preview and extract every file, and puts
your mods back into the game. It's now built with **Qt 6 / C++** (the original was C#/WinForms) and adds a Mod
Manager, Vortex support, a command line, a Mesh Viewer, Xbox 360 textures and much more.

> **Unofficial fan-made tool.** Blobset Tools is not an official product of Big Ant Studios or Nacon and isn't
> affiliated with them, though Big Ant Studios has kindly helped with this modding work. See the
> [Disclaimer](#disclaimer).

---

## Supported Games

| Game | PC | PS3 | Xbox 360 |
|---|:-:|:-:|:-:|
| AFL Live | ✔ | ✔ | ✔ |
| Rugby League Live 2 | | ✔ | ✔ |
| Rugby League Live 2 - World Cup Edition | | ✔ | ✔ |
| Don Bradman Cricket 14 | ✔ | ✔ | ✔ |
| Rugby League Live 3 | ✔ | ✔ | ✔ |
| TableTop Cricket | ✔ | ✔ | ✔ |
| Casey Powell Lacrosse 16 | ✔ | | |
| Don Bradman Cricket 17 | ✔ | | |
| Masquerade - The Baubles of Doom | ✔ | | |
| Ashes Cricket | ✔ | | |
| Rugby League Live 4 | ✔ | | |
| AO International Tennis | ✔ | | |
| Casey Powell Lacrosse 18 | ✔ | | |
| Cricket 19 | ✔ | | |
| AO Tennis 2 | ✔ | | |
| Tennis World Tour 2 | ✔ | | |
| Cricket 22 | ✔ | | |
| AFL 23 | ✔ | | |
| Cricket 24 | ✔ | | |
| Tiebreak | ✔ | | |
| Rugby 25 | ✔ | | |
| AFL 26 | ✔ | | |
| Rugby League 26 | ✔ | | |
| Cricket 26 | ✔ | | |

---

## Features

- **Browse and preview** every file in a blobset: DDS textures (PC, PS3 and Xbox 360), TXPK / TXPL texture packs,
  M3MP packages, meshes (3D Mesh Viewer), BSB Flash UI screens, WEM / BNK / FSB / FMOD audio, string tables, Lua,
  text and a Hex Viewer for everything else.
- **Extract** a whole blobset or **Save As** single files (DDS or PNG, WEM or WAV, ...).
- **Modify** the game with your mods (the originals are backed up first) and **Restore** them any time.
- **Creators**: TXPK Creator, M3MP Creator and BSB Creator to rebuild packs from extracted files.
- **Editors**: File Mapping Editor and String Editor (the game's text, one table per language).
- **Mod Manager**: install mods from .zip / .7z / .rar or folders, enable / disable them, set the load order, see
  conflicts, then Build.
- **Blobset Mod Loader** support for the newer (V4) games: mods go into `<game>\BlobsetMods` and the game's files stay
  untouched.
- **Vortex Mod Manager** extensions for all the PC games.
- **Command line** for scripts and mod managers (`Blobset-Tools.exe --help`).

---

## Installing

1. Download **BlobsetTools-Setup-x.x.x.x.exe** from the [Releases](https://github.com/Wouldubeinta/Blobset-Tools/releases) page and run it.
2. Choose **Install for all users** (Program Files) or **Install for me only** (no admin needed).
3. **Tick the games you own.** Blobset Tools will only list those games. Run the setup again any time to add or
   remove games (your settings and mods are kept).
4. Optionally tick **Set up Vortex Mod Manager support** (only if you use Vortex).

### Where your files go

The program folder is read only. Everything Blobset Tools writes lives in your Documents folder:

```
Documents\Blobset Tools\                (e.g. C:\Users\<you>\OneDrive\Documents\Blobset Tools)
    Settings.ini
    cli.log
    games\<Game>\<Platform>\
        GameInfo.ini                    the game's location
        data\                           file mapping
        backup\                         original files replaced by Modify
        mods\                           your modded files (workspace)
        mod_library\                    mods installed with the Mod Manager
        txpk\  m3mp\                    TXPK / M3MP Creator work folders
        data-0.blobset.pc\              extracted files (Blobset > Extract)
```

Use **Options > Open Data Folder** to open it. Uninstalling Blobset Tools leaves this folder alone.

> **Upgrading from an older version?** The first time the new version starts it moves your old `games` folder and
> `Settings.ini` from next to the exe into `Documents\Blobset Tools` (extracted blobsets are left behind; run Extract
> again if you need them).
>
> **Want a portable copy?** Put an empty file called `portable.txt` next to `Blobset-Tools.exe` and everything stays
> next to the exe, like before.

---

## How to Use

> # **$$\color{red}Important:$$**
> Ensure you have a fresh game installation without mods before proceeding.

### Backup and Update Process

1. **Restore Backup Files**: This puts the game's original files back. Use it before updating the game if an update
   is available.

2. **Steam Settings**:
   - Open Steam and click on **Steam** in the top left corner.
   - Navigate to **Settings** > **Downloads**.
   - Change **Update to installed games** to **Only update at game launch**.

3. **Launching the Game**: Always run the game from within Steam, not from the desktop icon, to prevent automatic
   updates.

4. **Updating the Game**:
   - If an update is available when you launch Steam, open Blobset Tools, go to **Options**, and select
     **Restore Backup Files**.
   - After updating the game, return to Blobset Tools and select **Options > Update File Mapping Data**.

5. **Verification**: If the update fails, run **Options > Validate Steam Files** (or **Verify Integrity of Game
   Files** in Steam) so the game is fresh, then start the process again.

> **Note**: For older games (AFL Live to RLL4), these steps are not required.
>
> **Tip**: With **Mod Loader: On** (V4 games) the game's files are never changed, so game updates don't need a restore.

---

### Selecting Your Game

Pick the system platform (PC, PS3 or Xbox 360), select your game and click **Load Blobset Tools**. The first time,
Blobset Tools asks you to find the game's blobset file (e.g. `data-0.blobset.pc`); it's remembered after that.

![Image 1](https://github.com/Wouldubeinta/Blobset-Tools/blob/qt6_master/readme/1.jpg?raw=true)

### First-Time Setup

On your first use, run **Update File Mapping Data** from the Options menu and allow it to complete. (Blobset Tools
does this by itself when there's no mapping yet or the game was updated.)

---

## Working with Blobset Files

### For Older BigAnt Games (Version One Blobset Files)

- Select the game and platform on the game selection screen; Blobset Tools asks for the blobset file to load.
- To return to the main game selection screen, select **File > Game Selection**.

![Image 2](https://github.com/Wouldubeinta/Blobset-Tools/blob/qt6_master/readme/2.jpg?raw=true)

---

On the left side, you will see the **Folders** panel. Select a folder to view its contents in the **Files** panel.
Use the search box to find files by name (Enter: next, Shift+Enter: previous). The mouse wheel anywhere over the
Files panel scrolls it left / right.

### Viewing Files

- **Double-click** a file to open it in the matching viewer:
  - `.dds` in the DDS Viewer (PC, PS3 and Xbox 360 textures)
  - `.txpk` / `.txpl` in the TXPK Viewer
  - `.m3mp` in the M3MP Viewer
  - `.mesh` in the Mesh Viewer
  - `.bsb` in the BSB Viewer
  - `.wem`, `.bnk`, `.fsb`, `.bank`, `.fev` in the audio viewers
  - `.str`, `.lua`, `.txt` as text
  - anything else in the Hex Viewer

![Image 3](https://github.com/Wouldubeinta/Blobset-Tools/blob/qt6_master/readme/3.jpg?raw=true)

- **Right-click** a file to **Save As...** (DDS or PNG for textures, WEM or WAV for audio).

![Image 4](https://github.com/Wouldubeinta/Blobset-Tools/blob/qt6_master/readme/4.jpg?raw=true)

### Image Preview

- Right-click the image preview to **Flip Image** or toggle the **Alpha** channel.

![Image 5](https://github.com/Wouldubeinta/Blobset-Tools/blob/qt6_master/readme/5.jpg?raw=true)

### Extracting Files

To extract all files, click **Extract**. The files are saved to:
**`Documents\Blobset Tools\games\<Game Folder>\<Game Platform (pc-ps3-xbox360)>\<Name of Blobset (data-0.blobset.pc)>\`**

### Modifying Blobset Files

1. **Place Your Modded Files**:
   - Navigate to:
     **`Documents\Blobset Tools\games\<The Game You Picked Folder>\<Game Platform (pc-ps3-xbox360)>\mods\`**
   - Make sure your modded files are in the same folders they have in the blobset.

2. **Modify the Blobset**:
   - Once your files are in place, click **Modify**. The original files are backed up first.

> **Note for Older BigAnt Games with Version One Blobset Files**:
> To create a new blobset file, place your modded files in the same `mods` folder and click **Create**.
> **$$\color{red}Important:$$** Make sure the main blobset file is loaded before creating a new one; don't have the
> update (DLC) blobset loaded.

![Image 6](https://github.com/Wouldubeinta/Blobset-Tools/blob/qt6_master/readme/6.jpg?raw=true)

---

### Creating TXPK Files

1. **Prepare the TXPK Folder**:
   - Make sure the `txpk` folder is empty:
     **`Documents\Blobset Tools\games\<The Game You Picked Folder>\<Game Platform (pc-ps3-xbox360)>\txpk\`**

2. **Select a TXPK File**:
   - In the Files panel, open the `dds_txpk` folder and double-click the `.txpk` file to load it into the TXPK Viewer.

3. **Extract the TXPK**:
   - Click **Extract All** (or **Extract Selected**) and choose the `txpk` folder above.

![Image 7](https://github.com/Wouldubeinta/Blobset-Tools/blob/qt6_master/readme/7.jpg?raw=true)

4. **Modify DDS Files**:
   - Edit the `.dds` files in the `txpk` folder.

5. **Create the TXPK**:
   - Click **Tools > TXPK Creator**, then **Create TXPK** and save it to:
     **`Documents\Blobset Tools\games\<The Game You Picked Folder>\<Game Platform (pc-ps3-xbox360)>\mods\dds_txpk\`**
   - You can name the TXPK file as you wish, but the matching `.xml` file must have the same name.

![Image 8](https://github.com/Wouldubeinta/Blobset-Tools/blob/qt6_master/readme/8.jpg?raw=true)

![Image 9](https://github.com/Wouldubeinta/Blobset-Tools/blob/qt6_master/readme/9.jpg?raw=true)

### Creating an M3MP File

1. **Prepare the M3MP Folder**:
   - Make sure the `m3mp` folder is empty:
     **`Documents\Blobset Tools\games\<The Game You Picked Folder>\<Game Platform (pc-ps3-xbox360)>\m3mp\`**

2. **Select the M3MP File**:
   - In the Files panel, open the `m3mp\uncompressed` or `m3mp\compressed` folder.
   - Double-click the `.m3mp` file to load it into the M3MP Viewer.

3. **Extract the M3MP File**:
   - Click **Extract All** (or **Extract Selected**) and choose the `m3mp` folder above.

![Image 10](https://github.com/Wouldubeinta/Blobset-Tools/qt6_master/readme/10.jpg?raw=true)

4. **Modify the Extracted Files** in the `m3mp` folder.

5. **Create the M3MP File**:
   - Click **Tools > M3MP Creator**, then **Create M3MP** and save it to:
     **`Documents\Blobset Tools\games\<The Game You Picked Folder>\<Game Platform (pc-ps3-xbox360)>\mods\m3mp\uncompressed\`**
     (if it was extracted from the uncompressed folder).
   - You can name the `.m3mp` file as you wish, but the matching `.xml` file must have the same name.

![Image 11](https://github.com/Wouldubeinta/Blobset-Tools/blob/qt6_master/readme/11.jpg?raw=true)

### BSB Creator

**Tools > BSB Creator** rebuilds a BSB (Flash UI screen) from its extracted movie and textures. Save the result into
the matching folder under `mods\` and click **Modify**.

---

## Mod Manager

**Tools > Mod Manager** keeps a mod library for each game and platform.

- **Install Archive...** (.zip / .7z / .rar), **Install Folder...**, or drop archives and folders onto the page.
  Blobset Tools finds the mod's root folder itself, so wrapper folders inside archives don't matter.
- **Enable / disable** mods with the check box; disabled mods stay installed.
- **Load order**: mods lower in the list win when two of them have the same file. The **Conflicts** tab lists them.
- **Build** (the same as **Modify**) puts the `mods` folder and every enabled mod into the game. Mods you disabled
  or removed since the last build are taken out again.

Mod authors can add a `blobsetmod.json` (name, author, version, description, url) to the mod's root folder.
See [docs/MOD_MANAGER.md](docs/MOD_MANAGER.md) for the details.

### Vortex Mod Manager

1. Tick **Set up Vortex Mod Manager support** in the setup, or use **Mod Manager > Set Up Vortex** in Blobset Tools.
2. Restart Vortex and manage the game (Games > search for it).
3. Install mods in Vortex as usual. After every deploy / purge Vortex runs Blobset Tools to build them into the game.

Close the Blobset Tools window before deploying in Vortex.

### Command Line

```
Blobset-Tools.exe --list-games
Blobset-Tools.exe --game "Cricket 24" --build
Blobset-Tools.exe --game "Cricket 24" --install "C:\Downloads\Better Bats.zip" --build
Blobset-Tools.exe --game "Cricket 24" --disable "Better Bats" --list-mods
Blobset-Tools.exe --game "Rugby League Live 3" --restore
```

`Blobset-Tools.exe --help` lists every option. The output is also written to `Documents\Blobset Tools\cli.log`.

---

## Options

![Image 12](https://github.com/Wouldubeinta/Blobset-Tools/blob/qt6_master/readme/12.jpg?raw=true)

- **Update File Mapping Data**: Maps all files in the blobset so Blobset Tools knows each file. It runs by itself the
  first time and after a game update.
- **Resolve File Names**: Works out the names of unknown hashes from the known file names.
- **Load Game**: When on, the game starts after **Modify** / **Create** succeeds.
- **Validate Steam Files**: Asks Steam to verify the game's files and download any that changed.
- **Mod Loader** (V4 games): When on, **Modify** writes the mods to `<game>\BlobsetMods` for the Blobset Mod Loader
  instead of changing the game's files.
- **Open Data Folder**: Opens `Documents\Blobset Tools`.
- **Restore Backup Files**: Puts back every original file that was replaced by mods.

---

## Building from Source

Requirements:

- **Qt 6.11** (MinGW 64-bit) with Qt Quick, Qt Quick 3D, Qt Multimedia, Qt Concurrent and Qt Network
- **CMake 3.21+** (comes with Qt Creator)
- The third-party libraries in `lib` / `include` (FFmpeg, vgmstream, DirectXTex, zstd, liblzma, Ogg / Vorbis / Opus,
  ATRAC9, CELT, mpg123)

Open `CMakeLists.txt` in Qt Creator and build the **Release** configuration. The build copies the DLLs,
`GameSelectionInfo.xml`, `assets\games`, `defaults` and `vortex` next to the exe and runs windeployqt.

### Building the Installer

1. Install [Inno Setup 6.3+](https://jrsoftware.org/isdl.php).
2. Build the Release configuration.
3. Run:

   ```
   installer\build_installer.bat
   ```

   (`-BuildDir`, `-QtBin` and `-Iscc` change the default folders.) The setup is written to
   `installer\output\BlobsetTools-Setup-<version>.exe`, with the version from `CMakeLists.txt`.

The Vortex extensions are generated with `python tools/make_vortex_extensions.py`.

---

## Special Thanks

- **Big Ant Studios**: For their ongoing support of the modding community and their help with this project.
- **Luigi Auriemma**: For his expertise over the years.
- **bartlomiejduda**: For PS3 and Xbox 360 image swizzling code - [bartlomiejduda](https://github.com/bartlomiejduda)
- **The Qt Project**: [Qt](https://www.qt.io/)
- **Microsoft**: [DirectXTex](https://github.com/microsoft/DirectXTex)
- **vgmstream**: For WEM / FSB / BNK audio decoding - [vgmstream](https://github.com/vgmstream/vgmstream)
- **FFmpeg**: [FFmpeg](https://ffmpeg.org/)
- **Facebook**: [Zstandard](https://github.com/facebook/zstd)
- **Lasse Collin and the XZ Utils team**: [liblzma](https://tukaani.org/xz/)
- **Xiph.Org**: [Ogg, Vorbis and Opus](https://xiph.org/)
- **Thealexbarney**: [LibAtrac9](https://github.com/Thealexbarney/LibAtrac9)
- **mpg123**: [mpg123](https://www.mpg123.de/)
- **Xenia**: For the Xbox 360 texture tiling reference - [Xenia](https://github.com/xenia-project/xenia)
- **Everyone who helped with the original C# version**: FeudalNate (PackageIO), nickbabcock (Pfim), oleg-st
  (ZstdSharp), shravan2x (Gameloop.Vdf), JamesNK (Newtonsoft.Json), Crauzer (WEMSharp), knot3 (OggSharp) and
  lostromb (concentus.oggfile).

---

## Disclaimer

Blobset Tools is an **unofficial, fan-made modding tool**. It is **not an official product of Big Ant Studios Pty
Ltd, Nacon S.A., or any other developer or publisher of the games it supports, and it is not affiliated with,
sponsored by or officially endorsed by them** (including their subsidiaries and affiliates).

Big Ant Studios has generously supported this modding work and helped with it in the past, and that help is greatly
appreciated. That doesn't make Blobset Tools an official Big Ant Studios or Nacon product: they don't provide
customer support for it, and nothing here speaks for them. Please don't contact Big Ant Studios or Nacon support
about problems with Blobset Tools or with modded games; report them on this project's
[Issues](https://github.com/Wouldubeinta/Blobset-Tools/issues) page instead.

- All game names, titles, logos, artwork, characters, audio and other game content are the property of their
  respective owners. They are used here only to identify the games this tool works with, and no claim of ownership
  is made. Steam is a trademark of Valve Corporation; PlayStation is a trademark of Sony Interactive Entertainment;
  Xbox and Xbox 360 are trademarks of Microsoft Corporation. All other trademarks belong to their respective owners.
- Blobset Tools contains no game content. You need your own legitimately owned copy of each game, and you are
  responsible for using this tool, and any mods you make or share, in line with each game's licence agreement
  (EULA), its terms of service and the law where you live. Don't use mods in online or competitive play where
  they aren't allowed.
- Modifying game files can break the game, corrupt saves or stop it from updating. Keep backups, use
  **Restore Backup Files** before game updates, and use **Validate Steam Files** if something goes wrong.
- This software is provided **"as is", without warranty of any kind**, express or implied. The authors and
  contributors are not liable for any damage, data loss or other problems that come from using it. You use it at
  your own risk.
