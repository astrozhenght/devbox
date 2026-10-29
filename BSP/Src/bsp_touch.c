#include "bsp_touch.h"
#include "i2c.h"
#include "stm32h7xx_hal.h"
#include <stdio.h>

#define GT911_INT_PORT                  GPIOI        // INT 引脚端口
#define GT911_INT_PIN                   GPIO_PIN_11  // INT 引脚
#define GT911_RST_PORT                  GPIOI        // RST 引脚端口
#define GT911_RST_PIN                   GPIO_PIN_10  // RST 引脚

#define GT911_I2C_RADDR                 0xBB         // I2C初始化地址
#define GT911_I2C_WADDR                 0xBA
#define GT911_CFG_ADDR                  0x8047       // 固件配置信息寄存器和配置起始地址
#define GT911_READ_ADDR                 0x814E       // 触摸信息寄存器
#define GT911_ID_ADDR                   0x8140       // 触摸面板ID寄存器

static bsp_touch_info_t touch_info;     // 触摸信息结构体，存储触摸数据

// 内部函数声明
static void bsp_touch_reset(void);
static bsp_status_t bsp_touch_send_register_address(uint16_t addr);
static bsp_status_t bsp_touch_write_register(uint16_t addr, uint8_t value);
static bsp_status_t bsp_touch_write_registers(uint16_t addr, uint8_t cnt, uint8_t *value);
static bsp_status_t bsp_touch_read_registers(uint16_t addr, uint8_t cnt, uint8_t *value);
static void bsp_touch_gpio_init(void);
static void bsp_touch_int_config_output(void);
static void bsp_touch_int_config_input(void);


/*************************************************************************************************************************************
 *    函 数 名:    bsp_touch_reset
 *    入口参数:    无
 *    返 回 值:    无
 *    函数功能:    复位GT911
 *    说    明:    复位GT911，并将芯片的I2C地址配置为0xBA/0xBB
 ************************************************************************************************************************************/
static void bsp_touch_reset(void)
{
    bsp_touch_int_config_output(); // 将INT引脚配置为输出

    // 初始化引脚状态
    HAL_GPIO_WritePin(GT911_INT_PORT, GT911_INT_PIN, GPIO_PIN_RESET); // INT输出低电平
    HAL_GPIO_WritePin(GT911_RST_PORT, GT911_RST_PIN, GPIO_PIN_SET);   // RST输出高电平
    platform_i2c_delay(10000); // 规格书没要求

    // 开始执行复位，RST引脚拉低复位
    // INT引脚保持低电平不变，将器件地址设置为0XBA/0XBB
    HAL_GPIO_WritePin(GT911_RST_PORT, GT911_RST_PIN, GPIO_PIN_RESET); // 拉低复位引脚，此时芯片执行复位
    platform_i2c_delay(150000);                                       // 根据规格书要求延时大于100us
    HAL_GPIO_WritePin(GT911_RST_PORT, GT911_RST_PIN, GPIO_PIN_SET);   // 拉高复位引脚，复位结束
    platform_i2c_delay(350000);    // 根据规格书要求延时大于5ms
    bsp_touch_int_config_input();  // INT引脚转为浮空输入
    platform_i2c_delay(20000);     // 延时
}

/*************************************************************************************************************************************
 *    函 数 名:    bsp_touch_send_register_address
 *    入口参数:    addr - 要操作的寄存器
 *    返 回 值:    BSP_OK       - 操作成功
 *                BSP_ERROR    - 操作失败
 *    函数功能:    GT9XX 写操作
 *    说    明:    对指定的寄存器执行写操作
 ************************************************************************************************************************************/
static bsp_status_t bsp_touch_send_register_address(uint16_t addr)
{
    bsp_status_t status = BSP_ERROR;   // 状态标志位

    platform_i2c_start();  // 启动I2C通信
    if (platform_i2c_write_byte(GT911_I2C_WADDR) == PLATFORM_I2C_ACK) // 写数据指令
    {
        // 写入16位地址
        if (platform_i2c_write_byte((uint8_t)(addr >> 8)) == PLATFORM_I2C_ACK)
        {
            if (platform_i2c_write_byte((uint8_t)(addr)) == PLATFORM_I2C_ACK)
            {
                status = BSP_OK; // 操作成功
            }
        }
    }

    return status;
}

/*************************************************************************************************************************************
 *    函 数 名:    bsp_touch_write_register
 *    入口参数:    addr - 要写入的寄存器
 *                    value - 要写入的数据
 *    返 回 值:    BSP_OK - 操作成功
 *                    BSP_ERROR      - 操作失败
 *    函数功能:    GT9XX 写一字节数据
 *    说    明:    对指定的寄存器写入一字节数据
 ************************************************************************************************************************************/
