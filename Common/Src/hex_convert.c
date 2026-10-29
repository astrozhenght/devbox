#include "hex_convert.h"
#include <stdio.h>  // snprintf()、sscanf()
#include <string.h> // strlen()
#include <ctype.h>  // isspace()

// 将原始字节转换为16进制字符串
// {0x48, 0x49, 0x50} -> "48 49 50"
void bytes_to_hex_string(const char* input, char* output, uint32_t output_capacity)
{
    uint32_t len = strlen(input);
    uint32_t pos = 0;

    for (uint32_t i = 0; i < len && (pos + 3) < output_capacity; i++)  // 每个字节输出3字符（2个hex+空格）
    {
        pos += snprintf(&output[pos], output_capacity - pos, "%02X ", (unsigned char)input[i]);
    }

    if (pos > 0 && output[pos - 1] == ' ')  // 去掉末尾多余空格
    {
        output[pos - 1] = '\0';
    }
}

// 将十六进制字符串转换为原始字节
// "48 49 50" -> {0x48, 0x49, 0x50}
void hex_string_to_bytes(const char* input, char* output, uint32_t output_capacity)
{
    uint32_t len = strlen(input);
    uint32_t dst_index = 0;
    uint32_t value;

    for (uint32_t i = 0; i < len; )
    {
        // 跳过空格
        while (i < len && isspace((unsigned char)input[i]))
        {
            i++;
        }

        if (i + 1 < len)
        {
            if (sscanf(&input[i], "%2x", &value) == 1)
            {
                if (dst_index + 1 < output_capacity)
                {
                    output[dst_index++] = (char)value;
                }
            }
            i += 2; // 移动到下一个字节
        }
        else
        {
            break;
        }
    }
    output[dst_index] = '\0'; // 结束符
}

