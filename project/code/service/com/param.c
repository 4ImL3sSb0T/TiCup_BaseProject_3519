#include "param.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "service/fs/fs_service.h"
#include "service/lfs/lfs.h"
#include "service/lfs/lfs_port.h"
#include "service/motion/motor.h"
#include "service/sys/sys_log.h"
#include "zf_common_interrupt.h"
#include "zf_common_typedef.h"

static param_t param_list[PARAM_MAX_COUNT] = {0};
static uint16_t param_count = 0;

/* LFS_NO_MALLOC：file 缓冲须 = cache_size（256） */
static uint8_t s_param_file_buf[LFS_FLASH_CACHE_SIZE];

#define PARAM_ENTER_CRITICAL() uint32 primask = interrupt_global_disable()
#define PARAM_EXIT_CRITICAL()  interrupt_global_enable(primask)

#define PARAM_LINE_MAX 96

typedef union {
    float f;
    int32_t i32;
    uint32_t u32;
    int16_t i16;
    uint16_t u16;
    int8_t i8;
    uint8_t u8;
} param_value_u;

static int param_name_equals(const char *a, const char *b)
{
    size_t len_a = strnlen(a, PARAM_NAME_MAX_LEN);
    size_t len_b = strnlen(b, PARAM_NAME_MAX_LEN);

    if (len_a >= PARAM_NAME_MAX_LEN || len_b >= PARAM_NAME_MAX_LEN) {
        return 0;
    }

    return (len_a == len_b) && (strncmp(a, b, PARAM_NAME_MAX_LEN) == 0);
}

static int parse_unsigned_value(const char *text, uint32_t *out)
{
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);

    if (text == end || (end && *end != '\0') || errno == ERANGE || value > UINT32_MAX) {
        return -1;
    }

    *out = (uint32_t)value;
    return 0;
}

static int parse_signed_value(const char *text, int32_t *out)
{
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);

    if (text == end || (end && *end != '\0') || errno == ERANGE || value > INT32_MAX || value < INT32_MIN) {
        return -1;
    }

    *out = (int32_t)value;
    return 0;
}

static int parse_float_value(const char *text, float *out)
{
    char *end = NULL;
    errno = 0;
    float value = strtof(text, &end);

    if (text == end || (end && *end != '\0') || errno == ERANGE) {
        return -1;
    }

    *out = value;
    return 0;
}

int param_set_value(const char *name, void *data)
{
    if (!name || !data) {
        return -1;
    }

    if (strnlen(name, PARAM_NAME_MAX_LEN) >= PARAM_NAME_MAX_LEN) {
        return -1;
    }

    PARAM_ENTER_CRITICAL();
    for (uint16_t i = 0; i < param_count; i++) {
        if (param_name_equals(param_list[i].name, name)) {
            if (!param_list[i].data) {
                PARAM_EXIT_CRITICAL();
                return -3;
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
                    return -2;
            }
            PARAM_EXIT_CRITICAL();
            return 0;
        }
    }
    PARAM_EXIT_CRITICAL();
    return 1;
}

int param_get(const char *name, param_t *param)
{
    if (!name || !param) {
        return -1;
    }

    size_t name_len = strnlen(name, PARAM_NAME_MAX_LEN);
    if (name_len >= PARAM_NAME_MAX_LEN) {
        return -1;
    }

    PARAM_ENTER_CRITICAL();
    for (uint16_t i = 0; i < param_count; i++) {
        if (param_name_equals(param_list[i].name, name)) {
            *param = param_list[i];
            PARAM_EXIT_CRITICAL();
            return 0;
        }
    }
    PARAM_EXIT_CRITICAL();
    return 1;
}

int param_get_by_index(uint16_t index, param_t *param)
{
    if (!param) {
        return -1;
    }
    PARAM_ENTER_CRITICAL();
    if (index >= param_count) {
        PARAM_EXIT_CRITICAL();
        return -2;
    }
    *param = param_list[index];
    PARAM_EXIT_CRITICAL();
    return 0;
}

