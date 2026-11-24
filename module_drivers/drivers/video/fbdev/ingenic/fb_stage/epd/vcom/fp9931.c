// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Zorro Liu <zorro.liu@rock-chips.com>
 */

#include <linux/device.h>
#include <linux/of_gpio.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/version.h>
#include <linux/suspend.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include "../ebc_pmic.h"

/* power up seq delay time(Value: 0x00 - 0ms, 0x01 - 1ms, 0x10 - 2ms, 0x11 - 4ms) */
#define UDLY1(value)	    (value << 0)
#define UDLY2(value)	    (value << 2)
#define UDLY3(value)	    (value << 4)
#define UDLY4(value)	    (value << 6)

#define ST_DLY_3MS	         (0x00 << 6)
#define ST_DLY_4MS	         (0x01 << 6)
#define ST_DLY_5MS	         (0x10 << 6)
#define ST_DLY_6MS	         (0x11 << 6)

#define PAPYRUS_ADDR_TMST_VALUE	            0x00
#define PAPYRUS_ADDR_VCOM_SETTING			0x01
#define PAPYRUS_ADDR_VPOS_VNEG_SETTING	    0x02
#define PAPYRUS_ADDR_PWRON_DELAY	        0x03
#define PAPYRUS_ADDR_CONTROL_REG1	        0x0b
#define PAPYRUS_ADDR_CONTROL_REG2	        0x0c

#define PAPYRUS_MV_TO_VCOMREG(MV)	        ((MV*51+500)/1000)

#define PAPYRUS_EEPROM_DELAY_MS 50

#define V3P3_EN_MASK		0x2
#define PAPYRUS_V3P3OFF_DELAY_MS	20//100

struct papyrus_sess {
	struct device *dev;
	struct i2c_client *client;
	uint8_t vcom;
	uint8_t vadj;
	uint8_t upseq0;
	uint8_t upseq1;

	struct gpio_desc *pwr_en_pin;
	struct gpio_desc *pwr_up_pin;
	struct gpio_desc *pwr_good_pin;
	struct gpio_desc *temp_en_pin;
	struct mutex power_lock;
	struct workqueue_struct *tmp_monitor_wq;
	struct delayed_work tmp_delay_work;
};
static bool papyrus_need_reconfig = false;
static int vcom = 0;

static int papyrus_hw_setreg(struct papyrus_sess *sess, uint8_t regaddr, uint8_t val)
{
	int stat;
	uint8_t txbuf[2] = { regaddr, val };
	struct i2c_msg msgs[] = {
		{
			.addr = sess->client->addr,
			.flags = 0,
			.len = 2,
			.buf = txbuf,
		}
	};

	stat = i2c_transfer(sess->client->adapter, msgs, ARRAY_SIZE(msgs));

	if (stat < 0) {
		dev_err(&sess->client->dev, "i2c send error: %d\n", stat);
	} else if (stat != ARRAY_SIZE(msgs)) {
		dev_err(&sess->client->dev, "i2c send N mismatch: %d\n", stat);
		stat = -EIO;
	} else {
		stat = 0;
	}

	return stat;
}

static int papyrus_hw_getreg(struct papyrus_sess *sess, uint8_t regaddr, uint8_t *val)
{
	int stat;
	struct i2c_msg msgs[] = {
		{
			.addr = sess->client->addr,
			.flags = 0,
			.len = 1,
			.buf = &regaddr,
		},
		{
			.addr = sess->client->addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = val,
		}
	};

	stat = i2c_transfer(sess->client->adapter, msgs, ARRAY_SIZE(msgs));
	if (stat < 0) {
		dev_err(&sess->client->dev, "i2c read error: %d\n", stat);
	} else if (stat != ARRAY_SIZE(msgs)) {
		dev_err(&sess->client->dev, "i2c read N mismatch: %d\n", stat);
		stat = -EIO;
	} else {
		stat = 0;
	}

	return stat;
}

static void papyrus_hw_arg_debug(struct papyrus_sess *sess)
{
	struct papyrus_sess s;
	int stat = 0;
	uint8_t control2 = 0;
	uint8_t tb;

	stat |= papyrus_hw_getreg(sess, PAPYRUS_ADDR_TMST_VALUE, &tb);
	stat |= papyrus_hw_getreg(sess, PAPYRUS_ADDR_VPOS_VNEG_SETTING, &s.vadj);
	stat |= papyrus_hw_getreg(sess, PAPYRUS_ADDR_PWRON_DELAY, &s.upseq0);
	stat |= papyrus_hw_getreg(sess, PAPYRUS_ADDR_CONTROL_REG1, &s.upseq1);
	stat |= papyrus_hw_getreg(sess, PAPYRUS_ADDR_VCOM_SETTING, &s.vcom);
	stat |= papyrus_hw_getreg(sess, PAPYRUS_ADDR_CONTROL_REG2, &control2);
	if (stat)
		dev_err(&sess->client->dev, "i2c error: %d\n", stat);

	printk(KERN_ERR"%s: temp = %d, vadj = 0x%x, upseq0 = 0x%x, upseq1 = 0x%x,vcom = %d,control2 = 0x%x\n",
		__func__, (int)(int8_t)tb, s.vadj, s.upseq0, s.upseq1, (((int)s.vcom * 1000) / 51), control2);
}

