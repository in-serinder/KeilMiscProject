using Avalonia.Controls;

namespace Capsa.Views.Illustrations
{
    /// <summary>
    /// 空状态矢量插画（对应 Assets/svg/noting.svg）。为避免额外引入
    /// Avalonia.Svg 依赖，这里使用等价的矢量 Path 描述，颜色跟随主题令牌。
    /// </summary>
    public partial class EmptyCardIllustration : UserControl
    {
        public EmptyCardIllustration()
        {
            InitializeComponent();
        }
    }
}
