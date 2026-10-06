using System.Windows.Input;
using Avalonia;
using Avalonia.Controls;

namespace NFG_Reader.Views.Components
{
    public partial class ActionBar : UserControl
    {
        public static readonly StyledProperty<string> ActionTextProperty =
            AvaloniaProperty.Register<ActionBar, string>(nameof(ActionText), "Read");

        public static readonly StyledProperty<ICommand?> ActionCommandProperty =
            AvaloniaProperty.Register<ActionBar, ICommand?>(nameof(ActionCommand));

        public static readonly StyledProperty<string> ProgressTextProperty =
            AvaloniaProperty.Register<ActionBar, string>(nameof(ProgressText), "Ready");

        public static readonly StyledProperty<double> ProgressValueProperty =
            AvaloniaProperty.Register<ActionBar, double>(nameof(ProgressValue));

        public static readonly StyledProperty<string> ProgressPercentProperty =
            AvaloniaProperty.Register<ActionBar, string>(nameof(ProgressPercent), "0%");

        public ActionBar()
        {
            InitializeComponent();
        }

        public string ActionText
        {
            get => GetValue(ActionTextProperty);
            set => SetValue(ActionTextProperty, value);
        }

        public ICommand? ActionCommand
        {
            get => GetValue(ActionCommandProperty);
            set => SetValue(ActionCommandProperty, value);
        }

        public string ProgressText
        {
            get => GetValue(ProgressTextProperty);
            set => SetValue(ProgressTextProperty, value);
        }

        public double ProgressValue
        {
            get => GetValue(ProgressValueProperty);
            set => SetValue(ProgressValueProperty, value);
        }

        public string ProgressPercent
        {
            get => GetValue(ProgressPercentProperty);
            set => SetValue(ProgressPercentProperty, value);
        }
    }
}

