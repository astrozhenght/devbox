#ifndef __I2C_H__
#define __I2C_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLATFORM_I2C_ACK = 0,   // 收到应答
    PLATFORM_I2C_NACK  = 1  // 未收到应答
} platform_i2c_ack_t;

void platform_i2c_init(void);            // 引脚初始化
void platform_i2c_delay(uint32_t delay); // 延时函数
void platform_i2c_start(void);           // 启动通信
void platform_i2c_stop(void);            // 发送停止信号
void platform_i2c_ack(void);             // 发送响应信号
void platform_i2c_nack(void);            // 发送非应答信号
uint8_t platform_i2c_read_byte(platform_i2c_ack_t is_ack); // 读字节
platform_i2c_ack_t platform_i2c_wait_ack(void);            // 等待应答信号
platform_i2c_ack_t platform_i2c_write_byte(uint8_t data);  // 写字节


#ifdef __cplusplus
}
#endif

#endif /* __I2C_H__ */
