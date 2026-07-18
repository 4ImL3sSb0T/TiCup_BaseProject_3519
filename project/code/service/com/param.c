#include "param.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// #include "service/storage/storage.h"  // TODO: 后期启用 Flash 持久化时恢复，并将 storage.c 加入 Keil 工程
#include "service/sys/sys_log.h"
#include "zf_common_interrupt.h"
#include "zf_common_typedef.h"
// #include "zf_driver_flash.h"  // TODO: 随 Flash 持久化一并恢复


static param_t param_list[PARAM_MAX_COUNT] = {0};
static uint16_t param_count = 0;

#define PARAM_ENTER_CRITICAL() uint32 primask = interrupt_global_disable()
#define PARAM_EXIT_CRITICAL()  interrupt_global_enable(primask)

/* ========== Flash 持久化数据结构（暂时禁用，后期恢复）==========
typedef struct
{
    char name[PARAM_NAME_MAX_LEN];
    param_type_t type;
    union {
        float f;
        int32_t i32;
        uint32_t u32;
        int16_t i16;
        uint16_t u16;
        int8_t i8;
        uint8_t u8;
    } value;
} param_flash_entry_t;

#define PARAM_FLASH_HEADER_SIZE (sizeof(uint32_t) * 2U + sizeof(uint16_t) * 2U)
#define PARAM_STORAGE_MAX ((FLASH_PAGE_SIZE - PARAM_FLASH_HEADER_SIZE) / sizeof(param_flash_entry_t))
_Static_assert(PARAM_STORAGE_MAX > 0, "PARAM_STORAGE_MAX must be positive");

// Flash存储数据结构（单页内）
typedef struct
{
    uint32_t magic;
    uint32_t crc32;
    uint16_t count;
    uint16_t reserved;
    param_flash_entry_t params[PARAM_STORAGE_MAX];
} param_flash_t;

_Static_assert(sizeof(param_flash_t) <= FLASH_PAGE_SIZE, "param_flash_t exceeds flash page size");
========== Flash 持久化数据结构 END ========== */

static int param_name_equals(const char *a, const char *b) {
    size_t len_a = strnlen(a, PARAM_NAME_MAX_LEN);
    size_t len_b = strnlen(b, PARAM_NAME_MAX_LEN);

    if (len_a >= PARAM_NAME_MAX_LEN || len_b >= PARAM_NAME_MAX_LEN) {
        return 0;
    }

    return (len_a == len_b) && (strncmp(a, b, PARAM_NAME_MAX_LEN) == 0);
}

static int parse_unsigned_value(const char *text, uint32_t *out) {
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);

    if (text == end || (end && *end != '\0') || errno == ERANGE || value > UINT32_MAX) {
        return -1;
    }

    *out = (uint32_t)value;
    return 0;
}

static int parse_signed_value(const char *text, int32_t *out) {
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);

    if (text == end || (end && *end != '\0') || errno == ERANGE || value > INT32_MAX || value < INT32_MIN) {
        return -1;
    }

    *out = (int32_t)value;
    return 0;
}

static int parse_float_value(const char *text, float *out) {
    char *end = NULL;
    errno = 0;
    float value = strtof(text, &end);

    if (text == end || (end && *end != '\0') || errno == ERANGE) {
        return -1;
    }

    *out = value;
    return 0;
}

// exit_code_t param_init(param_t param_list_arg[]) {
//     if (!param_list_arg) {
//         return EXIT_INVALID_PARAM;
//     }
//     param_list = param_list_arg;
// 	return EXIT_OK;
// }

