#include "imu.h"
#include "MadgwickAHRS/MadgwickAHRS.h"
#include "zf_device_imu963ra.h"
#include "common/filter/low_pass_filter.h"
#include "service/sys/sys_log.h"

// IMU校准时采样的样本数量
#define IMU_CALIBRATE_SAMPLES 100
// 磁力计校准时采样的样本数量（需要更多样本进行椭圆拟合）
#define IMU_MAG_CALIBRATE_SAMPLES 500

// 角度转弧度系数
#define DEG2RAD (0.0174532925f)

// 存储IMU姿态（欧拉角：roll, pitch, yaw）
vec3f imu_attitude = {0};

// 存储陀螺仪数据（角速度）
vec3f imu_gyro = {0};
// 存储加速度计数据
vec3f imu_accel = {0};
// 存储磁力计数据
vec3f imu_mag = {0};

// 存储加速度计校准后的偏移量
vec3f imu_calibrate_acc_offset = {0};
// 存储陀螺仪校准后的偏移量
// [修改]: 使用 vec3f 替代 vec3i 以保留零偏的小数部分，消除积分累计误差
vec3f imu_calibrate_gyro_offset = {0};
// 存储磁力计校准后的偏移量和缩放系数
vec3f imu_calibrate_mag_offset = {0.308f, 0.1956f, 0.1531f};
vec3f imu_calibrate_mag_scale = {0.937f, 1.03f, 1.01f};

static imu_mode_t imu_mode = imu_mode_no_mag;

// 低通滤波器实例
static low_pass_filter_t lpf_acc_x, lpf_acc_y, lpf_acc_z;
static low_pass_filter_t lpf_gyro_x, lpf_gyro_y, lpf_gyro_z;

// IMU初始化函数
exit_code_t imu_init(imu_mode_t mode) {
    sys_log_text(info, "IMU: Initializing IMU module in mode %d...", mode);
    
    sys_log_text(info, "IMU: Initializing IMU963RA sensor...");
    imu963ra_init(); // 初始化IMU963RA传感器
    imu_mode = mode;

    // 初始化低通滤波器，截止频率20Hz，采样频率1000Hz
    float gyro_cutoff_freq = 100.0f;
    float acc_cutoff_freq = 25.0f;
    float sample_freq = 1000.0f;
    
    sys_log_text(info, "IMU: Initializing low-pass filters (acc_cutoff=%.1fHz, sample=%.1fHz)...", acc_cutoff_freq, sample_freq);

    low_pass_filter_init(&lpf_acc_x, acc_cutoff_freq, sample_freq);
    low_pass_filter_init(&lpf_acc_y, acc_cutoff_freq, sample_freq);
    low_pass_filter_init(&lpf_acc_z, acc_cutoff_freq, sample_freq);
    
    low_pass_filter_init(&lpf_gyro_x, gyro_cutoff_freq, sample_freq);
    low_pass_filter_init(&lpf_gyro_y, gyro_cutoff_freq, sample_freq);
    low_pass_filter_init(&lpf_gyro_z, gyro_cutoff_freq, sample_freq);

    sys_log_text(info, "IMU: Initialization completed successfully");
    return EXIT_OK;
}