static void papyrus_hw_arg_init(struct papyrus_sess *sess)
{
	sess->vadj = 0x28;
	sess->upseq0 = UDLY1(2) | UDLY2(2) | UDLY3(2) | UDLY4(2);
	sess->upseq1 = ST_DLY_3MS;
	sess->vcom = (PAPYRUS_MV_TO_VCOMREG(1560) & 0x00FF);
}

static void papyrus_set_vcom_voltage(struct papyrus_sess *sess, int vcom_mv)
{
	sess->vcom = (PAPYRUS_MV_TO_VCOMREG(vcom_mv) & 0x00FF);
}

static int papyrus_hw_init(struct papyrus_sess *sess)
{
	int stat = 0;
	int read_vcom_mv = 0;
	uint8_t rev_val = 0;

	if (!IS_ERR_OR_NULL(sess->pwr_en_pin)) {
		gpiod_direction_output(sess->pwr_en_pin, 1);
		usleep_range(2 * 1000, 3 * 1000);
	}

	if (!IS_ERR_OR_NULL(sess->pwr_up_pin))
		gpiod_direction_output(sess->pwr_up_pin, 1);
	msleep(PAPYRUS_EEPROM_DELAY_MS);

	stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_CONTROL_REG1, V3P3_EN_MASK);
	if (stat){
		dev_err(&sess->client->dev, "papyrus: I2C error: %d\n", stat);
		return stat;
	}
	stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_VPOS_VNEG_SETTING, sess->vadj);
	if (stat){
		dev_err(&sess->client->dev, "papyrus: I2C error: %d\n", stat);
		return stat;
	}

	if (vcom != 0){
		dev_info(&sess->client->dev, "vendor_vcom %d, set vcom from vendor\n", vcom);
		papyrus_set_vcom_voltage(sess, vcom);
	} else {
	    stat |= papyrus_hw_getreg(sess, PAPYRUS_ADDR_VCOM_SETTING, &rev_val);
	    read_vcom_mv += ((int)rev_val * 1000) / 51;
	    if (stat)
		    dev_err(&sess->client->dev, "papyrus: I2C error: %d\n", stat);
	    printk(KERN_INFO"%s: rev_val = 0x%x, read_vcom_mv = %d\n", __func__, rev_val, read_vcom_mv);

	    /* 2509: chip default value */
	    if (2509 != read_vcom_mv && 0 < read_vcom_mv){
		    papyrus_set_vcom_voltage(sess, read_vcom_mv);
	    }
	}

	return stat;
}

static int papyrus_hw_read_temperature(struct ebc_pmic *pmic, int *t)
{
	struct papyrus_sess *sess = (struct papyrus_sess *)pmic->drvpar;
	int stat;
	uint8_t tb;

	stat = papyrus_hw_getreg(sess, PAPYRUS_ADDR_TMST_VALUE, &tb);
	*t = (int)(int8_t)tb;

	return stat;
}

static void papyrus_hw_send_powerup(struct papyrus_sess *sess)
{
	int stat = 0;

	stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_VPOS_VNEG_SETTING, sess->vadj);
	stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_PWRON_DELAY, sess->upseq0);
	sess->upseq1 |= V3P3_EN_MASK;
	stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_CONTROL_REG1, sess->upseq1);

	stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_CONTROL_REG2, 0x0A);
	stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_VCOM_SETTING, sess->vcom);
	usleep_range(2 * 1000, 3 * 1000);
	if (stat)
		dev_err(&sess->client->dev, "i2c error: %d\n", stat);
	if (!IS_ERR_OR_NULL(sess->pwr_up_pin))
		gpiod_direction_output(sess->pwr_up_pin, 1);

	papyrus_hw_arg_debug(sess);
	return;
}

static void papyrus_hw_send_powerdown(struct papyrus_sess *sess)
{
	int stat = 0;

	sess->upseq1 &= ~V3P3_EN_MASK;
	stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_CONTROL_REG1, sess->upseq1);
	if (stat)
		dev_err(&sess->client->dev, "i2c error: %d\n", stat);
	if (!IS_ERR_OR_NULL(sess->pwr_up_pin))
		gpiod_direction_output(sess->pwr_up_pin, 0);

	return;
}