int param_set_value(const char *name, void *data) {
    if (!name || !data) {
        return -1; // Invalid arguments
    }

    if (strnlen(name, PARAM_NAME_MAX_LEN) >= PARAM_NAME_MAX_LEN) {
        return -1; // Name too long
    }

    PARAM_ENTER_CRITICAL();
    for (uint16_t i = 0; i < param_count; i++) {
        if (param_name_equals(param_list[i].name, name)) {
            if (!param_list[i].data) {
                PARAM_EXIT_CRITICAL();
                return -3; // Data pointer is null
            }

            switch (param_list[i].type) {
                case PARAM_TYPE_UINT8:
                    *(uint8_t *)param_list[i].data = *(uint8_t *)data;
                    break;
                case PARAM_TYPE_UINT16:
                    *(uint16_t *)param_list[i].data = *(uint16_t *)data;
                    break;
                case PARAM_TYPE_UINT32:
                    *(uint32_t *)param_list[i].data = *(uint32_t *)data;
                    break;
                case PARAM_TYPE_INT8:
                    *(int8_t *)param_list[i].data = *(int8_t *)data;
                    break;
                case PARAM_TYPE_INT16:
                    *(int16_t *)param_list[i].data = *(int16_t *)data;
                    break;
                case PARAM_TYPE_INT32:
                    *(int32_t *)param_list[i].data = *(int32_t *)data;
                    break;
                case PARAM_TYPE_FLOAT:
                    *(float *)param_list[i].data = *(float *)data;
                    break;
                default:
                    PARAM_EXIT_CRITICAL();
                    return -2; // Unsupported type
            }
            PARAM_EXIT_CRITICAL();
            return 0; // Success
        }
    }
    PARAM_EXIT_CRITICAL();
    return 1;
}

int param_get(const char *name, param_t *param) {
    if (!name || !param) {
        return -1; // Invalid arguments
    }

    size_t name_len = strnlen(name, PARAM_NAME_MAX_LEN);
    if (name_len >= PARAM_NAME_MAX_LEN) {
        return -1; // Name too long
    }

    PARAM_ENTER_CRITICAL();
    for (uint16_t i = 0; i < param_count; i++) {
        if (param_name_equals(param_list[i].name, name)) {
            *param = param_list[i];
            PARAM_EXIT_CRITICAL();
            return 0; // Success
        }
    }
    PARAM_EXIT_CRITICAL();
    return 1;
}

int param_get_by_index(uint16_t index, param_t *param) {
    if (!param) {
        return -1; // Invalid arguments
    }
    PARAM_ENTER_CRITICAL();
    if (index >= param_count) {
        PARAM_EXIT_CRITICAL();
        return -2; // Index out of bounds
    }
    *param = param_list[index];
    PARAM_EXIT_CRITICAL();
    return 0;
}

int param_get_value(const char *name, void *data) {
    if (!name || !data) {
        return -1; // Invalid arguments
    }

    if (strnlen(name, PARAM_NAME_MAX_LEN) >= PARAM_NAME_MAX_LEN) {
        return -1; // Name too long
    }

    PARAM_ENTER_CRITICAL();
    for (uint16_t i = 0; i < param_count; i++) {
        if (param_name_equals(param_list[i].name, name)) {
            if (!param_list[i].data) {
                PARAM_EXIT_CRITICAL();
                return -3; // Data pointer is null
            }
            switch (param_list[i].type) {
                case PARAM_TYPE_UINT8:
                    *(uint8_t *)data = *(uint8_t *)param_list[i].data;
                    break;
                case PARAM_TYPE_UINT16:
                    *(uint16_t *)data = *(uint16_t *)param_list[i].data;
                    break;
                case PARAM_TYPE_UINT32:
                    *(uint32_t *)data = *(uint32_t *)param_list[i].data;
                    break;
                case PARAM_TYPE_INT8:
                    *(int8_t *)data = *(int8_t *)param_list[i].data;
                    break;
                case PARAM_TYPE_INT16:
                    *(int16_t *)data = *(int16_t *)param_list[i].data;
                    break;
                case PARAM_TYPE_INT32:
                    *(int32_t *)data = *(int32_t *)param_list[i].data;
                    break;
                case PARAM_TYPE_FLOAT:
                    *(float *)data = *(float *)param_list[i].data;
                    break;
                default:
                    PARAM_EXIT_CRITICAL();
                    return -2; // Unsupported type
            }
            PARAM_EXIT_CRITICAL();
            return 0; // Success
        }
    }
    PARAM_EXIT_CRITICAL();
    return 1;
}

