using System;
using System.IO;
using System.Threading.Tasks;
using System.Diagnostics;
using BlobsetIO;
using Gameloop.Vdf;
using Gameloop.Vdf.Linq;

namespace Blobset_Tools
{
    public static class SteamUpdateManager
    {
        private const string StateRequired = "6";
        private const string StateQueued = "1026";
        private const string StateFullyInstalled = "4";

        public static bool IsUpdatePending(string manifestPath)
        {
            if (!File.Exists(manifestPath)) return false;

            try
            {
                string vdfContent = File.ReadAllText(manifestPath);
                VProperty root = VdfConvert.Deserialize(vdfContent);
                VObject? appState = root.Value as VObject;

                if (appState != null && appState.ContainsKey("StateFlags"))
                {
                    string stateFlags = appState["StateFlags"]?.ToString() ?? "4";
                    return stateFlags == StateRequired || stateFlags == StateQueued;
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[Blobset Tools] Manifest parse failed: {ex.Message}");
            }
            return false;
        }

        public static void SafeDirectoryCopy(string sourceDir, string targetDir)
        {
            DirectoryInfo dir = new DirectoryInfo(sourceDir);
            if (!dir.Exists) throw new DirectoryNotFoundException($"Source directory not found: {sourceDir}");

            Directory.CreateDirectory(targetDir);

            foreach (FileInfo file in dir.GetFiles())
            {
                string targetFilePath = Path.Combine(targetDir, file.Name);
                file.CopyTo(targetFilePath, true);
            }

            foreach (DirectoryInfo subDir in dir.GetDirectories())
            {
                string targetSubDir = Path.Combine(targetDir, subDir.Name);
                SafeDirectoryCopy(subDir.FullName, targetSubDir);
            }
        }

        /// <summary>
        /// Forces Steam to update a specific app ID in the background without launching the game.
        /// </summary>
        public static bool ForceSteamAppUpdate(uint appId)
        {
            string steamLocation = UI.GetSteamLocation(); // Utilizes your Registry logic
            if (string.IsNullOrEmpty(steamLocation)) return false;

            string steamExePath = Path.Combine(steamLocation, "Steam.exe");
            if (!File.Exists(steamExePath)) return false;

            Process.Start(new ProcessStartInfo
            {
                FileName = steamExePath,
                // Tells Steam client to evaluate update requirements without initiating game binary execution
                Arguments = $"-applaunch {appId} +app_update {appId}",
                WindowStyle = ProcessWindowStyle.Hidden
            });

            return true;
        }

        public static async Task WaitForSteamUpdateAsync(string manifestPath, IProgress<string> progressReport)
        {
            // Give the local Steam service 5 seconds to wake up, re-verify cloud tables, and swap state flags
            await Task.Delay(5000);

            while (true)
            {
                try
                {
                    if (File.Exists(manifestPath))
                    {
                        string vdfContent = File.ReadAllText(manifestPath);
                        VProperty root = VdfConvert.Deserialize(vdfContent);
                        VObject? appState = root.Value as VObject;

                        if (appState != null)
                        {
                            string stateFlags = appState["StateFlags"]?.ToString() ?? "4";
                            if (stateFlags == StateFullyInstalled)
                            {
                                progressReport.Report("Update completed successfully by Steam!");
                                break;
                            }
                            progressReport.Report($"Steam update in progress... Status Code: {stateFlags}");
                        }
                    }
                }
                catch
                {
                    // Swallowing brief file-lock IO contentions while Steam writes down update progress chunks
                }

                await Task.Delay(3000);
            }
        }
    }
}