int param_get_value(const char *name, void *data)
{
    if (!name || !data) {
        return -1;
    }

    if (strnlen(name, PARAM_NAME_MAX_LEN) >= PARAM_NAME_MAX_LEN) {
        return -1;
    }

    PARAM_ENTER_CRITICAL();
    for (uint16_t i = 0; i < param_count; i++) {
        if (param_name_equals(param_list[i].name, name)) {
            if (!param_list[i].data) {
                PARAM_EXIT_CRITICAL();
                return -3;
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
                    return -2;
            }
            PARAM_EXIT_CRITICAL();
            return 0;
        }
    }
    PARAM_EXIT_CRITICAL();
    return 1;
}

exit_code_t param_add(const char *name, param_type_t type, void *data, bool need_storage)
{
    if (!name || !data || type == PARAM_TYPE_NONE) {
        return EXIT_INVALID_PARAM;
    }

    size_t name_len = strnlen(name, PARAM_NAME_MAX_LEN);
    if (name_len == 0 || name_len >= PARAM_NAME_MAX_LEN) {
        return EXIT_INVALID_PARAM;
    }

    PARAM_ENTER_CRITICAL();

    for (uint16_t i = 0; i < param_count; i++) {
        if (param_name_equals(param_list[i].name, name)) {
            PARAM_EXIT_CRITICAL();
            return EXIT_INVALID_PARAM;
        }
    }

    if (param_count >= PARAM_MAX_COUNT) {
        PARAM_EXIT_CRITICAL();
        return EXIT_NO_MEMORY;
    }

    strncpy(param_list[param_count].name, name, PARAM_NAME_MAX_LEN - 1);
    param_list[param_count].name[PARAM_NAME_MAX_LEN - 1] = '\0';
    param_list[param_count].type = type;
    param_list[param_count].data = data;
    param_list[param_count].need_storage = need_storage;
    param_count++;
    PARAM_EXIT_CRITICAL();

    return EXIT_OK;
}

/* -------------------------------------------------------------------------- */
/* key=value 文本编解码                                                        */
/* -------------------------------------------------------------------------- */

static void param_read_value_unlocked(const param_t *p, param_value_u *out)
{
    switch (p->type) {
        case PARAM_TYPE_UINT8:  out->u8 = *(uint8_t *)p->data; break;
        case PARAM_TYPE_UINT16: out->u16 = *(uint16_t *)p->data; break;
        case PARAM_TYPE_UINT32: out->u32 = *(uint32_t *)p->data; break;
        case PARAM_TYPE_INT8:   out->i8 = *(int8_t *)p->data; break;
        case PARAM_TYPE_INT16:  out->i16 = *(int16_t *)p->data; break;
        case PARAM_TYPE_INT32:  out->i32 = *(int32_t *)p->data; break;
        case PARAM_TYPE_FLOAT:  out->f = *(float *)p->data; break;
        default: memset(out, 0, sizeof(*out)); break;
    }
}

static int param_format_kv_line(const char *name, param_type_t type, const param_value_u *v,
                                char *line, size_t line_size)
{
    int n;

    switch (type) {
        case PARAM_TYPE_UINT8:
            n = snprintf(line, line_size, "%s=%u\n", name, (unsigned)v->u8);
            break;
        case PARAM_TYPE_UINT16:
            n = snprintf(line, line_size, "%s=%u\n", name, (unsigned)v->u16);
            break;
        case PARAM_TYPE_UINT32:
            n = snprintf(line, line_size, "%s=%lu\n", name, (unsigned long)v->u32);
            break;
        case PARAM_TYPE_INT8:
            n = snprintf(line, line_size, "%s=%d\n", name, (int)v->i8);
            break;
        case PARAM_TYPE_INT16:
            n = snprintf(line, line_size, "%s=%d\n", name, (int)v->i16);
            break;
        case PARAM_TYPE_INT32:
            n = snprintf(line, line_size, "%s=%ld\n", name, (long)v->i32);
            break;
        case PARAM_TYPE_FLOAT:
            n = snprintf(line, line_size, "%s=%.6f\n", name, v->f);
            break;
        default:
            return -1;
    }

    if (n <= 0 || (size_t)n >= line_size) {
        return -1;
    }
    return n;
}