exit_code_t param_add(const char *name, param_type_t type, void *data, bool need_storage) {
    if (!name || !data || type == PARAM_TYPE_NONE) {
        return EXIT_INVALID_PARAM;
    }

    size_t name_len = strnlen(name, PARAM_NAME_MAX_LEN);
    if (name_len == 0 || name_len >= PARAM_NAME_MAX_LEN) {
        return EXIT_INVALID_PARAM; // Invalid or too long name
    }

    PARAM_ENTER_CRITICAL();

    // Check if parameter already exists
    for (uint16_t i = 0; i < param_count; i++) {
        if (param_name_equals(param_list[i].name, name)) {
            PARAM_EXIT_CRITICAL();
            return EXIT_INVALID_PARAM; // Parameter already exists
        }
    }

    // Check if we have space for a new parameter
    if (param_count >= PARAM_MAX_COUNT) {
        PARAM_EXIT_CRITICAL();
        return EXIT_NO_MEMORY; // No space left
    }

    // Add new parameter
    strncpy(param_list[param_count].name, name, PARAM_NAME_MAX_LEN - 1);
    param_list[param_count].name[PARAM_NAME_MAX_LEN - 1] = '\0'; // Ensure null-termination
    param_list[param_count].type = type;
    param_list[param_count].data = data;
    param_list[param_count].need_storage = need_storage;
    param_count++;
    PARAM_EXIT_CRITICAL();

    return EXIT_OK;
}

static exit_code_t param_command_show_impl(i32 seq, int argc, char **argv) {
    (void)seq;
    (void)argc;
    (void)argv;

    sys_log_text(terminal, "Parameter List:");
    sys_log_text(terminal, "%-20s %-10s %-20s", "Name", "Type", "Value");
    sys_log_text(terminal, "-----------------------------------------------------");

    for (uint16_t i = 0; i < param_count; i++) {
        switch (param_list[i].type) {
            case PARAM_TYPE_UINT8:
                sys_log_text(terminal, "%-20s %-10s %-20u", param_list[i].name, "UINT8", *(uint8_t *)param_list[i].data);
                break;
            case PARAM_TYPE_UINT16:
                sys_log_text(terminal, "%-20s %-10s %-20u", param_list[i].name, "UINT16", *(uint16_t *)param_list[i].data);
                break;
            case PARAM_TYPE_UINT32:
                sys_log_text(terminal, "%-20s %-10s %-20lu", param_list[i].name, "UINT32", *(uint32_t *)param_list[i].data);
                break;
            case PARAM_TYPE_INT8:
                sys_log_text(terminal, "%-20s %-10s %-20d", param_list[i].name, "INT8", *(int8_t *)param_list[i].data);
                break;
            case PARAM_TYPE_INT16:
                sys_log_text(terminal, "%-20s %-10s %-20d", param_list[i].name, "INT16", *(int16_t *)param_list[i].data);
                break;
            case PARAM_TYPE_INT32:
                sys_log_text(terminal, "%-20s %-10s %-20ld", param_list[i].name, "INT32", *(int32_t *)param_list[i].data);
                break;
            case PARAM_TYPE_FLOAT:
                sys_log_text(terminal, "%-20s %-10s %-20.6f", param_list[i].name, "FLOAT", *(float *)param_list[i].data);
                break;
            default:
                sys_log_text(terminal, "%-20s %-10s %-20s", param_list[i].name, "UNKNOWN", "N/A");
                break;
        }
    }
    sys_log_text(terminal, "-----------------------------------------------------");
    return EXIT_OK;
}

