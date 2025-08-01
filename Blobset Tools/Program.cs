namespace Blobset_Tools
{
    internal static class Program
    {
        private static Mutex _mutex = null;

        /// <summary>
        ///  The main entry point for the application.
        /// </summary>
        [STAThread]
        private static void Main()
        {
            const string appName = "Blobset Tools";
            _mutex = new Mutex(true, appName, out bool createdNew);

            if (!createdNew)
            {
                // Another instance is already running
                // Optionally, bring the existing instance to the foreground or display a message
                // Then, exit the current instance
                MessageBox.Show("Another instance of this application is already running.", "Application Already Running", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            // To customize application configuration such as set high DPI settings or default font,
            // see https://aka.ms/applicationconfiguration.
            ApplicationConfiguration.Initialize();
            Application.Run(new GameSelection());
        }
    }
}