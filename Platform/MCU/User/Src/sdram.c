#include "sdram.h"

static SDRAM_HandleTypeDef hsdram1;

#define FMC_COMMAND_TARGET_BANK                 FMC_SDRAM_CMD_TARGET_BANK1   // SDRAM 的bank选择
#define SDRAM_TIMEOUT                           ((uint32_t)0x1000)           // UNUSED
#define SDRAM_MODEREG_BURST_LENGTH_1            ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_LENGTH_2            ((uint16_t)0x0001)
#define SDRAM_MODEREG_BURST_LENGTH_4            ((uint16_t)0x0002)
#define SDRAM_MODEREG_BURST_LENGTH_8            ((uint16_t)0x0003)
#define SDRAM_MODEREG_BURST_MODE_SEQUENTIAL     ((uint16_t)0x0000)  // 顺序模式
#define SDRAM_MODEREG_BURST_MODE_INTERLEAVED    ((uint16_t)0x0008)  // 交错模式
#define SDRAM_MODEREG_CAS_LATENCY_2             ((uint16_t)0x0020)
#define SDRAM_MODEREG_CAS_LATENCY_3             ((uint16_t)0x0030)
#define SDRAM_MODEREG_BURSTREAD_BURSTWRITE      ((uint16_t)0x0000) 
#define SDRAM_MODEREG_BURSTREAD_SINGLEWRITE     ((uint16_t)0x0200) 


static void HAL_FMC_MspInit(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    /* Peripheral clock enable */
    __HAL_RCC_FMC_CLK_ENABLE();

    /* GPIO Ports Clock Enable */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    gpio_init.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = GPIO_AF12_FMC;
    HAL_GPIO_Init(GPIOF, &gpio_init);

    gpio_init.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = GPIO_AF12_FMC;
    HAL_GPIO_Init(GPIOH, &gpio_init);

    gpio_init.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_15;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = GPIO_AF12_FMC;
    HAL_GPIO_Init(GPIOG, &gpio_init);

    gpio_init.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15 | GPIO_PIN_0 | GPIO_PIN_1;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = GPIO_AF12_FMC;
    HAL_GPIO_Init(GPIOE, &gpio_init);

    gpio_init.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15 | GPIO_PIN_0 | GPIO_PIN_1;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = GPIO_AF12_FMC;
    HAL_GPIO_Init(GPIOD, &gpio_init);

    gpio_init.Pin = GPIO_PIN_0;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = GPIO_AF12_FMC;
    HAL_GPIO_Init(GPIOC, &gpio_init);
}

// 重写weak函数，配置fmc时钟和初始化gpio引脚
void HAL_SDRAM_MspInit(SDRAM_HandleTypeDef *hsdram)
{
    HAL_FMC_MspInit();
}

/******************************************************************************************************
 *	函 数 名: platform_sdram_init_sequence
 *	入口参数: hsdram - SDRAM_HandleTypeDef定义的变量，即表示定义的sdram
 *				 command	- 控制指令
 *	返 回 值: 无
 *	函数功能: SDRAM 参数配置
 *	说    明: 配置SDRAM相关时序和控制方式
 *******************************************************************************************************/
