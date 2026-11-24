// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/input/touchscreen/tsc2007.c
 *
 * Copyright (c) 2008 MtekVision Co., Ltd.
 *	Kwangwoo Lee <kwlee@mtekvision.com>
 *
 * Using code from:
 *  - ads7846.c
 *	Copyright (c) 2005 David Brownell
 *	Copyright (c) 2006 Nokia Corporation
 *  - corgi_ts.c
 *	Copyright (C) 2004-2005 Richard Purdie
 *  - omap_ts.[hc], ads7846.h, ts_osk.c
 *	Copyright (C) 2002 MontaVista Software
 *	Copyright (C) 2004 Texas Instruments
 *	Copyright (C) 2005 Dirk Behme
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include "tsc2007-linux.h"
#include "tsc2007.h"



/**********tsc2007_xfer*******************/
/*用于从一个TSC2007传感器读取数据。
首先，通过I2C接口读取一个16位的数据，然后进行协议解析，将数据的格式转换为预期的格式。
如果读取失败，会返回一个错误代码*/
int tsc2007_xfer(struct tsc2007 *tsc, u8 cmd)
{
	s32 data;
	u16 val;

	data = i2c_smbus_read_word_data(tsc->client, cmd);
	if (data < 0) {
		dev_err(&tsc->client->dev, "i2c io error: %d\n", data);
		return data;
	}

	/* The protocol and raw data format from i2c interface:
	 * S Addr Wr [A] Comm [A] S Addr Rd [A] [DataLow] A [DataHigh] NA P
	 * Where DataLow has [D11-D4], DataHigh has [D3-D0 << 4 | Dummy 4bit].
	 */
	val = swab16(data) >> 4;

	dev_dbg(&tsc->client->dev, "data: 0x%x, val: 0x%x\n", data, val);

	return val;
}

/******************tsc2007_read_values**********************/
/*用于从TSC2007控制器读取触摸屏事件的各个维度的值。*/
/*关闭ADC（模拟-数字转换器）并启用PENIRQ（触摸屏的硬件中断线）*/
static void tsc2007_read_values(struct tsc2007 *tsc, struct ts_event *tc)
{
	/* y- still on; turn on only y+ (and ADC) */
	tc->y = tsc2007_xfer(tsc, READ_Y);

	/* turn y- off, x+ on, then leave in lowpower */
	tc->x = tsc2007_xfer(tsc, READ_X);

	/* turn y+ off, x- on; we'll use formula #1 */
	tc->z1 = tsc2007_xfer(tsc, READ_Z1);
	tc->z2 = tsc2007_xfer(tsc, READ_Z2);

	/* Prepare for next touch reading - power down ADC, enable PENIRQ */
	tsc2007_xfer(tsc, PWRDOWN);
}

/********************tsc2007_calculate_resistance*********************/
/*计算触摸屏的电阻值*/
u32 tsc2007_calculate_resistance(struct tsc2007 *tsc, struct ts_event *tc)
{
	u32 rt = 0;
	/* range filtering */
	if (tc->x == MAX_12BIT)
		tc->x = 0;
	if (likely(tc->x && tc->z1)) {
		/* compute touch resistance using equation #1 */
		rt = tc->z2 - tc->z1;
		rt *= tc->x;
		rt *= tsc->x_plate_ohms;
		rt /= tc->z1;
		rt = (rt + 2047) >> 12;
	}
	return rt;
}

/*******************tsc2007_is_pen_down**************************/
/*接收一个tsc2007结构体指针ts作为参数。它用于判断TSC2007触摸屏的笔是否已经放下。*/
/*先检查是否有一个名为get_pendown_state的回调函数。如果没有，函数直接返回true;
  如果存在get_pendown_state回调函数,返回true，那么这个函数也返回true，表示笔是放下的;
  如果回调函数返回false，那么这个函数返回false，表示笔还没有放下。*/
bool tsc2007_is_pen_down(struct tsc2007 *ts)
{
	/*
	 * NOTE: We can't rely on the pressure to determine the pen down
	 * state, even though this controller has a pressure sensor.
	 * The pressure value can fluctuate for quite a while after
	 * lifting the pen and in some cases may not even settle at the
	 * expected value.
	 *
	 * The only safe way to check for the pen up condition is in the
	 * work function by reading the pen signal state (it's a GPIO
	 * and IRQ). Unfortunately such callback is not always available,
	 * in that case we assume that the pen is down and expect caller
	 * to fall back on the pressure reading.
	 */
	if (!ts->get_pendown_state)
		return true;

	return ts->get_pendown_state(&ts->client->dev);
}

