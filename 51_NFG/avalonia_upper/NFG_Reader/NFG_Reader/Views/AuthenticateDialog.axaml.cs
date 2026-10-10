using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;

namespace NFG_Reader.Views
{
    /// <summary>
    /// Modal dialog that asks the user for the authentication password of a
    /// serial port before the connection is opened. The prompt reads
    /// "Enter the password for {port}".
    /// </summary>
    public partial class AuthenticateDialog : Window
    {
        public static readonly StyledProperty<string?> PortNameProperty =
            AvaloniaProperty.Register<AuthenticateDialog, string?>(nameof(PortName));

        public AuthenticateDialog()
        {
            InitializeComponent();

            ConfirmButton.Click += OnConfirm;
            CancelButton.Click += OnCancel;
            Opened += (_, _) => PasswordBox.Focus();
        }

        /// <summary>Port the password applies to (e.g. "COM3").</summary>
        public string? PortName
        {
            get => GetValue(PortNameProperty);
            set => SetValue(PortNameProperty, value);
        }

        /// <summary>Password typed by the user (only meaningful when confirmed).</summary>
        public string Password { get; private set; } = string.Empty;

        /// <summary>
        /// Convenience factory that wires the port name into the prompt text.
        /// </summary>
        public static AuthenticateDialog ForPort(string? portName)
        {
            var dialog = new AuthenticateDialog { PortName = portName };
            return dialog;
        }

        protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
        {
            base.OnPropertyChanged(change);

            if (change.Property == PortNameProperty)
            {
                var name = string.IsNullOrWhiteSpace(PortName) ? "the selected port" : PortName;
                PromptText.Text = $"Enter the password for {name}:";
            }
        }

        private void OnConfirm(object? sender, RoutedEventArgs e)
        {
            Password = PasswordBox.Text ?? string.Empty;
            Close(true);
        }

        private void OnCancel(object? sender, RoutedEventArgs e)
        {
            Password = string.Empty;
            Close(false);
        }
    }
}