static char *param_trim(char *s)
{
    char *end;

    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    end = s + strlen(s) - 1U;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

static int param_name_valid(const char *name)
{
    size_t i;
    size_t len = strnlen(name, PARAM_NAME_MAX_LEN);

    if (len == 0 || len >= PARAM_NAME_MAX_LEN) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        char c = name[i];
        if (!(isalnum((unsigned char)c) || c == '_')) {
            return 0;
        }
    }
    return 1;
}

static int param_parse_value_text(param_type_t type, const char *text, param_value_u *out)
{
    switch (type) {
        case PARAM_TYPE_UINT8: {
            uint32_t parsed;
            if (parse_unsigned_value(text, &parsed) != 0 || parsed > UINT8_MAX) {
                return -1;
            }
            out->u8 = (uint8_t)parsed;
            return 0;
        }
        case PARAM_TYPE_UINT16: {
            uint32_t parsed;
            if (parse_unsigned_value(text, &parsed) != 0 || parsed > UINT16_MAX) {
                return -1;
            }
            out->u16 = (uint16_t)parsed;
            return 0;
        }
        case PARAM_TYPE_UINT32: {
            uint32_t parsed;
            if (parse_unsigned_value(text, &parsed) != 0) {
                return -1;
            }
            out->u32 = parsed;
            return 0;
        }
        case PARAM_TYPE_INT8: {
            int32_t parsed;
            if (parse_signed_value(text, &parsed) != 0 || parsed > INT8_MAX || parsed < INT8_MIN) {
                return -1;
            }
            out->i8 = (int8_t)parsed;
            return 0;
        }
        case PARAM_TYPE_INT16: {
            int32_t parsed;
            if (parse_signed_value(text, &parsed) != 0 || parsed > INT16_MAX || parsed < INT16_MIN) {
                return -1;
            }
            out->i16 = (int16_t)parsed;
            return 0;
        }
        case PARAM_TYPE_INT32: {
            int32_t parsed;
            if (parse_signed_value(text, &parsed) != 0) {
                return -1;
            }
            out->i32 = parsed;
            return 0;
        }
        case PARAM_TYPE_FLOAT: {
            float parsed;
            if (parse_float_value(text, &parsed) != 0) {
                return -1;
            }
            out->f = parsed;
            return 0;
        }
        default:
            return -1;
    }
}

static void param_apply_value(void *data, param_type_t type, const param_value_u *v)
{
    switch (type) {
        case PARAM_TYPE_UINT8:  *(uint8_t *)data = v->u8; break;
        case PARAM_TYPE_UINT16: *(uint16_t *)data = v->u16; break;
        case PARAM_TYPE_UINT32: *(uint32_t *)data = v->u32; break;
        case PARAM_TYPE_INT8:   *(int8_t *)data = v->i8; break;
        case PARAM_TYPE_INT16:  *(int16_t *)data = v->i16; break;
        case PARAM_TYPE_INT32:  *(int32_t *)data = v->i32; break;
        case PARAM_TYPE_FLOAT:  *(float *)data = v->f; break;
        default: break;
    }
}

/** 将 need_storage 参数以字典序写出到 callback（export / save 共用） */
static int param_foreach_storage_sorted(
    int (*emit)(const char *line, size_t len, void *ctx),
    void *ctx)
{
    uint16_t indices[PARAM_MAX_COUNT];
    uint16_t n = 0;
    uint16_t i, j;
    char line[PARAM_LINE_MAX];

    PARAM_ENTER_CRITICAL();
    for (i = 0; i < param_count; i++) {
        if (param_list[i].need_storage && param_list[i].data) {
            indices[n++] = i;
        }
    }
    PARAM_EXIT_CRITICAL();

    /* 选择排序：按 name 字典序（n <= 72） */
    for (i = 0; i < n; i++) {
        uint16_t best = i;
        for (j = (uint16_t)(i + 1U); j < n; j++) {
            if (strcmp(param_list[indices[j]].name, param_list[indices[best]].name) < 0) {
                best = j;
            }
        }
        if (best != i) {
            uint16_t tmp = indices[i];
            indices[i] = indices[best];
            indices[best] = tmp;
        }
    }

    for (i = 0; i < n; i++) {
        param_t snap;
        param_value_u val;
        int len;

        PARAM_ENTER_CRITICAL();
        snap = param_list[indices[i]];
        if (snap.data) {
            param_read_value_unlocked(&snap, &val);
        } else {
            PARAM_EXIT_CRITICAL();
            continue;
        }
        PARAM_EXIT_CRITICAL();

        len = param_format_kv_line(snap.name, snap.type, &val, line, sizeof(line));
        if (len < 0) {
            return -1;
        }
        if (emit(line, (size_t)len, ctx) != 0) {
            return -1;
        }
    }

    return (int)n;
}

