/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Shared transport for TPMs reached through an OP-TEE trusted application.
 */

#ifndef __TPM_OPTEE_H__
#define __TPM_OPTEE_H__

#include <linux/types.h>
#include <linux/uuid.h>

struct device;

/**
 * struct tpm_optee_config - identity of a TPM reached through an OP-TEE TA
 * @ta_uuid:	UUID of the trusted application to bind
 * @login:	TEE_IOCTL_LOGIN_* client login used to open the session
 * @cmd_id:	TA function ID that submits a TPM command
 */
struct tpm_optee_config {
	uuid_t	ta_uuid;
	u32	login;
	u32	cmd_id;
};

int tpm_optee_probe(struct device *dev, const struct tpm_optee_config *cfg);
void tpm_optee_remove(struct device *dev);

#endif /* __TPM_OPTEE_H__ */
