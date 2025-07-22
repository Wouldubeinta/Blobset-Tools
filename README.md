![Title Image](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/title.jpg?raw=true)

# Blobset Tools

Extract and Modify BigAnt games blobset files.

# How to Use:

**Important:** 

> Before use, make sure you make a steam backup of the game if you have
> enough space on your hard drive. It just saves time doing a **Verify
> Integrity Of Game Files** just in case something goes wrong with the
> Blobset Tools. If it's the older games from AFL Live to RLL4, then you
> don't need to do this.

Ok, Pick your game that you want to mod.

![Title Image](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/1.jpg?raw=true)

First time use, the program may run the **Update File Mapping Data**, just let it do it's thing. This will also run if there is a game update.

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

To extract all the files click on **Extract Blobset** and all the files will be saved to this location - **"Blobset Tools\games\\"The Game You Picked Folder"\data-0.blobset\\"**.

To modify the blobset files, place your modded files in this location  - **"Blobset Tools\games\\"The Game You Picked Folder"\mods\\"**, in their corresponding folders. Then click on **Modify Blobset**.

**Tools:** 
To create a TXPK file, first you need to extract one to rebuild it. So first make sure the txpk folder in this location is empty - **"Blobset Tools\txpk\"**.  On the right hand side select the dds_txpk folder, then double click on a .txpk file you want, it will load it up into the TXPK Viewer. Click on **Extract TXPK** and select this location - **"Blobset Tools\txpk\"**.

![enter image description here](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/5.jpg?raw=true)

After the extraction, go to the **"Blobset Tools\txpk"** folder where you just extracted it to, then make your changers to the .dds files.

Once done, go to the Blobset Tools, click on **"Tools->TXPK Creator"**. Once loaded, click on **"Create TXPK"** and save it to this location - **"Blobset Tools\games\\"The Game You Picked Folder"\\mods\dds_txpk\\"**. By the way, you can name the txpk file to what ever you want, but just make sure the .xml file has the same name.

![enter image description here](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/6.jpg?raw=true)

![enter image description here](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/7.jpg?raw=true)

To create a M3MP file, first you need to extract one to rebuild it. So first make sure the m3mp folder in this location is empty - **"Blobset Tools\m3mp\\"**.  On the right hand side select the "m3mp\uncompressed" or "m3mp\compressed" folder, then double click on a .m3mp file you want, it will load it up into the M3MP Viewer. Click on **Extract M3MP** and select this location - **"Blobset Tools\m3mp\\"**.

![enter image description here](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/8.jpg?raw=true)

After the extraction, go to the **"Blobset Tools\m3mp\\"** folder where you just extracted it to, then make your changers to the files.

Once done, go to the Blobset Tools, click on **"Tools->M3MP Creator"**. Once loaded, click on **"Create M3MP"** and save it to this location - **"Blobset Tools\games\\"The Game You Picked Folder"\mods\m3mp\uncompressed\\"** if it was extracted from the uncompressed folder. Same as the TXPK, you can name the .m3mp file to what ever you want, but just make sure the .xml file has the same name.

![enter image description here](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/9.jpg?raw=true)

**Options:**

![enter image description here](https://github.com/Wouldubeinta/Blobset-Tools/blob/master/ReadMe/10.jpg?raw=true)

 - **Blobset Compression** - Not used at the moment, the files are created
   like the originals. I'm thinking of getting ride of it.
   
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

**Special Thanks To:**
BigAnt for the support.
Luigi Auriemma for he's expertise over the year's.
FeudalNate for PackageIO class.
7zip class - https://www.7-zip.org/a/lzma2500.7z
Facebook & oleg-st for ZstdSharp - https://github.com/oleg-st/ZstdSharp
nickbabcock for Pfim - https://github.com/nickbabcock/Pfim