/********************tsc2007_soft_irq*********************/
/*处理TSC2007触摸屏控制器中断的函数。
  当触摸屏被按下时，该函数会被调用，然后读取触摸屏的坐标和压力信息，并将这些信息输入到输入设备。*/
/* 当触摸屏没有被抬起，并且没有被停止时，函数会进入一个循环，在循环中，它读取触摸屏的值，并计算触摸电阻;
 "有效数据": "如果触摸电阻小于或等于最大电阻，函数会报告触摸事件，包括坐标和压力信息。",
 "无效数据": "如果触摸电阻大于最大电阻，函数会忽略，并等待一段时间再次测量。
 结束循环": "当触摸屏被抬起或停止时，循环结束。",
"清理工作": "函数报告触摸屏已经抬起，并同步输入设备。如果有清除中断的函数，那么就会调用它*/
static irqreturn_t tsc2007_soft_irq(int irq, void *handle)
{
	struct tsc2007 *ts = handle;
	struct input_dev *input = ts->input;
	struct ts_event tc;
	u32 rt;
	int x,y;
	while (!ts->stopped && tsc2007_is_pen_down(ts)) {

		/* pen is down, continue with the measurement */

		mutex_lock(&ts->mlock);
		tsc2007_read_values(ts, &tc);
		mutex_unlock(&ts->mlock);

		rt = tsc2007_calculate_resistance(ts, &tc);

		if (!rt && !ts->get_pendown_state) {
			/*
			 * If pressure reported is 0 and we don't have
			 * callback to check pendown state, we have to
			 * assume that pen was lifted up.
			 */
			break;
		}

		if (rt <= ts->max_rt) {
			dev_dbg(&ts->client->dev,
				"DOWN point(%4d,%4d), resistance (%4u)\n",
				tc.x, tc.y, rt);

			rt = ts->max_rt - rt;

			input_report_key(input, BTN_TOUCH, 1);
			x = tc.x * ts->x_max / ts->max_rt;
			if(ts->x_mirror)
				x = ts->x_max - x;
			y = tc.y * ts->y_max / ts->max_rt;
			if(ts->y_mirror)
				y = ts->y_max - y;
			input_report_abs(input, ABS_X, x);
			input_report_abs(input, ABS_Y, y);
			input_report_abs(input, ABS_PRESSURE, rt);

			input_sync(input);

		} else {
			/*
			 * Sample found inconsistent by debouncing or pressure is
			 * beyond the maximum. Don't report it to user space,
			 * repeat at least once more the measurement.
			 */
			dev_dbg(&ts->client->dev, "ignored pressure %d\n", rt);
		}

		wait_event_timeout(ts->wait, ts->stopped, ts->poll_period);
	}

	dev_dbg(&ts->client->dev, "UP\n");

	input_report_key(input, BTN_TOUCH, 0);
	input_report_abs(input, ABS_PRESSURE, 0);
	input_sync(input);

	if (ts->clear_penirq)
		ts->clear_penirq();

	return IRQ_HANDLED;
}



/**************************tsc2007_stop**************************/
/*用来停止TSC2007触摸屏控制器的工作。首先将ts->stopped设置为true，表示停止，然后强制刷新内存，唤醒等待队列中的线程，最后禁用中断 */
static void tsc2007_stop(struct tsc2007 *ts)
{
	ts->stopped = true;
	mb();
	wake_up(&ts->wait);
	disable_irq(ts->irq);
}

/***************************tsc2007_open**************************/
/*打开TSC2007触摸屏控制器。
首先将ts->stopped设置为false，表示开始工作，然后强制刷新内存。
接着启用中断，然后进行触摸读取的准备工作，将ADC电源关闭并启用PENIRQ。
如果准备工作失败，会调用tsc2007_stop停止控制器，并返回错误代码。*/
static int tsc2007_open(struct input_dev *input_dev)
{
	struct tsc2007 *ts = input_get_drvdata(input_dev);
	int err;

	ts->stopped = false;
	mb();

	enable_irq(ts->irq);

	/* Prepare for touch readings - power down ADC and enable PENIRQ */
	err = tsc2007_xfer(ts, PWRDOWN);
	if (err < 0) {
		tsc2007_stop(ts);
		return err;
	}

	return 0;
}

