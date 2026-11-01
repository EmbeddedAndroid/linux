// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#include <linux/array_size.h>
#include <linux/auxiliary_bus.h>
#include <linux/dma-buf.h>
#include <linux/err.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/limits.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <drm/drm_drv.h>
#include <uapi/misc/fastrpc.h>
#include "qda_drv.h"
#include "qda_legacy_ops.h"

/* State of one open /dev/fastrpc-* file */
struct qda_shim_file {
	struct qda_session *session;
};

/* One misc device node */
struct qda_shim_node {
	struct qda_shim_dev *sdev;
	struct miscdevice miscdev;
	char *name;
	bool registered;
};

/*
 * One per DSP domain, holding up to two misc nodes. As in
 * drivers/misc/fastrpc.c, a channel is secure unless its DT node has
 * qcom,non-secure-domain. ADSP, MDSP and SDSP get a single node, secure or
 * not according to the channel. CDSP and GDSP, which support unsigned PD
 * offload, always get both, so that untrusted applications can still reach
 * the non-secure node on a secure channel.
 *
 * The two nodes behave identically for now; what separates them is applied
 * to the operations that need it as those are added.
 */
struct qda_shim_dev {
	struct qda_dev *qdev;
	struct qda_shim_node nodes[2];
};

static const struct file_operations qda_shim_fops;

static int qda_shim_open(struct inode *inode, struct file *file)
{
	struct qda_shim_node *snode = container_of(file->private_data, struct qda_shim_node,
						   miscdev);
	struct qda_shim_file *sf;

	sf = kzalloc_obj(*sf);
	if (!sf)
		return -ENOMEM;

	sf->session = qda_legacy_ops_table.session_create(snode->sdev->qdev);
	if (IS_ERR(sf->session)) {
		int err = PTR_ERR(sf->session);

		kfree(sf);
		return err;
	}

	file->private_data = sf;

	return 0;
}

static int qda_shim_release(struct inode *inode, struct file *file)
{
	struct qda_shim_file *sf = file->private_data;

	qda_legacy_ops_table.session_destroy(sf->session);
	kfree(sf);

	return 0;
}

static long qda_shim_alloc_dma_buf(struct qda_shim_file *sf, void __user *argp)
{
	struct fastrpc_alloc_dma_buf args;
	struct dma_buf *dmabuf;
	int fd;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	/* The size is 64 bits wide on the wire but a size_t further down */
	if (args.size > SIZE_MAX)
		return -EINVAL;

	/* args.flags is not used, as in drivers/misc/fastrpc.c */
	dmabuf = qda_legacy_ops_table.alloc_dmabuf(sf->session, args.size);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		dma_buf_put(dmabuf);
		return fd;
	}

	args.fd = fd;
	if (copy_to_user(argp, &args, sizeof(args))) {
		put_unused_fd(fd);
		dma_buf_put(dmabuf);
		return -EFAULT;
	}

	/* Publish the fd only once nothing can fail */
	fd_install(fd, dmabuf->file);

	return 0;
}

static long qda_shim_dispatch(struct qda_shim_file *sf, unsigned int cmd, void __user *argp)
{
	switch (cmd) {
	case FASTRPC_IOCTL_ALLOC_DMA_BUFF:
		return qda_shim_alloc_dma_buf(sf, argp);
	default:
		/*
		 * Includes FASTRPC_IOCTL_FREE_DMA_BUFF, which the driver this
		 * replaces never implemented either: buffers are released by
		 * closing their fd.
		 */
		return -ENOTTY;
	}
}

/*
 * Mirrors drm_ioctl()'s unplug check. Not drm_dev_enter(): holding the SRCU
 * read side across a wait for the DSP's reply would stall drm_dev_unplug()
 * in qda_rpmsg_remove() for as long as the DSP takes to answer.
 */
static long qda_shim_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct qda_shim_file *sf = file->private_data;

	if (drm_dev_is_unplugged(&sf->session->qda_dev->drm_dev))
		return -ENODEV;

	return qda_shim_dispatch(sf, cmd, (void __user *)arg);
}

