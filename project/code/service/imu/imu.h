#ifndef __IMU_H__
#define __IMU_H__

#include "zf_common_headfile.h"
#include "common/tools/common_def.h"
#include "common/tools/vec_math.h"
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
void imu_calibrate();
void imu_calibrate_mag();
vec3f imu_get_mag_offset();
vec3f imu_get_mag_scale();

#ifdef __cplusplus
}
#endif

#endif // !__IMU_H__
