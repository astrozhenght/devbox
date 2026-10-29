#include "i2c.h"
#include "stm32h7xx_hal.h"

#define I2C_SCL_PORT            GPIOG       // SCL 引脚端口
#define I2C_SCL_PIN             GPIO_PIN_3  // SCL 引脚
#define I2C_SDA_PORT            GPIOG       // SDA 引脚端口
#define I2C_SDA_PIN             GPIO_PIN_7  // SDA 引脚
#define I2C_DELAY_VALUE         20          // IIC通信延时，20对应时钟速度100KHz左右

#define I2C_SCL_WRITE(a)                                                     \
    do {                                                                     \
        if (a) HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_SET);   \
        else   HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_RESET); \
    } while(0)

#define I2C_SDA_WRITE(a)                                                     \
    do {                                                                     \
        if (a) HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_SET);   \
        else   HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_RESET); \
    } while(0)

/*****************************************************************************************
 *    函 数 名: platform_i2c_init
 *    入口参数: 无
 *    返 回 值: 无
 *    函数功能: 初始化IIC的GPIO口，SDA和SCL配置为开漏输出无上下拉，INT和RST配置为推挽输出上拉。
 *    说    明: 由于IIC通信速度不高，这里的IO口速度配置为2M即可
 ******************************************************************************************/
void platform_i2c_init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOG_CLK_ENABLE();           // 使能GPIO时钟

    gpio_init.Pin   = I2C_SCL_PIN;          // SCL引脚
    gpio_init.Mode  = GPIO_MODE_OUTPUT_OD;  // 开漏输出
    gpio_init.Pull  = GPIO_NOPULL;          // 不带上下拉
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;  // 速度等级
    HAL_GPIO_Init(I2C_SCL_PORT, &gpio_init);
    gpio_init.Pin   = I2C_SDA_PIN;          // SDA引脚
    HAL_GPIO_Init(I2C_SDA_PORT, &gpio_init);

    HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_SET); // SCL输出高电平
    HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_SET); // SDA输出高电平
}

/*****************************************************************************************
 *    函 数 名: platform_i2c_delay
 *    入口参数: a - 延时时间
 *    返 回 值: 无
 *    函数功能: 简单延时函数
 *    说    明: 为了移植的简便性且对延时精度要求不高，所以不需要使用定时器做延时
 ******************************************************************************************/

void platform_i2c_delay(uint32_t delay)
{
    volatile uint16_t i;
    while (delay--)
    {
        for (i = 0; i < 8; i++);
    }
    // HAL_Delay(delay); // 单位ms
}


/*****************************************************************************************
 *    函 数 名: platform_i2c_start
 *    入口参数: 无
 *    返 回 值: 无
 *    函数功能: IIC起始信号
 *    说    明: 在SCL处于高电平期间，SDA由高到低跳变为起始信号
 ******************************************************************************************/
void platform_i2c_start(void)
{
    I2C_SDA_WRITE(1);
    I2C_SCL_WRITE(1);
    platform_i2c_delay(I2C_DELAY_VALUE);
    I2C_SDA_WRITE(0);
    platform_i2c_delay(I2C_DELAY_VALUE);
    I2C_SCL_WRITE(0);
    platform_i2c_delay(I2C_DELAY_VALUE);
}

/*****************************************************************************************
 *    函 数 名: platform_i2c_stop
 *    入口参数: 无
 *    返 回 值: 无
 *    函数功能: IIC停止信号
 *    说    明: 在SCL处于高电平期间，SDA由低到高跳变为起始信号
 ******************************************************************************************/

void platform_i2c_stop(void)
{
    I2C_SCL_WRITE(0);
    platform_i2c_delay(I2C_DELAY_VALUE);
    I2C_SDA_WRITE(0);
    platform_i2c_delay(I2C_DELAY_VALUE);
    I2C_SCL_WRITE(1);
    platform_i2c_delay(I2C_DELAY_VALUE);
    I2C_SDA_WRITE(1);
    platform_i2c_delay(I2C_DELAY_VALUE);
}

/*****************************************************************************************
 *    函 数 名: platform_i2c_ack
 *    入口参数: 无
 *    返 回 值: 无
 *    函数功能: IIC应答信号
 *    说    明: 在SCL为高电平期间，SDA引脚输出为低电平，产生应答信号
 ******************************************************************************************/

