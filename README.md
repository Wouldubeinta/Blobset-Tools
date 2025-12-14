![Title Image](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/title.jpg?raw=true)

# Blobset Tools

Extract and Modify BigAnt games blobset files.

![Download Count](https://img.shields.io/github/downloads/Wouldubeinta/Blobset-Tools/total.svg)

# How to Use:
 
> # **$$\color{red}Important:$$**
> 
> **Make sure u have a fresh game installation without mods and do the following. This is for later BigAnt games**
> 
> Now I've added a Restore Backup Files, used for restoring the original files. You will need to use this before updating the game, if there is a game update.
> 
> This will only work properly if u do the following -
> 
> In Steam click on **(Steam)** on the top left corner, click on **(Settings)** then go to Downloads. Where it's got **(Update to installed games)** change it to **(Only update at game launch)**.
> 
> From now on, only run the game from inside steam, not the desktop icon, this will prevent the game from updating automatically.
>
> If you load up Steam and the game has a **(Update)**, Open the Blobset Tools go to **(Options)** and click on **(Restore Backup Files)**. Now you can update the game and then in the Blobset Tools, go to **(Options->Update File Mapping Data)**.
>
> If that fails, u need to run **(Verify Integrity Of Game Files)** so the games is fresh and start again.
>
> If it's the older games from AFL Live to RLL4, then you don't need to do this.

Ok, Pick your game that you want to mod.

![Image 1](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/1.jpg?raw=true)

First time use, run the **Update File Mapping Data** in the Options, just let it do it's thing.

**For Older BigAnt Games with version one blobset files** If you select **File->Open** You can open a version one blobset file.
If you select **File->Game Selection** it will take you back to the main game selection screen.

![Image 2](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/2.jpg?raw=true)

On the left side you should see the folder layout. Select a folder and on the right it will show the files in that folder.

![Image 2](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/2.jpg?raw=true)

If you double click a .dds file it will load it into the DDS Viewer.
If you double click a .txpk file it will load into the TXPK Viewer.
If you doulble click a .m3mp file it will load into the M3MP Viewer.
If you double click a .dat file it will load into the Hex Viewer.

If you right click a file you can save it some where.

![Image 3](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/3.jpg?raw=true)

If you right click the picture box on the right side, you can flip the image.

![Image 4](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/4.jpg?raw=true)

To extract all the files click on **Blobset->Extract** and all the files will be saved to this location - **"Blobset Tools\games\\"Game Platform(pc)"\\"The Game You Picked Folder"\Name of Blobset(data-0.blobset.pc)\\"**.

To modify the blobset files, place your modded files in this location  - **"Blobset Tools\games\Game Platform(pc)\\"The Game You Picked Folder"\mods\\"**, in their corresponding folders. Then click on **Modify Blobset**.

**Tools:** 
To create a TXPK file, first you need to extract one to rebuild it. So first make sure the txpk folder in this location is empty - **"Blobset Tools\games\\"Game Platform(pc)"\\"The Game You Picked Folder"\txpk\"**.  On the right hand side select the dds_txpk folder, then double click on a .txpk file you want, it will load it up into the TXPK Viewer. Click on **Extract TXPK** and select this location - **"Blobset Tools\txpk\"**.

![Image 5](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/5.jpg?raw=true)

After the extraction, go to the **"Blobset Tools\games\\"Game Platform(pc)"\\"The Game You Picked Folder"\txpk\"** folder where you just extracted it to, then make your changers to the .dds files.

Once done, go to the Blobset Tools, click on **"Tools->TXPK Creator"**. Once loaded, click on **"Create TXPK"** and save it to this location - **"Blobset Tools\games\\"Game Platform(pc)"\\"The Game You Picked Folder"\\mods\dds_txpk\\"**. By the way, you can name the txpk file to what ever you want, but just make sure the .xml file has the same name.

![Image 6](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/6.jpg?raw=true)

![Image 7](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/7.jpg?raw=true)

To create a M3MP file, first you need to extract one to rebuild it. So first make sure the m3mp folder in this location is empty - **"Blobset Tools\m3mp\\"**.  On the right hand side select the "m3mp\uncompressed" or "m3mp\compressed" folder, then double click on a .m3mp file you want, it will load it up into the M3MP Viewer. Click on **Extract M3MP** and select this location - **"Blobset Tools\m3mp\\"**.

![Image 8](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/8.jpg?raw=true)

After the extraction, go to the **"Blobset Tools\m3mp\\"** folder where you just extracted it to, then make your changers to the files.

Once done, go to the Blobset Tools, click on **"Tools->M3MP Creator"**. Once loaded, click on **"Create M3MP"** and save it to this location - **"Blobset Tools\games\\"The Game You Picked Folder"\mods\m3mp\uncompressed\\"** if it was extracted from the uncompressed folder. Same as the TXPK, you can name the .m3mp file to what ever you want, but just make sure the .xml file has the same name.

![Image 9](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/9.jpg?raw=true)

**Options:**

![Image 10](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/10.jpg?raw=true)

 - **Update File Mapping Data** - This is used to map all of the files in the
   blobset, so the Blobset Tools knows what file is what. This will run
   the first time you load the Blobset Tools if it doesn't have the
   latest mapping file. Also if the game has a update, the Blobset Tools
   will run this automatically on launch.
   
 - **Load Game** - If ticked, this will load the game after clicking on
   Modify Blobset.
   
 - **Validate Steam Game Files** - This is used to repair the game files if
   something goes wrong. Not sure if this still works anymore, because I
   think Steam might of change something in their api.
   
 - **Skip Unknown Files** - If ticked, this will skip unknown file types
   like meshes, animations, ect..., which haven't been mapped yet.

 - **Restore Backup Files** - This will restore the files you replaced with mods.

**Special Thanks To:**
 - BigAnt for the support.
 - Luigi Auriemma for he's expertise over the year's.
 - FeudalNate for PackageIO class.
 - 7zip class - https://www.7-zip.org/a/lzma2500.7z
 - Facebook & oleg-st for ZstdSharp - https://github.com/oleg-st/ZstdSharp
 - nickbabcock for Pfim - https://github.com/nickbabcock/Pfim
 - shravan2x for https://github.com/shravan2x/Gameloop.Vdf
 - JamesNK for https://github.com/JamesNK/Newtonsoft.Json
 - Crauzer for https://github.com/Crauzer/WEMSharp
 - knot3 for OggSharp - https://github.com/knot3/OggSharp
 - lostromb for concentus.oggfile - https://github.com/lostromb/concentus.oggfile