static exit_code_t param_command_parse_impl(i32 seq, int argc, char **argv) {
    (void)seq;
    if (argc <= 0 || argv == NULL || argv[0] == NULL) {
        sys_log_text(terminal, "Usage: set <name> <value> or get <name>");
        return EXIT_INVALID_PARAM;
    }

    if (strcmp(argv[0], "set") == 0 && argc == 3) {
        const char *name = argv[1];

        // 先获取参数信息以确定类型
        param_t param_info;
        if (param_get(name, &param_info) != 0) {
            sys_log_text(terminal, "Parameter %s not found", name);
            return EXIT_DOES_NOT_EXIST;
        }

        // 根据参数类型转换和设置值
        switch (param_info.type) {
            case PARAM_TYPE_UINT8: {
                uint32_t parsed;
                if (parse_unsigned_value(argv[2], &parsed) != 0 || parsed > UINT8_MAX) {
                    sys_log_text(terminal, "Invalid value for %s", name);
                    return EXIT_INVALID_PARAM;
                }
                uint8_t value = (uint8_t)parsed;
                if (param_set_value(name, &value) != 0) {
                    return EXIT_FAIL;
                }
                sys_log_text(terminal, "Set %s = %u", name, value);
                return EXIT_OK;
            }
            case PARAM_TYPE_UINT16: {
                uint32_t parsed;
                if (parse_unsigned_value(argv[2], &parsed) != 0 || parsed > UINT16_MAX) {
                    sys_log_text(terminal, "Invalid value for %s", name);
                    return EXIT_INVALID_PARAM;
                }
                uint16_t value = (uint16_t)parsed;
                if (param_set_value(name, &value) != 0) {
                    return EXIT_FAIL;
                }
                sys_log_text(terminal, "Set %s = %u", name, value);
                return EXIT_OK;
            }
            case PARAM_TYPE_UINT32: {
                uint32_t parsed;
                if (parse_unsigned_value(argv[2], &parsed) != 0) {
                    sys_log_text(terminal, "Invalid value for %s", name);
                    return EXIT_INVALID_PARAM;
                }
                uint32_t value = parsed;
                if (param_set_value(name, &value) != 0) {
                    return EXIT_FAIL;
                }
                sys_log_text(terminal, "Set %s = %lu", name, value);
                return EXIT_OK;
            }
            case PARAM_TYPE_INT8: {
                int32_t parsed;
                if (parse_signed_value(argv[2], &parsed) != 0 || parsed > INT8_MAX || parsed < INT8_MIN) {
                    sys_log_text(terminal, "Invalid value for %s", name);
                    return EXIT_INVALID_PARAM;
                }
                int8_t value = (int8_t)parsed;
                if (param_set_value(name, &value) != 0) {
                    return EXIT_FAIL;
                }
                sys_log_text(terminal, "Set %s = %d", name, value);
                return EXIT_OK;
            }
            case PARAM_TYPE_INT16: {
                int32_t parsed;
                if (parse_signed_value(argv[2], &parsed) != 0 || parsed > INT16_MAX || parsed < INT16_MIN) {
                    sys_log_text(terminal, "Invalid value for %s", name);
                    return EXIT_INVALID_PARAM;
                }
                int16_t value = (int16_t)parsed;
                if (param_set_value(name, &value) != 0) {
                    return EXIT_FAIL;
                }
                sys_log_text(terminal, "Set %s = %d", name, value);
                return EXIT_OK;
            }
            case PARAM_TYPE_INT32: {
                int32_t parsed;
                if (parse_signed_value(argv[2], &parsed) != 0) {
                    sys_log_text(terminal, "Invalid value for %s", name);
                    return EXIT_INVALID_PARAM;
                }
                int32_t value = parsed;
                if (param_set_value(name, &value) != 0) {
                    return EXIT_FAIL;
                }
                sys_log_text(terminal, "Set %s = %ld", name, value);
                return EXIT_OK;
            }
            case PARAM_TYPE_FLOAT: {
                float parsed;
                if (parse_float_value(argv[2], &parsed) != 0) {
                    sys_log_text(terminal, "Invalid value for %s", name);
                    return EXIT_INVALID_PARAM;
                }
                float value = parsed;
                if (param_set_value(name, &value) != 0) {
                    return EXIT_FAIL;
                }
                sys_log_text(terminal, "Set %s = %.6f", name, value);
                return EXIT_OK;
            }
            default:
                sys_log_text(terminal, "Unsupported parameter type");
                return EXIT_NOT_SUPPORTED;
        }
    } else if (strcmp(argv[0], "get") == 0 && argc == 2) {
        const char *name = argv[1];

        // 先获取参数信息以确定类型
        param_t param_info;
        if (param_get(name, &param_info) != 0) {
            sys_log_text(terminal, "Parameter %s not found", name);
            return EXIT_DOES_NOT_EXIST;
        }

        // 根据参数类型获取和打印值
        switch (param_info.type) {
            case PARAM_TYPE_UINT8: {
                uint8_t data;
                if (param_get_value(name, &data) != 0) {
                    return EXIT_FAIL;
                }
                sys_log_text(terminal, "Parameter %s value: %u", name, data);
                return EXIT_OK;
            }
            case PARAM_TYPE_UINT16: {
                uint16_t data;
                if (param_get_value(name, &data) != 0) {
                    return EXIT_FAIL;
                }
                sys_log_text(terminal, "Parameter %s value: %u", name, data);
                return EXIT_OK;
            }
            case PARAM_TYPE_UINT32: {
                uint32_t data;
                if (param_get_value(name, &data) != 0) {
                    return EXIT_FAIL;
                }
                sys_log_text(terminal, "Parameter %s value: %lu", name, data);
                return EXIT_OK;
            }
            case PARAM_TYPE_INT8: {
                int8_t data;
                if (param_get_value(name, &data) != 0) {
                    return EXIT_FAIL;
                }
                sys_log_text(terminal, "Parameter %s value: %d", name, data);
                return EXIT_OK;
            }
            case PARAM_TYPE_INT16: {
                int16_t data;
                if (param_get_value(name, &data) != 0) {
                    return EXIT_FAIL;
                }
                sys_log_text(terminal, "Parameter %s value: %d", name, data);
                return EXIT_OK;
            }
            case PARAM_TYPE_INT32: {
                int32_t data;
                if (param_get_value(name, &data) != 0) {
                    return EXIT_FAIL;
                }
                sys_log_text(terminal, "Parameter %s value: %ld", name, data);
                return EXIT_OK;
            }
            case PARAM_TYPE_FLOAT: {
                float data;
                if (param_get_value(name, &data) != 0) {
                    return EXIT_FAIL;
                }
                sys_log_text(terminal, "Parameter %s value: %.6f", name, data);
                return EXIT_OK;
            }
            default:
                sys_log_text(terminal, "Unsupported parameter type");
                return EXIT_NOT_SUPPORTED;
        }
    } else if (strcmp(argv[0], "save") == 0 && argc == 1) {
        if (param_save() == 0) {
            sys_log_text(terminal, "Parameters saved to Flash");
            return EXIT_OK;
        } else {
            sys_log_text(terminal, "Failed to save parameters");
            return EXIT_FAIL;
        }
    } else if (strcmp(argv[0], "load") == 0 && argc == 1) {
        if (param_load("Command") == 0) {
            sys_log_text(terminal, "Parameters loaded from Flash");
            return EXIT_OK;
        } else {
            sys_log_text(terminal, "Failed to load parameters");
            return EXIT_FAIL;
        }
    } else {
        sys_log_text(terminal, "Usage:");
        sys_log_text(terminal, "  set <name> <value> - Set parameter value");
        sys_log_text(terminal, "  get <name>         - Get parameter value");
        sys_log_text(terminal, "  save               - Save parameters to Flash");
        sys_log_text(terminal, "  load               - Load parameters from Flash");
        return EXIT_INVALID_PARAM;
    }

    return EXIT_OK;
}

