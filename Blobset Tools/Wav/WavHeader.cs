namespace Blobset_Tools.Wav
{
    public class WavHeader
    {
        #region Fields
        private const int fmtChunkSize = 18;
        private const ushort formatType = 1;
        private const int bitsPerSample = 16;
        private readonly short bytesPerSample = Convert.ToInt16(bitsPerSample / 8);

        private int _magic = 1179011410; // RIFF
        private int _headerSize = 0;
        private int _fileType = 1163280727; // WAVE
        private int _fmtString = 544501094; // fmt 
        private int _dataString = 1635017060; // data
        private int _pcmDataSize = 0;

        private readonly int sampleRate = 0;
        private readonly int numChannels = 0;
        #endregion

        public WavHeader(int _sampleRate, int _numChannels)
        {
            sampleRate = _sampleRate;
            numChannels = _numChannels;
        }

        #region Properties
        public int magic
        {
            get { return _magic; }
            set { _magic = value; }
        }

        public int headerSize
        {
            get { return _headerSize; }
            set { _headerSize = value; }
        }

        public int fileType
        {
            get { return _fileType; }
            set { _fileType = value; }
        }

        public int fmtString
        {
            get { return _fmtString; }
            set { _fmtString = value; }
        }

        public int dataString
        {
            get { return _dataString; }
            set { _dataString = value; }
        }

        public int pcmDataSize
        {
            get { return _pcmDataSize; }
            set { _pcmDataSize = value; }
        }
        #endregion

        #region "Serialize"
        /// <summary>
        /// Serialize Wav Header
        /// </summary>
        /// <param name="ms">Wav header memory stream.</param>
        public void Serialize(MemoryStream ms)
        {
            ms.Write(BitConverter.GetBytes(magic), 0, 4);
            ms.Write(BitConverter.GetBytes(headerSize), 0, 4);
            ms.Write(BitConverter.GetBytes(fileType), 0, 4);
            ms.Write(BitConverter.GetBytes(fmtString), 0, 4);
            ms.Write(BitConverter.GetBytes(fmtChunkSize), 0, 4);
            ms.Write(BitConverter.GetBytes(formatType), 0, 2);
            ms.Write(BitConverter.GetBytes(numChannels), 0, 2);
            ms.Write(BitConverter.GetBytes(sampleRate), 0, 4);
            int bytesPerSecond = Convert.ToInt32(sampleRate * numChannels * bytesPerSample);
            ms.Write(BitConverter.GetBytes(bytesPerSecond), 0, 4);
            ms.Write(BitConverter.GetBytes(bytesPerSample * numChannels), 0, 2);
            ms.Write(BitConverter.GetBytes(bitsPerSample), 0, 4);
            ms.Write(BitConverter.GetBytes(dataString), 0, 4);
            ms.Write(BitConverter.GetBytes(pcmDataSize), 0, 4);
            ms.Flush();
        }
        #endregion
    }
}