static bsp_status_t bsp_touch_write_register(uint16_t addr, uint8_t value)
{
    bsp_status_t status = BSP_ERROR;

    if (bsp_touch_send_register_address(addr) == BSP_OK) // 写入要操作的寄存器
    {
        if (platform_i2c_write_byte(value) == PLATFORM_I2C_ACK) // 写数据
        {
            status = BSP_OK;
        }
    }
    platform_i2c_stop(); // 停止通讯

    return status;
}

/*************************************************************************************************************************************
 *    函 数 名:    bsp_touch_write_registers
 *    入口参数:    addr - 要写入的寄存器区域首地址
 *                cnt  - 数据长度
 *                value - 要写入的数据区
 *    返 回 值:    BSP_OK - 操作成功
 *                BSP_ERROR      - 操作失败
 *    函数功能:    GT9XX 写寄存器
 *    说    明:    往芯片的寄存器区写入指定长度的数据
 ************************************************************************************************************************************/
static bsp_status_t bsp_touch_write_registers(uint16_t addr, uint8_t cnt, uint8_t *value)
{
    bsp_status_t status = BSP_OK;

    if (bsp_touch_send_register_address(addr) == BSP_OK) // 写入要操作的寄存器
    {
        for (uint8_t i = 0; i < cnt; i++)
        {
            if (platform_i2c_write_byte(value[i]) != PLATFORM_I2C_ACK) // 写入数据
            {
                status = BSP_ERROR;
                break; // 写入失败，退出循环
            }
        }
    }
    else
    {
        status = BSP_ERROR; // 写入寄存器地址失败
    }
    platform_i2c_stop();

    return status;
}

/*************************************************************************************************************************************
 *    函 数 名:    bsp_touch_read_registers
 *    入口参数:    addr - 要读取的寄存器区域首地址
 *                    cnt  - 数据长度
 *                    value - 要读取的数据区
 *    返 回 值:    BSP_OK - 操作成功
 *                    BSP_ERROR      - 操作失败
 *    函数功能:    GT9XX 读寄存器
 *    说    明:    从芯片的寄存器区读取指定长度的数据
 ************************************************************************************************************************************/
static bsp_status_t bsp_touch_read_registers(uint16_t addr, uint8_t cnt, uint8_t *value)
{
    bsp_status_t status = BSP_ERROR;

    if (bsp_touch_send_register_address(addr) == BSP_OK) // 写入要操作的寄存器
    {
        platform_i2c_start(); // 重新启动I2C通讯
        if (platform_i2c_write_byte(GT911_I2C_RADDR) == PLATFORM_I2C_ACK) // 发送读命令
        {
            for (uint8_t i = 0; i < cnt; i++) // 计数
            {
                if (i == (cnt - 1))
                {
                    value[i] = platform_i2c_read_byte(PLATFORM_I2C_NACK); // 读到最后一个数据时发送 非应答信号
                }
                else
                {
                    value[i] = platform_i2c_read_byte(PLATFORM_I2C_ACK); // 发送应答信号
                }
            }
            status = BSP_OK;
        }
    }
    platform_i2c_stop(); // 停止通信

    return status;
}

static void bsp_touch_gpio_init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOI_CLK_ENABLE(); // 初始化时钟

    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;   // 推挽输出
    gpio_init.Pull = GPIO_PULLUP;           // 上拉
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;  // 速度等级
    gpio_init.Pin = GT911_INT_PIN;          // INT
    HAL_GPIO_Init(GT911_INT_PORT, &gpio_init);
    gpio_init.Pin = GT911_RST_PIN;          // RST
    HAL_GPIO_Init(GT911_RST_PORT, &gpio_init);

    HAL_GPIO_WritePin(GT911_INT_PORT, GT911_INT_PIN, GPIO_PIN_RESET); // INT输出低电平
    HAL_GPIO_WritePin(GT911_RST_PORT, GT911_RST_PIN, GPIO_PIN_SET);   // RST输出高电平
}

/*************************************************************************************************************************************
 *    函 数 名: bsp_touch_init
 *    入口参数: 无
 *    返 回 值: BSP_OK  - 初始化成功
 *            BSP_ERROR      - 错误，未检测到触摸屏
 *    函数功能: 触摸IC初始化，并读取相应信息发送到串口
 *    说    明: 初始化触摸面板
 ************************************************************************************************************************************/
bsp_status_t bsp_touch_init(void)
{
    uint8_t info[11];      // 触摸屏IC信息
    uint8_t version = 0;   // 触摸配置版本

    platform_i2c_init();   // 初始化I2C引脚
    bsp_touch_gpio_init(); // 初始化触摸屏的GPIO口
    bsp_touch_reset();     // 复位触摸IC
    if (bsp_touch_read_registers(GT911_ID_ADDR, 11, info) != BSP_OK) // 读触摸屏IC信息
    {
        return BSP_ERROR;
    }
    if (bsp_touch_read_registers(GT911_CFG_ADDR, 1, &version) != BSP_OK) // 读触摸配置版本
    {
        return BSP_ERROR;
    }

    if (info[0] == '9') // 判断第一个字符是否为 9
    {
        printf("Touch ID: GT%.4s \r\n", info);  // 打印触摸芯片的ID
        printf("Firmware Version: 0X%.4x\r\n", (info[5] << 8) + info[4]);  // 芯片固件版本
        printf("Touch Resolution: %d * %d\r\n", (info[7] << 8) + info[6], (info[9] << 8) + info[8]); // 当前触摸分辨率
        printf("Touch Parameter Configuration Version: 0X%.2x \r\n", version);  // 触摸配置版本
        return BSP_OK; // 初始化成功
    }
    else
    {
        printf("Touch Error\r\n"); // 错误，未检测到触摸屏
        return BSP_ERROR;
    }
}

