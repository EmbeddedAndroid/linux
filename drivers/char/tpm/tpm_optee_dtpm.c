// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Front-end for a discrete TPM owned by OP-TEE and exposed to the normal
 * world through a passthrough pseudo TA. The chip sits on a bus only the
 * secure world can reach (e.g. an SPI TPM behind OP-TEE's TPM TIS driver);
 * this driver supplies the TA identity and binds it, the shared transport in
 * tpm_optee does the rest.
 */

#include <linux/module.h>
#include <linux/tee_drv.h>
#include <linux/uuid.h>

#include "tpm_optee.h"

#define TPM_OPTEE_DTPM_SUBMIT_COMMAND		0
#define TPM_OPTEE_DTPM_SUBMIT_COMMAND_LOCALITY	1

static const struct tpm_optee_config tpm_optee_dtpm_config = {
	.ta_uuid = UUID_INIT(0x50568cd9, 0x370a, 0x46b8,
			     0x9f, 0x2c, 0x6f, 0xd9, 0x06, 0x64, 0xd2, 0xee),
	.login	 = TEE_IOCTL_LOGIN_REE_KERNEL,
	.cmd_id	 = TPM_OPTEE_DTPM_SUBMIT_COMMAND,
	.loc_cmd_id = TPM_OPTEE_DTPM_SUBMIT_COMMAND_LOCALITY,
	.has_loc_cmd = true,
};

static int tpm_optee_dtpm_probe(struct tee_client_device *tee_device)
{
	return tpm_optee_probe(&tee_device->dev, &tpm_optee_dtpm_config);
}

static void tpm_optee_dtpm_remove(struct tee_client_device *tee_device)
{
	tpm_optee_remove(&tee_device->dev);
}

static const struct tee_client_device_id tpm_optee_dtpm_id_table[] = {
	{UUID_INIT(0x50568cd9, 0x370a, 0x46b8,
		   0x9f, 0x2c, 0x6f, 0xd9, 0x06, 0x64, 0xd2, 0xee)},
	{}
};
MODULE_DEVICE_TABLE(tee, tpm_optee_dtpm_id_table);

static struct tee_client_driver tpm_optee_dtpm_driver = {
	.id_table	= tpm_optee_dtpm_id_table,
	.probe		= tpm_optee_dtpm_probe,
	.remove		= tpm_optee_dtpm_remove,
	.driver		= {
		.name		= "optee-dtpm",
	},
};

static int __init tpm_optee_dtpm_init(void)
{
	return tee_client_driver_register(&tpm_optee_dtpm_driver);
}

static void __exit tpm_optee_dtpm_exit(void)
{
	tee_client_driver_unregister(&tpm_optee_dtpm_driver);
}

module_init(tpm_optee_dtpm_init);
module_exit(tpm_optee_dtpm_exit);

MODULE_DESCRIPTION("OP-TEE driver for a discrete TPM");
MODULE_LICENSE("GPL");
