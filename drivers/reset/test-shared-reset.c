// SPDX-License-Identifier: GPL-2.0-only
/*
 * TEST ONLY, not for upstream: a consumer of a shared reset-gpios line,
 * used with a banked gpio-simulator controller in the board DT to check
 * that gpiolib-shared matches the reset-gpio device.
 */
#include <linux/device.h>
#include <linux/err.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

static int test_shared_reset_probe(struct platform_device *pdev)
{
	struct reset_control *rst;
	int ret;

	rst = devm_reset_control_get_shared(&pdev->dev, NULL);
	if (IS_ERR(rst))
		return dev_err_probe(&pdev->dev, PTR_ERR(rst), "TESTRESET get failed\n");

	ret = reset_control_deassert(rst);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "TESTRESET deassert failed\n");

	dev_info(&pdev->dev, "TESTRESET shared reset deasserted\n");

	return 0;
}

static const struct of_device_id test_shared_reset_of_match[] = {
	{ .compatible = "test,shared-reset-consumer" },
	{ }
};
MODULE_DEVICE_TABLE(of, test_shared_reset_of_match);

static struct platform_driver test_shared_reset_driver = {
	.probe = test_shared_reset_probe,
	.driver = {
		.name = "test-shared-reset",
		.of_match_table = test_shared_reset_of_match,
	},
};
module_platform_driver(test_shared_reset_driver);

MODULE_DESCRIPTION("TEST ONLY shared reset-gpios consumer");
MODULE_LICENSE("GPL");
