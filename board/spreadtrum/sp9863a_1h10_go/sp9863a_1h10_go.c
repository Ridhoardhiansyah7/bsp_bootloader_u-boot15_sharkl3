/*
 * (C) Copyright 2014
 * David Feng <fenghua@phytium.com.cn>
 * Sharma Bhupesh <bhupesh.sharma@freescale.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#include <asm/io.h>
#include <asm/arch/sprd_reg.h>
#include <common.h>
#include <malloc.h>
#include <boot_mode.h>
#include <adi_hal_internal.h>
#include <chipram_env.h>
#include <sprd_adc.h>
#include <sprd_led.h>
#include <sprd_battery.h>
#include <clk.h>

DECLARE_GLOBAL_DATA_PTR;
phys_size_t real_ram_size = 0x40000000;

phys_size_t get_real_ram_size(void)
{
        return real_ram_size;
}

void enable_global_clocks(void)
{
	__raw_writel(BIT_AON_APB_MM_EB, REG_AON_APB_APB_EB0 + 0x1000);
	__raw_writel(BIT_AON_APB_MM_VSP_EB, REG_AON_APB_APB_EB1 + 0x1000);
	__raw_writel(BIT_AON_APB_CLK_GPU_EB | BIT_AON_APB_CLK_DISP_EB,
			REG_AON_APB_AON_CLK_TOP_CFG + 0x1000);
	while (__raw_readl(REG_PMU_APB_PWR_STATUS0_DBG) & BIT_PMU_APB_PD_MM_TOP_STATE(~0));
	__raw_writel(BIT_MM_AHB_CKG_EB, REG_MM_AHB_AHB_EB + 0x1000);
}

extern void setup_chipram_env(void);
int board_init(void)
{
	setup_chipram_env();
#ifndef CONFIG_FPGA
	gd->bd->bi_boot_params = PHYS_SDRAM_1 + 0x100;
#ifdef CONFIG_CLK
	clk_init();
#endif
	/*config serial console*/
#ifdef CONFIG_DM_SERIAL
	serial_init();
	console_init_f();
#endif
	ADI_init();
	/*FPGA forbiden*/
	regulator_init();
	pmic_adc_Init();
	/*FPGA forbiden*/
	pin_init();
	sprd_gpio_init();
	misc_init();
	sprd_eic_init();
	sprd_led_init();
	/*FPGA forbiden*/
	sprd_pmu_lowpower_init();
	enable_global_clocks();
#endif
	return 0;
}
int dram_init(void)
{
#ifdef CONFIG_DDR_AUTO_DETECT
  ulong sdram_base = CONFIG_SYS_SDRAM_BASE;
  ulong sdram_size = 0;
  chipram_env_t * env = CHIPRAM_ENV_LOCATION;
  if (CHIPRAM_ENV_MAGIC != env->magic) {
    printf("Chipram magic wrong , ddr data may be broken\n");
    return 0;
  }

  real_ram_size = 0;

  if (env->cs_number == 1) {
    real_ram_size += env->cs0_size;
    debugf("dram cs0 size %x\n",env->cs0_size);
  } else if(env->cs_number == 2){
    real_ram_size += env->cs0_size;
    real_ram_size += env->cs1_size;
    debugf("dram cs0 size %x\ndram cs1 size %x\n",env->cs0_size, env->cs1_size);
  }

  //real_ram_size = get_ram_size((volatile void *)sdram_base, real_ram_size);
#else
  real_ram_size = REAL_SDRAM_SIZE;
#endif

  gd->ram_size = PHYS_SDRAM_1_SIZE;

  return 0;
}

#ifdef CONFIG_DUAL_DDR
void dram_init_banksize(void)
{
	gd->bd->bi_dram[0].start = PHYS_SDRAM_1;
	gd->bd->bi_dram[0].size = PHYS_SDRAM_1_SIZE;
	gd->bd->bi_dram[1].start = PHYS_SDRAM_2;
	gd->bd->bi_dram[1].size = PHYS_SDRAM_2_SIZE;
}
#endif

/* --- Hardware & I2C Communication --- */
#define BQ2560X_I2C_BUS         4       
#define BQ2560X_ADDR            0x6b    

/* --- IC Register Map (Texas Instruments BQ2560x) --- */
#define BQ2560X_REG_CONTROL    0x01    
#define BQ2560X_REG_VOLTAGE    0x02    
#define BQ2560X_REG_CURRENT    0x04    
#define BQ2560X_REG_SAFETY     0x06    

/* --- Register Configuration Values & Bitmasks --- */
#define BQ2560X_SAFETY_LIMITS  0xb7    
#define BQ2560X_VOLTAGE_CFG    ((42 << 2) | 0x02)
#define BQ2560X_CURRENT_CFG    ((5 << 3) | 2)     
#define BQ2560X_CONTROL_CFG    0x38    
#define BQ2560X_CEN_BIT        BIT(2) 

/* --- Battery NTC & Thermal Protection Parameters --- */
#define BAT_NTC_ADC_CHANNEL     0       
#define BAT_NTC_SAMPLES         15      
#define BAT_TEMP_INVALID        (-30000)
#define BAT_TEMP_MIN_DC         0       
#define BAT_TEMP_MAX_DC         600    
#define BAT_TEMP_HYST_DC        30      

