# 应用图标

`PWMCtrol` 的图标源文件与产物。所有位图都由 `gen_icons.py` 生成，**不要手工改 PNG/ICO**——
改设计就改脚本里的常量，重跑一次。

```
docs/icons/
├─ gen_icons.py      生成器（同一份几何 → SVG + PNG + ICO）
├─ app.svg           矢量母版（512×512）
├─ app.ico           Windows 图标（16/24/32/48/64/128/256 七档）
├─ app-*.png         各尺寸散图 16~512
├─ preview-bg.png    深/浅任务栏下的实际观感
└─ alternatives.png  落选的两个方向（留档）
```

产物已复制到工程：`PWMCtrol/PWMCtrol/Assets/app.ico`，
由 `PWMCtrol.csproj` 的 `<ApplicationIcon>` 与 `MainWindow.axaml` 的 `Icon=` 引用。

## 设计说明

**一句话**：深色瓦片上一根橙红方波，两个完整周期、50% 占空比。

| 决策 | 取值 | 理由 |
| --- | --- | --- |
| 主体 | 方波（PWM 的通用图形） | 产品核心功能就是这个；比"数码管""旋钮"更能一眼说明是做什么的 |
| 周期数 | **2** | 3 周期在 32px 下退化成"梳齿"，2 周期每个脉冲还有 5px，能读出来 |
| 占空比 | 50% | 高电平与低电平等长，图形重心最稳；递变占空比（35/50/65）在图标尺寸下像出错 |
| 线宽 | 34/512 ≈ 6.6% | 再粗（46）会像"挖槽的块"，再细（28）在 16px 下断线 |
| 主体色 | `#FF5533` | 与界面数码管的 LED 红一致，直接取自 `SegmentDisplay.OnColor` |
| 底色 | `#171C23` + `#2E3743` 描边 | 面板色；描边让它在深色任务栏上仍有边界 |
| 圆角 | 112/512 ≈ 22% | Windows 11 应用图标量级，边缘不刺眼 |

**小尺寸光学补偿**：16/24px 不画那圈 5px 描边——1px 的环在 16px 下只会糊出一圈脏边，
去掉之后反而干净。

## 落选方案

`alternatives.png` 里留了另外两个：

- **B 三通道泳道**（三条不同占空比的波形堆叠）：48px 以上最贴产品（就是上位机的三泳道示波器），
  但 16/32px 三色线挤在一起糊成一团，任务栏辨识度不如 A。
- **C 旋钮 + 波形**：想说"这是控制器不是示波器"，结果两半互相抢，圆环把波形压成小字，16px 直接变墨点。

## 重新生成

```bash
cd docs/icons
python gen_icons.py
copy app.ico ..\..\PWMCtrol\PWMCtrol\Assets\app.ico
```

只依赖 Pillow（不做 SVG 渲染：几何用 Python 描述，SVG 与 PNG 共用同一份坐标，
所以两者永远一致，也不需要 cairosvg 之类的原生依赖）。

## 换配色 / 改形状

- 颜色：`C_TILE` / `C_TILE_EDGE` / `C_LED`，位于脚本顶部。
- 形状：`variant_a(stroke=, periods=, y_low=, y_high=)`，四个参数都在 `finalize()` 里显式传。
- 想要"三通道点"装饰：`variant_a(legend=True)`，但那三个点只在 ≥128px 才看得见，
  32px 会变成三粒彩色脏点，**不建议**。