/***************************tsc2007_close***************************/
/*关闭TSC2007触摸屏控制器。它直接调用tsc2007_stop停止控制器的工作。*/
static void tsc2007_close(struct input_dev *input_dev)
{
	struct tsc2007 *ts = input_get_drvdata(input_dev);

	tsc2007_stop(ts);
}

/**************************tsc2007_get_pendown_state_gpio**********************/
/*用于获取TSC2007触摸屏控制器的笔按下状态。
函数tsc2007_get_pendown_state_gpio通过I2C接口从TSC2007设备获取笔的状态。
首先，函数将传入的设备指针dev转换为I2C客户端指针client，然后使用i2c_get_clientdata获取与该客户端相关联的TSC2007设备数据。
最后，函数使用gpiod_get_value_cansleep获取TSC2007设备的GPIO状态，该状态表示笔是否按下。*/
static int tsc2007_get_pendown_state_gpio(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct tsc2007 *ts = i2c_get_clientdata(client);

	return gpiod_get_value(ts->gpiod);
}

static int tsc2007_probe_properties(struct device *dev, struct tsc2007 *ts)
{
	u32 val32;
	u64 val64;
	int ret;
	struct i2c_client *client = to_i2c_client(dev);
	ret = of_property_read_u32(dev->of_node, "irq-flags",&ts->irq_flags);
	if (ret) {
		dev_info(&client->dev,
			 "Failed get int-trigger-type from dts,set default\n");
	}
	if (!device_property_read_u32(dev, "ti,x-mirror", &val32))
		ts->x_mirror = val32;
	else
		ts->x_mirror = 0;
	if (!device_property_read_u32(dev, "ti,y-mirror", &val32))
		ts->y_mirror = val32;
	else
		ts->y_mirror = 0;
	if (!device_property_read_u32(dev, "ti,x-max", &val32))
		ts->x_max = val32;
	else
		ts->x_max = MAX_12BIT;
	if (!device_property_read_u32(dev, "ti,y-max", &val32))
		ts->y_max = val32;
	else
		ts->y_max = MAX_12BIT;
	if (!device_property_read_u32(dev, "ti,max-rt", &val32))
		ts->max_rt = val32;
	else
		ts->max_rt = MAX_12BIT;
	if (!device_property_read_u64(dev, "ti,poll-period", &val64))
		ts->poll_period = msecs_to_jiffies(val64);
	else
		ts->poll_period = msecs_to_jiffies(1);
	if (!device_property_read_u32(dev, "ti,x-plate-ohms", &val32)) {
		ts->x_plate_ohms = val32;
	} else {
		dev_err(dev, "Missing ti,x-plate-ohms device property\n");
		return -EINVAL;
	}
	ts->gpiod = devm_gpiod_get_optional(dev, "tsc2007-irq", GPIOD_IN);
	if (ts->gpiod==NULL)
		dev_err(&client->dev, "No valid irq gpio");


	return 0;
}

static int tsc2007_probe_pdev(struct device *dev, struct tsc2007 *ts,
			      const struct tsc2007_platform_data *pdata,
			      const struct i2c_device_id *id)
{
	ts->model             = pdata->model;
	ts->x_plate_ohms      = pdata->x_plate_ohms;
	ts->max_rt            = pdata->max_rt ? : MAX_12BIT;
	ts->poll_period       = msecs_to_jiffies(pdata->poll_period ? : 1);
	ts->get_pendown_state = pdata->get_pendown_state;
	ts->clear_penirq      = pdata->clear_penirq;
	ts->fuzzx             = pdata->fuzzx;
	ts->fuzzy             = pdata->fuzzy;
	ts->fuzzz             = pdata->fuzzz;

	if (pdata->x_plate_ohms == 0) {
		dev_err(dev, "x_plate_ohms is not set up in platform data\n");
		return -EINVAL;
	}

	return 0;
}