static int param_emit_terminal(const char *line, size_t len, void *ctx)
{
    (void)ctx;
    (void)len;
    /* line 含 '\\n'，terminal 打印去掉末尾换行更干净 */
    if (len > 0 && line[len - 1U] == '\n') {
        char tmp[PARAM_LINE_MAX];
        if (len >= sizeof(tmp)) {
            return -1;
        }
        memcpy(tmp, line, len - 1U);
        tmp[len - 1U] = '\0';
        sys_log_text(terminal, "%s", tmp);
    } else {
        sys_log_text(terminal, "%s", line);
    }
    return 0;
}

typedef struct {
    lfs_t *lfs;
    lfs_file_t *file;
} param_file_emit_ctx_t;

static int param_emit_file(const char *line, size_t len, void *ctx)
{
    param_file_emit_ctx_t *fc = (param_file_emit_ctx_t *)ctx;
    lfs_ssize_t wr = lfs_file_write(fc->lfs, fc->file, line, len);
    if (wr < 0 || (size_t)wr != len) {
        return -1;
    }
    return 0;
}

static int param_open_file(lfs_t *lfs, lfs_file_t *file, int flags)
{
    struct lfs_file_config fcfg;

    memset(&fcfg, 0, sizeof(fcfg));
    fcfg.buffer = s_param_file_buf;

    return lfs_file_opencfg(lfs, file, FS_PARAM_PATH, flags, &fcfg);
}

/* -------------------------------------------------------------------------- */
/* 持久化 API                                                                  */
/* -------------------------------------------------------------------------- */

exit_code_t param_init(void)
{
    /* 注册表已是 BSS 零初始化；此处不 load（须等业务 param_add 之后） */
    if (!fs_is_ready()) {
        sys_log_text(warning, "param_init: fs not ready (save/load will fail until fs_init)");
    }
    return EXIT_OK;
}

int param_save(void)
{
    lfs_t *lfs;
    lfs_file_t file;
    param_file_emit_ctx_t emit_ctx;
    const char *header =
        "# BaseProject_3519 params\n"
        "# version=1\n";
    int n;
    int err;

    lfs = fs_lfs();
    if (lfs == NULL) {
        sys_log_text(error, "param_save: filesystem not ready");
        return 1;
    }

    err = param_open_file(lfs, &file, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err != 0) {
        sys_log_text(error, "param_save: open failed (%d)", err);
        return 1;
    }

    emit_ctx.lfs = lfs;
    emit_ctx.file = &file;

    if (param_emit_file(header, strlen(header), &emit_ctx) != 0) {
        sys_log_text(error, "param_save: write header failed");
        (void)lfs_file_close(lfs, &file);
        return 1;
    }

    n = param_foreach_storage_sorted(param_emit_file, &emit_ctx);
    if (n < 0) {
        sys_log_text(error, "param_save: write body failed");
        (void)lfs_file_close(lfs, &file);
        return 1;
    }

    err = lfs_file_sync(lfs, &file);
    if (err != 0) {
        sys_log_text(error, "param_save: sync failed (%d)", err);
        (void)lfs_file_close(lfs, &file);
        return 1;
    }

    err = lfs_file_close(lfs, &file);
    if (err != 0) {
        sys_log_text(error, "param_save: close failed (%d)", err);
        return 1;
    }

    sys_log_text(info, "param_save: wrote %d params to %s", n, FS_PARAM_PATH);
    return 0;
}

