// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Shared transport for TPMs reached through an OP-TEE trusted application:
 * open the context and session, relay raw TPM2 commands over TEE shared
 * memory, and register the result as a standard tpm_chip. A front-end driver
 * supplies the per-device identity (TA UUID, client login, submit function
 * ID) and binds the TA.
 */

#include <linux/module.h>
#include <linux/tee_drv.h>
#include <linux/tpm.h>
#include <linux/uuid.h>

#include "tpm.h"
#include "tpm_optee.h"

#define TPM_OPTEE_MAX_COMMAND_SIZE	4096
#define TPM_OPTEE_MAX_RESPONSE_SIZE	4096

struct tpm_optee_private {
	struct tpm_chip *chip;
	struct tee_context *ctx;
	u32 session;
	struct tee_shm *shm;
	const struct tpm_optee_config *cfg;
};

/* Validate the response header the TA wrote back, return its length or -EIO */
static int tpm_optee_resp_len(struct tpm_chip *chip, const u8 *resp,
			      size_t bufsiz)
{
	size_t resp_len = be32_to_cpu(((struct tpm_header *)resp)->length);

	if (resp_len < TPM_HEADER_SIZE || resp_len > TPM_OPTEE_MAX_RESPONSE_SIZE ||
	    resp_len > bufsiz) {
		dev_err(&chip->dev, "bad response length %zu (buffer %zu)\n",
			resp_len, bufsiz);
		return -EIO;
	}

	return resp_len;
}

/**
 * tpm_optee_op_send() - relay a TPM command through TEE shared memory.
 * @chip:	the tpm_chip description
 * @buf:	command on entry, response on return
 * @bufsiz:	size of @buf
 * @cmd_len:	number of command bytes to send
 *
 * Return: number of response bytes on success, -errno on failure.
 */
static int tpm_optee_op_send(struct tpm_chip *chip, u8 *buf, size_t bufsiz,
			     size_t cmd_len)
{
	struct tpm_optee_private *pvt_data = dev_get_drvdata(chip->dev.parent);
	struct tee_shm *shm = pvt_data->shm;
	struct tee_param command_params[4] = { };
	struct tee_ioctl_invoke_arg transceive_args = {
		.func = pvt_data->cfg->cmd_id,
		.session = pvt_data->session,
		.num_params = 4,
	};
	u8 *temp_buf;
	int rc;

	if (cmd_len > TPM_OPTEE_MAX_COMMAND_SIZE || cmd_len > bufsiz) {
		dev_err(&chip->dev, "command length %zu (buffer %zu)\n",
			cmd_len, bufsiz);
		return -EIO;
	}

	/* memref[0]: command in, memref[1]: response in/out */
	command_params[0] = (struct tee_param) {
		.attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT,
		.u.memref = {
			.shm = shm,
			.size = cmd_len,
			.shm_offs = 0,
		},
	};

	temp_buf = tee_shm_get_va(shm, 0);
	if (IS_ERR(temp_buf)) {
		dev_err(&chip->dev, "tee_shm_get_va failed for transmit\n");
		return PTR_ERR(temp_buf);
	}
	memset(temp_buf, 0, TPM_OPTEE_MAX_COMMAND_SIZE + TPM_OPTEE_MAX_RESPONSE_SIZE);
	memcpy(temp_buf, buf, cmd_len);

	command_params[1] = (struct tee_param) {
		.attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT,
		.u.memref = {
			.shm = shm,
			.size = TPM_OPTEE_MAX_RESPONSE_SIZE,
			.shm_offs = TPM_OPTEE_MAX_COMMAND_SIZE,
		},
	};

	rc = tee_client_invoke_func(pvt_data->ctx, &transceive_args,
				    command_params);
	if (rc < 0 || transceive_args.ret != 0) {
		dev_err(&chip->dev, "SUBMIT_COMMAND invoke error: 0x%x\n",
			transceive_args.ret);
		return (rc < 0) ? rc : -EIO;
	}

	temp_buf = tee_shm_get_va(shm, command_params[1].u.memref.shm_offs);
	if (IS_ERR(temp_buf)) {
		dev_err(&chip->dev, "tee_shm_get_va failed for receive\n");
		return PTR_ERR(temp_buf);
	}

	rc = tpm_optee_resp_len(chip, temp_buf, bufsiz);
	if (rc < 0)
		return rc;

	memcpy(buf, temp_buf, rc);

	return rc;
}

