/**
 * @file parser.c
 * @brief 命令解析器实现
 *
 * 实现了基于字符串的命令解析功能。
 * 支持注册命令和对应的处理函数，并能解析输入的字符串，
 * 调用匹配的命令处理函数。
 */
#include "parse.h"
#include "param.h"
#include "service/motion/motor.h"
#include "service/motion/chassis.h"
#include "service/motion/track_follow.h"
#include <string.h>
#include "service/sys/sys_log.h"

/* ARMCLANG (Keil MDK) provides _strtok_r instead of strtok_r */
#define strtok_r _strtok_r

/**
 * @brief 命令列表
 *
 * 存储所有已注册的命令及其处理函数。
 * 在这里可以添加新的命令。
 */
command_t command_list[] = {
    {"set", param_command_parse},
    {"get", param_command_parse},
    {"show", param_command_show},
    {"export", param_command_export},
    {"save", param_command_parse},
    {"load", param_command_parse},
    {"motor", motor_command_handler},
    {"chassis", chassis_command_handler},
    {"track", track_follow_command_handler},
    {"help", parser_helper_handler},
    {NULL, NULL}  // 结束标志
};

/**
 * @brief 解析命令字符串
 * 
 * 将输入的字符串按空格分割为命令和参数，
 * 然后在命令列表中查找匹配的命令并执行其处理函数。
 * 如果找不到匹配的命令，则调用默认处理函数。
 * 
 * @param input 指向包含命令和参数的字符串的指针
 * @return cmd_exec_result_t 解析结果
 */
cmd_exec_result_t parse_command(i32 seq, char *input) {
    if (!input) {
        sys_log_text(terminal, "Empty command");
        return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "empty_input");
    }

    char *argv[COMMAND_MAX_ARGS];
    int argc = 0;

    // Use isolated saveptr to avoid conflict with cmd_service_task
    static char *parse_saveptr = NULL;
    char *token = strtok_r(input, " \n\r", &parse_saveptr);
    while (token != NULL && argc < COMMAND_MAX_ARGS) {
        argv[argc++] = token;
        token = strtok_r(NULL, " \n\r", &parse_saveptr);
    }

    if (argc == 0) {
        sys_log_text(terminal, "Empty command");
        return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "empty_command");
    }

    // 查找匹配的命令并执行
    for (int i = 0; command_list[i].name != NULL; i++) {
        if (strcmp(command_list[i].name, argv[0]) == 0) {
            if (command_list[i].handler != NULL) {
                return command_list[i].handler(seq, argc, argv);
            } else {
                sys_log_text(terminal, "Command %s not supported yet", argv[0]);
                return CMD_EXEC_CTX(EXIT_NOT_SUPPORTED, "handler_null");
            }
        }
    }
    return default_handler(seq, argc, argv);
}

/**
 * @brief 默认命令处理函数
 * 
 * 当找不到匹配的命令时调用此函数。
 * 默认行为是打印未知命令的提示信息。
 * 
 * @param argc 参数个数
 * @param argv 参数数组
 */
cmd_exec_result_t default_handler(i32 seq, int argc, char **argv) {
    (void)seq;
    if (argc <= 0 || argv == NULL || argv[0] == NULL) {
        sys_log_text(terminal, "Unknown command");
        return CMD_EXEC_CTX(EXIT_NOT_SUPPORTED, "unknown");
    }
    sys_log_text(terminal, "Unknown command: %s", argv[0]);
    for (int i = 1; i < argc; i++) {
        sys_log_text(terminal, " Arg %d: %s", i, argv[i]);
    }
    return CMD_EXEC_CTX(EXIT_NOT_SUPPORTED, "unknown_command");
}

cmd_exec_result_t parser_helper_handler(i32 seq, int argc, char **argv) {
    (void)seq;
    (void)argc;
    (void)argv;
    sys_log_text(terminal, "Available commands:");
    for (int i = 0; command_list[i].name != NULL; i++) {
        sys_log_text(terminal, " - %s", command_list[i].name);
    }
    return CMD_EXEC_CTX(EXIT_OK, "help_listed");
}

