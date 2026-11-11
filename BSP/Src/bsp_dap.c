#include "bsp_dap.h"

#include "DAP.h"

bsp_status_t bsp_dap_init(void)
{
    /* CMSIS-DAP 初始化协议状态，并把 SWD 引脚置于安全的高阻状态。 */
    DAP_Setup();
    return BSP_OK;
}

uint32_t bsp_dap_execute_command(const uint8_t *request, uint8_t *response)
{
    if ((request == NULL) || (response == NULL))
    {
        return 0U;
    }

    return DAP_ExecuteCommand(request, response);
}