static const struct tpm_class_ops tpm_optee_ops = {
	.flags = TPM_OPS_AUTO_STARTUP,
	.send = tpm_optee_op_send,
};

static int tpm_optee_match(struct tee_ioctl_version_data *ver, const void *data)
{
	/* Only GP-compliant OP-TEE is supported */
	if (ver->impl_id == TEE_IMPL_ID_OPTEE && (ver->gen_caps & TEE_GEN_CAP_GP))
		return 1;

	return 0;
}

static int tpm_optee_open_session(struct device *dev, struct tee_context *ctx,
				  const struct tpm_optee_config *cfg,
				  u32 *session)
{
	struct tee_ioctl_open_session_arg sess_arg = {
		.clnt_login = cfg->login,
	};
	int rc;

	export_uuid(sess_arg.uuid, &cfg->ta_uuid);

	rc = tee_client_open_session(ctx, &sess_arg, NULL);
	if (rc < 0 || sess_arg.ret != 0) {
		dev_err(dev, "tee_client_open_session failed, err=0x%x\n",
			sess_arg.ret);
		return -EINVAL;
	}

	*session = sess_arg.session;

	return 0;
}

int tpm_optee_probe(struct device *dev, const struct tpm_optee_config *cfg)
{
	struct tpm_optee_private *pvt_data;
	struct tpm_chip *chip;
	int rc;

	pvt_data = devm_kzalloc(dev, sizeof(*pvt_data), GFP_KERNEL);
	if (!pvt_data)
		return -ENOMEM;

	pvt_data->cfg = cfg;
	dev_set_drvdata(dev, pvt_data);

	pvt_data->ctx = tee_client_open_context(NULL, tpm_optee_match, NULL,
						NULL);
	if (IS_ERR(pvt_data->ctx)) {
		if (PTR_ERR(pvt_data->ctx) == -ENOENT)
			return -EPROBE_DEFER;
		dev_err(dev, "tee_client_open_context failed\n");
		return PTR_ERR(pvt_data->ctx);
	}

	rc = tpm_optee_open_session(dev, pvt_data->ctx, cfg, &pvt_data->session);
	if (rc)
		goto out_tee_session;

	pvt_data->shm = tee_shm_alloc_kernel_buf(pvt_data->ctx,
						 TPM_OPTEE_MAX_COMMAND_SIZE +
						 TPM_OPTEE_MAX_RESPONSE_SIZE);
	if (IS_ERR(pvt_data->shm)) {
		dev_err(dev, "tee_shm_alloc_kernel_buf failed\n");
		rc = -ENOMEM;
		goto out_shm_alloc;
	}

	chip = tpm_chip_alloc(dev, &tpm_optee_ops);
	if (IS_ERR(chip)) {
		dev_err(dev, "tpm_chip_alloc failed\n");
		rc = PTR_ERR(chip);
		goto out_chip_alloc;
	}

	pvt_data->chip = chip;
	pvt_data->chip->flags |= TPM_CHIP_FLAG_TPM2 | TPM_CHIP_FLAG_SYNC;

	rc = tpm_chip_register(pvt_data->chip);
	if (rc) {
		dev_err(dev, "tpm_chip_register failed with rc=%d\n", rc);
		goto out_chip;
	}

	return 0;

out_chip:
	put_device(&pvt_data->chip->dev);
out_chip_alloc:
	tee_shm_free(pvt_data->shm);
out_shm_alloc:
	tee_client_close_session(pvt_data->ctx, pvt_data->session);
out_tee_session:
	tee_client_close_context(pvt_data->ctx);

	return rc;
}
EXPORT_SYMBOL_GPL(tpm_optee_probe);

void tpm_optee_remove(struct device *dev)
{
	struct tpm_optee_private *pvt_data = dev_get_drvdata(dev);

	tpm_chip_unregister(pvt_data->chip);
	put_device(&pvt_data->chip->dev);
	tee_shm_free(pvt_data->shm);
	tee_client_close_session(pvt_data->ctx, pvt_data->session);
	tee_client_close_context(pvt_data->ctx);
}
EXPORT_SYMBOL_GPL(tpm_optee_remove);

MODULE_DESCRIPTION("OP-TEE TPM transport core");
MODULE_LICENSE("GPL");
