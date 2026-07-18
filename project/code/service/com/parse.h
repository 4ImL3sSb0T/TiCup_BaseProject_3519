/**
 * @file parse.h
 * @brief 命令解析器头文件
 * 
 * 定义了命令解析器所需的数据结构、宏和函数原型。
 * 支持注册和解析基于字符串的命令。
 */
#ifndef __PARSE_H__
#define __PARSE_H__
#include "string.h"
#include "common/tools/common_def.h"
#include "service/sys/sys_log.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COMMAND_MAX_ARGS 8  ///< 命令的最大参数个数
#define COMMAND_MAX_LEN 128  ///< 命令字符串的最大长度

/**
 * @brief 命令结构体
 * 
 * 定义了一个命令的名称和其对应的处理函数。
 */
typedef struct {
    const char* name; ///< 命令名称
    cmd_exec_result_t (*handler)(i32 seq, int argc, char** argv); ///< 命令处理函数指针
} command_t;

extern command_t command_list[]; ///< 全局命令列表

/**
 * @brief 解析命令字符串
 * 
 * @param input 指向包含命令和参数的字符串的指针
 * @return cmd_exec_result_t 解析结果
 */
cmd_exec_result_t parse_command(i32 seq, char* input);

/**
 * @brief 默认命令处理函数
 * 
 * 当找不到匹配的命令时调用此函数。
 * 
 * @param argc 参数个数
 * @param argv 参数数组
 */
cmd_exec_result_t default_handler(i32 seq, int argc, char** argv);

cmd_exec_result_t parser_helper_handler(i32 seq, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif // !__PARSE_H__
