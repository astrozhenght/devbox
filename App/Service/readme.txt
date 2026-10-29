wifi模块的作用：
1. 连接wifi
2. 获取天气信息
3. 获取时间信息

uart_service.c        包含平台无关接口，不向UI暴露STM32 HAL库定义的东西
uart_service_stm32.c  包含平台相关接口，调用Platform接口
uart_service_port.c   Port层只用于隔离平台实现，不提供给UI使用
