#ifndef __HEX_CONVERT_H
#define __HEX_CONVERT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// 将字符串转为16进制字符串
void bytes_to_hex_string(const char* input, char* output, uint32_t output_capacity);

// 将十六进制字符串转换为 ASCII 字符串
void hex_string_to_bytes(const char* input, char* output, uint32_t output_capacity);

#ifdef __cplusplus
}
#endif

#endif /* __HEX_CONVERT_H */

