param([string]$BaselineRef = '3e19546805dce479fe13093670cb9fc772253b1e')

# Recolor original C pixels only. Never redraw, crop, resize or change alpha.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/..").Path
Add-Type -TypeDefinition @'
using System;
using System.Text.RegularExpressions;

public static class WeatherPixelPalette
{
    static bool IsCloudGray(int r, int g, int b)
    {
        return Math.Min(r, Math.Min(g, b)) >= 170 &&
            Math.Max(r, Math.Max(g, b)) - Math.Min(r, Math.Min(g, b)) <= 18;
    }

    public static string Recolor(string source, int code)
    {
        var original = Regex.Match(source, @"(?s)#if LV_COLOR_DEPTH == 32\s*\n(.*?)#endif");
        var pixels = Regex.Matches(original.Groups[1].Value, @"0x([0-9a-fA-F]{2})");
        int width = int.Parse(Regex.Match(source, @"\.header\.w = (\d+)").Groups[1].Value);
        int height = int.Parse(Regex.Match(source, @"\.header\.h = (\d+)").Groups[1].Value);
        int count = width * height;
        if (pixels.Count != count * 4) throw new InvalidOperationException("Invalid original pixels");
        var changed = new bool[count];
        var colors = new byte[count, 3];
        var rgba = new byte[count, 4];
        for (int i = 0; i < count*4; i++)
            rgba[i/4, i%4] = Convert.ToByte(pixels[i].Groups[1].Value, 16);
        var cloudGrays = new double[height];
        int cloudTop = height;
        for (int y = 0; y < height; y++) {
            double total = 0; int samples = 0;
            for (int x = 0; x < width; x++) {
                int i = y*width+x, b = rgba[i,0], g = rgba[i,1], r = rgba[i,2];
                if (rgba[i,3] > 200 && IsCloudGray(r,g,b)) {
                    double alpha = rgba[i,3]/255.0;
                    total += (r+g+b)/3.0*0.78*alpha + 255*(1-alpha);
                    samples++;
                }
            }
            cloudGrays[y] = samples > 0 ? total/samples : 190;
            if (samples > 0 && y < cloudTop) cloudTop = y;
        }
        // Fit the exposed moon's original color gradient above the cloud.
        double n=0, sumY=0, sumD=0, sumYY=0, sumYD=0;
        if (code == 6 || code == 8) {
            for (int y=0; y<cloudTop; y++) {
                int difference=0;
                for (int x=0; x<width; x++) {
                    int i=y*width+x;
                    if (rgba[i,3]>200) difference=Math.Max(difference,rgba[i,2]-rgba[i,0]);
                }
                if (difference<=18) continue;
                n++; sumY+=y; sumD+=difference; sumYY+=y*y; sumYD+=y*difference;
            }
        }
        double denominator=n*sumYY-sumY*sumY;
        double slope=denominator>0?(n*sumYD-sumY*sumD)/denominator:0;
        double intercept=n>0?(sumD-slope*sumY)/n:0;
        for (int i = 0; i < count; i++) {
            int b = Convert.ToInt32(pixels[4*i].Groups[1].Value, 16);
            int g = Convert.ToInt32(pixels[4*i+1].Groups[1].Value, 16);
            int r = Convert.ToInt32(pixels[4*i+2].Groups[1].Value, 16);
            int a = Convert.ToInt32(pixels[4*i+3].Groups[1].Value, 16);
            if (a == 0) continue;
            bool cloud = ((code >= 4 && code <= 25) || code == 30) && IsCloudGray(r,g,b);
            bool rain = ((code >= 10 && code <= 18) || (code == 20 && i%width < width*0.65))
                && b-r >= 25 && g-r >= 10 && b >= 190;
            bool warmMixture = r-b>18 && g-b>15 && b>30;
            bool sunOverlap = (code==5 || code==7 || code==10 || code==21) && warmMixture;
            // Native moon icons have a front cloud lobe over the moon's lower arc.
            // These circles follow that lobe inside the original alpha silhouette.
            double x=i%width, y=i/width;
            double cx=code==6?31:30, cy=code==6?37:39, radius=code==6?20:19;
            double rx=code==6?49:47, ry=code==6?44:45, rr=code==6?16:15;
            bool frontCloud=(x-cx)*(x-cx)+(y-cy)*(y-cy)<=radius*radius ||
                (x-rx)*(x-rx)+(y-ry)*(y-ry)<=rr*rr;
            bool moonOverlap = (code==6 || code==8) && n>0 && i/width>=cloudTop &&
                ((frontCloud && (warmMixture || cloud)) ||
                 (warmMixture && r-b < 0.90*(intercept+slope*(i/width))));
            if (!cloud && !rain && !sunOverlap && !moonOverlap) continue;
            changed[i] = true;
            if (moonOverlap) {
                double alpha=a/255.0;
                byte gray=(byte)Math.Round(Math.Max(0,Math.Min(255,
                    (cloudGrays[i/width]-255*(1-alpha))/alpha)));
                colors[i,0]=gray; colors[i,1]=gray; colors[i,2]=gray;
            } else if (cloud) {
                colors[i,0] = (byte)Math.Round(r*0.78);
                colors[i,1] = (byte)Math.Round(g*0.78);
                colors[i,2] = (byte)Math.Round(b*0.78);
            } else if (rain) {
                // Same blue as lv_palette_main(LV_PALETTE_BLUE) on the home page.
                colors[i,0] = 0x21; colors[i,1] = 0x96; colors[i,2] = 0xF3;
            } else {
                byte gray = (byte)Math.Round(cloudGrays[i/width]);
                colors[i,0] = gray; colors[i,1] = gray; colors[i,2] = gray;
            }
        }
        return Regex.Replace(source, @"(?s)(#if (LV_COLOR_DEPTH[^\n]+)\n)(.*?)(#endif)", block => {
            string condition = block.Groups[2].Value;
            int depth = condition.Contains("== 32") ? 32 : condition.Contains("== 16") ? 16 : 8;
            int stride = depth == 32 ? 4 : depth == 16 ? 3 : 2;
            bool swap = condition.Contains("SWAP != 0");
            int offset = 0;
            string body = Regex.Replace(block.Groups[3].Value, @"0x([0-9a-fA-F]{2})", token => {
                int index = offset / stride, channel = offset++ % stride;
                if (index >= count) throw new InvalidOperationException("Pixel overflow");
                if (!changed[index] || channel == stride-1) return token.Value;
                int r = colors[index,0], g = colors[index,1], b = colors[index,2];
                int value;
                if (depth == 32) value = channel == 0 ? b : channel == 1 ? g : r;
                else if (depth == 16) {
                    int packed = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                    value = ((channel == 0) != swap) ? packed & 255 : packed >> 8;
                } else value = (r & 0xE0) | ((g >> 3) & 0x1C) | (b >> 6);
                return "0x" + value.ToString("x2");
            });
            if (offset != count*stride) throw new InvalidOperationException("Invalid pixel count");
            return block.Groups[1].Value + body + block.Groups[4].Value;
        });
    }
}
'@

Push-Location $root
try {
    foreach ($code in (@(0..38) + @(99))) {
        $relative = "App/LVGL/assets/images/ui_img_weather_$code.c"
        $originalLines = & git -c safe.directory=E:/GitHub/devbox show ($BaselineRef + ':' + $relative)
        if ($LASTEXITCODE) { throw "Cannot read original resource: $relative" }
        $original = ($originalLines -join "`n") + "`n"
        $result = [WeatherPixelPalette]::Recolor($original, $code)
        $result = [System.Text.RegularExpressions.Regex]::Replace(
            $result,
            '[ \t]+(?=\r?$)',
            '',
            [System.Text.RegularExpressions.RegexOptions]::Multiline)
        [System.IO.File]::WriteAllText((Join-Path $root $relative), $result, (New-Object System.Text.UTF8Encoding $false))
    }
} finally { Pop-Location }
Write-Output 'Updated original pixels: lighter cloud gray, opaque cloud overlap and home-page blue rain; all alpha bytes preserved.'
