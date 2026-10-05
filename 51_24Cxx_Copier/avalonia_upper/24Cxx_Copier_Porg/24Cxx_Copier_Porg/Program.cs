using Avalonia;
using System;


namespace _24Cxx_Copier_Porg
{
    internal sealed class Program
    {
        
        // Initialization code. Don't use any Avalonia, third-party APIs or any
        // SynchronizationContext-reliant code before AppMain is called: things aren't initialized
        // yet and stuff might break.
        [STAThread]
        // public static void Main(string[] args) => BuildAvaloniaApp()
        //     .StartWithClassicDesktopLifetime(args);
        public static void Main(string[] args)
        {

            try
            {
                System.Text.Encoding.RegisterProvider(System.Text.CodePagesEncodingProvider.Instance);


            }
            catch (Exception e)
            {
                Console.WriteLine(e.Message);
                throw;
            }

            BuildAvaloniaApp()
                .StartWithClassicDesktopLifetime(args);
   
        }


        


        // Avalonia configuration, don't remove; also used by visual designer.
        public static AppBuilder BuildAvaloniaApp()
            => AppBuilder.Configure<App>()
                .UsePlatformDetect()
                .WithInterFont()
                .LogToTrace();
    }
}
