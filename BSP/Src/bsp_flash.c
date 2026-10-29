/****
    ***********************************************************************************************************************************************************************************
    *    @version V1.0
    *    @date    2023-12-13
    *    @author  反客科技
   *************************************************************************************************
   *  @description
    *
    *    实验平台：反客STM32H743XIH6核心板 （型号：FK743M5-XIH6）
    *    淘宝地址：https://shop212360197.taobao.com
    *    QQ交流群：536665479
    *
>>>>> 文件说明：
    *
    *  1.例程参考于官方驱动文件 stm32h743i_eval_qspi.c
    *    2.例程使用的是 QUADSPI_BK1
    *    3.提供的读写函数均使用HAL库函数直接操作，没有用到DMA和中断
    *    4.默认配置QSPI驱动时钟为120M
    *
>>>>> 重要说明：
    *
    *    1.W25QXX的擦除时间是限定的!!! 手册给出的典型参考值为: 4K-45ms, 32K-120ms ,64K-150ms,整片擦除20S
    *
    *    2.W25QXX的写入时间是限定的!!! 手册给出的典型参考值为: 256字节-0.4ms，也就是 1M字节/s （实测大概在600K字节/s左右）
    *
    *    3.如果使用库函数直接读取，那么是否使用DMA、是否开启Cache、编译器的优化等级以及数据存储区的位置(内部 TCM SRAM 或者 AXI SRAM)都会影响读取的速度
    *
    *    4.如果使用内存映射模式，则读取性能只与QSPI的驱动时钟以及是否开启Cache有关
    *
    *    5.使用库函数进行直接读取，keil版本5.30，编译器AC6.14，编译等级Oz image size，读取速度为 7M字节/S ，数据放在TCM SRAM 或者 AXI SRAM
    *    都是差不多的结果，因为CPU直接访问外设寄存器的效率很低，直接使用HAL库进行读取的话，速度很慢
    *
    *    6.如果使用MDMA进行读取，可以达到 58M字节/S，使用内存映射模式的话，几乎可以达到驱动时钟的全速，62.14M/s (133MHz时钟下)
    *
    *    7.W25Q64JV 所允许的最高驱动频率为133MHz，750的QSPI最高驱动频率也是133MHz ，但是对于HAL库函数直接读取而言，驱动时钟超过15M已经不会有性能提升
    *
    *    8.对于内存映射模式直接读取而言，驱动时钟超过127.5M已经不会有性能提升，因为QSPI内核时钟最高限定为250M，所以建议实际QSPI驱动时钟不要超过125M，
    *      具体的时钟配置请参考 SystemClock_Config 函数
    *
    *    9.实际使用中，当数据比较大时，建议使用64K或者32K擦除，擦除时间比4K擦除块
    *
    **************************************************************************************************************************************************************************************FANKE*****
***/
#include "bsp_flash.h"
#include "qspi.h"

/* W25Q64操作命令 */
#define W25Q_CMD_ENABLE_RESET             0x66U  // 使能复位
#define W25Q_CMD_RESET_DEVICE             0x99U  // 复位器件
#define W25Q_CMD_READ_JEDEC_ID            0x9FU  // 读取 JEDEC ID
#define W25Q_CMD_WRITE_ENABLE             0x06U  // 写使能
#define W25Q_CMD_SECTOR_ERASE_4K          0x20U  // 4 KB 扇区擦除，参考时间 45 ms
#define W25Q_CMD_BLOCK_ERASE_32K          0x52U  // 32 KB 块擦除，参考时间 120 ms
#define W25Q_CMD_BLOCK_ERASE_64K          0xD8U  // 64 KB 块擦除，参考时间 150 ms
#define W25Q_CMD_CHIP_ERASE               0xC7U  // 整片擦除，参考时间 20 s
#define W25Q_CMD_QUAD_INPUT_PAGE_PROGRAM  0x32U  // 1-1-4模式下(1线指令1线地址4线数据)，页编程指令，参考写入时间 0.4ms
#define W25Q_CMD_FAST_READ_QUAD_IO        0xEBU  // 1-4-4模式下(1线指令4线地址4线数据)，快速读取指令
#define W25Q_CMD_READ_STATUS_REG1         0x05U  // 读状态寄存器1

/* 状态寄存器1位掩码 */
#define W25Q_STATUS_REG1_BUSY_Msk         0x01U  // 读状态寄存器1的第0位（只读），Busy标志位，当正在擦除/写入数据/写命令时会被置1
#define W25Q_STATUS_REG1_WEL_Msk          0x02U  // 读状态寄存器1的第1位（只读），WEL写使能标志位，该标志位为1时，代表可以进行写操作

