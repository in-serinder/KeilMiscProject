namespace PDL_upper.Models
{
    /// <summary>命令行参数：--tcp 127.0.0.1:5555 / --selftest</summary>
    public static class AppArgs
    {
        public static string Host { get; private set; } = "127.0.0.1";
        public static int Port { get; private set; } = 5555;
        public static bool SelfTest { get; private set; }
        public static bool Probe { get; private set; }
        public static bool AutoConnect { get; private set; }
        public static bool DemoDialogs { get; private set; }
        public static bool DemoPdl { get; private set; }

        public static void Parse(string[] args)
        {
            for (int i = 0; i < args.Length; i++)
            {
                var a = args[i];
                if (a == "--selftest") SelfTest = true;
                else if (a == "--probe") Probe = true;
                else if (a == "--auto") AutoConnect = true;
                else if (a == "--demo-dialogs") DemoDialogs = true;
                else if (a == "--demo-pdl") DemoPdl = true;
                else if (a == "--tcp" && i + 1 < args.Length)
                {
                    var t = args[++i];
                    int c = t.LastIndexOf(':');
                    if (c > 0)
                    {
                        Host = t[..c];
                        if (int.TryParse(t[(c + 1)..], out var p)) Port = p;
                    }
                    else Host = t;
                }
            }
        }
    }
}