int misc_init_r(void)
{
	/*reserver for future use*/
	return 0;
}

static const struct {
    int uv;
    int temp_dc;
} bat_ntc_table[] = {
    { 1095000, -200 }, { 986000, -150 }, { 878000, -100 }, { 775000, -50 },
    {  678000,    0 }, { 590000,   50 }, { 510000,  100 }, { 440000, 150 },
    {  378000,  200 }, { 324000,  250 }, { 278000,  300 }, { 238000, 350 },
    {  204000,  400 }, { 175000,  450 }, { 150000,  500 }, { 129000, 550 },
    {  111000,  600 }, {  96000,  650 },
};

static int bq2560x_battery_temp_dc(void)
{
	int32_t raw[BAT_NTC_SAMPLES];
	int32_t sum = 0;
	int i, uv;

	if (pmic_adc_get_values(BAT_NTC_ADC_CHANNEL, ADC_SCALE_0,
				BAT_NTC_SAMPLES, raw))
		return BAT_TEMP_INVALID;

	for (i = 0; i < BAT_NTC_SAMPLES; i++)
		sum += raw[i];

	uv = sprd_chan_small_adc_to_vol(BAT_NTC_ADC_CHANNEL, ADC_SCALE_0, 0,
					sum / BAT_NTC_SAMPLES) * 1000;

	if (uv >= bat_ntc_table[0].uv)
		return bat_ntc_table[0].temp_dc;

	for (i = 1; i < ARRAY_SIZE(bat_ntc_table); i++) {
		int hi_uv = bat_ntc_table[i - 1].uv;
		int lo_uv = bat_ntc_table[i].uv;

		if (uv < lo_uv)
			continue;

		return bat_ntc_table[i].temp_dc +
		       (bat_ntc_table[i - 1].temp_dc - bat_ntc_table[i].temp_dc) *
		       (uv - lo_uv) / (hi_uv - lo_uv);
	}

	return bat_ntc_table[ARRAY_SIZE(bat_ntc_table) - 1].temp_dc;
}

static int bq2560x_set_charging(int enable)
{
	struct udevice *chg;
	int ret, val;

	ret = i2c_get_chip_for_busnum(BQ2560X_I2C_BUS, BQ2560X_ADDR, 1, &chg);
	if (ret)
		return ret;

	val = dm_i2c_reg_read(chg, BQ2560X_REG_CONTROL);
	if (val < 0)
		return val;

	if (enable)
		val &= ~BQ2560X_CEN_BIT;
	else
		val |= BQ2560X_CEN_BIT;

	ret = dm_i2c_reg_write(chg, BQ2560X_REG_CONTROL, (u8)val);
	return ret < 0 ? ret : 0;
}

int bq2560x_temp_guard(int blocked)
{
	int temp = bq2560x_battery_temp_dc();
	int min = BAT_TEMP_MIN_DC, max = BAT_TEMP_MAX_DC;
	int block;

	if (temp == BAT_TEMP_INVALID) {
		printf("[uboot] bq2560x: battery temperature unreadable, guard inactive\n");
		return blocked;
	}

	if (blocked) {
		min += BAT_TEMP_HYST_DC;
		max -= BAT_TEMP_HYST_DC;
	}

	block = (temp < min || temp > max);

	if (block != blocked) {
		if (bq2560x_set_charging(!block)) {
			printf("[uboot] bq2560x: failed to %s charging\n",
			       block ? "inhibit" : "resume");
			return blocked;
		}
		printf("[uboot] bq2560x: battery %d.%dC, charging %s\n",
		       temp / 10, temp < 0 ? -(temp % 10) : temp % 10,
		       block ? "inhibited" : "resumed");
	}

	return block;
}

int bq2560x_battery_temp(void)
{
	return bq2560x_battery_temp_dc();
}

