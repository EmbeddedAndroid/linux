// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm Peripheral Authentication Service (PAS) OP-TEE backend.
 *
 * Talks to the PAS Pseudo-Trusted-Application running in OP-TEE so that
 * firmware authentication and bring-up of remote peripherals (modem, ADSP,
 * CDSP, video, GPU/GMU, IPA, ...) can be performed via the secure world
 * instead of via SCM/TrustZone fastcalls. Used on platforms such as the
 * SA8797P (Nord / IQ-10) where the PAS lives behind OP-TEE.
 *
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2026 Linaro Ltd.
 */

#define pr_fmt(fmt) "qcom_pas_tee: " fmt

#include <linux/firmware/qcom/qcom_pas_tee.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/tee_drv.h>
#include <linux/types.h>
#include <linux/uuid.h>

/*
 * PAS Pseudo-TA command IDs (the secure side it talks to).
 *
 * PTA_QCOM_PAS_IS_SUPPORTED   = 1:
 *   [in]  params[0].value.a = pas_id
 *
 * PTA_QCOM_PAS_INIT_IMAGE     = 3:
 *   [in]  params[0].value.a = pas_id
 *   [in]  params[1].memref  = firmware metadata blob
 *
 * PTA_QCOM_PAS_MEM_SETUP      = 4:
 *   [in]  params[0].value.a = pas_id
 *   [in]  params[0].value.b = relocated firmware size
 *   [in]  params[1].value.a = addr LSB(32)
 *   [in]  params[1].value.b = addr MSB(32)
 *
 * PTA_QCOM_PAS_AUTH_AND_RESET = 6:
 *   [in]  params[0].value.a = pas_id
 *   [in]  params[0].value.b = firmware size
 *   [in]  params[1].value.a = addr LSB(32)
 *   [in]  params[1].value.b = addr MSB(32)
 *   params[2] = none (optional memref not required)
 *
 * PTA_QCOM_PAS_SHUTDOWN       = 8:
 *   [in]  params[0].value.a = pas_id
 */
#define PTA_QCOM_PAS_IS_SUPPORTED	1
#define PTA_QCOM_PAS_INIT_IMAGE		3
#define PTA_QCOM_PAS_MEM_SETUP		4
#define PTA_QCOM_PAS_AUTH_AND_RESET	6
#define PTA_QCOM_PAS_SHUTDOWN		8

/*
 * PAS Pseudo-TA UUID: daedbae4-cf3e-4b76-a5c5-dbf8b6fd5af4
 */
static const uuid_t pta_qcom_pas_uuid =
	UUID_INIT(0xdaedbae4, 0xcf3e, 0x4b76,
		  0xa5, 0xc5, 0xdb, 0xf8, 0xb6, 0xfd, 0x5a, 0xf4);

/**
 * struct qcom_pas_tee - cached PAS Pseudo-TA context/session
 * @ctx:	OP-TEE context handle.
 * @session_id:	PAS Pseudo-TA session identifier.
 * @lock:	Serializes invocations and guards @ctx / @session_id.
 * @available:	True once the session has been opened successfully.
 */
struct qcom_pas_tee {
	struct tee_context *ctx;
	u32 session_id;
	struct mutex lock;
	bool available;
};

static struct qcom_pas_tee pas_tee = {
	.lock = __MUTEX_INITIALIZER(pas_tee.lock),
};

/* Map a TEEC_* return origin/value onto a kernel errno. */
static int qcom_pas_tee_errno(u32 tee_ret)
{
	switch (tee_ret) {
	case 0:				/* TEEC_SUCCESS */
		return 0;
	case 0xffff0006:		/* TEEC_ERROR_BAD_PARAMETERS */
		return -EINVAL;
	case 0xffff000a:		/* TEEC_ERROR_NOT_SUPPORTED */
		return -EOPNOTSUPP;
	default:
		return -EIO;
	}
}