static int param_load_apply_line(char *line, uint16_t *restored, uint16_t *skipped)
{
    char *eq;
    char *name;
    char *value_text;
    param_t info;
    param_value_u val;

    line = param_trim(line);
    if (line[0] == '\0' || line[0] == '#') {
        return 0;
    }

    eq = strchr(line, '=');
    if (eq == NULL) {
        sys_log_text(warning, "param_load: skip bad line (no '=')");
        (*skipped)++;
        return 0;
    }

    *eq = '\0';
    name = param_trim(line);
    value_text = param_trim(eq + 1);

    if (!param_name_valid(name) || value_text[0] == '\0') {
        sys_log_text(warning, "param_load: skip invalid key/value");
        (*skipped)++;
        return 0;
    }

    if (param_get(name, &info) != 0) {
        sys_log_text(warning, "param_load: unknown key %s", name);
        (*skipped)++;
        return 0;
    }

    if (!info.need_storage || !info.data) {
        sys_log_text(warning, "param_load: skip non-storage key %s", name);
        (*skipped)++;
        return 0;
    }

    if (param_parse_value_text(info.type, value_text, &val) != 0) {
        sys_log_text(warning, "param_load: bad value for %s", name);
        (*skipped)++;
        return 0;
    }

    /* 单参数提交：短临界区，避免整文件解析占临界区 */
    PARAM_ENTER_CRITICAL();
    param_apply_value(info.data, info.type, &val);
    PARAM_EXIT_CRITICAL();

    (*restored)++;
    return 0;
}

int param_load(const char *flash_tag)
{
    lfs_t *lfs;
    lfs_file_t file;
    char line[PARAM_LINE_MAX];
    size_t line_len = 0;
    int overflow = 0;
    uint16_t restored = 0;
    uint16_t skipped = 0;
    int err;
    const char *tag = (flash_tag != NULL) ? flash_tag : "boot";

    lfs = fs_lfs();
    if (lfs == NULL) {
        sys_log_text(error, "param_load[%s]: filesystem not ready", tag);
        return 1;
    }

    err = param_open_file(lfs, &file, LFS_O_RDONLY);
    if (err == LFS_ERR_NOENT) {
        sys_log_text(info, "param_load[%s]: %s not found, using defaults", tag, FS_PARAM_PATH);
        return 0;
    }
    if (err != 0) {
        sys_log_text(error, "param_load[%s]: open failed (%d)", tag, err);
        return 1;
    }

    for (;;) {
        char c;
        lfs_ssize_t n = lfs_file_read(lfs, &file, &c, 1);

        if (n < 0) {
            sys_log_text(error, "param_load[%s]: read failed (%d)", tag, (int)n);
            (void)lfs_file_close(lfs, &file);
            return 1;
        }
        if (n == 0) {
            if (line_len > 0 && !overflow) {
                line[line_len] = '\0';
                (void)param_load_apply_line(line, &restored, &skipped);
            }
            break;
        }

        if (c == '\n') {
            if (!overflow) {
                line[line_len] = '\0';
                (void)param_load_apply_line(line, &restored, &skipped);
            }
            line_len = 0;
            overflow = 0;
            continue;
        }
        if (c == '\r') {
            continue;
        }

        if (overflow) {
            continue;
        }
        if (line_len + 1U >= sizeof(line)) {
            overflow = 1;
            sys_log_text(warning, "param_load[%s]: line too long, skipped", tag);
            skipped++;
            continue;
        }
        line[line_len++] = c;
    }

    (void)lfs_file_close(lfs, &file);
    sys_log_text(info, "param_load[%s]: restored=%u skipped=%u from %s",
                 tag, (unsigned)restored, (unsigned)skipped, FS_PARAM_PATH);

    /* 注册表已更新；把 motor_kp/ki 同步进速度环 PID（含积分限） */
    motor_apply_param();
    return 0;
}