static const char *param_type_to_ctx_name(param_type_t type)
{
    switch (type) {
        case PARAM_TYPE_UINT8: return "uint8";
        case PARAM_TYPE_UINT16: return "uint16";
        case PARAM_TYPE_UINT32: return "uint32";
        case PARAM_TYPE_INT8: return "int8";
        case PARAM_TYPE_INT16: return "int16";
        case PARAM_TYPE_INT32: return "int32";
        case PARAM_TYPE_FLOAT: return "float";
        default: return "unknown";
    }
}

cmd_exec_result_t param_command_show(i32 seq, int argc, char **argv)
{
    static char ctx[96];
    exit_code_t code = param_command_show_impl(seq, argc, argv);
    if (code == EXIT_OK) {
        snprintf(ctx, sizeof(ctx), "count=%u", (unsigned int)param_count);
        return CMD_EXEC_CTX(code, ctx);
    }
    return CMD_EXEC_CODE(code);
}

cmd_exec_result_t param_command_parse(i32 seq, int argc, char **argv)
{
    static char ctx[96];
    exit_code_t code = param_command_parse_impl(seq, argc, argv);

    if (code == EXIT_OK && argv != NULL && argv[0] != NULL && strcmp(argv[0], "get") == 0 && argc == 2) {
        const char *name = argv[1];
        param_t info = {0};
        if (param_get(name, &info) == 0) {
            switch (info.type) {
                case PARAM_TYPE_UINT8: {
                    uint8_t value = 0;
                    if (param_get_value(name, &value) == 0) {
                        snprintf(ctx, sizeof(ctx), "name=%s;type=%s;value=%u", name, param_type_to_ctx_name(info.type), (unsigned)value);
                        return CMD_EXEC_CTX(code, ctx);
                    }
                    break;
                }
                case PARAM_TYPE_UINT16: {
                    uint16_t value = 0;
                    if (param_get_value(name, &value) == 0) {
                        snprintf(ctx, sizeof(ctx), "name=%s;type=%s;value=%u", name, param_type_to_ctx_name(info.type), (unsigned)value);
                        return CMD_EXEC_CTX(code, ctx);
                    }
                    break;
                }
                case PARAM_TYPE_UINT32: {
                    uint32_t value = 0;
                    if (param_get_value(name, &value) == 0) {
                        snprintf(ctx, sizeof(ctx), "name=%s;type=%s;value=%lu", name, param_type_to_ctx_name(info.type), (unsigned long)value);
                        return CMD_EXEC_CTX(code, ctx);
                    }
                    break;
                }
                case PARAM_TYPE_INT8: {
                    int8_t value = 0;
                    if (param_get_value(name, &value) == 0) {
                        snprintf(ctx, sizeof(ctx), "name=%s;type=%s;value=%d", name, param_type_to_ctx_name(info.type), (int)value);
                        return CMD_EXEC_CTX(code, ctx);
                    }
                    break;
                }
                case PARAM_TYPE_INT16: {
                    int16_t value = 0;
                    if (param_get_value(name, &value) == 0) {
                        snprintf(ctx, sizeof(ctx), "name=%s;type=%s;value=%d", name, param_type_to_ctx_name(info.type), (int)value);
                        return CMD_EXEC_CTX(code, ctx);
                    }
                    break;
                }
                case PARAM_TYPE_INT32: {
                    int32_t value = 0;
                    if (param_get_value(name, &value) == 0) {
                        snprintf(ctx, sizeof(ctx), "name=%s;type=%s;value=%ld", name, param_type_to_ctx_name(info.type), (long)value);
                        return CMD_EXEC_CTX(code, ctx);
                    }
                    break;
                }
                case PARAM_TYPE_FLOAT: {
                    float value = 0.0f;
                    if (param_get_value(name, &value) == 0) {
                        snprintf(ctx, sizeof(ctx), "name=%s;type=%s;value=%.6f", name, param_type_to_ctx_name(info.type), value);
                        return CMD_EXEC_CTX(code, ctx);
                    }
                    break;
                }
                default:
                    break;
            }
        }
        return CMD_EXEC_CTX(code, "get_ok");
    }

    if (code == EXIT_OK && argv != NULL && argv[0] != NULL && strcmp(argv[0], "set") == 0 && argc == 3) {
        snprintf(ctx, sizeof(ctx), "name=%s;updated=1", argv[1]);
        return CMD_EXEC_CTX(code, ctx);
    }

    return CMD_EXEC_CODE(code);
}

