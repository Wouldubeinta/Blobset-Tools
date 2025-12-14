using BlobsetIO;

namespace Blobset_Tools
{
    /// <summary>
    /// Holds all the global variables.
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
    /// [Wouldubeinta]	   19/06/2025	Created
    /// </history>
    internal class Global
    {
        private static string? _version = string.Empty;
        private static string? _currentPath = string.Empty;
        private static int _fileIndex = 0;
        private static BlobsetFile? _blobsetHeaderData = null;
        private static List<Structs.FileIndexInfo>? _filelist = null;
        private static GameInfo? _gameInfo = null;
        private static Enums.Platforms _platforms = Enums.Platforms.Windows;
        private static bool _isBigendian = false;
        private static bool _isCreateBlobset = false;

        public static string? version
        {
            get { return _version; }
            set { _version = value; }
        }

        public static string? currentPath
        {
            get { return _currentPath; }
            set { _currentPath = value; }
        }

        public static int fileIndex
        {
            get { return _fileIndex; }
            set { _fileIndex = value; }
        }

        public static BlobsetFile? blobsetHeaderData
        {
            get { return _blobsetHeaderData; }
            set { _blobsetHeaderData = value; }
        }

        public static List<Structs.FileIndexInfo>? filelist
        {
            get { return _filelist; }
            set { _filelist = value; }
        }

        public static GameInfo? gameInfo
        {
            get { return _gameInfo; }
            set { _gameInfo = value; }
        }

        public static Enums.Platforms platforms
        {
            get { return _platforms; }
            set { _platforms = value; }
        }

        public static bool isBigendian
        {
            get { return _isBigendian; }
            set { _isBigendian = value; }
        }

        public static bool isCreateBlobset
        {
            get { return _isCreateBlobset; }
            set { _isCreateBlobset = value; }
        }
    }
}