void platform_i2c_ack(void)
{
    I2C_SCL_WRITE(0);
    platform_i2c_delay(I2C_DELAY_VALUE);
    I2C_SDA_WRITE(0);
    platform_i2c_delay(I2C_DELAY_VALUE);
    I2C_SCL_WRITE(1);
    platform_i2c_delay(I2C_DELAY_VALUE);
    I2C_SCL_WRITE(0); // SCL输出低时，SDA应立即拉高，释放总线
    I2C_SDA_WRITE(1);
    platform_i2c_delay(I2C_DELAY_VALUE);
}

/*****************************************************************************************
 *    函 数 名: platform_i2c_nack
 *    入口参数: 无
 *    返 回 值: 无
 *    函数功能: IIC非应答信号
 *    说    明: 在SCL为高电平期间，若SDA引脚为高电平，产生非应答信号
 ******************************************************************************************/

void platform_i2c_nack(void)
{
    I2C_SCL_WRITE(0);
    platform_i2c_delay(I2C_DELAY_VALUE);
    I2C_SDA_WRITE(1);
    platform_i2c_delay(I2C_DELAY_VALUE);
    I2C_SCL_WRITE(1);
    platform_i2c_delay(I2C_DELAY_VALUE);
    I2C_SCL_WRITE(0);
    platform_i2c_delay(I2C_DELAY_VALUE);
}

/*****************************************************************************************
 *    函 数 名: platform_i2c_wait_ack
 *    入口参数: 无
 *    返 回 值: 无
 *    函数功能: 等待接收设备发出应答信号
 *    说    明: 在SCL为高电平期间，若检测到SDA引脚为低电平，则接收设备响应正常
 ******************************************************************************************/

platform_i2c_ack_t platform_i2c_wait_ack(void)
{
    I2C_SDA_WRITE(1);
    platform_i2c_delay(I2C_DELAY_VALUE);
    I2C_SCL_WRITE(1);
    platform_i2c_delay(I2C_DELAY_VALUE);
    if (HAL_GPIO_ReadPin(I2C_SDA_PORT, I2C_SDA_PIN) != 0) // 判断设备是否有做出响应
    {
        I2C_SCL_WRITE(0);
        platform_i2c_delay(I2C_DELAY_VALUE);
        return PLATFORM_I2C_NACK; // 无应答
    }
    else
    {
        I2C_SCL_WRITE(0);
        platform_i2c_delay(I2C_DELAY_VALUE);
        return PLATFORM_I2C_ACK; // 应答正常
    }
}

/*****************************************************************************************
 *    函 数 名:    platform_i2c_write_byte
 *    入口参数:    data - 要写入的8位数据
 *    返 回 值:    PLATFORM_I2C_ACK  - 设备响应正常
 *                PLATFORM_I2C_NACK - 设备响应错误
 *    函数功能:    写一字节数据
 *    说    明:高位在前
 ******************************************************************************************/

platform_i2c_ack_t platform_i2c_write_byte(uint8_t data)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        I2C_SDA_WRITE(data & 0x80);
        platform_i2c_delay(I2C_DELAY_VALUE);
        I2C_SCL_WRITE(1);
        platform_i2c_delay(I2C_DELAY_VALUE);
        I2C_SCL_WRITE(0);
        if (i == 7)
        {
            I2C_SDA_WRITE(1);
        }
        data <<= 1;
    }
    return platform_i2c_wait_ack(); // 等待设备响应
}

/*****************************************************************************************
 *    函 数 名:    platform_i2c_read_byte
 *    入口参数:    is_ack - 响应模式，输入1则发出应答信号，输入0发出非应答信号
 *    返 回 值:    读到的8位数据
 *    函数功能:读一字节数据
 *    说    明:1.高位在前
 *                2.应在主机接收最后一字节数据时发送非应答信号
 ******************************************************************************************/
uint8_t platform_i2c_read_byte(platform_i2c_ack_t is_ack)
{
    uint8_t data = 0;

    for (uint8_t i = 0; i < 8; i++)
    {
        data <<= 1;
        I2C_SCL_WRITE(1);
        platform_i2c_delay(I2C_DELAY_VALUE);
        data |= (HAL_GPIO_ReadPin(I2C_SDA_PORT, I2C_SDA_PIN) & 0x01);
        I2C_SCL_WRITE(0);
        platform_i2c_delay(I2C_DELAY_VALUE);
    }
    if (is_ack == PLATFORM_I2C_ACK) // 应答信号
    {
        platform_i2c_ack();
    }
    else
    {
        platform_i2c_nack(); // 非应答
    }

    return data;
}

