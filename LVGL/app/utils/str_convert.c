#include <stdio.h>
#include <string.h>
#include "str_convert.h"
#include <ctype.h>

// 将ASCII码字符串转为16进制字符串
// "012" -> "48 49 50"
void ascii_to_hexstr(const char* src, char* dst, uint32_t dst_size)
{
    uint32_t len = strlen(src);
    uint32_t pos = 0;

    for (uint32_t i = 0; i < len && (pos + 3) < dst_size; i++)  // 每个字节输出3字符（2个hex+空格）
    {
        pos += snprintf(&dst[pos], dst_size - pos, "%02X ", (unsigned char)src[i]);
    }

    if (pos > 0 && dst[pos - 1] == ' ')  // 去掉末尾多余空格
    {
        dst[pos - 1] = '\0';
    }
}

// 将十六进制字符串转换为ASCII码字符串
// "48 49 50" -> "012"
void hexstr_to_ascii(const char* src, char* dst, uint32_t dst_size)
{
    uint32_t len = strlen(src);
    uint32_t dst_index = 0;
    uint32_t value;

    for (uint32_t i = 0; i < len; )
    {
        // 跳过空格
        while (i < len && isspace((unsigned char)src[i]))
        {
            i++;
        }

        if (i + 1 < len)
        {
            if (sscanf(&src[i], "%2x", &value) == 1)
            {
                if (dst_index + 1 < dst_size)
                {
                    dst[dst_index++] = (char)value;
                }
            }
            i += 2; // 移动到下一个字节
        }
        else
        {
            break;
        }
    }
    dst[dst_index] = '\0'; // 结束符
}