// Flash 持久化功能（暂时禁用：storage 模块未加入工程，后期恢复）
// 恢复步骤：
//   1. 取消注释 storage.h / zf_driver_flash.h 与上方数据结构
//   2. 用下方被注释的完整实现替换当前桩函数
//   3. 将 project/code/service/storage/storage.c 加入 Keil 工程

exit_code_t param_init(void) {
    // TODO: 后期恢复 storage_init()
    // if (storage_init() != 0) {
    //     return EXIT_FAIL;
    // }
    return EXIT_OK;
}

int param_save(void) {
    // TODO: 后期恢复 Flash 写入
    sys_log_text(warning, "param_save: Flash persistence disabled");
    return 1;
}

int param_load(const char* flash_tag) {
    // TODO: 后期恢复 Flash 读取
    (void)flash_tag;
    sys_log_text(warning, "param_load: Flash persistence disabled");
    return 1;
}

int param_clear_flash(void) {
    // TODO: 后期恢复 storage_clear()
    sys_log_text(warning, "param_clear_flash: Flash persistence disabled");
    return 1;
}

#if 0  // ========== 完整 Flash 持久化实现（后期启用）==========
int param_save(void) {
    param_flash_t flash_data;
    uint16_t i, storage_idx = 0;

    // 清零结构，避免残留脏数据影响CRC
    memset(&flash_data, 0, sizeof(flash_data));

    // 构建Flash数据结构 - 只保存need_storage=true的参数
    flash_data.magic = STORAGE_MAGIC_NUMBER;

    PARAM_ENTER_CRITICAL();
    for (i = 0; i < param_count && storage_idx < PARAM_STORAGE_MAX; i++) {
        if (!param_list[i].need_storage) {
            continue;  // 跳过不需要持久化的参数
        }

        strncpy(flash_data.params[storage_idx].name, param_list[i].name, PARAM_NAME_MAX_LEN - 1);
        flash_data.params[storage_idx].name[PARAM_NAME_MAX_LEN - 1] = '\0';
        flash_data.params[storage_idx].type = param_list[i].type;

        // 读取当前值
        switch (param_list[i].type) {
            case PARAM_TYPE_FLOAT:
                flash_data.params[storage_idx].value.f = *(float*)param_list[i].data;
                break;
            case PARAM_TYPE_INT32:
                flash_data.params[storage_idx].value.i32 = *(int32_t*)param_list[i].data;
                break;
            case PARAM_TYPE_UINT32:
                flash_data.params[storage_idx].value.u32 = *(uint32_t*)param_list[i].data;
                break;
            case PARAM_TYPE_INT16:
                flash_data.params[storage_idx].value.i16 = *(int16_t*)param_list[i].data;
                break;
            case PARAM_TYPE_UINT16:
                flash_data.params[storage_idx].value.u16 = *(uint16_t*)param_list[i].data;
                break;
            case PARAM_TYPE_INT8:
                flash_data.params[storage_idx].value.i8 = *(int8_t*)param_list[i].data;
                break;
            case PARAM_TYPE_UINT8:
                flash_data.params[storage_idx].value.u8 = *(uint8_t*)param_list[i].data;
                break;
            default:
                break;
        }
        storage_idx++;
    }
    PARAM_EXIT_CRITICAL();

    flash_data.count = storage_idx;
    flash_data.reserved = 0;

    // 计算CRC32（跳过magic和crc32字段）
    flash_data.crc32 = storage_crc32((uint8_t*)&flash_data + 8, sizeof(flash_data) - 8);

    // 写入Flash
    return storage_write_data((uint8_t*)&flash_data, sizeof(flash_data));
}

