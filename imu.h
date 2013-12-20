// vi:sw=4:ts=4

#pragma once

uint32_t imu_init(enum SPI_Channel channel);

uint16_t imu_accel_reg_read(uint8_t *buf);
uint16_t imu_read_regs(uint8_t *buf);

uint16_t imu_fifo_size();
uint16_t imu_fifo_read(uint16_t count, uint8_t *buf);
void imu_fifo_reset();

// Based on MPU-6500 Register Map and Descriptions Revision 2.0

#define CHIP_ID 0x70

typedef enum MPU_Registers {
	MPU_REG_ACC_SELF_TEST_X		 = 13,
	MPU_REG_ACC_SELF_TEST_Y		 = 14,
	MPU_REG_ACC_SELF_TEST_Z		 = 15,
	MPU_REG_SAMPLE_RATE_DIVIDER	 = 25,
	MPU_REG_CONFIG				 = 26,
	MPU_REG_GYRO_CFG			 = 27,
	MPU_REG_ACC_CFG				 = 28,
	MPU_REG_ACC_CFG2			 = 29,
	MPU_REG_ACCEL_ODR			 = 30,
	MPU_REG_WOM_THR				 = 31,
	MPU_REG_FIFO_EN				 = 35,
	MPU_REG_INT_CFG				 = 55,
	MPU_REG_INT_EN				 = 56,
	MPU_REG_INT_STS				 = 58,
	MPU_REG_ACC_X_HI			 = 59,
	MPU_REG_ACC_X_LO			 = 60,
	MPU_REG_ACC_Y_HI			 = 61,
	MPU_REG_ACC_Y_LO			 = 62,
	MPU_REG_ACC_Z_HI			 = 63,
	MPU_REG_ACC_Z_LO			 = 64,
	MPU_REG_TMP_HI				 = 65,
	MPU_REG_TMP_LO				 = 66,
	MPU_REG_GYRO_X_HI			 = 67,
	MPU_REG_GYRO_X_LO			 = 68,
	MPU_REG_GYRO_Y_HI			 = 69,
	MPU_REG_GYRO_Y_LO			 = 70,
	MPU_REG_GYRO_Z_HI			 = 71,
	MPU_REG_GYRO_Z_LO			 = 72,
	MPU_REG_SIG_RST				 = 104,
	MPU_REG_ACCEL_INTEL_CTRL	 = 105,
	MPU_REG_USER_CTL			 = 106,
	MPU_REG_PWR_MGMT_1			 = 107,
	MPU_REG_PWR_MGMT_2			 = 108,
	MPU_REG_FIFO_CNT_HI			 = 114,
	MPU_REG_FIFO_CNT_LO			 = 115,
	MPU_REG_FIFO				 = 116,
	MPU_REG_WHO_AM_I			 = 117,
} MPU_Register_t;

enum MPU_Reg_Bits {
	CONFIG_FIFO_MODE_DROP = (1UL << 6),
	CONFIG_DLPF_MASK = 0x7,

	INT_CFG_ACT_LO        = (1UL << 7),
	INT_CFG_ACT_HI        = (0UL << 7),
	INT_CFG_PUSH_PULL     = (0UL << 6),
	INT_CFG_OPEN_DRN      = (1UL << 6),
	INT_CFG_LATCH_OUT     = (1UL << 5),
	INT_CFG_PULSE_OUT     = (0UL << 5),
	INT_CFG_CLR_ANY_READ  = (1UL << 4),
	INT_CFG_CLR_ON_STS    = (1UL << 4),
	INT_CFG_FSYNC_ACT_LO  = (1UL << 3),
	INT_CFG_FSYNC_ACT_HI  = (0UL << 3),
	INT_CFG_INT_ON_FSYNC  = (1UL << 2),
	INT_CFG_BYPASS_EN     = (1UL << 1),

	INT_EN_WOM         = (1UL << 6),
	INT_EN_FIFO_OVRFLO = (1UL << 4),
	INT_EN_FSYNC       = (1UL << 3),
	INT_EN_RAW_READY   = (1UL << 0),

	INT_STS_FIFO_OVRFLO = (1UL << 4),

	CONFIG_LPF_32kHz_8800bw = 0,
	CONFIG_LPF_32kHz_3600bw = 0,
	CONFIG_LPF_8kHz_250bw   = 0,
	CONFIG_LPF_1kHz_184bw   = 1,
	CONFIG_LPF_1kHz_92bw    = 2,
	CONFIG_LPF_1kHz_41bw    = 3,
	CONFIG_LPF_1kHz_20bw    = 4,
	CONFIG_LPF_1kHz_10bw    = 5,
	CONFIG_LPF_1kHz_5bw     = 6,
	CONFIG_LPF_8kHz_3600bw  = 7,

