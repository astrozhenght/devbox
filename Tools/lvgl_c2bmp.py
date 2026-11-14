#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""LVGL 图片 .c -> 位图图片 转换上位机。

把任意 LVGL 生成的图片 .c 文件（lv_img_conv 输出）中的像素数组解析出来，
渲染成 PNG 或 BMP 图片。只依赖 Python 标准库，无需第三方包。

用法:
    python lvgl_c2bmp.py                # 启动图形界面
    python lvgl_c2bmp.py --cli in.c out.png
    python lvgl_c2bmp.py --batch <in_dir> <out_dir> [--format png|bmp]
"""

import os
import re
import struct
import sys
import zlib


PNG_SIG = b"\x89PNG\r\n\x1a\n"


def _extract_block(text, opener):
    m = re.search(r"#if\s+" + opener + r"\s*(.*?)#endif", text, re.S)
    if not m:
        return None
    return [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", m.group(1))]


def _rgb565(v):
    r = ((v >> 11) & 0x1F) * 255 // 31
    g = ((v >> 5) & 0x3F) * 255 // 63
    b = (v & 0x1F) * 255 // 31
    return r, g, b


def _decode_32bit(vals, w, h):
    rgba = bytearray()
    for i in range(0, len(vals), 4):
        b, g, r, a = vals[i], vals[i + 1], vals[i + 2], vals[i + 3]
        rgba += bytes((r, g, b, a))
    return bytes(rgba)


def _decode_16bit(vals, w, h, swapped):
    count = w * h
    step = 3 if len(vals) == count * 3 else 2
    has_alpha = step == 3
    rgba = bytearray()
    for i in range(0, len(vals), step):
        if swapped:
            word = (vals[i] << 8) | vals[i + 1]
        else:
            word = vals[i] | (vals[i + 1] << 8)
        r, g, b = _rgb565(word)
        a = vals[i + 2] if has_alpha else 255
        rgba += bytes((r, g, b, a))
    return bytes(rgba)


def _decode_8bit(vals, w, h):
    count = w * h
    step = 2 if len(vals) == count * 2 else 1
    has_alpha = step == 2
    rgba = bytearray()
    for i in range(0, len(vals), step):
        c = vals[i]
        r = c & 0xE0
        g = (c & 0x1C) << 3
        b = (c & 0x03) << 6
        a = vals[i + 1] if has_alpha else 255
        rgba += bytes((r, g, b, a))
    return bytes(rgba)


def _extract_map_bytes(text):
    """提取 `xxx_map[] = { ... }` 里的全部字节（去掉注释）。"""
    m = re.search(r"\w+_map\s*\[\s*\]\s*=\s*\{", text)
    if not m:
        return None
    i = m.end()
    depth = 1
    while i < len(text) and depth > 0:
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
        i += 1
    body = text[m.end():i - 1]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    return [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", body)]


def _decode_indexed(vals, w, h, bits):
    palette_bytes = (1 << bits) * 4
    if vals is None or len(vals) < palette_bytes:
        raise ValueError("索引色图片缺少调色板")
    row_bytes = (w * bits + 7) // 8
    data = vals[palette_bytes:]
    if len(data) < row_bytes * h:
        raise ValueError("索引色像素数据长度不足")
    mask = (1 << bits) - 1
    rgba = bytearray()
    for y in range(h):
        base = y * row_bytes
        for x in range(w):
            bit = x * bits
            byte = data[base + (bit >> 3)]
            shift = 8 - bits - (bit & 7)
            idx = (byte >> shift) & mask
            p = idx * 4
            b, g, r, a = vals[p], vals[p + 1], vals[p + 2], vals[p + 3]
            rgba += bytes((r, g, b, a))
    return bytes(rgba)


def _decode_alpha_mask(vals, w, h, bits):
    row_bytes = (w * bits + 7) // 8
    if vals is None or len(vals) < row_bytes * h:
        raise ValueError("Alpha 掩码数据长度不足")
    maxv = (1 << bits) - 1
    rgba = bytearray()
    for y in range(h):
        base = y * row_bytes
        for x in range(w):
            bit = x * bits
            byte = vals[base + (bit >> 3)]
            shift = 8 - bits - (bit & 7)
            v = (byte >> shift) & maxv
            rgba += bytes((0, 0, 0, v * 255 // maxv))
    return bytes(rgba)


def parse_c_file(path):
    """解析 LVGL 图片 .c 文件，返回 (width, height, rgba_bytes)。

    支持 LVGL 图片转换器输出的常见分支：32 位 BGRA、16 位 RGB565
    （含/不含 Alpha、字节序是否交换）、8 位 RGB332，以及索引色
    （1/2/4/8 bit）和 Alpha 掩码（1/2/4/8 bit）格式。
    """
    with open(path, "rb") as f:
        raw = f.read()
    text = raw.decode("utf-8", errors="replace")

    m = re.search(r"\.header\.w\s*=\s*(\d+)", text)
    w = int(m.group(1)) if m else 0
    m = re.search(r"\.header\.h\s*=\s*(\d+)", text)
    h = int(m.group(1)) if m else 0
    if w <= 0 or h <= 0:
        raise ValueError("无法从文件中读取宽高")

    count = w * h

    cf_match = re.search(r"\.header\.cf\s*=\s*(LV_IMG_CF_\w+)", text)
    cf = cf_match.group(1) if cf_match else ""

    if cf.startswith("LV_IMG_CF_INDEXED_"):
        bits = int(cf.rsplit("_", 1)[1].replace("BIT", ""))
        return w, h, _decode_indexed(_extract_map_bytes(text), w, h, bits)

    if cf.startswith("LV_IMG_CF_ALPHA_"):
        bits = int(cf.rsplit("_", 1)[1].replace("BIT", ""))
        return w, h, _decode_alpha_mask(_extract_map_bytes(text), w, h, bits)

    vals = _extract_block(text, r"LV_COLOR_DEPTH\s*==\s*32")
    if vals is not None and len(vals) == count * 4:
        return w, h, _decode_32bit(vals, w, h)

    vals = _extract_block(text, r"LV_COLOR_DEPTH\s*==\s*16\s*&&\s*LV_COLOR_16_SWAP\s*==\s*0")
    if vals is not None and len(vals) in (count * 2, count * 3):
        return w, h, _decode_16bit(vals, w, h, swapped=False)

    vals = _extract_block(text, r"LV_COLOR_DEPTH\s*==\s*16\s*&&\s*LV_COLOR_16_SWAP\s*!=\s*0")
    if vals is not None and len(vals) in (count * 2, count * 3):
        return w, h, _decode_16bit(vals, w, h, swapped=True)

    vals = _extract_block(text, r"LV_COLOR_DEPTH\s*==\s*16")
    if vals is not None and len(vals) in (count * 2, count * 3):
        return w, h, _decode_16bit(vals, w, h, swapped=False)

    vals = _extract_block(text, r"LV_COLOR_DEPTH\s*==\s*1\s*\|\|\s*LV_COLOR_DEPTH\s*==\s*8")
    if vals is not None and len(vals) in (count, count * 2):
        return w, h, _decode_8bit(vals, w, h)

    raise ValueError("不是受支持的 LVGL 图片 C 文件（缺少可识别的像素分支）")


def _png_chunk(typ, data):
    c = struct.pack(">I", len(data)) + typ + data
    c += struct.pack(">I", zlib.crc32(typ + data) & 0xFFFFFFFF)
    return c


def write_png(path, w, h, rgba):
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    raw = b"".join(b"\x00" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    data = (
        PNG_SIG
        + _png_chunk(b"IHDR", ihdr)
        + _png_chunk(b"IDAT", zlib.compress(raw, 6))
        + _png_chunk(b"IEND", b"")
    )
    with open(path, "wb") as f:
        f.write(data)


def write_rgb_png(path, w, h, rgb):
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    data = (
        PNG_SIG
        + _png_chunk(b"IHDR", ihdr)
        + _png_chunk(b"IDAT", zlib.compress(raw, 6))
        + _png_chunk(b"IEND", b"")
    )
    with open(path, "wb") as f:
        f.write(data)


def write_bmp(path, w, h, rgba):
    """写 32 位带透明通道的 BMP(BITMAPV4HEADER, 自下而上, BGRA)。"""
    row_size = (w * 4 + 3) // 4 * 4
    pixel_size = row_size * h
    file_size = 14 + 108 + pixel_size
    header = struct.pack(
        "<2sIHHI",
        b"BM", file_size, 0, 0, 14 + 108,
    )
    info = struct.pack(
        "<IiiHHIIiiII",
        108, w, h, 1, 32, 3, pixel_size, 0, 0, 0, 0,
    )
    masks = struct.pack("<IIII", 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    v4_extra = masks + struct.pack("<I", 0) + b"\x00" * 36 + struct.pack("<III", 0, 0, 0)
    pixels = bytearray()
    for y in range(h - 1, -1, -1):
        row = bytearray(rgba[y * w * 4:(y + 1) * w * 4])
        # RGBA -> BGRA
        for x in range(0, len(row), 4):
            r, g, b, a = row[x], row[x + 1], row[x + 2], row[x + 3]
            row[x], row[x + 1], row[x + 2], row[x + 3] = b, g, r, a
        pixels += row
        pixels += b"\x00" * (row_size - len(row))
    with open(path, "wb") as f:
        f.write(header + info + v4_extra + bytes(pixels))


def save_image(path, w, h, rgba):
    ext = os.path.splitext(path)[1].lower()
    if ext == ".bmp":
        write_bmp(path, w, h, rgba)
    else:
        write_png(path, w, h, rgba)


def composite_on_white(w, h, rgba):
    """把 RGBA 合成到白底，返回 RGB 字节，用于无透明预览。"""
    rgb = bytearray()
    for i in range(0, len(rgba), 4):
        r, g, b, a = rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]
        r = (r * a + 255 * (255 - a)) // 255
        g = (g * a + 255 * (255 - a)) // 255
        b = (b * a + 255 * (255 - a)) // 255
        rgb += bytes((r, g, b))
    return bytes(rgb)


def convert_file(in_path, out_path):
    w, h, rgba = parse_c_file(in_path)
    save_image(out_path, w, h, rgba)
    return w, h


def batch_convert(in_dir, out_dir, fmt="png"):
    os.makedirs(out_dir, exist_ok=True)
    converted = 0
    skipped = []
    for name in sorted(os.listdir(in_dir)):
        if not name.lower().endswith(".c"):
            continue
        in_path = os.path.join(in_dir, name)
        if not os.path.isfile(in_path):
            continue
        stem = os.path.splitext(name)[0]
        out_path = os.path.join(out_dir, stem + "." + fmt)
        try:
            convert_file(in_path, out_path)
            converted += 1
        except Exception as exc:  # noqa: BLE001
            skipped.append((name, str(exc)))
            print("跳过 %s: %s" % (name, exc))
    return converted, skipped


def _cli(argv):
    if argv[1] == "--cli":
        in_path, out_path = argv[2], argv[3]
        w, h = convert_file(in_path, out_path)
        print("已转换 %s -> %s (%dx%d)" % (in_path, out_path, w, h))
    elif argv[1] == "--batch":
        in_dir = argv[2]
        out_dir = argv[3]
        fmt = "png"
        if "--format" in argv:
            fmt = argv[argv.index("--format") + 1]
        n, skipped = batch_convert(in_dir, out_dir, fmt)
        print("批量转换完成: 成功 %d 个, 跳过 %d 个 -> %s" % (n, len(skipped), out_dir))
    else:
        print(__doc__)
        return 2
    return 0


def _gui():
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

    root = tk.Tk()
    root.title("LVGL 图片 .c -> 位图转换器  (lvgl_c2bmp)")
    root.geometry("720x520")

    state = {"path": None, "w": 0, "h": 0, "rgba": b""}
    fmt_var = tk.StringVar(value="png")

    toolbar = ttk.Frame(root, padding=8)
    toolbar.pack(fill="x")

    def set_file(path):
        try:
            w, h, rgba = parse_c_file(path)
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("解析失败", str(exc))
            return
        state.update(path=path, w=w, h=h, rgba=rgba)
        name_var.set(os.path.basename(path))
        info_var.set("%d x %d" % (w, h))
        refresh_preview()

    def pick_file():
        path = filedialog.askopenfilename(
            title="选择 LVGL 图片 C 文件",
            filetypes=[("C 源文件", "*.c"), ("所有文件", "*.*")],
            initialdir=default_dir(),
        )
        if path:
            set_file(path)

    def pick_batch_dir():
        path = filedialog.askdirectory(title="选择包含 LVGL 图片 C 文件的目录", initialdir=default_dir())
        if not path:
            return
        out = filedialog.askdirectory(title="选择输出目录")
        if not out:
            return
        try:
            n, skipped = batch_convert(path, out, fmt_var.get())
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("批量转换失败", str(exc))
            return
        message = "成功转换 %d 个文件到 %s" % (n, out)
        if skipped:
            message += "\n跳过 %d 个非 LVGL 图片或解析失败的文件。" % len(skipped)
        messagebox.showinfo("完成", message)

    def save_one():
        if not state["path"]:
            messagebox.showwarning("提示", "请先选择文件")
            return
        fmt = fmt_var.get()
        out = filedialog.asksaveasfilename(
            title="保存图片",
            defaultextension="." + fmt,
            filetypes=[("PNG", "*.png"), ("BMP", "*.bmp")],
            initialfile=os.path.splitext(os.path.basename(state["path"]))[0] + "." + fmt,
        )
        if not out:
            return
        try:
            save_image(out, state["w"], state["h"], state["rgba"])
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("保存失败", str(exc))
            return
        messagebox.showinfo("完成", "已保存 %s" % out)

    name_var = tk.StringVar(value="(未选择)")
    info_var = tk.StringVar(value="-")

    def refresh_preview():
        preview = state.get("preview")
        if preview is None or not state["rgba"]:
            return
        rgb = composite_on_white(state["w"], state["h"], state["rgba"])
        tmp = os.path.join(os.environ.get("TEMP", "."), "_lvgl_preview.png")
        write_rgb_png(tmp, state["w"], state["h"], rgb)
        img = tk.PhotoImage(file=tmp)
        img = img.zoom(3, 3)
        state["photo"] = img
        preview.delete("all")
        cw = preview.winfo_width() or 720
        ch = preview.winfo_height() or 400
        preview.create_image(
            max(0, (cw - state["w"] * 3) // 2),
            max(0, (ch - state["h"] * 3) // 2),
            anchor="nw",
            image=img,
        )

    ttk.Button(toolbar, text="选择文件", command=pick_file).pack(side="left", padx=2)
    ttk.Button(toolbar, text="批量转换目录", command=pick_batch_dir).pack(side="left", padx=2)
    ttk.Button(toolbar, text="保存图片", command=save_one).pack(side="left", padx=2)
    ttk.Label(toolbar, text="格式:").pack(side="left", padx=(16, 2))
    ttk.Radiobutton(toolbar, text="PNG", variable=fmt_var, value="png").pack(side="left")
    ttk.Radiobutton(toolbar, text="BMP", variable=fmt_var, value="bmp").pack(side="left")

    info_bar = ttk.Frame(root, padding=(8, 0, 8, 4))
    info_bar.pack(fill="x")
    ttk.Label(info_bar, textvariable=name_var).pack(side="left")
    ttk.Label(info_bar, textvariable=info_var).pack(side="right")

    preview = tk.Canvas(root, bg="#ffffff", highlightthickness=1, highlightbackground="#cccccc")
    preview.pack(fill="both", expand=True, padx=8, pady=4)
    state["preview"] = preview
    state["name_var"] = name_var
    state["info_var"] = info_var
    state["tk_root"] = root

    root.mainloop()


def default_dir():
    if getattr(sys, "frozen", False):
        here = os.path.dirname(os.path.abspath(sys.executable))
    else:
        here = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(here, "..", "App", "LVGL", "assets", "images"),
        os.path.join(here, "App", "LVGL", "assets", "images"),
        os.path.join(os.getcwd(), "App", "LVGL", "assets", "images"),
    ]
    for c in candidates:
        if os.path.isdir(c):
            return os.path.abspath(c)
    return os.getcwd()


if __name__ == "__main__":
    if len(sys.argv) > 1:
        sys.exit(_cli(sys.argv))
    _gui()