static void bq2560x_init(void)
{
	struct udevice *chg;
	int ret;

	ret = i2c_get_chip_for_busnum(BQ2560X_I2C_BUS, BQ2560X_ADDR, 1, &chg);
	if (ret) {
		printf("[uboot] bq2560x: i2c%d addr 0x%02x not found (%d)\n",
		       BQ2560X_I2C_BUS, BQ2560X_ADDR, ret);
		return;
	}

	ret = dm_i2c_reg_write(chg, BQ2560X_REG_SAFETY, BQ2560X_SAFETY_LIMITS);
	if (ret < 0) {
		printf("[uboot] bq2560x: REG06 write failed (%d)\n", ret);
		return;
	}

	ret = dm_i2c_reg_read(chg, BQ2560X_REG_SAFETY);
	if (ret < 0)
		printf("[uboot] bq2560x: REG06 read-back failed (%d)\n", ret);
	else if (ret == BQ2560X_SAFETY_LIMITS)
		printf("[uboot] bq2560x: safety limits set, REG06=0x%02x (4.34V/1984mA)\n",
		       ret);
	else
		printf("[uboot] bq2560x: REG06 locked, kept 0x%02x (wanted 0x%02x)\n",
		       ret, BQ2560X_SAFETY_LIMITS);

	ret = dm_i2c_reg_write(chg, BQ2560X_REG_VOLTAGE, BQ2560X_VOLTAGE_CFG);
	if (ret < 0)
		printf("[uboot] bq2560x: REG02 write failed (%d)\n", ret);

	ret = dm_i2c_reg_write(chg, BQ2560X_REG_CURRENT, BQ2560X_CURRENT_CFG);
	if (ret < 0)
		printf("[uboot] bq2560x: REG04 write failed (%d)\n", ret);

	ret = dm_i2c_reg_write(chg, BQ2560X_REG_CONTROL, BQ2560X_CONTROL_CFG);
	if (ret < 0)
		printf("[uboot] bq2560x: REG01 write failed (%d)\n", ret);

	printf("[uboot] bq2560x: charge cfg 4.34V/1240mA (REG02=0x%02x REG04=0x%02x REG01=0x%02x)\n",
	       BQ2560X_VOLTAGE_CFG, BQ2560X_CURRENT_CFG, BQ2560X_CONTROL_CFG);

	bq2560x_temp_guard(0);
}

static void battery_init(void)
{

	/* Must run before anything else can touch the charger. */
	bq2560x_init();
	
	sprdchg_common_cfg();
	//sprdchg_fan54015_init();
	//sprdbat_init();
#ifdef CONFIG_BQ2560X_CHARGE_IC
	sprdchg_bq2560x_init();
#endif
	sprdbat_init();
}

int board_late_init(void)
{

 boot_mode_t boot_role;
        extern chipram_env_t* get_chipram_env(void);
        chipram_env_t* cr_env = get_chipram_env();
        boot_role = cr_env->mode;

	boot_pwr_check();

#if !defined(CONFIG_FPGA)
#ifdef CONFIG_NAND_BOOT
	//extern int nand_ubi_dev_init(void);
	nand_ubi_dev_init();
	debugf("nand ubi init OK!\n");
#endif
	battery_init();
	debugf("CHG init OK!\n");
#endif
	board_keypad_init();
	return 0;
}


CBOOT_FUNC s_boot_func_array[CHECK_BOOTMODE_FUN_NUM] = {
	get_mode_from_bat_low,
	write_sysdump_before_boot_extend,
	get_mode_from_miscdata_boot_flag,
	/* 1 get mode from file*/
	get_mode_from_file_extend,
	/* 2 get mode from watch dog*/
	get_mode_from_watchdog,
	/*3 get mode from alarm register*/
	get_mode_from_alarm_register,
	/*0 get mode from calibration detect*/
	get_mode_from_pctool,
	/*4 get mode from charger*/
	get_mode_from_charger,
	/*5 get mode from keypad*/
	get_mode_from_keypad,
	/*6 get mode from gpio*/
	get_mode_from_gpio_extend,

	/*shutdown device*/
	//get_mode_from_shutdown,
	0
};



void board_boot_mode_regist(CBOOT_MODE_ENTRY *array)
{
	MODE_REGIST(CMD_NORMAL_MODE, normal_mode);
	MODE_REGIST(CMD_RECOVERY_MODE, recovery_mode);
	MODE_REGIST(CMD_FASTBOOT_MODE, fastboot_mode);
	MODE_REGIST(CMD_WATCHDOG_REBOOT, watchdog_mode);
	MODE_REGIST(CMD_AP_WATCHDOG_REBOOT, ap_watchdog_mode);
	MODE_REGIST(CMD_UNKNOW_REBOOT_MODE, unknow_reboot_mode);
	MODE_REGIST(CMD_PANIC_REBOOT, panic_reboot_mode);
	MODE_REGIST(CMD_AUTODLOADER_REBOOT, autodloader_mode);
	MODE_REGIST(CMD_SPECIAL_MODE, special_mode);
	MODE_REGIST(CMD_CHARGE_MODE, charge_mode);
	MODE_REGIST(CMD_ENGTEST_MODE,engtest_mode);
	/*MODE_REGIST(CMD_FACTORYTEST_MODE, factorytest_mode);*/
	MODE_REGIST(CMD_CALIBRATION_MODE, calibration_mode);
	MODE_REGIST(CMD_EXT_RSTN_REBOOT_MODE, normal_mode);
	MODE_REGIST(CMD_IQ_REBOOT_MODE, iq_mode);
	MODE_REGIST(CMD_ALARM_MODE, alarm_mode);
	MODE_REGIST(CMD_SPRDISK_MODE, sprdisk_mode);
	MODE_REGIST(CMD_AUTOTEST_MODE, autotest_mode);
	MODE_REGIST(CMD_APKMMI_MODE, apkmmi_mode);
	MODE_REGIST(CMD_UPT_MODE, upt_mode);
	MODE_REGIST(CMD_APKMMI_AUTO_MODE, apkmmi_auto_mode);
	MODE_REGIST(CMD_ABNORMAL_REBOOT_MODE, abnormal_reboot_mode);
	return ;
}

