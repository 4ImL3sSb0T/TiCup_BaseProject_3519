#ifndef __PARAM_H__
#define __PARAM_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "common/tools/common_def.h"
#include "service/sys/sys_log.h"

#define PARAM_NAME_MAX_LEN 32
#define PARAM_MAX_COUNT 72

#ifdef __cplusplus
extern "C" {
#endif

typedef enum param_type_t
{
    PARAM_TYPE_NONE = 0,
    PARAM_TYPE_UINT8,
    PARAM_TYPE_UINT16,
    PARAM_TYPE_UINT32,
    PARAM_TYPE_INT8,
    PARAM_TYPE_INT16,
    PARAM_TYPE_INT32,
    PARAM_TYPE_FLOAT,
} param_type_t;

typedef struct param_t
{
    void* data;
    param_type_t type;
    char name[PARAM_NAME_MAX_LEN];
    bool need_storage;  // 是否写入 LFS /param.txt
} param_t;

/**
 * @brief 初始化参数注册表（不 load）
 * @note  须在 fs_init() 之后调用；业务 param_add 全部完成后再 param_load()
 */
exit_code_t param_init(void);

// 参数管理接口
int param_set_value(const char* name, void* data);
int param_get(const char* name, param_t* param);
int param_get_by_index(uint16_t index, param_t* param);
int param_get_value(const char* name, void* data);
/** 已注册参数数量（只读快照，非临界区） */
uint16_t param_get_count(void);
exit_code_t param_add(const char* name, param_type_t type, void* data, bool need_storage);

/**
 * LFS 文本持久化（/param.txt，key=value）
 * - 仅任务上下文调用；禁止 ISR / 1ms 控制环
 * - load 须在全部 param_add 之后
 */
int param_save(void);                        // 保存 need_storage 参数到 LFS
int param_load(const char* flash_tag);       // 从 LFS 加载；无文件视为成功
int param_clear_flash(void);                 // 删除 /param.txt

// 命令行接口
cmd_exec_result_t param_command_parse(i32 seq, int argc, char** argv);
cmd_exec_result_t param_command_show(i32 seq, int argc, char** argv);
cmd_exec_result_t param_command_export(i32 seq, int argc, char** argv);

// ============================================================================
// 便捷宏定义 - 自动使用变量名
// ============================================================================

/**
 * @brief 添加参数，自动使用变量名作为参数名
 * @param var  变量名（会被转换为字符串作为参数名）
 * @param type 参数类型（PARAM_TYPE_UINT8, PARAM_TYPE_FLOAT 等）
 * @param need_storage 是否需要 LFS 持久化
 * @return exit_code_t
 *
 * 示例:
 *   static uint32_t my_param = 100;
 *   PARAM_ADD(my_param, PARAM_TYPE_UINT32, true);  // 参数名为 "my_param"
 */
#define PARAM_ADD(var, type, need_storage) \
    param_add(#var, type, &(var), need_storage)

/**
 * @brief 设置参数值，自动使用变量名
 * @param var  变量名
 * @param value 要设置的值
 *
 * 示例:
 *   PARAM_SET(my_param, 200);
 */
#define PARAM_SET(var, value) \
    do { \
        typeof(var) _tmp = (value); \
        param_set_value(#var, &_tmp); \
    } while(0)

/**
 * @brief 获取参数值到变量，自动使用变量名
 * @param var  变量名
 *
 * 示例:
 *   PARAM_GET(my_param);
 */
#define PARAM_GET(var) \
    param_get_value(#var, &(var))

#ifdef __cplusplus
}
#endif

#endif // !__PARAM_H__
