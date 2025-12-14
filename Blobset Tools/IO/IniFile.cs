using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

/*
Creates or loads an INI file in the same directory as your executable
named EXE.ini (where EXE is the name of your executable)
var MyIni = new IniFile();

Or specify a specific name in the current dir
var MyIni = new IniFile("Settings.ini");

Or specify a specific name in a specific dir
var MyIni = new IniFile(@"C:\Settings.ini");
You can write some values like so:

MyIni.Write("DefaultVolume", "100");
MyIni.Write("HomePage", "http://www.google.com");
To create a file like this:

[MyProg]
DefaultVolume=100
HomePage=http://www.google.com
To read the values out of the INI file:

var DefaultVolume = MyIni.Read("DefaultVolume");
var HomePage = MyIni.Read("HomePage");
Optionally, you can set [Section]'s:

MyIni.Write("DefaultVolume", "100", "Audio");
MyIni.Write("HomePage", "http://www.google.com", "Web");
To create a file like this:

[Audio]
DefaultVolume=100

[Web]
HomePage=http://www.google.com

You can also check for the existence of a key like so:

if(!MyIni.KeyExists("DefaultVolume", "Audio"))
{
    MyIni.Write("DefaultVolume", "100", "Audio");
}
You can delete a key like so:

MyIni.DeleteKey("DefaultVolume", "Audio");
You can also delete a whole section (including all keys) like so:

MyIni.DeleteSection("Web");
*/

namespace Blobset_Tools
{
    /// <summary>
    /// IniFile Class
    /// </summary>
    /// <remarks>
    ///   Blobset Tools. Written by Wouldubeinta
    ///   Copyright (C) 2025 Wouldy Mods.
    ///   
    ///   This program is free software; you can redistribute it and/or
    ///   modify it under the terms of the GNU General Public License
    ///   as published by the Free Software Foundation; either version 3
    ///   of the License, or (at your option) any later version.
    ///   
    ///   This program is distributed in the hope that it will be useful,
    ///   but WITHOUT ANY WARRANTY; without even the implied warranty of
    ///   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    ///   GNU General Public License for more details.
    /// 
    ///   The author may be contacted at:
    ///   Discord: Wouldubeinta
    /// </remarks>
    /// <history>
    /// [Wouldubeinta]	   23/11/2025	Created
    /// </history>
    internal class IniFile
    {
        private readonly string Path;
        private readonly string EXE = Assembly.GetExecutingAssembly().GetName().Name;

        [DllImport("kernel32", CharSet = CharSet.Unicode)]
        private static extern long WritePrivateProfileString(string Section, string Key, string Value, string FilePath);

        [DllImport("kernel32", CharSet = CharSet.Unicode)]
        private static extern int GetPrivateProfileString(string Section, string Key, string Default, StringBuilder RetVal, int Size, string FilePath);

        public IniFile(string? IniPath = null)
        {
            Path = new FileInfo(IniPath ?? EXE + ".ini").FullName;
        }

        /// <summary>
        /// IniFile Read.
        /// </summary>
        /// <param name="Key">IniFile key.</param>
        /// <param name="Section">IniFile section.</param>
        /// <returns>Returns string value.</returns>
        /// <history>
        /// [Wouldubeinta]		23/11/2025	Created
        /// </history>
        public string Read(string Key, string? Section = null)
        {
            var RetVal = new StringBuilder(255);
            int error = GetPrivateProfileString(Section ?? EXE, Key, string.Empty, RetVal, 255, Path);
            return RetVal.ToString();
        }

        /// <summary>
        /// IniFile Write.
        /// </summary>
        /// <param name="Key">IniFile key.</param>
		/// <param name="Value">IniFile value.</param>
        /// <param name="Section">IniFile section.</param>
        /// <history>
        /// [Wouldubeinta]		23/11/2025	Created
        /// </history>
        public void Write(string Key, string Value, string? Section = null)
        {
            WritePrivateProfileString(Section ?? EXE, Key, Value, Path);
        }

        /// <summary>
        /// IniFile Delete Key.
        /// </summary>
        /// <param name="Key">IniFile key.</param>
        /// <param name="Section">IniFile section.</param>
        /// <history>
        /// [Wouldubeinta]		23/11/2025	Created
        /// </history>
        public void DeleteKey(string Key, string? Section = null)
        {
            Write(Key, null, Section ?? EXE);
        }

        /// <summary>
        /// IniFile Delete Section.
        /// </summary>
        /// <param name="Section">IniFile section.</param>
        /// <history>
        /// [Wouldubeinta]		23/11/2025	Created
        /// </history>
        public void DeleteSection(string? Section = null)
        {
            Write(null, null, Section ?? EXE);
        }

        /// <summary>
        /// IniFile Key Exists.
        /// </summary>
        /// <param name="Key">IniFile key.</param>
        /// <param name="Section">IniFile section.</param>
		/// <returns>Returns true if key exists.</returns>
        /// <history>
        /// [Wouldubeinta]		23/11/2025	Created
        /// </history>
        public bool KeyExists(string Key, string? Section = null)
        {
            return Read(Key, Section).Length > 0;
        }
    }
}