static void platform_sdram_init_sequence(SDRAM_HandleTypeDef *hsdram)
{
    volatile uint32_t tmp32 = 0;
    FMC_SDRAM_CommandTypeDef command = {0};

    /* Configure a clock configuration enable command */
    command.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;  // 开启SDRAM时钟
    command.CommandTarget = FMC_COMMAND_TARGET_BANK; // 选择要控制的区域
    command.AutoRefreshNumber = 1;
    command.ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(hsdram, &command, SDRAM_TIMEOUT); // 发送控制指令
    HAL_Delay(1);  // 延时等待1ms，等待SDRAM时钟稳定

    /* Configure a PALL (precharge all) command */
    command.CommandMode = FMC_SDRAM_CMD_PALL;  // 预充电命令
    command.CommandTarget = FMC_COMMAND_TARGET_BANK; // 选择要控制的区域
    command.AutoRefreshNumber = 1;
    command.ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(hsdram, &command, SDRAM_TIMEOUT); // 发送控制指令

    /* Configure a Auto-Refresh command */
    command.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE; // 使用自动刷新
    command.CommandTarget = FMC_COMMAND_TARGET_BANK;  // 选择要控制的区域
    command.AutoRefreshNumber = 8;  // 自动刷新次数
    command.ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(hsdram, &command, SDRAM_TIMEOUT); // 发送控制指令

    /* Program the external memory mode register */
    tmp32 = (uint32_t)SDRAM_MODEREG_BURST_LENGTH_2 |
                      SDRAM_MODEREG_BURST_MODE_SEQUENTIAL | // 顺序模式
                      SDRAM_MODEREG_CAS_LATENCY_2 |
                      SDRAM_MODEREG_BURSTREAD_SINGLEWRITE;
    command.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;   // 加载模式寄存器命令
    command.CommandTarget = FMC_COMMAND_TARGET_BANK; // 选择要控制的区域
    command.AutoRefreshNumber = 1;
    command.ModeRegisterDefinition = tmp32;
    HAL_SDRAM_SendCommand(hsdram, &command, SDRAM_TIMEOUT); // 发送控制指令

    // 刷新计数 = (SDRAM refresh period / Number of rows) * SDRAM 时钟速度 – 20
    //          = (64ms / 8192) * 100MHz - 20
    //          = 761.25
    HAL_SDRAM_ProgramRefreshRate(hsdram, 762); // 配置刷新率
}

/******************************************************************************************************
 *	函 数 名: MX_FMC_Init
 *	入口参数: 无
 *	返 回 值: 无
 *	函数功能: SDRAM初始化
 *	说    明: 初始化FMC和SDRAM配置，MX_SDRAM_Init()->HAL_SDRAM_Init()->HAL_SDRAM_MspInit()->HAL_FMC_MspInit()
 *******************************************************************************************************/
HAL_StatusTypeDef platform_sdram_init(void)
{
    FMC_SDRAM_TimingTypeDef sdram_timing = {0};

    // FMC内核时钟=200MHz
    hsdram1.Instance = FMC_SDRAM_DEVICE;
    hsdram1.Init.SDBank = FMC_SDRAM_BANK1;                             // 选择BANK区
    hsdram1.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_9;       // 行地址宽度
    hsdram1.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_13;            // 列地址线宽度
    hsdram1.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;         // 数据宽度
    hsdram1.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;    // bank数量
    hsdram1.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_2;                 // CAS
    hsdram1.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE; // 禁止写保护
    hsdram1.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;             // 分频
    hsdram1.Init.ReadBurst = FMC_SDRAM_RBURST_ENABLE;                  // 突发模式
    hsdram1.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_0;              // 读延迟

    // sdram_timing, CLK=100MHz，一个时钟周期10ns
    sdram_timing.LoadToActiveDelay = 2;      // 定义从加载模式寄存器命令到激活或刷新命令之间的延迟，tRSC，手册2tCK
    sdram_timing.ExitSelfRefreshDelay = 8;   // 定义从释放自刷新命令到发出激活命令之间的延迟，tXSR，手册72ns
    sdram_timing.SelfRefreshTime = 5;        // 定义最小自刷新周期，tRAS 行有效至预充电命令间隔周期，手册42ns
    sdram_timing.RowCycleDelay = 6;          // 定义刷新命令与激活命令之间的延迟，tRC，手册60ns
    sdram_timing.WriteRecoveryTime = 2;      // 定义写恢复时间，tWR，手册2tCK
    sdram_timing.RPDelay = 2;                // 定义预充电命令与其他命令之间的延迟，重写当前行数据+复位当前行地址+释放S-AMP，tRP，手册18ns
    sdram_timing.RCDDelay = 2;               // 定义激活命令与读/写命令之间的延迟，tRCD，手册18ns
    HAL_SDRAM_Init(&hsdram1, &sdram_timing); // 初始化FMC接口

    platform_sdram_init_sequence(&hsdram1);  // 配置SDRAM
    return HAL_OK;
}
