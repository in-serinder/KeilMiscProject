using System.Collections;
using System.Windows.Input;
using Avalonia;
using Avalonia.Controls;

namespace NFG_Reader.Views.Components
{
    public partial class TextEditorPanel : UserControl
    {
        public static readonly StyledProperty<string> TextProperty =
            AvaloniaProperty.Register<TextEditorPanel, string>(nameof(Text), string.Empty,
                defaultBindingMode: Avalonia.Data.BindingMode.TwoWay);

        public static readonly StyledProperty<bool> IsReadOnlyProperty =
            AvaloniaProperty.Register<TextEditorPanel, bool>(nameof(IsReadOnly), true);

        public static readonly StyledProperty<bool> ShowEncodingBarProperty =
            AvaloniaProperty.Register<TextEditorPanel, bool>(nameof(ShowEncodingBar), true);

        public static readonly StyledProperty<bool> ShowDownloadProperty =
            AvaloniaProperty.Register<TextEditorPanel, bool>(nameof(ShowDownload), true);

        public static readonly StyledProperty<IEnumerable?> EncodingsProperty =
            AvaloniaProperty.Register<TextEditorPanel, IEnumerable?>(nameof(Encodings));
        public static readonly StyledProperty<string> SelectedEncodingProperty =
            AvaloniaProperty.Register<TextEditorPanel, string>(nameof(SelectedEncoding), "UTF-8",
                defaultBindingMode: Avalonia.Data.BindingMode.TwoWay);

        public static readonly StyledProperty<string> CountTextProperty =
            AvaloniaProperty.Register<TextEditorPanel, string>(nameof(CountText), "0 chars");

        public static readonly StyledProperty<string> EncodingCaptionProperty =
            AvaloniaProperty.Register<TextEditorPanel, string>(nameof(EncodingCaption), "UTF-8");

        public static readonly StyledProperty<ICommand?> SaveAsTxtCommandProperty =
            AvaloniaProperty.Register<TextEditorPanel, ICommand?>(nameof(SaveAsTxtCommand));

        public TextEditorPanel()
        {
            InitializeComponent();
        }

        public string Text
        {
            get => GetValue(TextProperty);
            set => SetValue(TextProperty, value);
        }

        public bool IsReadOnly
        {
            get => GetValue(IsReadOnlyProperty);
            set => SetValue(IsReadOnlyProperty, value);
        }

        public bool ShowEncodingBar
        {
            get => GetValue(ShowEncodingBarProperty);
            set => SetValue(ShowEncodingBarProperty, value);
        }

        public bool ShowDownload
        {
            get => GetValue(ShowDownloadProperty);
            set => SetValue(ShowDownloadProperty, value);
        }

        public IEnumerable? Encodings
        {
            get => GetValue(EncodingsProperty);
            set => SetValue(EncodingsProperty, value);
        }

        public string SelectedEncoding
        {
            get => GetValue(SelectedEncodingProperty);
            set => SetValue(SelectedEncodingProperty, value);
        }

        public string CountText
        {
            get => GetValue(CountTextProperty);
            set => SetValue(CountTextProperty, value);
        }

        public string EncodingCaption
        {
            get => GetValue(EncodingCaptionProperty);
            set => SetValue(EncodingCaptionProperty, value);
        }

        public ICommand? SaveAsTxtCommand
        {
            get => GetValue(SaveAsTxtCommandProperty);
            set => SetValue(SaveAsTxtCommandProperty, value);
        }
    }
}