static void tsc2007_call_exit_platform_hw(void *data)
{
	struct device *dev = data;
	const struct tsc2007_platform_data *pdata = dev_get_platdata(dev);

	pdata->exit_platform_hw();
}

static int tsc2007_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
	const struct tsc2007_platform_data *pdata =
		dev_get_platdata(&client->dev);
	struct tsc2007 *ts;
	struct input_dev *input_dev;
	int err;
	dev_info(&client->dev, "I2C Address: 0x%02x\n", client->addr);
	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -EIO;

	ts = devm_kzalloc(&client->dev, sizeof(struct tsc2007), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	if (pdata)
		err = tsc2007_probe_pdev(&client->dev, ts, pdata, id);
	else
		err = tsc2007_probe_properties(&client->dev, ts);
	if (err)
		return err;
    /* use irq */
	ts->irq = gpiod_to_irq(ts->gpiod);
        ts->get_pendown_state = tsc2007_get_pendown_state_gpio;
	dev_info(&client->dev, "INT num %d, trigger type:%d,ts->gpiod:%p\n",client->irq, ts->irq_flags,ts->gpiod);


	input_dev = devm_input_allocate_device(&client->dev);
	if (!input_dev)
		return -ENOMEM;

	i2c_set_clientdata(client, ts);

	ts->client = client;
	ts->input = input_dev;

	init_waitqueue_head(&ts->wait);
	mutex_init(&ts->mlock);

	snprintf(ts->phys, sizeof(ts->phys),
		 "%s/input0", dev_name(&client->dev));

	input_dev->name = "TSC2007 Touchscreen";
	input_dev->phys = ts->phys;
	input_dev->id.bustype = BUS_I2C;

	input_dev->open = tsc2007_open;
	input_dev->close = tsc2007_close;

	input_set_drvdata(input_dev, ts);

	input_set_capability(input_dev, EV_ABS, BTN_TOUCH);


	input_set_abs_params(input_dev, ABS_X, 0, MAX_12BIT, ts->fuzzx, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, MAX_12BIT, ts->fuzzy, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, MAX_12BIT,
			     ts->fuzzz, 0);

	if (pdata) {
		if (pdata->exit_platform_hw) {
			err = devm_add_action(&client->dev,
					      tsc2007_call_exit_platform_hw,
					      &client->dev);
			if (err) {
				dev_err(&client->dev,
					"Failed to register exit_platform_hw action, %d\n",
					err);
				return err;
			}
		}

		if (pdata->init_platform_hw)
			pdata->init_platform_hw();
	}


    /* Ensure the touchscreen is off */

	err = devm_request_threaded_irq(&client->dev, ts->irq,
					NULL, tsc2007_soft_irq,
					ts->irq_flags|IRQF_ONESHOT,
					client->dev.driver->name, ts);
	if (err) {
		dev_err(&client->dev, "Failed to request irq %d: %d\n",
			ts->irq, err);
		return err;
	}

	tsc2007_stop(ts);

	/* power down the chip (TSC2007_SETUP does not ACK on I2C) */
	err = tsc2007_xfer(ts, PWRDOWN);

	dev_info(&client->dev, "touchscreen, irq %d\n", ts->irq);

	err = input_register_device(input_dev);
	if (err) {
		dev_err(&client->dev,
			"Failed to register input device: %d\n", err);
		return err;
	}
	err =  tsc2007_iio_configure(ts);
	if (err) {
		dev_err(&client->dev,
			"Failed to register with IIO: %d\n", err);
		return err;
	}

	return 0;
}

static const struct i2c_device_id tsc2007_idtable[] = {
	{ "tsc2007", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, tsc2007_idtable);

static const struct of_device_id tsc2007_of_match[] = {
	{ .compatible = "ti,tsc2007" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tsc2007_of_match);

static struct i2c_driver tsc2007_driver = {
	.driver = {
		.name	= "tsc2007",
		.of_match_table = tsc2007_of_match,
	},
	.id_table	= tsc2007_idtable,
	.probe		= tsc2007_probe,
};

module_i2c_driver(tsc2007_driver);

MODULE_AUTHOR("Kwangwoo Lee <kwlee@mtekvision.com>");
MODULE_DESCRIPTION("TSC2007 TouchScreen Driver");
MODULE_LICENSE("GPL");
