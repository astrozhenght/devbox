# FatFS R0.16 升级记录

本工程于 2026-11-06 将 `source/ff.c`、`source/ff.h`、`source/ffunicode.c`
与官方 R0.16 源码对齐，以支持文件管理 App 读取真实创建时间。

- 官方包：https://elm-chan.org/fsw/ff/arc/ff16.zip
- ZIP SHA-256：`99F7DC1F7E095356E4A9E3DBE29959090D8B948AFE2BBC5441E52FDF4B85449E`
- `FF_DEFINED / FFCONF_DEF`：80386。
- 保留官方源文件中的版权与许可证声明。
- `ffunicode.c` 与仓库原有文件内容相同，无有效差异。

工程配置继续保留中文代码页936、UTF-8长文件名和长文件名分配接口、
三个逻辑卷与原有RTC接口；新增 `FF_FS_CRTIME=1`，
将 `FF_LFN_BUF` 调整为765字节，避免合法中文长文件名退化为短文件名。
`FF_PATH_DEPTH=16` 在当前关闭exFAT的配置下不影响FAT目录深度。

ST磁盘适配仍位于 `Platform/Port/FatFS`。
文件系统操作由文件任务串行调用，`FF_FS_REENTRANT` 保持为0。
长文件名分配由 `Platform/Port/FatFS/fatfs_memory.c` 提供两个固定容量工作区，
避免与Wi-Fi任务并发使用libc堆；Keil不再编译示例 `ffsystem.c`。
不在应用中调用格式化接口；无文件系统时仅报告错误。

集成测试使用同一套FatFS源码与配置在RAM磁盘上运行，见
`Tests/file_manager/test_file_service.c`。目标板SDMMC读写仍需实卡验收。