static void papyrus_hw_power_req(struct ebc_pmic *pmic, bool up)
{
	struct papyrus_sess *sess = (struct papyrus_sess *)pmic->drvpar;

	/* if (up) */
	/* 	mutex_lock(&sess->power_lock); */
	if (papyrus_need_reconfig) {
		if (up) {
			papyrus_hw_send_powerup(sess);
			//enable_irq(sess->irq);
		} else {
			//disable_irq(sess->irq);
			papyrus_hw_send_powerdown(sess);
		}
		papyrus_need_reconfig = false;
	} else {
		if (up) {
			if (!IS_ERR_OR_NULL(sess->pwr_up_pin))
				gpiod_direction_output(sess->pwr_up_pin, 1);
			//enable_irq(sess->irq);
		} else {
			//disable_irq(sess->irq);
			if (!IS_ERR_OR_NULL(sess->pwr_up_pin))
				gpiod_direction_output(sess->pwr_up_pin, 0);
		}
	}
	/* if (!up) */
	/* 	mutex_unlock(&sess->power_lock); */

	return;
}

static int papyrus_hw_vcom_get(struct ebc_pmic *pmic)
{
	struct papyrus_sess *sess = (struct papyrus_sess *)pmic->drvpar;
	uint8_t rev_val = 0;
	int stat = 0;
	int read_vcom_mv = 0;

	mutex_lock(&sess->power_lock);
	// VERIFICATION
//	gpiod_direction_output(sess->pwr_up_pin, 1);
//	msleep(10);

	stat |= papyrus_hw_getreg(sess, PAPYRUS_ADDR_VCOM_SETTING, &rev_val);
	read_vcom_mv += ((int)rev_val * 1000) / 51;
	if (stat)
		dev_err(&sess->client->dev, "papyrus: I2C error: %d\n", stat);
	mutex_unlock(&sess->power_lock);

    printk(KERN_ERR"%s: rev_val = 0x%x, read_vcom_mv = %d\n", __func__, rev_val, read_vcom_mv);
	return read_vcom_mv;
}

static int papyrus_hw_vcom_set(struct ebc_pmic *pmic, int vcom_mv)
{
	struct papyrus_sess *sess = (struct papyrus_sess *)pmic->drvpar;
	int stat = 0;

	mutex_lock(&sess->power_lock);
//	gpiod_direction_output(sess->pwr_up_pin, 1);
//	msleep(10);
	// Set vcom voltage
	papyrus_set_vcom_voltage(sess, vcom_mv);
	stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_VCOM_SETTING, sess->vcom);
	if (stat)
		dev_err(&sess->client->dev, "papyrus: I2C error: %d\n", stat);
	mutex_unlock(&sess->power_lock);

	printk(KERN_ERR"%s: vcom_mv = %d, sess->vcom = 0x%x\n", __func__, vcom_mv, sess->vcom);
	return 0;
}

static void papyrus_pm_sleep(struct ebc_pmic *pmic)
{
	struct papyrus_sess *s = (struct papyrus_sess *)pmic->drvpar;

	cancel_delayed_work_sync(&s->tmp_delay_work);

	mutex_lock(&s->power_lock);
	gpiod_direction_output(s->temp_en_pin, 0);
	gpiod_direction_output(s->pwr_up_pin, 0);
	if (!IS_ERR_OR_NULL(s->pwr_en_pin))
		gpiod_direction_output(s->pwr_en_pin, 0);
	papyrus_need_reconfig = true;
	mutex_unlock(&s->power_lock);
}

static void papyrus_pm_resume(struct ebc_pmic *pmic)
{
	struct papyrus_sess *s = (struct papyrus_sess *)pmic->drvpar;

	mutex_lock(&s->power_lock);
	if (!IS_ERR_OR_NULL(s->pwr_en_pin)) {
		gpiod_direction_output(s->pwr_en_pin, 1);
		usleep_range(2 * 1000, 3 * 1000);
	}
	gpiod_direction_output(s->temp_en_pin, 1);
//	gpiod_direction_output(s->pwr_up_pin, 1);
	usleep_range(2 * 1000, 3 * 1000);
	mutex_unlock(&s->power_lock);

	papyrus_hw_arg_debug(s);
//	papyrus_hw_setreg(s, PAPYRUS_ADDR_CONTROL_REG1, V3P3_EN_MASK);
//	papyrus_hw_setreg(s, PAPYRUS_ADDR_VPOS_VNEG_SETTING, s->vadj);
	queue_delayed_work(s->tmp_monitor_wq, &s->tmp_delay_work,
			   msecs_to_jiffies(10000));
}