// IMU数据更新函数
void imu_update() {
    // 从传感器获取原始数据
    imu963ra_get_acc();
    imu963ra_get_gyro();


    // 对加速度计数据进行转换和校准
    imu_accel.x = imu963ra_acc_transition(imu963ra_acc_x - imu_calibrate_acc_offset.x);
    imu_accel.y = imu963ra_acc_transition(imu963ra_acc_y - imu_calibrate_acc_offset.y);
    imu_accel.z = imu963ra_acc_transition(imu963ra_acc_z - imu_calibrate_acc_offset.z);

    // 对加速度计数据进行低通滤波
    // imu_accel.x = low_pass_filter_update(&lpf_acc_x, imu_accel.x);
    // imu_accel.y = low_pass_filter_update(&lpf_acc_y, imu_accel.y);
    // imu_accel.z = low_pass_filter_update(&lpf_acc_z, imu_accel.z);

    // [修改]: 对陀螺仪数据进行校准和转换
    // 先将原始整数转换为浮点数，减去浮点类型的 offset，再进行单位转换，最大限度保留精度
    float raw_gyro_x = (float)imu963ra_gyro_x - imu_calibrate_gyro_offset.x;
    float raw_gyro_y = (float)imu963ra_gyro_y - imu_calibrate_gyro_offset.y;
    float raw_gyro_z = (float)imu963ra_gyro_z - imu_calibrate_gyro_offset.z;

    imu_gyro.x = imu963ra_gyro_transition(raw_gyro_x);
    imu_gyro.y = imu963ra_gyro_transition(raw_gyro_y);
    imu_gyro.z = imu963ra_gyro_transition(raw_gyro_z);

    // 对陀螺仪数据进行低通滤波
    // imu_gyro.x = low_pass_filter_update(&lpf_gyro_x, imu_gyro.x);
    // imu_gyro.y = low_pass_filter_update(&lpf_gyro_y, imu_gyro.y);
    // imu_gyro.z = low_pass_filter_update(&lpf_gyro_z, imu_gyro.z);

    // 使用Madgwick的AHRS更新算法（9轴）来更新姿态
    // 注意：Madgwick算法要求陀螺仪单位为 弧度/秒 (rad/s)
    // 而 imu963ra_gyro_transition 返回的是 角度/秒 (deg/s)
    // 因此需要乘以 DEG2RAD 进行转换
    if (imu_mode == imu_mode_mag) {
        // 对磁力计数据进行转换和校准（应用偏移和缩放）
        imu963ra_get_mag();
        imu_mag.x = (imu963ra_mag_transition(imu963ra_mag_x) - imu_calibrate_mag_offset.x) * imu_calibrate_mag_scale.x;
        imu_mag.y = (imu963ra_mag_transition(imu963ra_mag_y) - imu_calibrate_mag_offset.y) * imu_calibrate_mag_scale.y;
        imu_mag.z = (imu963ra_mag_transition(imu963ra_mag_z) - imu_calibrate_mag_offset.z) * imu_calibrate_mag_scale.z;
        MadgwickAHRSupdate(imu_gyro.x * DEG2RAD, imu_gyro.y * DEG2RAD, imu_gyro.z * DEG2RAD, 
                           imu_accel.x, imu_accel.y, imu_accel.z,
                           imu_mag.x, imu_mag.y, imu_mag.z);
    } else {
        // 如果不使用磁力计，则调用仅6轴的更新函数
        MadgwickAHRSupdateIMU(imu_gyro.x * DEG2RAD, imu_gyro.y * DEG2RAD, imu_gyro.z * DEG2RAD, 
                           imu_accel.x, imu_accel.y, imu_accel.z);
    }
    // 从Madgwick算法中获取欧拉角形式的姿态
    MadgwickAHRS_getEuler(&imu_attitude.x, &imu_attitude.y, &imu_attitude.z);
}

// 获取姿态数据
vec3f imu_get_attitude() {
    return imu_attitude;
}

// 获取陀螺仪数据
vec3f imu_get_gyro() {
    return imu_gyro;
}

// 获取加速度计数据
vec3f imu_get_accel() {
    return imu_accel;
}

// 获取磁力计数据
vec3f imu_get_mag() {
    return imu_mag;
}

// IMU校准函数（校准加速度计和陀螺仪）
void imu_calibrate() {
    // [修改]: 增加采样数量到 1000，提高校准精度
    const int calibrate_samples = 1000;
    sys_log_text(info, "IMU: Starting calibration (samples=%d)...", calibrate_samples);

    system_delay_ms(100);
    
    // 使用 int64 防止累加溢出，或者直接用 double 累加
    double gyro_sum_x = 0, gyro_sum_y = 0, gyro_sum_z = 0;
    double acc_sum_x = 0, acc_sum_y = 0, acc_sum_z = 0;
    
    // 采集指定数量的样本
    for (int i = 0; i < calibrate_samples; i++) {
        imu963ra_get_acc();
        imu963ra_get_gyro();
        
        // 累加陀螺仪原始数据
        gyro_sum_x += imu963ra_gyro_x;
        gyro_sum_y += imu963ra_gyro_y;
        gyro_sum_z += imu963ra_gyro_z;

        acc_sum_x += imu963ra_acc_x;
        acc_sum_y += imu963ra_acc_y;
        // [修改]: 加速度计的 Z 轴通常会受到重力影响，校准时可以选择不使用 Z 轴数据，或者在静止状态下采集以平均重力加速度
        // acc_sum_z += imu963ra_acc_z;
        
        // [修改]: 减少延时到 1ms，因为现在 ODR 已经是 1.66kHz，读取速度可以更快
        system_delay_ms(10); 
        
        if (i % 100 == 0) {
            sys_log_text(debug, "IMU: Calibration progress: %d/%d", i, calibrate_samples);
        }
    }

    // 计算平均值作为偏移量，保留小数精度
    imu_calibrate_gyro_offset.x = (float)(gyro_sum_x / calibrate_samples);
    imu_calibrate_gyro_offset.y = (float)(gyro_sum_y / calibrate_samples);
    imu_calibrate_gyro_offset.z = (float)(gyro_sum_z / calibrate_samples);

    imu_calibrate_acc_offset.x = (float)(acc_sum_x / calibrate_samples);
    imu_calibrate_acc_offset.y = (float)(acc_sum_y / calibrate_samples);
    imu_calibrate_acc_offset.z = (float)(acc_sum_z / calibrate_samples);
    
    sys_log_text(info, "IMU: Calibration completed");
    // 打印浮点型 Offset
    sys_log_text(info, "IMU: Gyro offsets (x=%.3f, y=%.3f, z=%.3f)", 
                 imu_calibrate_gyro_offset.x, imu_calibrate_gyro_offset.y, imu_calibrate_gyro_offset.z);
    sys_log_text(info, "IMU: Acc offsets (x=%.3f, y=%.3f, z=%.3f)", 
                 imu_calibrate_acc_offset.x, imu_calibrate_acc_offset.y, imu_calibrate_acc_offset.z);
}