static int qcom_pas_tee_match(struct tee_ioctl_version_data *ver,
			      const void *data)
{
	return ver->impl_id == TEE_IMPL_ID_OPTEE;
}

static void qcom_pas_tee_try_open(void);

bool qcom_pas_tee_available(void)
{
	bool ret;

	mutex_lock(&pas_tee.lock);
	ret = pas_tee.available;
	mutex_unlock(&pas_tee.lock);

	if (!ret) {
		/*
		 * optee (PTA + TAs in the initramfs) may come up after this
		 * module_init. Open the context+session lazily on first use so
		 * PAS routes through OP-TEE once ready, not the SMC fallback.
		 */
		qcom_pas_tee_try_open();
		mutex_lock(&pas_tee.lock);
		ret = pas_tee.available;
		mutex_unlock(&pas_tee.lock);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(qcom_pas_tee_available);

int qcom_pas_tee_init_image(u32 pas_id, const void *metadata, size_t size)
{
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param[4];
	struct tee_shm *shm;
	void *shm_va;
	int ret;

	mutex_lock(&pas_tee.lock);
	if (!pas_tee.available) {
		ret = -ENODEV;
		goto out;
	}

	shm = tee_shm_alloc_kernel_buf(pas_tee.ctx, size);
	if (IS_ERR(shm)) {
		ret = PTR_ERR(shm);
		goto out;
	}

	shm_va = tee_shm_get_va(shm, 0);
	if (IS_ERR(shm_va)) {
		ret = PTR_ERR(shm_va);
		goto out_free;
	}
	memcpy(shm_va, metadata, size);

	memset(&arg, 0, sizeof(arg));
	memset(param, 0, sizeof(param));

	arg.func = PTA_QCOM_PAS_INIT_IMAGE;
	arg.session = pas_tee.session_id;
	arg.num_params = 4;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = pas_id;

	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	param[1].u.memref.shm = shm;
	param[1].u.memref.size = size;
	param[1].u.memref.shm_offs = 0;

	ret = tee_client_invoke_func(pas_tee.ctx, &arg, param);
	if (ret < 0)
		goto out_free;
	ret = qcom_pas_tee_errno(arg.ret);

out_free:
	tee_shm_free(shm);
out:
	mutex_unlock(&pas_tee.lock);
	return ret;
}
EXPORT_SYMBOL_GPL(qcom_pas_tee_init_image);

int qcom_pas_tee_mem_setup(u32 pas_id, phys_addr_t addr, phys_addr_t size)
{
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param[4];
	int ret;

	mutex_lock(&pas_tee.lock);
	if (!pas_tee.available) {
		ret = -ENODEV;
		goto out;
	}

	memset(&arg, 0, sizeof(arg));
	memset(param, 0, sizeof(param));

	arg.func = PTA_QCOM_PAS_MEM_SETUP;
	arg.session = pas_tee.session_id;
	arg.num_params = 4;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = pas_id;
	param[0].u.value.b = size;

	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[1].u.value.a = lower_32_bits(addr);
	param[1].u.value.b = upper_32_bits(addr);

	ret = tee_client_invoke_func(pas_tee.ctx, &arg, param);
	if (ret < 0)
		goto out;
	ret = qcom_pas_tee_errno(arg.ret);

out:
	mutex_unlock(&pas_tee.lock);
	return ret;
}
EXPORT_SYMBOL_GPL(qcom_pas_tee_mem_setup);

int qcom_pas_tee_auth_and_reset(u32 pas_id, phys_addr_t addr, phys_addr_t size)
{
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param[4];
	int ret;

	mutex_lock(&pas_tee.lock);
	if (!pas_tee.available) {
		ret = -ENODEV;
		goto out;
	}

	memset(&arg, 0, sizeof(arg));
	memset(param, 0, sizeof(param));

	arg.func = PTA_QCOM_PAS_AUTH_AND_RESET;
	arg.session = pas_tee.session_id;
	arg.num_params = 4;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = pas_id;
	param[0].u.value.b = size;

	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[1].u.value.a = lower_32_bits(addr);
	param[1].u.value.b = upper_32_bits(addr);

	/* params[2] = none (optional memref not required) */
	param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	ret = tee_client_invoke_func(pas_tee.ctx, &arg, param);
	if (ret < 0)
		goto out;
	ret = qcom_pas_tee_errno(arg.ret);

out:
	mutex_unlock(&pas_tee.lock);
	return ret;
}
EXPORT_SYMBOL_GPL(qcom_pas_tee_auth_and_reset);

int qcom_pas_tee_shutdown(u32 pas_id)
{
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param[4];
	int ret;

	mutex_lock(&pas_tee.lock);
	if (!pas_tee.available) {
		ret = -ENODEV;
		goto out;
	}

	memset(&arg, 0, sizeof(arg));
	memset(param, 0, sizeof(param));

	arg.func = PTA_QCOM_PAS_SHUTDOWN;
	arg.session = pas_tee.session_id;
	arg.num_params = 4;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = pas_id;

	ret = tee_client_invoke_func(pas_tee.ctx, &arg, param);
	if (ret < 0)
		goto out;
	ret = qcom_pas_tee_errno(arg.ret);

out:
	mutex_unlock(&pas_tee.lock);
	return ret;
}
EXPORT_SYMBOL_GPL(qcom_pas_tee_shutdown);

static void qcom_pas_tee_try_open(void)
{
	struct tee_ioctl_open_session_arg sess_arg;
	struct tee_context *ctx;
	int ret;

	mutex_lock(&pas_tee.lock);
	if (pas_tee.available) {
		mutex_unlock(&pas_tee.lock);
		return;
	}
	mutex_unlock(&pas_tee.lock);

	ctx = tee_client_open_context(NULL, qcom_pas_tee_match, NULL, NULL);
	if (IS_ERR(ctx)) {
		/*
		 * No OP-TEE present yet (or at all). This is not fatal: the
		 * SCM layer simply won't route PAS calls through us.
		 */
		return;
	}

	memset(&sess_arg, 0, sizeof(sess_arg));
	export_uuid(sess_arg.uuid, &pta_qcom_pas_uuid);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_PUBLIC;
	sess_arg.num_params = 0;

	ret = tee_client_open_session(ctx, &sess_arg, NULL);
	if (ret < 0 || sess_arg.ret != 0) {
		/*
		 * PAS Pseudo-TA absent (-ENOENT / TEEC_ERROR_ITEM_NOT_FOUND).
		 * Stay quiet: not every OP-TEE build ships the PAS PTA.
		 */
		pr_debug("PAS PTA not available (ret=%d, tee_ret=0x%x)\n",
			 ret, sess_arg.ret);
		tee_client_close_context(ctx);
		return;
	}

	mutex_lock(&pas_tee.lock);
	if (pas_tee.available) {
		mutex_unlock(&pas_tee.lock);
		tee_client_close_session(ctx, sess_arg.session);
		tee_client_close_context(ctx);
		return;
	}
	pas_tee.ctx = ctx;
	pas_tee.session_id = sess_arg.session;
	pas_tee.available = true;
	mutex_unlock(&pas_tee.lock);

	pr_info("PAS OP-TEE backend ready\n");
}

static int __init qcom_pas_tee_init(void)
{
	qcom_pas_tee_try_open();
	return 0;
}

static void __exit qcom_pas_tee_exit(void)
{
	mutex_lock(&pas_tee.lock);
	if (pas_tee.available) {
		tee_client_close_session(pas_tee.ctx, pas_tee.session_id);
		tee_client_close_context(pas_tee.ctx);
		pas_tee.available = false;
		pas_tee.ctx = NULL;
	}
	mutex_unlock(&pas_tee.lock);
}

module_init(qcom_pas_tee_init);
module_exit(qcom_pas_tee_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm PAS OP-TEE backend");
