![Title Image](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/title.jpg?raw=true)

# Blobset Tools

**Extract and Modify BigAnt Games Blobset Files**

![Download Count](https://img.shields.io/github/downloads/Wouldubeinta/Blobset-Tools/total.svg)

---

## How to Use

> **Important:**  
> Ensure you have a fresh game installation without mods before proceeding.

### Backup and Update Process

1. **Restore Backup Files**: This feature is essential for restoring original files. Use it before updating the game if an update is available.
   
2. **Steam Settings**: 
   - Open Steam and click on **Steam** in the top left corner.
   - Navigate to **Settings** > **Downloads**.
   - Change **Update to installed games** to **Only update at game launch**.

3. **Launching the Game**: Always run the game from within Steam, not from the desktop icon, to prevent automatic updates.

4. **Updating the Game**: 
   - If an update is available when you launch Steam, open Blobset Tools, go to **Options**, and select **Restore Backup Files**.
   - After updating the game, return to Blobset Tools and navigate to **Options > Update File Mapping Data**.

5. **Verification**: If the update fails, run **Verify Integrity of Game Files** to ensure the game is fresh, then start the process again.

> **Note**: For older games (AFL Live to RLL4), these steps are not required.

---

### Selecting Your Game

Choose the game you want to mod:

![Image 1](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/1.jpg?raw=true)

### First-Time Setup

On your first use, run **Update File Mapping Data** from the Options menu and allow it to complete.

---

## Working with Blobset Files

### For Older BigAnt Games (Version One Blobset Files)

- To open a version one blobset file, select **File > Open**.
- To return to the main game selection screen, select **File > Game Selection**.

![Image 2](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/2.jpg?raw=true)

---

On the left side, you will see the folder layout. Select a folder to view its contents on the right.

### Viewing Files

- **Double-click** a file to open it in the appropriate viewer:
  - `.dds` files in the DDS Viewer
  - `.txpk` files in the TXPK Viewer
  - `.m3mp` files in the M3MP Viewer
  - `.dat` files in the Hex Viewer

![Image 3](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/3.jpg?raw=true)

- **Right-click** a file to save it to your desired location.

![Image 4](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/4.jpg?raw=true)

### Image Manipulation

- Right-click the picture box on the right side to flip the image or change the alpha.

![Image 5](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/5.jpg?raw=true)

### Extracting Files

To extract all files, click on **Blobset > Extract**. The files will be saved to: Blobset Tools\games<Game Folder><Game Platform (pc-ps3-xbox360)>\Name of Blobset (data-0.blobset.pc)\

### Modifying Blobset Files

To modify blobset files, follow these steps:

1. **Place Your Modded Files**: 
   - Navigate to the following directory:  
     **`Blobset Tools\games\<The Game You Picked Folder>\<Game Platform (PC)>\mods\`**  
   - Ensure that your modded files are placed in their corresponding folders.

2. **Modify the Blobset**: 
   - Once your files are in place, click on **Blobset > Modify**.

> **Note for Older BigAnt Games with Version One Blobset Files**:  
> To create a new blobset file, place your modded files in the same directory:  
> **`Blobset Tools\games\<The Game You Picked Folder>\<Game Platform (PC)>\mods\`**  
> After placing your files, click on **Blobset > Create**.  
> **Important**: Ensure that the main blobset file is loaded before creating a new one; do not use an update blobset.

![Image 6](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/6.jpg?raw=true)

---

### Creating TXPK Files

To create a TXPK file, follow these steps:

1. **Prepare the TXPK Folder**: 
   - Ensure that the `txpk` folder is empty. Navigate to:  
     **`Blobset Tools\games\<The Game You Picked Folder>\<Game Platform (PC)>\txpk\`**

2. **Select a TXPK File**: 
   - On the right side, select the `dds_txpk` folder and double-click on the desired `.txpk` file to load it into the TXPK Viewer.

3. **Extract the TXPK**: 
   - Click on **Extract TXPK** and choose the destination:  
     **`Blobset Tools\games\<The Game You Picked Folder>\<Game Platform (PC)\txpk\`**

![Image 7](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/7.jpg?raw=true)

4. **Modify DDS Files**: 
   - After extraction, navigate to:  
     **`Blobset Tools\games\<The Game You Picked Folder>\<Game Platform (PC)\txpk\`**  
   - Make your desired changes to the `.dds` files.

5. **Create the TXPK**: 
   - In Blobset Tools, click on **Tools > TXPK Creator**. Once loaded, click on **Create TXPK** and save it to:  
     **`Blobset Tools\games\<The Game You Picked Folder>\<Game Platform (PC)\mods\dds_txpk\`**  
   - You can name the TXPK file as you wish, but ensure that the corresponding `.xml` file has the same name.

---

![Image 8](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/8.jpg?raw=true)

![Image 9](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/9.jpg?raw=true)

## Creating an M3MP File

To create an M3MP file, follow these steps:

1. **Prepare the M3MP Folder**: 
   - Ensure that the `m3mp` folder is empty. Navigate to:  
     **`Blobset Tools\games\<The Game You Picked Folder>\<Game Platform (PC)>\m3mp\`**.

2. **Select the M3MP File**: 
   - On the right side, choose either the `m3mp\uncompressed` or `m3mp\compressed` folder.
   - Double-click on the desired `.m3mp` file to load it into the M3MP Viewer.

3. **Extract the M3MP File**: 
   - Click on **Extract M3MP** and select the destination:  
     **`Blobset Tools\games\<The Game You Picked Folder>\<Game Platform (PC)>\m3mp\`**.

![Image 10](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/10.jpg?raw=true)

4. **Modify the Extracted Files**: 
   - After extraction, navigate to the folder:  
     **`Blobset Tools\games\<The Game You Picked Folder>\<Game Platform (PC)>\m3mp\`**.
   - Make your desired changes to the files.

5. **Create the M3MP File**: 
   - In Blobset Tools, click on **Tools > M3MP Creator**.
   - Once loaded, click on **Create M3MP** and save it to:  
     **`Blobset Tools\games\<The Game You Picked Folder>\mods\m3mp\uncompressed\`** (if it was extracted from the uncompressed folder).
   - You can name the `.m3mp` file as you wish, but ensure that the corresponding `.xml` file has the same name.

![Image 11](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/11.jpg?raw=true)

## Options

![Image 12](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/12.jpg?raw=true)
![Image 13](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/13.jpg?raw=true)

- **Update File Mapping Data**: This option maps all files in the blobset, allowing Blobset Tools to recognize each file. This process will run automatically the first time you load Blobset Tools if the latest mapping file is not available. Additionally, if the game has an update, Blobset Tools will execute this automatically upon launch.

- **Load Game**: If checked, this option will load the game after you click on "Modify Blobset."

- **Validate Steam Game Files**: This feature repairs game files if issues arise. Please note that its functionality may vary, as Steam may have changed aspects of their API.

- **Skip Unknown Files**: If checked, this option will bypass unknown file types (e.g., meshes, animations) that have not yet been mapped.

- **Restore Backup Files**: This option restores any files that you replaced with mods.

- **Reset Blobset**: This feature resets the blobset file to its original state for older BigAnt games.

---

## Special Thanks

- **BigAnt**: For their ongoing support.
- **Luigi Auriemma**: For his expertise over the years.
- **FeudalNate**: For the PackageIO class.
- **7zip Class**: [7-zip](https://www.7-zip.org/a/lzma2500.7z)
- **Facebook & oleg-st**: For ZstdSharp - [ZstdSharp](https://github.com/oleg-st/ZstdSharp)
- **nickbabcock**: For Pfim - [Pfim](https://github.com/nickbabcock/Pfim)
- **shravan2x**: For [Gameloop.Vdf](https://github.com/shravan2x/Gameloop.Vdf)
- **JamesNK**: For [Newtonsoft.Json](https://github.com/JamesNK/Newtonsoft.Json)
- **Crauzer**: For [WEMSharp](https://github.com/Crauzer/WEMSharp)
- **knot3**: For OggSharp - [OggSharp](https://github.com/knot3/OggSharp)
- **lostromb**: For concentus.oggfile - [concentus.oggfile](https://github.com/lostromb/concentus.oggfile)
- **bartlomiejduda**: For PS3 and Xbox 360 image swizzling code - [bartlomiejduda](https://github.com/bartlomiejduda)
