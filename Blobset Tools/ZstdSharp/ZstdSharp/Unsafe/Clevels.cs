namespace ZstdSharp.Unsafe
{
    public static unsafe partial class Methods
    {
        private static readonly ZSTD_compressionParameters[][] ZSTD_defaultCParameters = new ZSTD_compressionParameters[4][]
        {
            new ZSTD_compressionParameters[23]
            {
                new(windowLog: 19, chainLog: 12, hashLog: 13, searchLog: 1, minMatch: 6, targetLength: 1, strategy: ZSTD_strategy.ZSTD_fast),
                new(windowLog: 19, chainLog: 13, hashLog: 14, searchLog: 1, minMatch: 7, targetLength: 0, strategy: ZSTD_strategy.ZSTD_fast),
                new(windowLog: 20, chainLog: 15, hashLog: 16, searchLog: 1, minMatch: 6, targetLength: 0, strategy: ZSTD_strategy.ZSTD_fast),
                new(windowLog: 21, chainLog: 16, hashLog: 17, searchLog: 1, minMatch: 5, targetLength: 0, strategy: ZSTD_strategy.ZSTD_dfast),
                new(windowLog: 21, chainLog: 18, hashLog: 18, searchLog: 1, minMatch: 5, targetLength: 0, strategy: ZSTD_strategy.ZSTD_dfast),
                new(windowLog: 21, chainLog: 18, hashLog: 19, searchLog: 3, minMatch: 5, targetLength: 2, strategy: ZSTD_strategy.ZSTD_greedy),
                new(windowLog: 21, chainLog: 18, hashLog: 19, searchLog: 3, minMatch: 5, targetLength: 4, strategy: ZSTD_strategy.ZSTD_lazy),
                new(windowLog: 21, chainLog: 19, hashLog: 20, searchLog: 4, minMatch: 5, targetLength: 8, strategy: ZSTD_strategy.ZSTD_lazy),
                new(windowLog: 21, chainLog: 19, hashLog: 20, searchLog: 4, minMatch: 5, targetLength: 16, strategy: ZSTD_strategy.ZSTD_lazy2),
                new(windowLog: 22, chainLog: 20, hashLog: 21, searchLog: 4, minMatch: 5, targetLength: 16, strategy: ZSTD_strategy.ZSTD_lazy2),
                new(windowLog: 22, chainLog: 21, hashLog: 22, searchLog: 5, minMatch: 5, targetLength: 16, strategy: ZSTD_strategy.ZSTD_lazy2),
                new(windowLog: 22, chainLog: 21, hashLog: 22, searchLog: 6, minMatch: 5, targetLength: 16, strategy: ZSTD_strategy.ZSTD_lazy2),
                new(windowLog: 22, chainLog: 22, hashLog: 23, searchLog: 6, minMatch: 5, targetLength: 32, strategy: ZSTD_strategy.ZSTD_lazy2),
                new(windowLog: 22, chainLog: 22, hashLog: 22, searchLog: 4, minMatch: 5, targetLength: 32, strategy: ZSTD_strategy.ZSTD_btlazy2),
                new(windowLog: 22, chainLog: 22, hashLog: 23, searchLog: 5, minMatch: 5, targetLength: 32, strategy: ZSTD_strategy.ZSTD_btlazy2),
                new(windowLog: 22, chainLog: 23, hashLog: 23, searchLog: 6, minMatch: 5, targetLength: 32, strategy: ZSTD_strategy.ZSTD_btlazy2),
                new(windowLog: 22, chainLog: 22, hashLog: 22, searchLog: 5, minMatch: 5, targetLength: 48, strategy: ZSTD_strategy.ZSTD_btopt),
                new(windowLog: 23, chainLog: 23, hashLog: 22, searchLog: 5, minMatch: 4, targetLength: 64, strategy: ZSTD_strategy.ZSTD_btopt),
                new(windowLog: 23, chainLog: 23, hashLog: 22, searchLog: 6, minMatch: 3, targetLength: 64, strategy: ZSTD_strategy.ZSTD_btultra),
                new(windowLog: 23, chainLog: 24, hashLog: 22, searchLog: 7, minMatch: 3, targetLength: 256, strategy: ZSTD_strategy.ZSTD_btultra2),
                new(windowLog: 25, chainLog: 25, hashLog: 23, searchLog: 7, minMatch: 3, targetLength: 256, strategy: ZSTD_strategy.ZSTD_btultra2),
                new(windowLog: 26, chainLog: 26, hashLog: 24, searchLog: 7, minMatch: 3, targetLength: 512, strategy: ZSTD_strategy.ZSTD_btultra2),
                new(windowLog: 27, chainLog: 27, hashLog: 25, searchLog: 9, minMatch: 3, targetLength: 999, strategy: ZSTD_strategy.ZSTD_btultra2)
            },
            new ZSTD_compressionParameters[23]
            {
                new(windowLog: 18, chainLog: 12, hashLog: 13, searchLog: 1, minMatch: 5, targetLength: 1, strategy: ZSTD_strategy.ZSTD_fast),
                new(windowLog: 18, chainLog: 13, hashLog: 14, searchLog: 1, minMatch: 6, targetLength: 0, strategy: ZSTD_strategy.ZSTD_fast),
                new(windowLog: 18, chainLog: 14, hashLog: 14, searchLog: 1, minMatch: 5, targetLength: 0, strategy: ZSTD_strategy.ZSTD_dfast),
                new(windowLog: 18, chainLog: 16, hashLog: 16, searchLog: 1, minMatch: 4, targetLength: 0, strategy: ZSTD_strategy.ZSTD_dfast),
                new(windowLog: 18, chainLog: 16, hashLog: 17, searchLog: 3, minMatch: 5, targetLength: 2, strategy: ZSTD_strategy.ZSTD_greedy),
                new(windowLog: 18, chainLog: 17, hashLog: 18, searchLog: 5, minMatch: 5, targetLength: 2, strategy: ZSTD_strategy.ZSTD_greedy),
                new(windowLog: 18, chainLog: 18, hashLog: 19, searchLog: 3, minMatch: 5, targetLength: 4, strategy: ZSTD_strategy.ZSTD_lazy),
                new(windowLog: 18, chainLog: 18, hashLog: 19, searchLog: 4, minMatch: 4, targetLength: 4, strategy: ZSTD_strategy.ZSTD_lazy),
                new(windowLog: 18, chainLog: 18, hashLog: 19, searchLog: 4, minMatch: 4, targetLength: 8, strategy: ZSTD_strategy.ZSTD_lazy2),
                new(windowLog: 18, chainLog: 18, hashLog: 19, searchLog: 5, minMatch: 4, targetLength: 8, strategy: ZSTD_strategy.ZSTD_lazy2),
                new(windowLog: 18, chainLog: 18, hashLog: 19, searchLog: 6, minMatch: 4, targetLength: 8, strategy: ZSTD_strategy.ZSTD_lazy2),
                new(windowLog: 18, chainLog: 18, hashLog: 19, searchLog: 5, minMatch: 4, targetLength: 12, strategy: ZSTD_strategy.ZSTD_btlazy2),
                new(windowLog: 18, chainLog: 19, hashLog: 19, searchLog: 7, minMatch: 4, targetLength: 12, strategy: ZSTD_strategy.ZSTD_btlazy2),
                new(windowLog: 18, chainLog: 18, hashLog: 19, searchLog: 4, minMatch: 4, targetLength: 16, strategy: ZSTD_strategy.ZSTD_btopt),
                new(windowLog: 18, chainLog: 18, hashLog: 19, searchLog: 4, minMatch: 3, targetLength: 32, strategy: ZSTD_strategy.ZSTD_btopt),
                new(windowLog: 18, chainLog: 18, hashLog: 19, searchLog: 6, minMatch: 3, targetLength: 128, strategy: ZSTD_strategy.ZSTD_btopt),
                new(windowLog: 18, chainLog: 19, hashLog: 19, searchLog: 6, minMatch: 3, targetLength: 128, strategy: ZSTD_strategy.ZSTD_btultra),
                new(windowLog: 18, chainLog: 19, hashLog: 19, searchLog: 8, minMatch: 3, targetLength: 256, strategy: ZSTD_strategy.ZSTD_btultra),
                new(windowLog: 18, chainLog: 19, hashLog: 19, searchLog: 6, minMatch: 3, targetLength: 128, strategy: ZSTD_strategy.ZSTD_btultra2),
                new(windowLog: 18, chainLog: 19, hashLog: 19, searchLog: 8, minMatch: 3, targetLength: 256, strategy: ZSTD_strategy.ZSTD_btultra2),
                new(windowLog: 18, chainLog: 19, hashLog: 19, searchLog: 10, minMatch: 3, targetLength: 512, strategy: ZSTD_strategy.ZSTD_btultra2),
                new(windowLog: 18, chainLog: 19, hashLog: 19, searchLog: 12, minMatch: 3, targetLength: 512, strategy: ZSTD_strategy.ZSTD_btultra2),
                new(windowLog: 18, chainLog: 19, hashLog: 19, searchLog: 13, minMatch: 3, targetLength: 999, strategy: ZSTD_strategy.ZSTD_btultra2)
            },
            new ZSTD_compressionParameters[23]
            {
                new(windowLog: 17, chainLog: 12, hashLog: 12, searchLog: 1, minMatch: 5, targetLength: 1, strategy: ZSTD_strategy.ZSTD_fast),
                new(windowLog: 17, chainLog: 12, hashLog: 13, searchLog: 1, minMatch: 6, targetLength: 0, strategy: ZSTD_strategy.ZSTD_fast),
                new(windowLog: 17, chainLog: 13, hashLog: 15, searchLog: 1, minMatch: 5, targetLength: 0, strategy: ZSTD_strategy.ZSTD_fast),
                new(windowLog: 17, chainLog: 15, hashLog: 16, searchLog: 2, minMatch: 5, targetLength: 0, strategy: ZSTD_strategy.ZSTD_dfast),
                new(windowLog: 17, chainLog: 17, hashLog: 17, searchLog: 2, minMatch: 4, targetLength: 0, strategy: ZSTD_strategy.ZSTD_dfast),
                new(windowLog: 17, chainLog: 16, hashLog: 17, searchLog: 3, minMatch: 4, targetLength: 2, strategy: ZSTD_strategy.ZSTD_greedy),
                new(windowLog: 17, chainLog: 16, hashLog: 17, searchLog: 3, minMatch: 4, targetLength: 4, strategy: ZSTD_strategy.ZSTD_lazy),
                new(windowLog: 17, chainLog: 16, hashLog: 17, searchLog: 3, minMatch: 4, targetLength: 8, strategy: ZSTD_strategy.ZSTD_lazy2),
                new(windowLog: 17, chainLog: 16, hashLog: 17, searchLog: 4, minMatch: 4, targetLength: 8, strategy: ZSTD_strategy.ZSTD_lazy2),
                new(windowLog: 17, chainLog: 16, hashLog: 17, searchLog: 5, minMatch: 4, targetLength: 8, strategy: ZSTD_strategy.ZSTD_lazy2),
                new(windowLog: 17, chainLog: 16, hashLog: 17, searchLog: 6, minMatch: 4, targetLength: 8, strategy: ZSTD_strategy.ZSTD_lazy2),
                new(windowLog: 17, chainLog: 17, hashLog: 17, searchLog: 5, minMatch: 4, targetLength: 8, strategy: ZSTD_strategy.ZSTD_btlazy2),
                new(windowLog: 17, chainLog: 18, hashLog: 17, searchLog: 7, minMatch: 4, targetLength: 12, strategy: ZSTD_strategy.ZSTD_btlazy2),
                new(windowLog: 17, chainLog: 18, hashLog: 17, searchLog: 3, minMatch: 4, targetLength: 12, strategy: ZSTD_strategy.ZSTD_btopt),
                new(windowLog: 17, chainLog: 18, hashLog: 17, searchLog: 4, minMatch: 3, targetLength: 32, strategy: ZSTD_strategy.ZSTD_btopt),
                new(windowLog: 17, chainLog: 18, hashLog: 17, searchLog: 6, minMatch: 3, targetLength: 256, strategy: ZSTD_strategy.ZSTD_btopt),
                new(windowLog: 17, chainLog: 18, hashLog: 17, searchLog: 6, minMatch: 3, targetLength: 128, strategy: ZSTD_strategy.ZSTD_btultra),
                new(windowLog: 17, chainLog: 18, hashLog: 17, searchLog: 8, minMatch: 3, targetLength: 256, strategy: ZSTD_strategy.ZSTD_btultra),
                new(windowLog: 17, chainLog: 18, hashLog: 17, searchLog: 10, minMatch: 3, targetLength: 512, strategy: ZSTD_strategy.ZSTD_btultra),
                new(windowLog: 17, chainLog: 18, hashLog: 17, searchLog: 5, minMatch: 3, targetLength: 256, strategy: ZSTD_strategy.ZSTD_btultra2),
                new(windowLog: 17, chainLog: 18, hashLog: 17, searchLog: 7, minMatch: 3, targetLength: 512, strategy: ZSTD_strategy.ZSTD_btultra2),
                new(windowLog: 17, chainLog: 18, hashLog: 17, searchLog: 9, minMatch: 3, targetLength: 512, strategy: ZSTD_strategy.ZSTD_btultra2),
                new(windowLog: 17, chainLog: 18, hashLog: 17, searchLog: 11, minMatch: 3, targetLength: 999, strategy: ZSTD_strategy.ZSTD_btultra2)
            },
            new ZSTD_compressionParameters[23]
            {
                new(windowLog: 14, chainLog: 12, hashLog: 13, searchLog: 1, minMatch: 5, targetLength: 1, strategy: ZSTD_strategy.ZSTD_fast),
                new(windowLog: 14, chainLog: 14, hashLog: 15, searchLog: 1, minMatch: 5, targetLength: 0, strategy: ZSTD_strategy.ZSTD_fast),
                new(windowLog: 14, chainLog: 14, hashLog: 15, searchLog: 1, minMatch: 4, targetLength: 0, strategy: ZSTD_strategy.ZSTD_fast),
                new(windowLog: 14, chainLog: 14, hashLog: 15, searchLog: 2, minMatch: 4, targetLength: 0, strategy: ZSTD_strategy.ZSTD_dfast),
                new(windowLog: 14, chainLog: 14, hashLog: 14, searchLog: 4, minMatch: 4, targetLength: 2, strategy: ZSTD_strategy.ZSTD_greedy),
                new(windowLog: 14, chainLog: 14, hashLog: 14, searchLog: 3, minMatch: 4, targetLength: 4, strategy: ZSTD_strategy.ZSTD_lazy),
                new(windowLog: 14, chainLog: 14, hashLog: 14, searchLog: 4, minMatch: 4, targetLength: 8, strategy: ZSTD_strategy.ZSTD_lazy2),
                new(windowLog: 14, chainLog: 14, hashLog: 14, searchLog: 6, minMatch: 4, targetLength: 8, strategy: ZSTD_strategy.ZSTD_lazy2),
                new(windowLog: 14, chainLog: 14, hashLog: 14, searchLog: 8, minMatch: 4, targetLength: 8, strategy: ZSTD_strategy.ZSTD_lazy2),
                new(windowLog: 14, chainLog: 15, hashLog: 14, searchLog: 5, minMatch: 4, targetLength: 8, strategy: ZSTD_strategy.ZSTD_btlazy2),
                new(windowLog: 14, chainLog: 15, hashLog: 14, searchLog: 9, minMatch: 4, targetLength: 8, strategy: ZSTD_strategy.ZSTD_btlazy2),
                new(windowLog: 14, chainLog: 15, hashLog: 14, searchLog: 3, minMatch: 4, targetLength: 12, strategy: ZSTD_strategy.ZSTD_btopt),
                new(windowLog: 14, chainLog: 15, hashLog: 14, searchLog: 4, minMatch: 3, targetLength: 24, strategy: ZSTD_strategy.ZSTD_btopt),
                new(windowLog: 14, chainLog: 15, hashLog: 14, searchLog: 5, minMatch: 3, targetLength: 32, strategy: ZSTD_strategy.ZSTD_btultra),
                new(windowLog: 14, chainLog: 15, hashLog: 15, searchLog: 6, minMatch: 3, targetLength: 64, strategy: ZSTD_strategy.ZSTD_btultra),
                new(windowLog: 14, chainLog: 15, hashLog: 15, searchLog: 7, minMatch: 3, targetLength: 256, strategy: ZSTD_strategy.ZSTD_btultra),
                new(windowLog: 14, chainLog: 15, hashLog: 15, searchLog: 5, minMatch: 3, targetLength: 48, strategy: ZSTD_strategy.ZSTD_btultra2),
                new(windowLog: 14, chainLog: 15, hashLog: 15, searchLog: 6, minMatch: 3, targetLength: 128, strategy: ZSTD_strategy.ZSTD_btultra2),
                new(windowLog: 14, chainLog: 15, hashLog: 15, searchLog: 7, minMatch: 3, targetLength: 256, strategy: ZSTD_strategy.ZSTD_btultra2),
                new(windowLog: 14, chainLog: 15, hashLog: 15, searchLog: 8, minMatch: 3, targetLength: 256, strategy: ZSTD_strategy.ZSTD_btultra2),
                new(windowLog: 14, chainLog: 15, hashLog: 15, searchLog: 8, minMatch: 3, targetLength: 512, strategy: ZSTD_strategy.ZSTD_btultra2),
                new(windowLog: 14, chainLog: 15, hashLog: 15, searchLog: 9, minMatch: 3, targetLength: 512, strategy: ZSTD_strategy.ZSTD_btultra2),
                new(windowLog: 14, chainLog: 15, hashLog: 15, searchLog: 10, minMatch: 3, targetLength: 999, strategy: ZSTD_strategy.ZSTD_btultra2)
            }
        };
    }
}