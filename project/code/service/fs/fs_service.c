/*********************************************************************************************************************
 * fs_service — LittleFS 薄封装
 ********************************************************************************************************************/

#include "service/fs/fs_service.h"

#include "service/lfs/lfs_port.h"
#include "service/sys/sys_log.h"

#include <string.h>

static lfs_t g_lfs;
static bool g_fs_ready = false;

exit_code_t fs_init(void)
{
    int err;

    if (g_fs_ready) {
        return EXIT_OK;
    }

    err = lfs_port_init();
    if (err != 0) {
        sys_log_text(error, "fs: lfs_port_init failed (%d)", err);
        return EXIT_FAIL;
    }

    err = lfs_mount(&g_lfs, &g_lfs_cfg);
    if (err != 0) {
        sys_log_text(warning, "fs: mount failed (%d), formatting...", err);
        err = lfs_format(&g_lfs, &g_lfs_cfg);
        if (err != 0) {
            sys_log_text(error, "fs: format failed (%d)", err);
            return EXIT_FAIL;
        }
        err = lfs_mount(&g_lfs, &g_lfs_cfg);
        if (err != 0) {
            sys_log_text(error, "fs: remount after format failed (%d)", err);
            return EXIT_FAIL;
        }
        sys_log_text(info, "fs: formatted and mounted");
    }

    g_fs_ready = true;
    sys_log_text(info, "fs: ready (blocks=%lu size=%lu B path=%s)",
                 (unsigned long)g_lfs_cfg.block_count,
                 (unsigned long)(g_lfs_cfg.block_count * g_lfs_cfg.block_size),
                 FS_PARAM_PATH);
    return EXIT_OK;
}

bool fs_is_ready(void)
{
    return g_fs_ready;
}

lfs_t *fs_lfs(void)
{
    return g_fs_ready ? &g_lfs : NULL;
}
