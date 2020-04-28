// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2014 Linaro Ltd
 *
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 *  Simple MMC power sequence management
 */
#include <linux/clk.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/property.h>

#include <linux/mmc/host.h>

#include "pwrseq.h"

struct mmc_pwrseq_simple {
	struct mmc_pwrseq pwrseq;
	bool clk_enabled;
	u32 post_power_on_delay_ms;
	u32 power_off_delay_us;
	struct clk *ext_clk;
	struct gpio_descs *reset_gpios;
};

#define to_pwrseq_simple(p) container_of(p, struct mmc_pwrseq_simple, pwrseq)

static void mmc_pwrseq_simple_set_gpios_value(struct mmc_pwrseq_simple *pwrseq,
					      int value)
{
	struct gpio_descs *reset_gpios = pwrseq->reset_gpios;

	pr_err("%s: E\n", __func__);
	if (!IS_ERR(reset_gpios)) {
		unsigned long *values;
		int nvalues = reset_gpios->ndescs;

		pr_err("%s: nvalues(%d)\n", __func__, nvalues);
		values = bitmap_alloc(nvalues, GFP_KERNEL);
		if (!values) {
			pr_err("%s: bitmap_alloc error return\n", __func__);
			return;
		}

		if (value)
			bitmap_fill(values, nvalues);
		else
			bitmap_zero(values, nvalues);

		gpiod_set_array_value_cansleep(nvalues, reset_gpios->desc,
					       reset_gpios->info, values);

		pr_err("%s: X\n", __func__);

		kfree(values);
	}
}

static void mmc_pwrseq_simple_pre_power_on(struct mmc_host *host)
{
	struct mmc_pwrseq_simple *pwrseq = to_pwrseq_simple(host->pwrseq);

	pr_err("%s:\n", __func__);
	if (!IS_ERR(pwrseq->ext_clk) && !pwrseq->clk_enabled) {
		pr_err("%s: calling clk_prepare_enable()\n", __func__);
		clk_prepare_enable(pwrseq->ext_clk);
		pwrseq->clk_enabled = true;
	}

	mmc_pwrseq_simple_set_gpios_value(pwrseq, 1);
}

static void mmc_pwrseq_simple_post_power_on(struct mmc_host *host)
{
	struct mmc_pwrseq_simple *pwrseq = to_pwrseq_simple(host->pwrseq);

	pr_err("%s:\n", __func__);
	mmc_pwrseq_simple_set_gpios_value(pwrseq, 0);

	if (pwrseq->post_power_on_delay_ms)
		msleep(pwrseq->post_power_on_delay_ms);
}

static void mmc_pwrseq_simple_power_off(struct mmc_host *host)
{
	struct mmc_pwrseq_simple *pwrseq = to_pwrseq_simple(host->pwrseq);

	mmc_pwrseq_simple_set_gpios_value(pwrseq, 1);

	pr_err("%s:\n", __func__);

	if (pwrseq->power_off_delay_us)
		usleep_range(pwrseq->power_off_delay_us,
			2 * pwrseq->power_off_delay_us);

	if (!IS_ERR(pwrseq->ext_clk) && pwrseq->clk_enabled) {
		pr_err("%s: calling clk_disable_unprepare()\n", __func__);
		clk_disable_unprepare(pwrseq->ext_clk);
		pwrseq->clk_enabled = false;
	}
}

static const struct mmc_pwrseq_ops mmc_pwrseq_simple_ops = {
	.pre_power_on = mmc_pwrseq_simple_pre_power_on,
	.post_power_on = mmc_pwrseq_simple_post_power_on,
	.power_off = mmc_pwrseq_simple_power_off,
};

static const struct of_device_id mmc_pwrseq_simple_of_match[] = {
	{ .compatible = "mmc-pwrseq-simple",},
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, mmc_pwrseq_simple_of_match);

static int mmc_pwrseq_simple_probe(struct platform_device *pdev)
{
	struct mmc_pwrseq_simple *pwrseq;
	struct device *dev = &pdev->dev;

	pr_err("%s:\n", __func__);
	pwrseq = devm_kzalloc(dev, sizeof(*pwrseq), GFP_KERNEL);
	if (!pwrseq)
		return -ENOMEM;

	pwrseq->ext_clk = devm_clk_get(dev, "ext_clock");
	if (IS_ERR(pwrseq->ext_clk) && PTR_ERR(pwrseq->ext_clk) != -ENOENT) {
		pr_err("%s: ext_clock not found\n", __func__);
		return PTR_ERR(pwrseq->ext_clk);
	}

	pwrseq->reset_gpios = devm_gpiod_get_array(dev, "reset",
							GPIOD_OUT_HIGH);
	if (IS_ERR(pwrseq->reset_gpios) &&
	    PTR_ERR(pwrseq->reset_gpios) != -ENOENT &&
	    PTR_ERR(pwrseq->reset_gpios) != -ENOSYS) {
		pr_err("%s: reset_gpios not found\n", __func__);
		return PTR_ERR(pwrseq->reset_gpios);
	}

	device_property_read_u32(dev, "post-power-on-delay-ms",
				 &pwrseq->post_power_on_delay_ms);
	device_property_read_u32(dev, "power-off-delay-us",
				 &pwrseq->power_off_delay_us);

	pwrseq->pwrseq.dev = dev;
	pwrseq->pwrseq.ops = &mmc_pwrseq_simple_ops;
	pwrseq->pwrseq.owner = THIS_MODULE;
	platform_set_drvdata(pdev, pwrseq);

	return mmc_pwrseq_register(&pwrseq->pwrseq);
}

static int mmc_pwrseq_simple_remove(struct platform_device *pdev)
{
	struct mmc_pwrseq_simple *pwrseq = platform_get_drvdata(pdev);

	mmc_pwrseq_unregister(&pwrseq->pwrseq);

	return 0;
}

static struct platform_driver mmc_pwrseq_simple_driver = {
	.probe = mmc_pwrseq_simple_probe,
	.remove = mmc_pwrseq_simple_remove,
	.driver = {
		.name = "pwrseq_simple",
		.of_match_table = mmc_pwrseq_simple_of_match,
	},
};

module_platform_driver(mmc_pwrseq_simple_driver);
MODULE_LICENSE("GPL v2");