/* W25Q64设备参数 */
#define W25Q_PAGE_SIZE                    256U          // 页大小，256字节
#define W25Q_FLASH_SIZE                   0x800000UL    // W25Q64大小，8M字节
#define W25Q_JEDEC_ID                     0xEF4017UL    // W25Q64 ID
#define W25Q_CHIP_ERASE_TIMEOUT_MS        100000UL      // 超时等待时间，W25Q64整片擦除所需最大时间是100S
#define W25Q_MEMORY_MAPPED_ADDRESS        0x90000000UL  // 内存映射模式的地址

// 内部函数声明
static bsp_flash_result_t bsp_flash_reset(void);
static bsp_flash_result_t bsp_flash_write_enable(void);
static bsp_flash_result_t bsp_flash_wait_ready(uint32_t timeout_ms);
static bsp_flash_result_t bsp_flash_write_page(uint8_t *buffer, uint32_t address, uint16_t size);


bsp_flash_result_t bsp_flash_init(void)
{
    uint32_t flash_id = 0U;  // 器件ID
    bsp_flash_result_t result;

    if (platform_qspi_init() != HAL_OK)
    {
        return BSP_FLASH_ERROR_INIT;
    }

    result = bsp_flash_reset(); // 复位器件
    if (result != BSP_FLASH_OK)
    {
        return result;
    }

    result = bsp_flash_read_id(&flash_id); // 读取器件ID
    if (result != BSP_FLASH_OK)
    {
        return result;
    }

    if (flash_id == W25Q_JEDEC_ID) // 进行匹配
    {
        return BSP_FLASH_OK;  // 返回成功标志
    }
    else
    {
        return BSP_FLASH_ERROR_ID_MISMATCH;   // 返回错误
    }
}

/*************************************************************************************************
 *    函 数 名: bsp_flash_wait_ready
 *    入口参数: 无
 *    返 回 值: BSP_FLASH_OK - 通信正常结束，W25Qxx_ERROR_AUTOPOLLING - 轮询等待无响应
 *    函数功能: 使用自动轮询标志查询，等待通信结束
 *    说    明: 每一次通信都应该调用次函数，等待通信结束，避免错误的操作
 **************************************************************************************************/

static bsp_flash_result_t bsp_flash_wait_ready(uint32_t timeout_ms)
{
    QSPI_CommandTypeDef command = {0};    // QSPI传输配置
    QSPI_AutoPollingTypeDef config = {0}; // 轮询比较相关配置参数

    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;     // 1线指令模式
    command.AddressMode = QSPI_ADDRESS_NONE;               // 无地址模式
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE; // 无交替字节
    command.DdrMode = QSPI_DDR_MODE_DISABLE;               // 禁止DDR模式
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;  // DDR模式中数据延迟，这里用不到
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;           // 每次传输数据都发送指令
    command.DataMode = QSPI_DATA_1_LINE;                   // 1线数据模式
    command.DummyCycles = 0;                               // 空周期个数
    command.Instruction = W25Q_CMD_READ_STATUS_REG1;       // 读状态信息寄存器

    // 不停的查询 W25Q_CMD_READ_STATUS_REG1 寄存器，将读取到的状态字节中的 W25Q_STATUS_REG1_BUSY_Msk 不停的与0作比较
    // 读状态寄存器1的第0位（只读），Busy标志位，当正在擦除/写入数据/写命令时会被置1，空闲或通信结束为0
    config.Match = 0;                                  // 匹配值
    config.MatchMode = QSPI_MATCH_MODE_AND;            // 与运算
    config.Interval = 0x10;                            // 轮询间隔
    config.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE; // 自动停止模式
    config.StatusBytesSize = 1;                        // 状态字节数
    config.Mask = W25Q_STATUS_REG1_BUSY_Msk;           // 对在轮询模式下接收的状态字节进行屏蔽，只比较需要用到的位

    // 发送轮询等待命令
    if (HAL_QSPI_AutoPolling(&hqspi, &command, &config, timeout_ms) != HAL_OK)
    {
        return BSP_FLASH_ERROR_AUTOPOLLING; // 轮询等待无响应
    }
    return BSP_FLASH_OK; // 通信正常结束
}

/*************************************************************************************************
 *    函 数 名: bsp_flash_reset
 *    入口参数: 无
 *    返 回 值: BSP_FLASH_OK - 复位成功，W25Qxx_ERROR_INIT - 初始化错误
 *    函数功能: 复位器件
 *    说    明: 无
 *************************************************************************************************/
