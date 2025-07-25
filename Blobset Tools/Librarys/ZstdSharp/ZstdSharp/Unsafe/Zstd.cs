namespace ZstdSharp.Unsafe
{
    public static unsafe partial class Methods
    {
        private static readonly ZSTD_customMem ZSTD_defaultCMem = new(customAlloc: null, customFree: null, opaque: null);
    }
}