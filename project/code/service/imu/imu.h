#ifndef __IMU_H__
#define __IMU_H__

#include "zf_common_headfile.h"
#include "common/tools/common_def.h"
#include "common/tools/vec_math.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif


typedef enum {
    imu_mode_mag,
    imu_mode_no_mag
} imu_mode_t;

exit_code_t imu_init(imu_mode_t mode);

void imu_update();

vec3f imu_get_attitude();
vec3f imu_get_gyro();
vec3f imu_get_accel();
vec3f imu_get_mag();

/**
 * @brief 校准加速度计 / 陀螺仪零偏
 * @param use_flash
 *   true  — 若 Flash 中已有有效参数则直接使用；否则现场校准并写入 Flash
 *   false — 强制重新校准，并覆盖写入 Flash
 * @note  若 use_flash=true，须先完成 param_load()
 */
void imu_calibrate(bool use_flash);

/**
 * @brief 磁力计硬/软铁校准（需旋转设备）
 * @param use_flash 语义同 imu_calibrate()
 */
void imu_calibrate_mag(bool use_flash);

vec3f imu_get_mag_offset();
vec3f imu_get_mag_scale();

#ifdef __cplusplus
}
#endif

#endif // !__IMU_H__
