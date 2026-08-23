#include "sdram.h"

SDRAM_HandleTypeDef hsdram1;      // SDRAM_HandleTypeDef 结构体变量
FMC_SDRAM_CommandTypeDef command; // 控制指令

static void HAL_FMC_MspInit(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Peripheral clock enable */
    __HAL_RCC_FMC_CLK_ENABLE();

    /* GPIO Ports Clock Enable */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_FMC;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_FMC;
    HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_FMC;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15 | GPIO_PIN_0 | GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_FMC;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15 | GPIO_PIN_0 | GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_FMC;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_FMC;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

// 重写weak函数，配置fmc时钟和初始化gpio引脚
void HAL_SDRAM_MspInit(SDRAM_HandleTypeDef *hsdram)
{
    HAL_FMC_MspInit();
}

/******************************************************************************************************
 *	函 数 名: SDRAM_Initialization_Sequence
 *	入口参数: hsdram - SDRAM_HandleTypeDef定义的变量，即表示定义的sdram
 *				 Command	- 控制指令
 *	返 回 值: 无
 *	函数功能: SDRAM 参数配置
 *	说    明: 配置SDRAM相关时序和控制方式
 *******************************************************************************************************/
void SDRAM_Initialization_Sequence(SDRAM_HandleTypeDef *hsdram, FMC_SDRAM_CommandTypeDef *Command)
{
    __IO uint32_t tmpmrd = 0;

    /* Configure a clock configuration enable command */
    Command->CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;  // 开启SDRAM时钟
    Command->CommandTarget = FMC_COMMAND_TARGET_BANK; // 选择要控制的区域
    Command->AutoRefreshNumber = 1;
    Command->ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(hsdram, Command, SDRAM_TIMEOUT); // 发送控制指令
    HAL_Delay(1);                                          // 延时等待

    /* Configure a PALL (precharge all) command */
    Command->CommandMode = FMC_SDRAM_CMD_PALL;        // 预充电命令
    Command->CommandTarget = FMC_COMMAND_TARGET_BANK; // 选择要控制的区域
    Command->AutoRefreshNumber = 1;
    Command->ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(hsdram, Command, SDRAM_TIMEOUT); // 发送控制指令

    /* Configure a Auto-Refresh command */
    Command->CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE; // 使用自动刷新
    Command->CommandTarget = FMC_COMMAND_TARGET_BANK;      // 选择要控制的区域
    Command->AutoRefreshNumber = 8;                        // 自动刷新次数
    Command->ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(hsdram, Command, SDRAM_TIMEOUT); // 发送控制指令

    /* Program the external memory mode register */
    tmpmrd = (uint32_t)SDRAM_MODEREG_BURST_LENGTH_2 |
             SDRAM_MODEREG_BURST_MODE_SEQUENTIAL | // 顺序模式
             SDRAM_MODEREG_CAS_LATENCY_2 |
             SDRAM_MODEREG_BURSTREAD_SINGLEWRITE;
    Command->CommandMode = FMC_SDRAM_CMD_LOAD_MODE;   // 加载模式寄存器命令
    Command->CommandTarget = FMC_COMMAND_TARGET_BANK; // 选择要控制的区域
    Command->AutoRefreshNumber = 1;
    Command->ModeRegisterDefinition = tmpmrd;
    HAL_SDRAM_SendCommand(hsdram, Command, SDRAM_TIMEOUT); // 发送控制指令

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
 *	说    明: 初始化FMC和SDRAM配置
 *******************************************************************************************************/

void MX_SDRAM_Init(void)
{
    FMC_SDRAM_TimingTypeDef SdramTiming = {0};

    hsdram1.Instance = FMC_SDRAM_DEVICE;
    /* hsdram1.Init, FMC内核时钟=200MHz */
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

    /* SdramTiming, CLK=100MHz，一个时钟周期10ns */
    SdramTiming.LoadToActiveDelay = 2;      // 定义从加载模式寄存器命令到激活或刷新命令之间的延迟，tRSC，手册2tCK
    SdramTiming.ExitSelfRefreshDelay = 8;   // 定义从释放自刷新命令到发出激活命令之间的延迟，tXSR，手册72ns
    SdramTiming.SelfRefreshTime = 5;        // 定义最小自刷新周期，tRAS 行有效至预充电命令间隔周期，手册42ns
    SdramTiming.RowCycleDelay = 6;          // 定义刷新命令与激活命令之间的延迟，tRC，手册60ns
    SdramTiming.WriteRecoveryTime = 2;      // 定义写恢复时间，tWR，手册2tCK
    SdramTiming.RPDelay = 2;                // 定义预充电命令与其他命令之间的延迟，重写当前行数据+复位当前行地址+释放S-AMP，tRP，手册18ns
    SdramTiming.RCDDelay = 2;               // 定义激活命令与读/写命令之间的延迟，tRCD，手册18ns
    HAL_SDRAM_Init(&hsdram1, &SdramTiming); // 初始化FMC接口


    SDRAM_Initialization_Sequence(&hsdram1, &command); // 配置SDRAM
}