static bsp_flash_result_t bsp_flash_reset(void)
{
    QSPI_CommandTypeDef command = {0}; // QSPI传输配置

    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;     // 1线指令模式
    command.AddressMode = QSPI_ADDRESS_NONE;               // 无地址模式
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE; // 无交替字节
    command.DdrMode = QSPI_DDR_MODE_DISABLE;               // 禁止DDR模式
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;  // DDR模式中数据延迟，这里用不到
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;           // 每次传输数据都发送指令
    command.DataMode = QSPI_DATA_NONE;                     // 无数据模式
    command.DummyCycles = 0;                               // 空周期个数
    command.Instruction = W25Q_CMD_ENABLE_RESET;           // 执行复位使能命令

    // 发送复位使能命令
    if (HAL_QSPI_Command(&hqspi, &command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return BSP_FLASH_ERROR_INIT; // 如果发送失败，返回错误信息
    }
    // 使用自动轮询标志位，等待通信结束
    if (bsp_flash_wait_ready(HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != BSP_FLASH_OK)
    {
        return BSP_FLASH_ERROR_AUTOPOLLING; // 轮询等待无响应
    }

    command.Instruction = W25Q_CMD_RESET_DEVICE; // 复位器件命令

    // 发送复位器件命令
    if (HAL_QSPI_Command(&hqspi, &command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return BSP_FLASH_ERROR_INIT; // 如果发送失败，返回错误信息
    }
    // 使用自动轮询标志位，等待通信结束
    if (bsp_flash_wait_ready(HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != BSP_FLASH_OK)
    {
        return BSP_FLASH_ERROR_AUTOPOLLING; // 轮询等待无响应
    }
    return BSP_FLASH_OK; // 复位成功
}

/*************************************************************************************************
 *    函 数 名: bsp_flash_read_id
 *    入口参数: W25Qxx_ID - 读取到的器件ID
 *    返 回 值: BSP_FLASH_ERROR_INIT - 通信、初始化错误
 *    函数功能: 初始化 QSPI 配置，读取器件ID
 *    说    明: 无
 **************************************************************************************************/

bsp_flash_result_t bsp_flash_read_id(uint32_t *flash_id)
{
    QSPI_CommandTypeDef command = {0};  // QSPI传输配置
    uint8_t QSPI_ReceiveBuff[3];  // 存储QSPI读到的数据

    if (flash_id == NULL)
    {
        return BSP_FLASH_ERROR_INVALID_ARGUMENT;
    }

    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;     // 1线指令模式
    command.AddressSize = QSPI_ADDRESS_24_BITS;            // 24位地址
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE; // 无交替字节
    command.DdrMode = QSPI_DDR_MODE_DISABLE;               // 禁止DDR模式
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;  // DDR模式中数据延迟，这里用不到
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;           // 每次传输数据都发送指令
    command.AddressMode = QSPI_ADDRESS_NONE;               // 无地址模式
    command.DataMode = QSPI_DATA_1_LINE;                   // 1线数据模式
    command.DummyCycles = 0;                               // 空周期个数
    command.NbData = 3;                                    // 传输数据的长度
    command.Instruction = W25Q_CMD_READ_JEDEC_ID;          // 执行读器件ID命令

    // 发送指令
    if (HAL_QSPI_Command(&hqspi, &command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return BSP_FLASH_ERROR_INIT;      // 如果发送失败，返回错误信息
    }
    // 接收数据
    if (HAL_QSPI_Receive(&hqspi, QSPI_ReceiveBuff, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return BSP_FLASH_ERROR_TRANSMIT;  // 如果接收失败，返回错误信息
    }
    // 将得到的数据组合成ID
    *flash_id = (QSPI_ReceiveBuff[0] << 16) | (QSPI_ReceiveBuff[1] << 8) | QSPI_ReceiveBuff[2];

    return BSP_FLASH_OK; // 接收成功
}

/*************************************************************************************************
 *    函 数 名: bsp_flash_enable_memory_mapped
 *    入口参数: 无
 *    返 回 值: BSP_FLASH_OK - 写使能成功，W25Qxx_ERROR_WriteEnable - 写使能失败
 *    函数功能: 将QSPI设置为内存映射模式
 *    说    明: 设置为内存映射模式时，只能读，不能写！！！
 **************************************************************************************************/

bsp_flash_result_t bsp_flash_enable_memory_mapped(void)
{
    QSPI_CommandTypeDef command = {0};     // QSPI传输配置
    QSPI_MemoryMappedTypeDef config = {0}; // 内存映射访问参数

    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;     // 1线指令模式
    command.AddressSize = QSPI_ADDRESS_24_BITS;            // 24位地址
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE; // 无交替字节
    command.DdrMode = QSPI_DDR_MODE_DISABLE;               // 禁止DDR模式
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;  // DDR模式中数据延迟，这里用不到
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;           // 每次传输数据都发送指令
    command.AddressMode = QSPI_ADDRESS_4_LINES;            // 4线地址模式
    command.DataMode = QSPI_DATA_4_LINES;                  // 4线数据模式
    command.DummyCycles = 6;                               // 空周期个数
    command.Instruction = W25Q_CMD_FAST_READ_QUAD_IO;      // 1-4-4模式下(1线指令4线地址4线数据)，快速读取指令

    config.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE; // 禁用超时计数器, nCS 保持激活状态
    config.TimeOutPeriod = 0; // 超时判断周期

    bsp_flash_reset(); // 复位W25Qxx

    if (HAL_QSPI_MemoryMapped(&hqspi, &command, &config) != HAL_OK) // 进行配置
    {
        return BSP_FLASH_ERROR_MEMORY_MAPPED; // 设置内存映射模式错误
    }

    return BSP_FLASH_OK; // 配置成功
}

/*************************************************************************************************
 *    函 数 名: bsp_flash_write_enable
 *    入口参数: 无
 *    返 回 值: BSP_FLASH_OK - 写使能成功，W25Qxx_ERROR_WriteEnable - 写使能失败
 *    函数功能: 发送写使能命令
 *    说    明: 无
 **************************************************************************************************/

static bsp_flash_result_t bsp_flash_write_enable(void)
{
    QSPI_CommandTypeDef command = {0};    // QSPI传输配置
    QSPI_AutoPollingTypeDef config = {0}; // 轮询比较相关配置参数

    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;     // 1线指令模式
    command.AddressMode = QSPI_ADDRESS_NONE;               // 无地址模式
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE; // 无交替字节
    command.DdrMode = QSPI_DDR_MODE_DISABLE;               // 禁止DDR模式
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;  // DDR模式中数据延迟，这里用不到
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;           // 每次传输数据都发送指令
    command.DataMode = QSPI_DATA_NONE;                     // 无数据模式
    command.DummyCycles = 0;                               // 空周期个数
    command.Instruction = W25Q_CMD_WRITE_ENABLE;           // 发送写使能命令

    // 发送写使能命令
    if (HAL_QSPI_Command(&hqspi, &command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return BSP_FLASH_ERROR_WRITE_ENABLE; //
    }

    // 不停的查询 W25Q_CMD_READ_STATUS_REG1 寄存器，将读取到的状态字节中的 W25Q_STATUS_REG1_WEL_Msk 不停的与 0x02 作比较
    // 读状态寄存器1的第1位（只读），WEL写使能标志位，该标志位为1时，代表可以进行写操作

    config.Match = 0x02;                      // 匹配值
    config.Mask = W25Q_STATUS_REG1_WEL_Msk;   // 读状态寄存器1的第1位（只读），WEL写使能标志位，该标志位为1时，代表可以进行写操作
    config.MatchMode = QSPI_MATCH_MODE_AND;   // 与运算
    config.StatusBytesSize = 1;               // 状态字节数
    config.Interval = 0x10;                   // 轮询间隔
    config.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE; // 自动停止模式

    command.Instruction = W25Q_CMD_READ_STATUS_REG1;   // 读状态信息寄存器
    command.DataMode = QSPI_DATA_1_LINE;               // 1线数据模式
    command.NbData = 1;                                // 数据长度

    // 发送轮询等待命令
    if (HAL_QSPI_AutoPolling(&hqspi, &command, &config, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return BSP_FLASH_ERROR_AUTOPOLLING; // 轮询等待无响应
    }
    return BSP_FLASH_OK; // 通信正常结束
}

/*************************************************************************************************
 *
 *    函 数 名: bsp_flash_erase_sector
 *
 *    入口参数: address - 要擦除的地址
 *
 *    返 回 值: BSP_FLASH_OK - 擦除成功
 *                BSP_FLASH_ERROR_ERASE - 擦除失败
 *                 BSP_FLASH_ERROR_AUTOPOLLING - 轮询等待无响应
 *
 *    函数功能: 进行扇区擦除操作，每次擦除4K字节
 *
 *    说    明: 1.按照 W25Q64JV 数据手册给出的擦除参考时间，典型值为 45ms，最大值为400ms
 *                 2.实际的擦除速度可能大于45ms，也可能小于45ms
 *                 3.flash使用的时间越长，擦除所需时间也会越长
 *
 **************************************************************************************************/
bsp_flash_result_t bsp_flash_erase_sector(uint32_t address)
{
    QSPI_CommandTypeDef command = {0}; // QSPI传输配置

    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;     // 1线指令模式
    command.AddressSize = QSPI_ADDRESS_24_BITS;            // 24位地址模式
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE; // 无交替字节
    command.DdrMode = QSPI_DDR_MODE_DISABLE;               // 禁止DDR模式
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;  // DDR模式中数据延迟，这里用不到
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;           // 每次传输数据都发送指令
    command.AddressMode = QSPI_ADDRESS_1_LINE;             // 1线地址模式
    command.DataMode = QSPI_DATA_NONE;                     // 无数据
    command.DummyCycles = 0;                               // 空周期个数
    command.Address = address;                             // 要擦除的地址
    command.Instruction = W25Q_CMD_SECTOR_ERASE_4K;        // 扇区擦除命令

    // 发送写使能
    if (bsp_flash_write_enable() != BSP_FLASH_OK)
    {
        return BSP_FLASH_ERROR_WRITE_ENABLE; // 写使能失败
    }

    // 发出擦除命令
    if (HAL_QSPI_Command(&hqspi, &command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return BSP_FLASH_ERROR_ERASE; // 擦除失败
    }

    // 使用自动轮询标志位，等待擦除的结束
    if (bsp_flash_wait_ready(HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != BSP_FLASH_OK)
    {
        return BSP_FLASH_ERROR_AUTOPOLLING; // 轮询等待无响应
    }

    return BSP_FLASH_OK; // 擦除成功
}

/*************************************************************************************************
 *
 *    函 数 名: bsp_flash_erase_block_32k
 *
 *    入口参数: address - 要擦除的地址
 *
 *    返 回 值: BSP_FLASH_OK - 擦除成功
 *             BSP_FLASH_ERROR_ERASE - 擦除失败
 *             BSP_FLASH_ERROR_AUTOPOLLING - 轮询等待无响应
 *
 *    函数功能: 进行块擦除操作，每次擦除32K字节
 *
 *    说    明: 1.按照 W25Q64JV 数据手册给出的擦除参考时间，典型值为 120ms，最大值为1600ms
 *                 2.实际的擦除速度可能大于120ms，也可能小于120ms
 *                 3.flash使用的时间越长，擦除所需时间也会越长
 *
 *************************************************************************************************/

bsp_flash_result_t bsp_flash_erase_block_32k(uint32_t address)
{
    QSPI_CommandTypeDef command = {0}; // QSPI传输配置

    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;     // 1线指令模式
    command.AddressSize = QSPI_ADDRESS_24_BITS;            // 24位地址模式
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE; // 无交替字节
    command.DdrMode = QSPI_DDR_MODE_DISABLE;               // 禁止DDR模式
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;  // DDR模式中数据延迟，这里用不到
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;           // 每次传输数据都发送指令
    command.AddressMode = QSPI_ADDRESS_1_LINE;             // 1线地址模式
    command.DataMode = QSPI_DATA_NONE;                     // 无数据
    command.DummyCycles = 0;                               // 空周期个数
    command.Address = address;                             // 要擦除的地址
    command.Instruction = W25Q_CMD_BLOCK_ERASE_32K;        // 块擦除命令，每次擦除32K字节

    // 发送写使能
    if (bsp_flash_write_enable() != BSP_FLASH_OK)
    {
        return BSP_FLASH_ERROR_WRITE_ENABLE; // 写使能失败
    }

    // 发出擦除命令
    if (HAL_QSPI_Command(&hqspi, &command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return BSP_FLASH_ERROR_ERASE; // 擦除失败
    }

    // 使用自动轮询标志位，等待擦除的结束
    if (bsp_flash_wait_ready(HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != BSP_FLASH_OK)
    {
        return BSP_FLASH_ERROR_AUTOPOLLING; // 轮询等待无响应
    }

    return BSP_FLASH_OK; // 擦除成功
}

/*************************************************************************************************
 *
 *    函 数 名: bsp_flash_erase_block_64k
 *
 *    入口参数: address - 要擦除的地址
 *
 *    返 回 值: BSP_FLASH_OK - 擦除成功
 *                BSP_FLASH_ERROR_ERASE - 擦除失败
 *                 BSP_FLASH_ERROR_AUTOPOLLING - 轮询等待无响应
 *
 *    函数功能: 进行块擦除操作，每次擦除64K字节
 *
 *    说    明: 1.按照 W25Q64JV 数据手册给出的擦除参考时间，典型值为 150ms，最大值为2000ms
 *                 2.实际的擦除速度可能大于150ms，也可能小于150ms
 *                 3.flash使用的时间越长，擦除所需时间也会越长
 *                 4.实际使用建议使用64K擦除，擦除的时间最快
 *
 **************************************************************************************************/

bsp_flash_result_t bsp_flash_erase_block_64k(uint32_t address)
{
    QSPI_CommandTypeDef command = {0}; // QSPI传输配置

    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;     // 1线指令模式
    command.AddressSize = QSPI_ADDRESS_24_BITS;            // 24位地址模式
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE; // 无交替字节
    command.DdrMode = QSPI_DDR_MODE_DISABLE;               // 禁止DDR模式
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;  // DDR模式中数据延迟，这里用不到
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;           // 每次传输数据都发送指令
    command.AddressMode = QSPI_ADDRESS_1_LINE;             // 1线地址模式
    command.DataMode = QSPI_DATA_NONE;                     // 无数据
    command.DummyCycles = 0;                               // 空周期个数
    command.Address = address;                             // 要擦除的地址
    command.Instruction = W25Q_CMD_BLOCK_ERASE_64K;        // 块擦除命令，每次擦除64K字节

    // 发送写使能
    if (bsp_flash_write_enable() != BSP_FLASH_OK)
    {
        return BSP_FLASH_ERROR_WRITE_ENABLE; // 写使能失败
    }

    // 发出擦除命令
    if (HAL_QSPI_Command(&hqspi, &command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return BSP_FLASH_ERROR_ERASE; // 擦除失败
    }

    // 使用自动轮询标志位，等待擦除的结束
    if (bsp_flash_wait_ready(HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != BSP_FLASH_OK)
    {
        return BSP_FLASH_ERROR_AUTOPOLLING; // 轮询等待无响应
    }

    return BSP_FLASH_OK; // 擦除成功
}

/*************************************************************************************************
 *
 *    函 数 名: bsp_flash_erase_chip
 *
 *    入口参数: 无
 *
 *    返 回 值: BSP_FLASH_OK - 擦除成功
 *                BSP_FLASH_ERROR_ERASE - 擦除失败
 *                 BSP_FLASH_ERROR_AUTOPOLLING - 轮询等待无响应
 *
 *    函数功能: 进行整片擦除操作
 *
 *    说    明: 1.按照 W25Q64JV 数据手册给出的擦除参考时间，典型值为 20s，最大值为100s
 *                 2.实际的擦除速度可能大于20s，也可能小于20s
 *                 3.flash使用的时间越长，擦除所需时间也会越长
 *
 *************************************************************************************************/

bsp_flash_result_t bsp_flash_erase_chip(void)
{
    QSPI_CommandTypeDef command = {0};    // QSPI传输配置
    QSPI_AutoPollingTypeDef config = {0}; // 轮询等待配置参数

    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;     // 1线指令模式
    command.AddressSize = QSPI_ADDRESS_24_BITS;            // 24位地址模式
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE; // 无交替字节
    command.DdrMode = QSPI_DDR_MODE_DISABLE;               // 禁止DDR模式
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;  // DDR模式中数据延迟，这里用不到
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;           // 每次传输数据都发送指令
    command.AddressMode = QSPI_ADDRESS_NONE;               // 无地址
    command.DataMode = QSPI_DATA_NONE;                     // 无数据
    command.DummyCycles = 0;                               // 空周期个数
    command.Instruction = W25Q_CMD_CHIP_ERASE;             // 擦除命令，进行整片擦除

    // 发送写使能
    if (bsp_flash_write_enable() != BSP_FLASH_OK)
    {
        return BSP_FLASH_ERROR_WRITE_ENABLE; // 写使能失败
    }

    // 发出擦除命令
    if (HAL_QSPI_Command(&hqspi, &command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return BSP_FLASH_ERROR_ERASE; // 擦除失败
    }

    // 不停的查询 W25Q_CMD_READ_STATUS_REG1 寄存器，将读取到的状态字节中的 W25Q_STATUS_REG1_BUSY_Msk 不停的与0作比较
    // 读状态寄存器1的第0位（只读），Busy标志位，当正在擦除/写入数据/写命令时会被置1，空闲或通信结束为0

    config.Match = 0;                                  // 匹配值
    config.MatchMode = QSPI_MATCH_MODE_AND;            // 与运算
    config.Interval = 0x10;                            // 轮询间隔
    config.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE; // 自动停止模式
    config.StatusBytesSize = 1;                        // 状态字节数
    config.Mask = W25Q_STATUS_REG1_BUSY_Msk;           // 对在轮询模式下接收的状态字节进行屏蔽，只比较需要用到的位

    command.Instruction = W25Q_CMD_READ_STATUS_REG1;   // 读状态信息寄存器
    command.DataMode = QSPI_DATA_1_LINE;               // 1线数据模式
    command.NbData = 1;                                // 数据长度

    // W25Q64整片擦除的典型参考时间为20s，最大时间为100s，这里的超时等待值 W25Q_CHIP_ERASE_TIMEOUT_MS 为 100S
    if (HAL_QSPI_AutoPolling(&hqspi, &command, &config, W25Q_CHIP_ERASE_TIMEOUT_MS) != HAL_OK)
    {
        return BSP_FLASH_ERROR_AUTOPOLLING; // 轮询等待无响应
    }
    return BSP_FLASH_OK;
}

/**********************************************************************************************************
 *
 *    函 数 名: bsp_flash_write_page
 *
 *    入口参数: buffer          - 要写入的数据
 *                 address          - 要写入 W25Qxx 的地址
 *                 size - 数据长度，最大只能256字节
 *
 *    返 回 值: BSP_FLASH_OK              - 写数据成功
 *                BSP_FLASH_ERROR_WRITE_ENABLE - 写使能失败
 *                 BSP_FLASH_ERROR_TRANSMIT      - 传输失败
 *                 BSP_FLASH_ERROR_AUTOPOLLING - 轮询等待无响应
 *
 *    函数功能: 按页写入，最大只能256字节，在数据写入之前，请务必完成擦除操作
 *
 *    说    明: 1.Flash的写入时间和擦除时间一样，是限定的，并不是说QSPI驱动时钟133M就可以以这个速度进行写入
 *                 2.按照 W25Q64JV 数据手册给出的 页(256字节) 写入参考时间，典型值为 0.4ms，最大值为3ms
 *                 3.实际的写入速度可能大于0.4ms，也可能小于0.4ms
 *                 4.Flash使用的时间越长，写入所需时间也会越长
 *                 5.在数据写入之前，请务必完成擦除操作
 *
 ***********************************************************************************************************/

static bsp_flash_result_t bsp_flash_write_page(uint8_t *buffer, uint32_t address, uint16_t size)
{
    QSPI_CommandTypeDef command = {0}; // QSPI传输配置

    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;      // 1线指令模式
    command.AddressSize = QSPI_ADDRESS_24_BITS;             // 24位地址
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;  // 无交替字节
    command.DdrMode = QSPI_DDR_MODE_DISABLE;                // 禁止DDR模式
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;   // DDR模式中数据延迟，这里用不到
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;            // 每次传输数据都发送指令
    command.AddressMode = QSPI_ADDRESS_1_LINE;              // 1线地址模式
    command.DataMode = QSPI_DATA_4_LINES;                   // 4线数据模式
    command.DummyCycles = 0;                                // 空周期个数
    command.NbData = size;                                  // 数据长度，最大只能256字节
    command.Address = address;                              // 要写入 W25Qxx 的地址
    command.Instruction = W25Q_CMD_QUAD_INPUT_PAGE_PROGRAM; // 1-1-4模式下(1线指令1线地址4线数据)，页编程指令

    // 写使能
    if (bsp_flash_write_enable() != BSP_FLASH_OK)
    {
        return BSP_FLASH_ERROR_WRITE_ENABLE; // 写使能失败
    }

    // 写命令
    if (HAL_QSPI_Command(&hqspi, &command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return BSP_FLASH_ERROR_TRANSMIT; // 传输数据错误
    }

    // 开始传输数据
    if (HAL_QSPI_Transmit(&hqspi, buffer, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return BSP_FLASH_ERROR_TRANSMIT; // 传输数据错误
    }

    // 使用自动轮询标志位，等待写入的结束
    if (bsp_flash_wait_ready(HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != BSP_FLASH_OK)
    {
        return BSP_FLASH_ERROR_AUTOPOLLING; // 轮询等待无响应
    }

    return BSP_FLASH_OK; // 写数据成功
}

/**********************************************************************************************************
 *
 *    函 数 名: bsp_flash_write
 *
 *    入口参数: buffer          - 要写入的数据
 *                 address          - 要写入 W25Qxx 的地址
 *                 size - 数据长度，最大不能超过flash芯片的大小
 *
 *    返 回 值: BSP_FLASH_OK              - 写数据成功
 *                BSP_FLASH_ERROR_WRITE_ENABLE - 写使能失败
 *                 BSP_FLASH_ERROR_TRANSMIT      - 传输失败
 *                 BSP_FLASH_ERROR_AUTOPOLLING - 轮询等待无响应
 *
 *    函数功能: 写入数据，最大不能超过flash芯片的大小，请务必完成擦除操作
 *
 *    说    明: 1.Flash的写入时间和擦除时间一样，是有限定的，并不是说QSPI驱动时钟133M就可以以这个速度进行写入
 *                 2.按照 W25Q64JV 数据手册给出的 页 写入参考时间，典型值为 0.4ms，最大值为3ms
 *                 3.实际的写入速度可能大于0.4ms，也可能小于0.4ms
 *                 4.Flash使用的时间越长，写入所需时间也会越长
 *                 5.在数据写入之前，请务必完成擦除操作
 *                 6.该函数移植于 stm32h743i_eval_qspi.c
 *
 **********************************************************************************************************/

bsp_flash_result_t bsp_flash_write(const uint8_t *buffer, uint32_t address, uint32_t size)
{
    uint32_t end_addr, current_size, current_addr;
    uint8_t  *write_data; // 要写入的数据

    // 参数检查
    if ((buffer == NULL) || (size == 0U))
    {
        return BSP_FLASH_ERROR_INVALID_ARGUMENT;
    }
    if ((address >= W25Q_FLASH_SIZE) || (size > (W25Q_FLASH_SIZE - address)))
    {
        return BSP_FLASH_ERROR_OUT_OF_RANGE;
    }

    current_size = W25Q_PAGE_SIZE - (address % W25Q_PAGE_SIZE); // 计算当前页还剩余的空间

    if (current_size > size) // 判断当前页剩余的空间是否足够写入所有数据
    {
        current_size = size; // 如果足够，则直接获取当前长度
    }

    end_addr     = address + size;      // 计算结束地址
    write_data   = (uint8_t *)buffer;   // 获取要写入的数据
    current_addr = address;             // 获取要写入的地址

    do
    {
        // 发送写使能
        if (bsp_flash_write_enable() != BSP_FLASH_OK)
        {
            return BSP_FLASH_ERROR_WRITE_ENABLE;
        }
        // 按页写入数据
        else if (bsp_flash_write_page(write_data, current_addr, current_size) != BSP_FLASH_OK)
        {
            return BSP_FLASH_ERROR_TRANSMIT;
        }
        // 使用自动轮询标志位，等待写入的结束
        else if (bsp_flash_wait_ready(HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != BSP_FLASH_OK)
        {
            return BSP_FLASH_ERROR_AUTOPOLLING;
        }
        else // 按页写入数据成功，进行下一次写数据的准备工作
        {
            current_addr += current_size; // 计算下一次要写入的地址
            write_data += current_size;   // 获取下一次要写入的数据存储区地址
            current_size = ((current_addr + W25Q_PAGE_SIZE) > end_addr) ?  // 计算下一次写数据的长度
                            (end_addr - current_addr) : W25Q_PAGE_SIZE;
        }
    } while (current_addr < end_addr); // 判断数据是否全部写入完毕

    return BSP_FLASH_OK; // 写入数据成功
}

/**********************************************************************************************************************************
 *
 *    函 数 名: bsp_flash_read
 *
 *    入口参数: buffer          - 要读取的数据
 *                 ReadAddr          - 要读取 W25Qxx 的地址
 *                 NumByteToRead  - 数据长度，最大不能超过flash芯片的大小
 *
 *    返 回 值: BSP_FLASH_OK              - 读数据成功
 *                 BSP_FLASH_ERROR_TRANSMIT      - 传输失败
 *                 BSP_FLASH_ERROR_AUTOPOLLING - 轮询等待无响应
 *
 *    函数功能: 读取数据，最大不能超过flash芯片的大小
 *
 *    说    明: 1.Flash的读取速度取决于QSPI的通信时钟，最大不能超过133M
 *             2.这里使用的是1-4-4模式下(1线指令4线地址4线数据)，快速读取指令 Fast Read Quad I/O
 *             3.使用快速读取指令是有空周期的，具体参考W25Q64JV的手册  Fast Read Quad I/O  （0xEB）指令
 *             4.实际使用中，是否使用DMA、编译器的优化等级以及数据存储区的位置(内部 TCM SRAM 或者 AXI SRAM)都会影响读取的速度
 *             5.在本例程中，使用的是库函数进行直接读写，keil版本5.30，编译器AC6.14，编译等级Oz image size，读取速度为 7M字节/S ，
 *               数据放在 TCM SRAM 或者 AXI SRAM 都是差不多的结果
 *             6.因为CPU直接访问外设寄存器的效率很低，直接使用HAL库进行读写的话，速度很慢，使用MDMA进行读取，可以达到 58M字节/S
 *             7. W25Q64JV 所允许的最高驱动频率为133MHz，750的QSPI最高驱动频率也是133MHz ，但是对于HAL库函数直接读取而言，
 *               驱动时钟超过15M已经不会对性能有提升，对速度要求高的场合可以用MDMA的方式
 *
 *****************************************************************************************************************FANKE************/

bsp_flash_result_t bsp_flash_read(uint8_t *buffer, uint32_t address, uint32_t size)
{
    QSPI_CommandTypeDef command = {0}; // QSPI传输配置

    // 参数检查
    if ((buffer == NULL) || (size == 0U))
    {
        return BSP_FLASH_ERROR_INVALID_ARGUMENT;
    }
    if ((address >= W25Q_FLASH_SIZE) || (size > (W25Q_FLASH_SIZE - address)))
    {
        return BSP_FLASH_ERROR_OUT_OF_RANGE;
    }

    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;     // 1线指令模式
    command.AddressSize = QSPI_ADDRESS_24_BITS;            // 24位地址
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE; // 无交替字节
    command.DdrMode = QSPI_DDR_MODE_DISABLE;               // 禁止DDR模式
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;  // DDR模式中数据延迟，这里用不到
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;           // 每次传输数据都发送指令
    command.AddressMode = QSPI_ADDRESS_4_LINES;            // 4线地址模式
    command.DataMode = QSPI_DATA_4_LINES;                  // 4线数据模式
    command.DummyCycles = 6;                               // 空周期个数
    command.NbData = size;                                 // 数据长度，最大不能超过flash芯片的大小
    command.Address = address;                             // 要读取 W25Qxx 的地址
    command.Instruction = W25Q_CMD_FAST_READ_QUAD_IO;      // 1-4-4模式下(1线指令4线地址4线数据)，快速读取指令

    // 发送读取命令
    if (HAL_QSPI_Command(&hqspi, &command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return BSP_FLASH_ERROR_TRANSMIT; // 传输数据错误
    }

    // 接收数据
    if (HAL_QSPI_Receive(&hqspi, buffer, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return BSP_FLASH_ERROR_TRANSMIT; // 传输数据错误
    }

    // 使用自动轮询标志位，等待接收的结束
    if (bsp_flash_wait_ready(HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != BSP_FLASH_OK)
    {
        return BSP_FLASH_ERROR_AUTOPOLLING; // 轮询等待无响应
    }
    return BSP_FLASH_OK; // 读取数据成功
}