static void papyrus_tmp_work(struct work_struct *work)
{
	struct papyrus_sess *s =
		container_of(work, struct papyrus_sess, tmp_delay_work.work);

	queue_delayed_work(s->tmp_monitor_wq, &s->tmp_delay_work,
			   msecs_to_jiffies(10000));
}

static int papyrus_probe(struct ebc_pmic *pmic, struct i2c_client *client)
{
	struct papyrus_sess *sess;
	int stat;

	sess = devm_kzalloc(&client->dev, sizeof(*sess), GFP_KERNEL);
	if (!sess) {
		dev_err(&client->dev, "%s:%d: kzalloc failed\n", __func__, __LINE__);
		return -ENOMEM;
	}
	sess->client = client;
	mutex_init(&sess->power_lock);
	papyrus_hw_arg_init(sess);

	sess->pwr_en_pin = devm_gpiod_get_optional(&client->dev, "poweren", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(sess->pwr_en_pin)) {
		dev_err(&client->dev, "fp9931: failed to find poweren pin, no defined\n");
	}

	sess->pwr_up_pin = devm_gpiod_get_optional(&client->dev, "powerup", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(sess->pwr_up_pin))
		dev_err(&client->dev, "fp9931: no pwr_up pin find\n");

	sess->pwr_good_pin = devm_gpiod_get(&client->dev, "powergood", GPIOD_IN);
	if (IS_ERR(sess->pwr_good_pin)) {
		dev_err(&client->dev, "fp9931: failed to find pwr_good pin\n");
		return PTR_ERR(sess->pwr_good_pin);
	}

	sess->temp_en_pin = devm_gpiod_get_optional(&client->dev, "tempen", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(sess->temp_en_pin)) {
		dev_err(&client->dev, "fp9931: failed to find temp_en pin\n");
		return -ENOMEM;
	}

	stat = papyrus_hw_init(sess);
	if (stat)
		return stat;

	pmic->drvpar = sess;

	pmic->pmic_get_vcom = papyrus_hw_vcom_get;
	pmic->pmic_set_vcom = papyrus_hw_vcom_set;
	pmic->pmic_pm_resume = papyrus_pm_resume;
	pmic->pmic_pm_suspend = papyrus_pm_sleep;
	pmic->pmic_power_req = papyrus_hw_power_req;
	pmic->pmic_read_temperature = papyrus_hw_read_temperature;

	sess->tmp_monitor_wq = alloc_ordered_workqueue("%s",
			WQ_MEM_RECLAIM | WQ_FREEZABLE, "tps-tmp-monitor-wq");
	INIT_DELAYED_WORK(&sess->tmp_delay_work, papyrus_tmp_work);

	queue_delayed_work(sess->tmp_monitor_wq, &sess->tmp_delay_work,
			   msecs_to_jiffies(10000));

	return 0;
}

static int fp9931_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct ebc_pmic *pmic;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "i2c check functionality failed.");
		return -ENODEV;
	}

	pmic = devm_kzalloc(&client->dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic) {
		dev_err(&client->dev, "%s:%d: kzalloc failed\n", __func__, __LINE__);
		return -ENOMEM;
	}

	if (0 != papyrus_probe(pmic, client)) {
		dev_err(&client->dev, "fp9931 hw init failed.");
		return -ENODEV;
	}

	pmic->dev = &client->dev;
	sprintf(pmic->pmic_name, "fp9931");
	i2c_set_clientdata(client, pmic);

	dev_info(&client->dev, "fp9931 probe ok.\n");

	return 0;
}

static int fp9931_remove(struct i2c_client *client)
{
	struct ebc_pmic *pmic = i2c_get_clientdata(client);
	struct papyrus_sess *sess = pmic->drvpar;

	if (sess->tmp_monitor_wq)
		destroy_workqueue(sess->tmp_monitor_wq);

	return 0;
}

static const struct i2c_device_id fp9931_id[] = {
	{ "fp9931", 0 },
	{ }
};

static const struct of_device_id fp9931_dt_ids[] = {
	{ .compatible = "fp,fp9931", },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, fp9931_dt_ids);
static struct i2c_driver fp9931_driver = {
	.probe	= fp9931_probe,
	.remove 	= fp9931_remove,
	.id_table	= fp9931_id,
	.driver = {
		.of_match_table = fp9931_dt_ids,
		.name	  = "fp9931",
		.owner	  = THIS_MODULE,
	},
};

module_i2c_driver(fp9931_driver);

MODULE_DESCRIPTION("fp fp9931 pmic");
MODULE_LICENSE("GPL");
