#ifndef __STR_CONVERT_H
#define __STR_CONVERT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// 将字符串转为16进制字符串
void ascii_to_hexstr(const char* src, char* dst, uint32_t dst_size);

// 将十六进制字符串转换为 ASCII 字符串
void hexstr_to_ascii(const char* src, char* dst, uint32_t dst_size);

#ifdef __cplusplus
}
#endif

#endif /* __STR_CONVERT_H */

