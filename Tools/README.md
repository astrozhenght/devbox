# lvgl_c2bmp — LVGL 图片 C 文件转换上位机

把任意 LVGL 生成的图片 `.c` 文件（lv_img_conv 输出）还原成位图图片的小工具，
纯 Python 标准库实现，无第三方依赖。也可直接双击同目录下的
`lvgl_c2bmp.exe` 使用。

## 启动图形界面

```text
python Tools/lvgl_c2bmp.py
```

界面提供（支持任意 LVGL 图片 C 文件，不限于天气图标）：

- 选择单个图片 `.c` 文件并预览。
- 保存为 PNG（带透明通道）或 BMP（32 位带透明通道）。
- 选择一个目录批量转换其中的全部 `.c` 文件，输出文件名与源文件名保持一致
  （例如 `ui_img_weather_13.c` 生成 `ui_img_weather_13.png`）。

## 命令行

```text
python Tools/lvgl_c2bmp.py --cli 输入.c 输出.png
python Tools/lvgl_c2bmp.py --batch 输入目录 输出目录 --format png
```

`--format` 可选 `png`（默认）或 `bmp`。批量模式下不合法的 C 文件会被跳过并统计。

## 说明

- 解析支持 LVGL 图片转换器的常见分支：32 位 BGRA、16 位 RGB565
  （含/不含 Alpha、字节序是否交换）、8 位 RGB332。优先使用 32 位分支保证精度。
- 输出文件名与源 `.c` 文件同名，仅替换扩展名。
- PNG 输出自带透明背景；BMP 输出使用带 Alpha 的 32 位格式，部分老旧查看器
  可能不显示透明通道，此时建议使用 PNG。