	GYRO_CFG_X_TEST         = (1UL << 7),
	GYRO_CFG_Y_TEST         = (1UL << 6),
	GYRO_CFG_Z_TEST         = (1UL << 5),
	GYRO_CFG_RATE_2k_DPS    = 0x3,
	GYRO_CFG_RATE_1k_DPS    = 0x2,
	GYRO_CFG_RATE_500_DPS   = 0x1,
	GYRO_CFG_RATE_250_DPS   = 0,
	GYRO_CFG_RATE_OFFET     = 3,
	GYRO_CFG_FCHOICE_00     = 0x3,
	GYRO_CFG_FCHOICE_01     = 0x2,
	GYRO_CFG_FCHOICE_11     = 0x0,
	GYRO_CFG_FCHOICE_B_MASK = 0x3,

	ACCEL_CFG_X_TEST     = (1UL << 7),
	ACCEL_CFG_Y_TEST     = (1UL << 6),
	ACCEL_CFG_Z_TEST     = (1UL << 5),
	ACCEL_CFG_SCALE_2G   = (0UL << 4) | (0UL << 3),
	ACCEL_CFG_SCALE_4G   = (0UL << 4) | (1UL << 3),
	ACCEL_CFG_SCALE_8G   = (1UL << 4) | (0UL << 3),
	ACCEL_CFG_SCALE_16G  = (1UL << 4) | (1UL << 3),
	ACCEL_CFG_SCALE_OFFSET = 0x3,

	ACCEL_CFG2_FCHOICE_1        = (0UL << 3),
	ACCEL_CFG2_FCHOICE_0        = (1UL << 3),
	ACCEL_CFG2_LPF_4kHz_1130bw  = 0,
	ACCEL_CFG2_LPF_1kHz_460bw   = 0,
	ACCEL_CFG2_LPF_1kHz_184bw   = 1,
	ACCEL_CFG2_LPF_1kHz_92bw    = 2,
	ACCEL_CFG2_LPF_1kHz_41bw    = 3,
	ACCEL_CFG2_LPF_1kHz_20bw    = 4,
	ACCEL_CFG2_LPF_1kHz_10bw    = 5,
	ACCEL_CFG2_LPF_1kHz_5bw     = 6,
	ACCEL_CFG2_LPF_1kHz_460bw_2 = 7,
	ACCEL_CFG2_FIFO_SIZE_512    = (0UL << 7) | (0UL << 6),
	ACCEL_CFG2_FIFO_SIZE_1024   = (0UL << 7) | (1UL << 6),
	ACCEL_CFG2_FIFO_SIZE_2048   = (1UL << 7) | (0UL << 6),
	ACCEL_CFG2_FIFO_SIZE_4096   = (1UL << 7) | (1UL << 6),

	FIFO_EN_QUEUE_TEMP   = (1UL << 7),
	FIFO_EN_QUEUE_GYRO_X = (1UL << 6),
	FIFO_EN_QUEUE_GYRO_Y = (1UL << 5),
	FIFO_EN_QUEUE_GYRO_Z = (1UL << 4),
	FIFO_EN_QUEUE_ACCEL  = (1UL << 3),
	FIFO_EN_QUEUE_SLAVE2 = (1UL << 2),
	FIFO_EN_QUEUE_SLAVE1 = (1UL << 1),
	FIFO_EN_QUEUE_SLAVE0 = (1UL << 0),

	ACCEL_INTEL_CTRL_EN        = (1UL << 7),
	ACCEL_INTEL_CTRL_6500_MODE = (1UL << 6),

	USR_CTL_FIFO_EN  = (1UL << 6),
	USR_CTL_I2C_EN   = (1UL << 5),
	USR_CTL_I2C_DIS  = (1UL << 4),
	USR_CTL_FIFO_RST = (1UL << 2),
	USR_CTL_SIG_RST  = (1UL << 0),

	PWR_MGMT_1_RESET = (1UL << 7),
	PWR_MGMT_1_SLEEP = (1UL << 6),
	PWR_MGMT_1_CYCLE = (1UL << 5),
	PWR_MGMT_1_GYRO_STANDBY = (1UL << 4),
	PWR_MGMT_1_PD_PTAT = (1UL << 3),
	PWR_MGMT_1_CLK_STOP = 0x7,
	PWR_MGMT_1_CLK_OSC = 0x0,
	PWR_MGMT_1_CLK_BEST = 0x1,

	PWR_MGMT_2_LP_WAKE_OFFSET = 0x6,
	PWR_MGMT_2_WAKE_1_25HZ = 0x0,
	PWR_MGMT_2_WAKE_5HZ    = 0x1,
	PWR_MGMT_2_WAKE_20HZ   = 0x2,
	PWR_MGMT_2_WAKE_40HZ   = 0x3,
	PWR_MGMT_2_ACCEL_X_DIS = (1UL << 5),
	PWR_MGMT_2_ACCEL_Y_DIS = (1UL << 4),
	PWR_MGMT_2_ACCEL_Z_DIS = (1UL << 3),
	PWR_MGMT_2_GYRO_X_DIS  = (1UL << 2),
	PWR_MGMT_2_GYRO_Y_DIS  = (1UL << 1),
	PWR_MGMT_2_GYRO_Z_DIS  = (1UL << 0),

};