// 磁力计校准函数（需要在不同方向旋转设备）
void imu_calibrate_mag() {
    sys_log_text(info, "IMU: Starting magnetometer calibration (samples=%d)...", IMU_MAG_CALIBRATE_SAMPLES);
    sys_log_text(warning, "IMU: Rotate the device in all directions during calibration!");
    
    float mag_x_max = -32768, mag_x_min = 32767;
    float mag_y_max = -32768, mag_y_min = 32767;
    float mag_z_max = -32768, mag_z_min = 32767;
    
    // 采集指定数量的样本，在采集过程中需要旋转设备
    for (int i = 0; i < IMU_MAG_CALIBRATE_SAMPLES; i++) {
        imu963ra_get_mag();
        
        float mag_x = imu963ra_mag_transition(imu963ra_mag_x);
        float mag_y = imu963ra_mag_transition(imu963ra_mag_y);
        float mag_z = imu963ra_mag_transition(imu963ra_mag_z);
        
        // 记录每个轴的最大值和最小值
        if (mag_x > mag_x_max) mag_x_max = mag_x;
        if (mag_x < mag_x_min) mag_x_min = mag_x;
        if (mag_y > mag_y_max) mag_y_max = mag_y;
        if (mag_y < mag_y_min) mag_y_min = mag_y;
        if (mag_z > mag_z_max) mag_z_max = mag_z;
        if (mag_z < mag_z_min) mag_z_min = mag_z;
        
        if (i % 50 == 0) {
            sys_log_text(debug, "IMU: Mag calibration progress: %d/%d", i, IMU_MAG_CALIBRATE_SAMPLES);
        }
        
        system_delay_ms(100); // 延时
    }
    
    // 计算硬铁偏移（每个轴的中心点）
    imu_calibrate_mag_offset.x = (mag_x_max + mag_x_min) / 2.0f;
    imu_calibrate_mag_offset.y = (mag_y_max + mag_y_min) / 2.0f;
    imu_calibrate_mag_offset.z = (mag_z_max + mag_z_min) / 2.0f;
    
    // 计算软铁缩放系数（使各轴范围一致）
    float mag_x_range = mag_x_max - mag_x_min;
    float mag_y_range = mag_y_max - mag_y_min;
    float mag_z_range = mag_z_max - mag_z_min;
    
    // 使用平均范围作为参考
    float avg_range = (mag_x_range + mag_y_range + mag_z_range) / 3.0f;
    
    // 防止除零
    if (mag_x_range > 0.01f) imu_calibrate_mag_scale.x = avg_range / mag_x_range;
    if (mag_y_range > 0.01f) imu_calibrate_mag_scale.y = avg_range / mag_y_range;
    if (mag_z_range > 0.01f) imu_calibrate_mag_scale.z = avg_range / mag_z_range;
    
    sys_log_text(info, "IMU: Magnetometer calibration completed");
    sys_log_text(info, "IMU: Mag offsets (x=%.3f, y=%.3f, z=%.3f)", 
                 imu_calibrate_mag_offset.x, imu_calibrate_mag_offset.y, imu_calibrate_mag_offset.z);
    sys_log_text(info, "IMU: Mag scales (x=%.3f, y=%.3f, z=%.3f)", 
                 imu_calibrate_mag_scale.x, imu_calibrate_mag_scale.y, imu_calibrate_mag_scale.z);
}

vec3f imu_get_mag_offset() {
	return imu_calibrate_mag_offset;
}

vec3f imu_get_mag_scale() {
	return imu_calibrate_mag_scale;
}