static const struct file_operations qda_shim_fops = {
	.owner = THIS_MODULE,
	.open = qda_shim_open,
	.release = qda_shim_release,
	.unlocked_ioctl = qda_shim_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

static int qda_shim_node_register(struct qda_shim_dev *sdev, int i, bool secure)
{
	struct qda_shim_node *snode = &sdev->nodes[i];
	int err;

	snode->name = kasprintf(GFP_KERNEL, "fastrpc-%s%s", sdev->qdev->dsp_name,
				secure ? "-secure" : "");
	if (!snode->name)
		return -ENOMEM;

	snode->sdev = sdev;
	snode->miscdev.minor = MISC_DYNAMIC_MINOR;
	snode->miscdev.name = snode->name;
	snode->miscdev.fops = &qda_shim_fops;

	err = misc_register(&snode->miscdev);
	if (err) {
		kfree(snode->name);
		snode->name = NULL;
		return err;
	}

	snode->registered = true;

	return 0;
}

/*
 * misc_deregister() takes misc_mtx, which misc_open() holds across the
 * lookup and our ->open(). So once it returns no open can still be using
 * the node. Files that are already open hold neither the node nor @sdev,
 * only their session.
 */
static void qda_shim_nodes_unregister(struct qda_shim_dev *sdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sdev->nodes); i++) {
		if (sdev->nodes[i].registered)
			misc_deregister(&sdev->nodes[i].miscdev);
		kfree(sdev->nodes[i].name);
	}
}

static int qda_shim_probe(struct auxiliary_device *auxdev,
			  const struct auxiliary_device_id *id)
{
	struct qda_dev *qdev = dev_get_platdata(&auxdev->dev);
	bool secure_channel = !of_property_read_bool(qdev->dev->of_node,
						     "qcom,non-secure-domain");
	struct qda_shim_dev *sdev;
	int err;

	sdev = devm_kzalloc(&auxdev->dev, sizeof(*sdev), GFP_KERNEL);
	if (!sdev)
		return -ENOMEM;

	sdev->qdev = qdev;

	if (!strncmp(qdev->dsp_name, "cdsp", 4) || !strncmp(qdev->dsp_name, "gdsp", 4)) {
		/* Both nodes, so signed and unsigned PDs are both reachable */
		err = qda_shim_node_register(sdev, 0, true);
		if (!err)
			err = qda_shim_node_register(sdev, 1, false);
	} else if (!strncmp(qdev->dsp_name, "adsp", 4) ||
		   !strncmp(qdev->dsp_name, "mdsp", 4) ||
		   !strncmp(qdev->dsp_name, "sdsp", 4)) {
		err = qda_shim_node_register(sdev, 0, secure_channel);
	} else {
		err = -ENODEV;
	}

	if (err) {
		dev_err(&auxdev->dev, "Failed to create %s node(s): %d\n",
			qdev->dsp_name, err);
		qda_shim_nodes_unregister(sdev);
		return err;
	}

	auxiliary_set_drvdata(auxdev, sdev);

	return 0;
}

static void qda_shim_remove(struct auxiliary_device *auxdev)
{
	qda_shim_nodes_unregister(auxiliary_get_drvdata(auxdev));
}

static const struct auxiliary_device_id qda_shim_id_table[] = {
	{ .name = QDA_DRIVER_NAME "." QDA_LEGACY_AUXDEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(auxiliary, qda_shim_id_table);

static struct auxiliary_driver qda_shim_driver = {
	.name = "fastrpc_shim",
	.id_table = qda_shim_id_table,
	.probe = qda_shim_probe,
	.remove = qda_shim_remove,
};

module_auxiliary_driver(qda_shim_driver);

MODULE_IMPORT_NS("DMA_BUF");
MODULE_AUTHOR("Qualcomm AI Infra Team");
MODULE_DESCRIPTION("Qualcomm DSP accelerator legacy FastRPC compatibility shim");
MODULE_LICENSE("GPL");