/*****************************************************************************************
 *    函 数 名: bsp_touch_int_config_output
 *    入口参数: 无
 *    返 回 值: 无
 *    函数功能: 配置I2C的INT脚为输出模式
 *    说    明: 无
 ******************************************************************************************/
static void bsp_touch_int_config_output(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;  // 输出模式
    gpio_init.Pull = GPIO_PULLUP;          // 上拉
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW; // 速度等级
    gpio_init.Pin = GT911_INT_PIN;         // 初始化 INT 引脚

    HAL_GPIO_Init(GT911_INT_PORT, &gpio_init);
}

/*****************************************************************************************
 *    函 数 名: bsp_touch_int_config_input
 *    入口参数: 无
 *    返 回 值: 无
 *    函数功能: 配置I2C的INT脚为输入模式
 *    说    明: 无
 ******************************************************************************************/
static void bsp_touch_int_config_input(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    gpio_init.Mode = GPIO_MODE_INPUT;      // 输入模式
    gpio_init.Pull = GPIO_NOPULL;          // 浮空
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW; // 速度等级
    gpio_init.Pin = GT911_INT_PIN;         // 初始化 INT 引脚

    HAL_GPIO_Init(GT911_INT_PORT, &gpio_init);
}

/*************************************************************************************************************************************
 *    函 数 名: bsp_touch_read_data
 *    入口参数: 无
 *    返 回 值: 无
 *    函数功能: 触摸扫描
 *    说    明: 在程序里周期性的调用该函数，用以检测触摸操作，触摸信息存储在 touch_info 结构体
 ************************************************************************************************************************************/
bsp_status_t bsp_touch_read_data(bsp_touch_info_t *touch_info)
{
    uint8_t touch_data[2 + 8 * BSP_TOUCH_MAX_POINTS]; // 用于存储触摸数据

    if (bsp_touch_read_registers(GT911_READ_ADDR, 2 + 8 * BSP_TOUCH_MAX_POINTS, touch_data) != BSP_OK) // 读数据
    {
        return BSP_ERROR;
    }
    if (bsp_touch_write_register(GT911_READ_ADDR, 0) != BSP_OK)  // 清除触摸芯片的寄存器标志位
    {
        return BSP_ERROR;
    }
    touch_info->num = touch_data[0] & 0x0f;  // 取当前的触摸点数

    if ((touch_info->num >= 1) && (touch_info->num <= BSP_TOUCH_MAX_POINTS))
    {
        for (uint8_t i = 0; i < touch_info->num; i++) // 取相应的触摸坐标
        {
            touch_info->y[i] = (touch_data[5 + 8 * i] << 8) | touch_data[4 + 8 * i]; // 获取Y坐标
            touch_info->x[i] = (touch_data[3 + 8 * i] << 8) | touch_data[2 + 8 * i]; // 获取X坐标
        }
        touch_info->flag = 1; // 触摸标志位置1，代表有触摸动作发生
    }
    else
    {
        touch_info->flag = 0; // 触摸标志位置0，无触摸动作
    }
    return BSP_OK;
}

bool bsp_touch_is_pressed(void)
{
    uint8_t touch_data[2 + 8 * BSP_TOUCH_MAX_POINTS]; // 用于存储触摸数据

    if (bsp_touch_read_registers(GT911_READ_ADDR, sizeof(touch_data), touch_data) != BSP_OK) // 读数据
    {
        return false;
    }
    bsp_touch_write_register(GT911_READ_ADDR, 0);    // 清除触摸芯片的寄存器标志位
    touch_info.num = touch_data[0] & 0x0f;    // 取当前的触摸点数

    if (touch_info.num >= 1) // 当触摸数大于等于1时
    {
        touch_info.y[0] = (touch_data[5] << 8) | touch_data[4]; // 获取Y坐标
        touch_info.x[0] = (touch_data[3] << 8) | touch_data[2]; // 获取X坐标
        return true;  // 代表有触摸动作发生
    }
    else
    {
        return false; // 无触摸动作
    }
}

void bsp_touch_get_position(uint16_t *x, uint16_t *y)
{
    *y = touch_info.y[0];    // 获取Y坐标
    *x = touch_info.x[0];    // 获取X坐标
}