int param_clear_flash(void)
{
    lfs_t *lfs = fs_lfs();
    int err;

    if (lfs == NULL) {
        sys_log_text(error, "param_clear: filesystem not ready");
        return 1;
    }

    err = lfs_remove(lfs, FS_PARAM_PATH);
    if (err == LFS_ERR_NOENT) {
        sys_log_text(info, "param_clear: %s already absent", FS_PARAM_PATH);
        return 0;
    }
    if (err != 0) {
        sys_log_text(error, "param_clear: remove failed (%d)", err);
        return 1;
    }

    sys_log_text(info, "param_clear: removed %s", FS_PARAM_PATH);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* 命令接口                                                                    */
/* -------------------------------------------------------------------------- */

static const char *param_type_str(param_type_t type)
{
    switch (type) {
        case PARAM_TYPE_UINT8:  return "UINT8";
        case PARAM_TYPE_UINT16: return "UINT16";
        case PARAM_TYPE_UINT32: return "UINT32";
        case PARAM_TYPE_INT8:   return "INT8";
        case PARAM_TYPE_INT16:  return "INT16";
        case PARAM_TYPE_INT32:  return "INT32";
        case PARAM_TYPE_FLOAT:  return "FLOAT";
        default: return "UNKNOWN";
    }
}

static exit_code_t param_command_show_impl(i32 seq, int argc, char **argv)
{
    const char *prefix = NULL;
    size_t prefix_len = 0;
    uint16_t shown = 0;

    (void)seq;

    if (argc >= 2 && argv != NULL && argv[1] != NULL) {
        prefix = argv[1];
        prefix_len = strlen(prefix);
    }

    sys_log_text(terminal, "Parameter List%s%s:",
                 prefix ? " prefix=" : "",
                 prefix ? prefix : "");
    sys_log_text(terminal, "%-20s %-10s %-20s", "Name", "Type", "Value");
    sys_log_text(terminal, "-----------------------------------------------------");

    for (uint16_t i = 0; i < param_count; i++) {
        if (prefix != NULL) {
            if (strncmp(param_list[i].name, prefix, prefix_len) != 0) {
                continue;
            }
        }

        shown++;
        switch (param_list[i].type) {
            case PARAM_TYPE_UINT8:
                sys_log_text(terminal, "%-20s %-10s %-20u",
                             param_list[i].name, param_type_str(param_list[i].type),
                             *(uint8_t *)param_list[i].data);
                break;
            case PARAM_TYPE_UINT16:
                sys_log_text(terminal, "%-20s %-10s %-20u",
                             param_list[i].name, param_type_str(param_list[i].type),
                             *(uint16_t *)param_list[i].data);
                break;
            case PARAM_TYPE_UINT32:
                sys_log_text(terminal, "%-20s %-10s %-20lu",
                             param_list[i].name, param_type_str(param_list[i].type),
                             (unsigned long)*(uint32_t *)param_list[i].data);
                break;
            case PARAM_TYPE_INT8:
                sys_log_text(terminal, "%-20s %-10s %-20d",
                             param_list[i].name, param_type_str(param_list[i].type),
                             *(int8_t *)param_list[i].data);
                break;
            case PARAM_TYPE_INT16:
                sys_log_text(terminal, "%-20s %-10s %-20d",
                             param_list[i].name, param_type_str(param_list[i].type),
                             *(int16_t *)param_list[i].data);
                break;
            case PARAM_TYPE_INT32:
                sys_log_text(terminal, "%-20s %-10s %-20ld",
                             param_list[i].name, param_type_str(param_list[i].type),
                             (long)*(int32_t *)param_list[i].data);
                break;
            case PARAM_TYPE_FLOAT:
                sys_log_text(terminal, "%-20s %-10s %-20.6f",
                             param_list[i].name, param_type_str(param_list[i].type),
                             *(float *)param_list[i].data);
                break;
            default:
                sys_log_text(terminal, "%-20s %-10s %-20s",
                             param_list[i].name, "UNKNOWN", "N/A");
                break;
        }
    }
    sys_log_text(terminal, "-----------------------------------------------------");
    sys_log_text(terminal, "shown=%u total=%u", (unsigned)shown, (unsigned)param_count);
    return EXIT_OK;
}

static exit_code_t param_command_parse_impl(i32 seq, int argc, char **argv)
{
    (void)seq;
    if (argc <= 0 || argv == NULL || argv[0] == NULL) {
        sys_log_text(terminal, "Usage: set/get/save/load/export");
        return EXIT_INVALID_PARAM;
    }

    if (strcmp(argv[0], "set") == 0 && argc == 3) {
        const char *name = argv[1];
        param_t param_info;

        if (param_get(name, &param_info) != 0) {
            sys_log_text(terminal, "Parameter %s not found", name);
            return EXIT_DOES_NOT_EXIST;
        }

        switch (param_info.type) {
            case PARAM_TYPE_UINT8: {
                uint32_t parsed;
                if (parse_unsigned_value(argv[2], &parsed) != 0 || parsed > UINT8_MAX) {
                    sys_log_text(terminal, "Invalid value for %s", name);
                    return EXIT_INVALID_PARAM;
                }
                {
                    uint8_t value = (uint8_t)parsed;
                    if (param_set_value(name, &value) != 0) {
                        return EXIT_FAIL;
                    }
                    sys_log_text(terminal, "Set %s = %u", name, value);
                }
                return EXIT_OK;
            }
            case PARAM_TYPE_UINT16: {
                uint32_t parsed;
                if (parse_unsigned_value(argv[2], &parsed) != 0 || parsed > UINT16_MAX) {
                    sys_log_text(terminal, "Invalid value for %s", name);
                    return EXIT_INVALID_PARAM;
                }
                {
                    uint16_t value = (uint16_t)parsed;
                    if (param_set_value(name, &value) != 0) {
                        return EXIT_FAIL;
                    }
                    sys_log_text(terminal, "Set %s = %u", name, value);
                }
                return EXIT_OK;
            }
            case PARAM_TYPE_UINT32: {
                uint32_t value;
                if (parse_unsigned_value(argv[2], &value) != 0) {
                    sys_log_text(terminal, "Invalid value for %s", name);
                    return EXIT_INVALID_PARAM;
                }
                if (param_set_value(name, &value) != 0) {
                    return EXIT_FAIL;
                }
                sys_log_text(terminal, "Set %s = %lu", name, (unsigned long)value);
                return EXIT_OK;
            }
            case PARAM_TYPE_INT8: {
                int32_t parsed;
                if (parse_signed_value(argv[2], &parsed) != 0 || parsed > INT8_MAX || parsed < INT8_MIN) {
                    sys_log_text(terminal, "Invalid value for %s", name);
                    return EXIT_INVALID_PARAM;
                }
                {
                    int8_t value = (int8_t)parsed;
                    if (param_set_value(name, &value) != 0) {
                        return EXIT_FAIL;
                    }
                    sys_log_text(terminal, "Set %s = %d", name, value);
                }
                return EXIT_OK;
            }
            case PARAM_TYPE_INT16: {
                int32_t parsed;
                if (parse_signed_value(argv[2], &parsed) != 0 || parsed > INT16_MAX || parsed < INT16_MIN) {
                    sys_log_text(terminal, "Invalid value for %s", name);
                    return EXIT_INVALID_PARAM;
                }
                {
                    int16_t value = (int16_t)parsed;
                    if (param_set_value(name, &value) != 0) {
                        return EXIT_FAIL;
                    }
                    sys_log_text(terminal, "Set %s = %d", name, value);
                }
                return EXIT_OK;
            }
            case PARAM_TYPE_INT32: {
                int32_t value;
                if (parse_signed_value(argv[2], &value) != 0) {
                    sys_log_text(terminal, "Invalid value for %s", name);
                    return EXIT_INVALID_PARAM;
                }
                if (param_set_value(name, &value) != 0) {
                    return EXIT_FAIL;
                }
                sys_log_text(terminal, "Set %s = %ld", name, (long)value);
                return EXIT_OK;
            }
            case PARAM_TYPE_FLOAT: {
                float value;
                if (parse_float_value(argv[2], &value) != 0) {
                    sys_log_text(terminal, "Invalid value for %s", name);
                    return EXIT_INVALID_PARAM;
                }
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
        param_t param_info;

        if (param_get(name, &param_info) != 0) {
            sys_log_text(terminal, "Parameter %s not found", name);
            return EXIT_DOES_NOT_EXIST;
        }

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
                sys_log_text(terminal, "Parameter %s value: %lu", name, (unsigned long)data);
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
                sys_log_text(terminal, "Parameter %s value: %ld", name, (long)data);
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
            sys_log_text(terminal, "Parameters saved to %s", FS_PARAM_PATH);
            return EXIT_OK;
        }
        sys_log_text(terminal, "Failed to save parameters");
        return EXIT_FAIL;
    } else if (strcmp(argv[0], "load") == 0 && argc == 1) {
        if (param_load("Command") == 0) {
            sys_log_text(terminal, "Parameters loaded from %s", FS_PARAM_PATH);
            return EXIT_OK;
        }
        sys_log_text(terminal, "Failed to load parameters");
        return EXIT_FAIL;
    } else if (strcmp(argv[0], "export") == 0 && argc == 1) {
        int n = param_foreach_storage_sorted(param_emit_terminal, NULL);
        if (n < 0) {
            return EXIT_FAIL;
        }
        sys_log_text(terminal, "# export count=%d", n);
        return EXIT_OK;
    }

    sys_log_text(terminal, "Usage:");
    sys_log_text(terminal, "  set <name> <value> - Set parameter (RAM)");
    sys_log_text(terminal, "  get <name>         - Get parameter (RAM)");
    sys_log_text(terminal, "  show [prefix]      - List parameters");
    sys_log_text(terminal, "  export             - Print key=value (need_storage)");
    sys_log_text(terminal, "  save               - Save to LFS %s", FS_PARAM_PATH);
    sys_log_text(terminal, "  load               - Load from LFS %s", FS_PARAM_PATH);
    return EXIT_INVALID_PARAM;
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

cmd_exec_result_t param_command_export(i32 seq, int argc, char **argv)
{
    static char fake_argv0[] = "export";
    char *av[1] = { fake_argv0 };
    (void)seq;
    (void)argc;
    (void)argv;
    return CMD_EXEC_CODE(param_command_parse_impl(seq, 1, av));
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
                        snprintf(ctx, sizeof(ctx), "name=%s;type=%s;value=%u",
                                 name, param_type_to_ctx_name(info.type), (unsigned)value);
                        return CMD_EXEC_CTX(code, ctx);
                    }
                    break;
                }
                case PARAM_TYPE_UINT16: {
                    uint16_t value = 0;
                    if (param_get_value(name, &value) == 0) {
                        snprintf(ctx, sizeof(ctx), "name=%s;type=%s;value=%u",
                                 name, param_type_to_ctx_name(info.type), (unsigned)value);
                        return CMD_EXEC_CTX(code, ctx);
                    }
                    break;
                }
                case PARAM_TYPE_UINT32: {
                    uint32_t value = 0;
                    if (param_get_value(name, &value) == 0) {
                        snprintf(ctx, sizeof(ctx), "name=%s;type=%s;value=%lu",
                                 name, param_type_to_ctx_name(info.type), (unsigned long)value);
                        return CMD_EXEC_CTX(code, ctx);
                    }
                    break;
                }
                case PARAM_TYPE_INT8: {
                    int8_t value = 0;
                    if (param_get_value(name, &value) == 0) {
                        snprintf(ctx, sizeof(ctx), "name=%s;type=%s;value=%d",
                                 name, param_type_to_ctx_name(info.type), (int)value);
                        return CMD_EXEC_CTX(code, ctx);
                    }
                    break;
                }
                case PARAM_TYPE_INT16: {
                    int16_t value = 0;
                    if (param_get_value(name, &value) == 0) {
                        snprintf(ctx, sizeof(ctx), "name=%s;type=%s;value=%d",
                                 name, param_type_to_ctx_name(info.type), (int)value);
                        return CMD_EXEC_CTX(code, ctx);
                    }
                    break;
                }
                case PARAM_TYPE_INT32: {
                    int32_t value = 0;
                    if (param_get_value(name, &value) == 0) {
                        snprintf(ctx, sizeof(ctx), "name=%s;type=%s;value=%ld",
                                 name, param_type_to_ctx_name(info.type), (long)value);
                        return CMD_EXEC_CTX(code, ctx);
                    }
                    break;
                }
                case PARAM_TYPE_FLOAT: {
                    float value = 0.0f;
                    if (param_get_value(name, &value) == 0) {
                        snprintf(ctx, sizeof(ctx), "name=%s;type=%s;value=%.6f",
                                 name, param_type_to_ctx_name(info.type), value);
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

    if (code == EXIT_OK && argv != NULL && argv[0] != NULL &&
        (strcmp(argv[0], "save") == 0 || strcmp(argv[0], "load") == 0 || strcmp(argv[0], "export") == 0)) {
        snprintf(ctx, sizeof(ctx), "op=%s;path=%s", argv[0], FS_PARAM_PATH);
        return CMD_EXEC_CTX(code, ctx);
    }

    return CMD_EXEC_CODE(code);
}
