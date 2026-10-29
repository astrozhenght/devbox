#ifndef __BSP_FLASH_H__
#define __BSP_FLASH_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BSP_FLASH_OK = 0,                  // 通信正常
    BSP_FLASH_ERROR_INIT,              // 初始化失败
    BSP_FLASH_ERROR_WRITE_ENABLE,      // 写使能失败
    BSP_FLASH_ERROR_AUTOPOLLING,       // 自动轮询超时或无响应
    BSP_FLASH_ERROR_ERASE,             // 擦除失败
    BSP_FLASH_ERROR_TRANSMIT,          // 数据传输失败
    BSP_FLASH_ERROR_MEMORY_MAPPED,     // 设置内存映射模式失败
    BSP_FLASH_ERROR_INVALID_ARGUMENT,  // 参数无效
    BSP_FLASH_ERROR_OUT_OF_RANGE,      // 地址超出范围
    BSP_FLASH_ERROR_ID_MISMATCH,       // 读取到的ID与预期不匹配
} bsp_flash_result_t;

bsp_flash_result_t bsp_flash_init(void);   // W25Q初始化
bsp_flash_result_t bsp_flash_read_id(uint32_t *flash_id);  // 读取器件ID
bsp_flash_result_t bsp_flash_write(const uint8_t *buffer, uint32_t address, uint32_t size);  // 写入数据，最大不能超过flash芯片的大小
bsp_flash_result_t bsp_flash_read(uint8_t *buffer, uint32_t address, uint32_t size);   // 读取数据，最大不能超过flash芯片的大小
bsp_flash_result_t bsp_flash_erase_sector(uint32_t address);    // 扇区擦除，4K字节， 参考擦除时间 45ms
bsp_flash_result_t bsp_flash_erase_block_32k(uint32_t address); // 块擦除，32K字节，参考擦除时间 120ms
bsp_flash_result_t bsp_flash_erase_block_64k(uint32_t address); // 块擦除，64K字节，参考擦除时间 150ms，实际使用建议使用64K擦除，擦除的时间最快
bsp_flash_result_t bsp_flash_erase_chip(void);                  // 整片擦除，参考擦除时间 20S
bsp_flash_result_t bsp_flash_enable_memory_mapped(void);        // 进入内存映射模式
bsp_flash_result_t bsp_flash_disable_memory_mapped(void);       // 退出内存映射模式

#ifdef __cplusplus
}
#endif

#endif

