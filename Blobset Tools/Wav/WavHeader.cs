using System.Runtime.InteropServices;

namespace Blobset_Tools
{
    public class WavHeader
    {
        #region Fields
        private int _magic = 1179011410; // RIFF
        private int _headerSize = 0;
        private int _fileType = 1163280727; // WAVE
        private int _fmtString = 544501094; // fmt
        private int _fmtChunkSize = 18;
        private ushort _formatType = 1;
        private int _numChannels = 2;
        private int _sampleRate = 0;
        private int _bytesPerSecond = 0;
        private int _bytesPerSample = 0;
        private int _bitsPerSample = 16;
        private int _dataString = 1635017060; // data
        private int _pcmDataSize = 0;
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

        public int fmtChunkSize
        {
            get { return _fmtChunkSize; }
            set { _fmtChunkSize = value; }
        }

        public ushort formatType
        {
            get { return _formatType; }
            set { _formatType = value; }
        }

        public int numChannels
        {
            get { return _numChannels; }
            set { _numChannels = value; }
        }

        public int sampleRate
        {
            get { return _sampleRate; }
            set { _sampleRate = value; }
        }

        public int bytesPerSecond
        {
            get { return _bytesPerSecond; }
            set { _bytesPerSecond = value; }
        }

        public int bytesPerSample
        {
            get { return _bytesPerSample; }
            set { _bytesPerSample = value; }
        }

        public int bitsPerSample
        {
            get { return _bitsPerSample; }
            set { _bitsPerSample = value; }
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

        #region "Deserialize"
        /// <summary>
        /// Deserialize Wav Header
        /// </summary>
        /// <param name="ms">Wav header memory stream.</param>
        public void Deserialize(MemoryStream ms)
        {
            byte[] magic_buffer = new byte[4];
            ms.Read(magic_buffer, 0, 4);
            magic = MemoryMarshal.Read<int>(magic_buffer);

            byte[] headerSize_buffer = new byte[4];
            ms.Read(headerSize_buffer, 0, 4);
            headerSize = MemoryMarshal.Read<int>(headerSize_buffer);

            byte[] fileType_buffer = new byte[4];
            ms.Read(fileType_buffer, 0, 4);
            fileType = MemoryMarshal.Read<int>(fileType_buffer);

            byte[] fmtString_buffer = new byte[4];
            ms.Read(fmtString_buffer, 0, 4);
            fmtString = MemoryMarshal.Read<int>(fmtString_buffer);

            byte[] fmtChunkSize_buffer = new byte[4];
            ms.Read(fmtChunkSize_buffer, 0, 4);
            fmtChunkSize = MemoryMarshal.Read<int>(fmtChunkSize_buffer);

            byte[] formatType_buffer = new byte[2];
            ms.Read(formatType_buffer, 0, 2);
            fmtChunkSize = MemoryMarshal.Read<ushort>(formatType_buffer);

            byte[] numChannels_buffer = new byte[2];
            ms.Read(numChannels_buffer, 0, 2);
            numChannels = MemoryMarshal.Read<ushort>(numChannels_buffer);

            byte[] sampleRate_buffer = new byte[4];
            ms.Read(sampleRate_buffer, 0, 4);
            sampleRate = MemoryMarshal.Read<int>(sampleRate_buffer);

            byte[] bytesPerSecond_buffer = new byte[4];
            ms.Read(bytesPerSecond_buffer, 0, 4);
            bytesPerSecond = MemoryMarshal.Read<int>(bytesPerSecond_buffer);

            byte[] bytesPerSample_buffer = new byte[2];
            ms.Read(bytesPerSample_buffer, 0, 2);
            bytesPerSample = MemoryMarshal.Read<ushort>(bytesPerSample_buffer);

            byte[] bitsPerSample_buffer = new byte[4];
            ms.Read(bitsPerSample_buffer, 0, 4);
            bitsPerSample = MemoryMarshal.Read<int>(bitsPerSample_buffer);

            byte[] dataString_buffer = new byte[4];
            ms.Read(dataString_buffer, 0, 4);
            dataString = MemoryMarshal.Read<int>(dataString_buffer);

            byte[] pcmDataSize_buffer = new byte[4];
            ms.Read(pcmDataSize_buffer, 0, 4);
            pcmDataSize = MemoryMarshal.Read<int>(pcmDataSize_buffer);
        }
        #endregion

        #region "Serialize"
        /// <summary>
        /// Serialize Wav Header
        /// </summary>
        /// <param name="ms">Wav header memory stream.</param>
        public void Serialize(MemoryStream ms)
        {
            bytesPerSample = Convert.ToInt16(bitsPerSample / 8);

            ms.Write(BitConverter.GetBytes(magic), 0, 4);
            ms.Write(BitConverter.GetBytes(headerSize), 0, 4);
            ms.Write(BitConverter.GetBytes(fileType), 0, 4);
            ms.Write(BitConverter.GetBytes(fmtString), 0, 4);
            ms.Write(BitConverter.GetBytes(fmtChunkSize), 0, 4);
            ms.Write(BitConverter.GetBytes(formatType), 0, 2);
            ms.Write(BitConverter.GetBytes(numChannels), 0, 2);
            ms.Write(BitConverter.GetBytes(sampleRate), 0, 4);
            bytesPerSecond = Convert.ToInt32(sampleRate * numChannels * bytesPerSample);
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
