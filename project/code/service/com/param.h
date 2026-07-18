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
    bool need_storage;  // 是否需要Flash持久化存储
} param_t;

// 初始化参数系统（会自动从Flash加载参数）
exit_code_t param_init(void);

// 参数管理接口
int param_set_value(const char* name, void* data);
int param_get(const char* name, param_t* param);
int param_get_by_index(uint16_t index, param_t* param);
int param_get_value(const char* name, void* data);
exit_code_t param_add(const char* name, param_type_t type, void* data, bool need_storage);

// Flash 持久化接口（当前为桩实现，storage 未接入；调用会返回失败）
int param_save(void);                        // 保存需要持久化的参数到Flash
int param_load(const char* flash_tag);       // 从Flash加载参数
int param_clear_flash(void);                 // 清除Flash中的参数数据

// 命令行接口
cmd_exec_result_t param_command_parse(i32 seq, int argc, char** argv);
cmd_exec_result_t param_command_show(i32 seq, int argc, char** argv);

// ============================================================================
// 便捷宏定义 - 自动使用变量名
// ============================================================================

/**
 * @brief 添加参数，自动使用变量名作为参数名
 * @param var  变量名（会被转换为字符串作为参数名）
 * @param type 参数类型（PARAM_TYPE_UINT8, PARAM_TYPE_FLOAT 等）
 * @param need_storage 是否需要Flash持久化存储
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
 *   PARAM_GET(my_param);  // 从Flash/存储中恢复值到 my_param
 */
#define PARAM_GET(var) \
    param_get_value(#var, &(var))

#ifdef __cplusplus
}
#endif

#endif // !__PARAM_H__
