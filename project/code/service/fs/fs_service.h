/*********************************************************************************************************************
 * fs_service — LittleFS 薄封装（全局 mount）
 *
 * 介质：片内 DATA Flash（bsp_flash + lfs_port），与 MAIN 程序区分离。
 * 供 param 等业务通过文件 API 做持久化；禁止在 ISR / 1ms 控制环中做写操作。
 *
 * 启动：fs_init() → 业务使用 fs_lfs()
 ********************************************************************************************************************/

#ifndef FS_SERVICE_H
#define FS_SERVICE_H

#include <stdbool.h>
#include "common/tools/common_def.h"
#include "service/lfs/lfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 参数持久化默认路径（key=value 文本） */
#define FS_PARAM_PATH "/param.txt"

/**
 * @brief 初始化底层 Flash 并 mount LittleFS（失败则 format 再 mount）
 * @return EXIT_OK 成功；EXIT_FAIL / EXIT_ALREADY_INITIALIZED 等
 * @note 幂等：已成功 mount 后再次调用返回 EXIT_OK
 */
exit_code_t fs_init(void);

/** @return 是否已成功 mount */
bool fs_is_ready(void);

/**
 * @brief 全局 lfs 句柄（仅 fs_is_ready() 为 true 时有效）
 * @return 非 NULL 指针，或未就绪时 NULL
 */
lfs_t *fs_lfs(void);

#ifdef __cplusplus
}
#endif

#endif /* FS_SERVICE_H */
