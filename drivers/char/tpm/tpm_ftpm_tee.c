// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Microsoft Corporation
 *
 * Implements a firmware TPM as described here:
 * https://www.microsoft.com/en-us/research/publication/ftpm-software-implementation-tpm-chip/
 *
 * A reference implementation is available here:
 * https://github.com/microsoft/ms-tpm-20-ref/tree/master/Samples/ARM32-FirmwareTPM/optee_ta/fTPM
 *
 * The TEE transport is shared with other OP-TEE TPM front-ends and lives in
 * tpm_optee; this file only carries the fTPM identity and its platform/OF
 * binding.
 */

#include <linux/acpi.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/tee_drv.h>
#include <linux/uuid.h>

#include "tpm_optee.h"

/* The TAF ID implemented in the fTPM TA */
#define FTPM_OPTEE_TA_SUBMIT_COMMAND	0

/*
 * TA_FTPM_UUID: BC50D971-D4C9-42C4-82CB-343FB7F37896
 *
 * Randomly generated, and must correspond to the GUID on the TA side.
 * Defined here in the reference implementation:
 * https://github.com/microsoft/ms-tpm-20-ref/blob/master/Samples/ARM32-FirmwareTPM/optee_ta/fTPM/include/fTPM.h#L42
 */
static const struct tpm_optee_config ftpm_tee_config = {
	.ta_uuid = UUID_INIT(0xBC50D971, 0xD4C9, 0x42C4,
			     0x82, 0xCB, 0x34, 0x3F, 0xB7, 0xF3, 0x78, 0x96),
	.login	 = TEE_IOCTL_LOGIN_PUBLIC,
	.cmd_id	 = FTPM_OPTEE_TA_SUBMIT_COMMAND,
};

static int ftpm_tee_probe(struct tee_client_device *tcdev)
{
	return tpm_optee_probe(&tcdev->dev, &ftpm_tee_config);
}

static int ftpm_plat_tee_probe(struct platform_device *pdev)
{
	return tpm_optee_probe(&pdev->dev, &ftpm_tee_config);
}

static void ftpm_tee_remove(struct tee_client_device *tcdev)
{
	tpm_optee_remove(&tcdev->dev);
}

static void ftpm_plat_tee_remove(struct platform_device *pdev)
{
	tpm_optee_remove(&pdev->dev);
}

static void ftpm_plat_tee_shutdown(struct platform_device *pdev)
{
	tpm_optee_shutdown(&pdev->dev);
}

static const struct of_device_id of_ftpm_tee_ids[] = {
	{ .compatible = "microsoft,ftpm" },
	{ }
};
MODULE_DEVICE_TABLE(of, of_ftpm_tee_ids);

static struct platform_driver ftpm_tee_plat_driver = {
	.driver = {
		.name = "ftpm-tee",
		.of_match_table = of_ftpm_tee_ids,
	},
	.shutdown = ftpm_plat_tee_shutdown,
	.probe = ftpm_plat_tee_probe,
	.remove = ftpm_plat_tee_remove,
};

/* UUID of the fTPM TA */
static const struct tee_client_device_id optee_ftpm_id_table[] = {
	{UUID_INIT(0xbc50d971, 0xd4c9, 0x42c4,
		   0x82, 0xcb, 0x34, 0x3f, 0xb7, 0xf3, 0x78, 0x96)},
	{}
};
MODULE_DEVICE_TABLE(tee, optee_ftpm_id_table);

static struct tee_client_driver ftpm_tee_driver = {
	.probe		= ftpm_tee_probe,
	.remove		= ftpm_tee_remove,
	.id_table	= optee_ftpm_id_table,
	.driver		= {
		.name		= "optee-ftpm",
	},
};

static int __init ftpm_mod_init(void)
{
	int rc;

	rc = platform_driver_register(&ftpm_tee_plat_driver);
	if (rc)
		return rc;

	rc = tee_client_driver_register(&ftpm_tee_driver);
	if (rc) {
		platform_driver_unregister(&ftpm_tee_plat_driver);
		return rc;
	}

	return 0;
}

static void __exit ftpm_mod_exit(void)
{
	platform_driver_unregister(&ftpm_tee_plat_driver);
	tee_client_driver_unregister(&ftpm_tee_driver);
}

module_init(ftpm_mod_init);
module_exit(ftpm_mod_exit);

MODULE_AUTHOR("Thirupathaiah Annapureddy <thiruan@microsoft.com>");
MODULE_DESCRIPTION("TPM Driver for fTPM TA in TEE");
MODULE_LICENSE("GPL v2");