int param_load(const char* flash_tag) {
    param_flash_t flash_data;
    uint32_t calc_crc;
    uint16_t i, j;
    uint16_t restored_count = 0;
    uint16_t snapshot_count = 0;
    param_t param_snapshot[PARAM_MAX_COUNT] = {0};
    typedef struct {
        void *data;
        param_type_t type;
        union {
            float f;
            int32_t i32;
            uint32_t u32;
            int16_t i16;
            uint16_t u16;
            int8_t i8;
            uint8_t u8;
        } value;
    } param_restore_entry_t;
    param_restore_entry_t restore_list[PARAM_STORAGE_MAX] = {0};
    uint16_t restore_count = 0;

    sys_log_text(info, "Module %s: Loading params from flash...", flash_tag);

    // 从Flash读取
    if (storage_read_data((uint8_t*)&flash_data, sizeof(flash_data)) != 0) {
        sys_log_text(error, "Module %s: Storage read failed", flash_tag);
        return 1;
    }

    // 校验魔法数字
    if (flash_data.magic != STORAGE_MAGIC_NUMBER) {
        sys_log_text(warning, "Module %s: Invalid magic number (0x%08lX), using defaults", flash_tag, flash_data.magic);
        return 1;
    }

    // 合法性检查：count 不得超出存储上限
    if (flash_data.count > PARAM_STORAGE_MAX) {
        sys_log_text(error, "Module %s: Param count %u exceeds max %u", flash_tag, flash_data.count, PARAM_STORAGE_MAX);
        return 1;
    }

    // 校验CRC32
    calc_crc = storage_crc32((uint8_t*)&flash_data + 8, sizeof(flash_data) - 8);
    if (calc_crc != flash_data.crc32) {
        sys_log_text(error, "Module %s: CRC check failed (expect 0x%08lX, got 0x%08lX)", flash_tag, flash_data.crc32, calc_crc);
        return 1;
    }

    {
        PARAM_ENTER_CRITICAL();
        snapshot_count = param_count;
        if (snapshot_count > PARAM_MAX_COUNT) {
            snapshot_count = PARAM_MAX_COUNT;
        }
        for (i = 0; i < snapshot_count; i++) {
            param_snapshot[i] = param_list[i];
        }
        PARAM_EXIT_CRITICAL();
    }

    // 先在快照中匹配，构建待恢复列表
    for (i = 0; i < flash_data.count; i++) {
        for (j = 0; j < snapshot_count; j++) {
            if (param_name_equals(param_snapshot[j].name, flash_data.params[i].name)) {
                if (param_snapshot[j].type == flash_data.params[i].type &&
                    param_snapshot[j].need_storage &&
                    param_snapshot[j].data) {
                    restore_list[restore_count].data = param_snapshot[j].data;
                    restore_list[restore_count].type = param_snapshot[j].type;
                    memcpy(&restore_list[restore_count].value,
                           &flash_data.params[i].value,
                           sizeof(restore_list[restore_count].value));
                    restore_count++;
                }
                break;
            }
        }
    }

    // 在短临界区内一次性提交，避免与控制环并发写
    {
        PARAM_ENTER_CRITICAL();
        for (i = 0; i < restore_count; i++) {
            switch (restore_list[i].type) {
                case PARAM_TYPE_FLOAT:
                    *(float*)restore_list[i].data = restore_list[i].value.f;
                    break;
                case PARAM_TYPE_INT32:
                    *(int32_t*)restore_list[i].data = restore_list[i].value.i32;
                    break;
                case PARAM_TYPE_UINT32:
                    *(uint32_t*)restore_list[i].data = restore_list[i].value.u32;
                    break;
                case PARAM_TYPE_INT16:
                    *(int16_t*)restore_list[i].data = restore_list[i].value.i16;
                    break;
                case PARAM_TYPE_UINT16:
                    *(uint16_t*)restore_list[i].data = restore_list[i].value.u16;
                    break;
                case PARAM_TYPE_INT8:
                    *(int8_t*)restore_list[i].data = restore_list[i].value.i8;
                    break;
                case PARAM_TYPE_UINT8:
                    *(uint8_t*)restore_list[i].data = restore_list[i].value.u8;
                    break;
                default:
                    break;
            }
        }
        PARAM_EXIT_CRITICAL();
    }

    restored_count = restore_count;

    sys_log_text(info, "Module %s: Restored %u/%u params from flash", flash_tag, restored_count, flash_data.count);
    return 0;
}

int param_clear_flash(void) {
    return storage_clear();
}
#endif  // ========== 完整 Flash 持久化实现 END ==========
