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
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/qda_accel.h>
#include <uapi/misc/fastrpc.h>
#include "qda_drv.h"
#include "qda_fastrpc.h"
#include "qda_legacy_ops.h"

/*
 * A buffer the DSP has mapped for an open file. A legacy session has no
 * handle table, so this is what keeps the buffer alive for as long as the
 * mapping lasts, the job a GEM handle does for the accel UAPI.
 *
 * Plain mappings are found by the address map returned, which is all
 * MUNMAP carries. Attribute-based ones are found by the fd, which doubles
 * as the identifier the DSP tracks the buffer by.
 */
struct qda_shim_mapping {
	struct list_head node;
	u64 key;
	u64 vaddr;
	u64 size;
	struct drm_gem_object *obj;
};

/* State of one open /dev/fastrpc-* file */
struct qda_shim_file {
	struct qda_session *session;
	struct mutex maps_lock;
	struct list_head mmap_list;
	struct list_head mem_map_list;
	/* Opened through the /dev/fastrpc-<dsp>-secure node */
	bool secure_node;
	/* Properties of the qda_shim_dev this file was opened on */
	bool secure_channel;
	bool unsigned_support;
};

/* One misc device node */
struct qda_shim_node {
	struct qda_shim_dev *sdev;
	struct miscdevice miscdev;
	char *name;
	bool secure;
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
 * What separates the two nodes is applied to the operations that need it,
 * using the channel properties recorded here.
 */
struct qda_shim_dev {
	struct qda_dev *qdev;
	bool secure_channel;
	bool unsigned_support;
	struct qda_shim_node nodes[2];
};

static const struct file_operations qda_shim_fops;

/* A negative @fd means the argument has no buffer, which is not an error */
static struct drm_gem_object *shim_import_fd(struct qda_session *session, s32 fd)
{
	struct drm_gem_object *obj;
	struct dma_buf *dmabuf;

	if (fd < 0)
		return NULL;

	dmabuf = dma_buf_get(fd);
	if (IS_ERR(dmabuf))
		return ERR_CAST(dmabuf);

	obj = qda_legacy_ops_table.import_dmabuf(session, dmabuf);
	dma_buf_put(dmabuf);

	return obj;
}

/* Takes ownership of @obj on success; on failure the caller still owns it */
static struct qda_shim_mapping *shim_mapping_add(struct qda_shim_file *sf,
						 struct list_head *list, u64 key, u64 vaddr,
						 u64 size, struct drm_gem_object *obj)
{
	struct qda_shim_mapping *m, *new;

	new = kzalloc_obj(*new);
	if (!new)
		return ERR_PTR(-ENOMEM);

	new->key = key;
	new->vaddr = vaddr;
	new->size = size;
	new->obj = obj;

	guard(mutex)(&sf->maps_lock);

	list_for_each_entry(m, list, node) {
		if (m->key == key) {
			kfree(new);
			return ERR_PTR(-EEXIST);
		}
	}
	list_add_tail(&new->node, list);

	return new;
}

static struct qda_shim_mapping *shim_mapping_detach(struct qda_shim_file *sf,
						    struct list_head *list, u64 key, u64 vaddr)
{
	struct qda_shim_mapping *m;

	guard(mutex)(&sf->maps_lock);

	list_for_each_entry(m, list, node) {
		if (m->key == key && m->vaddr == vaddr) {
			list_del(&m->node);
			return m;
		}
	}

	return NULL;
}

static void shim_mapping_attach(struct qda_shim_file *sf, struct list_head *list,
				struct qda_shim_mapping *m)
{
	guard(mutex)(&sf->maps_lock);
	list_add_tail(&m->node, list);
}

static void shim_mapping_free(struct qda_shim_mapping *m)
{
	drm_gem_object_put(m->obj);
	kfree(m);
}

/* The DSP unmap comes first: the buffer must outlive the DSP's use of it */
static void shim_mappings_release(struct qda_shim_file *sf)
{
	struct qda_shim_mapping *m, *tmp;

	list_for_each_entry_safe(m, tmp, &sf->mmap_list, node) {
		list_del(&m->node);
		qda_legacy_ops_table.munmap(sf->session, m->vaddr, m->size);
		shim_mapping_free(m);
	}

	list_for_each_entry_safe(m, tmp, &sf->mem_map_list, node) {
		list_del(&m->node);
		qda_legacy_ops_table.mem_unmap(sf->session, m->key, m->vaddr, m->size);
		shim_mapping_free(m);
	}
}

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

	mutex_init(&sf->maps_lock);
	INIT_LIST_HEAD(&sf->mmap_list);
	INIT_LIST_HEAD(&sf->mem_map_list);
	sf->secure_node = snode->secure;
	sf->secure_channel = snode->sdev->secure_channel;
	sf->unsigned_support = snode->sdev->unsigned_support;

	file->private_data = sf;

	return 0;
}

static int qda_shim_release(struct inode *inode, struct file *file)
{
	struct qda_shim_file *sf = file->private_data;

	shim_mappings_release(sf);
	qda_legacy_ops_table.release(sf->session);
	qda_legacy_ops_table.session_destroy(sf->session);
	mutex_destroy(&sf->maps_lock);
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

/*
 * As in drivers/misc/fastrpc.c: on a secure channel, a client that came in
 * through the non-secure node may only create an unsigned PD, and only on a
 * domain that supports unsigned PD offload.
 */
static bool qda_shim_session_rejected(struct qda_shim_file *sf, bool unsigned_pd)
{
	if (!sf->secure_node && sf->secure_channel)
		return !sf->unsigned_support || !unsigned_pd;

	return false;
}

static long qda_shim_init_create(struct qda_shim_file *sf, void __user *argp)
{
	struct drm_gem_object *elf_obj = NULL;
	struct fastrpc_init_create args;
	int err;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	if (qda_shim_session_rejected(sf, args.attrs & FASTRPC_MODE_UNSIGNED_MODULE))
		return -ECONNREFUSED;

	elf_obj = shim_import_fd(sf->session, args.filefd);
	if (IS_ERR(elf_obj))
		return PTR_ERR(elf_obj);

	err = qda_legacy_ops_table.init_create(sf->session, elf_obj, args.filelen, args.attrs,
					       args.siglen);
	if (elf_obj)
		drm_gem_object_put(elf_obj);

	return err;
}

static long qda_shim_attach(struct qda_shim_file *sf, u32 pd_type)
{
	return qda_legacy_ops_table.attach(sf->session, pd_type, NULL, 0);
}

/* memlen is ignored: the Audio PD heap size comes from the device tree */
static long qda_shim_init_create_static(struct qda_shim_file *sf, void __user *argp)
{
	struct fastrpc_init_create_static args;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	return qda_legacy_ops_table.attach(sf->session, QDA_ATTACH_AUDIO_PD,
					   u64_to_user_ptr(args.name), args.namelen);
}

static long qda_shim_mmap(struct qda_shim_file *sf, void __user *argp)
{
	struct qda_shim_mapping *m;
	struct fastrpc_req_mmap args;
	struct drm_gem_object *obj;
	int err;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	obj = shim_import_fd(sf->session, args.fd);
	if (IS_ERR(obj))
		return PTR_ERR(obj);
	if (!obj)
		return -EINVAL;

	err = qda_legacy_ops_table.mmap(sf->session, obj, args.vaddrin, args.size, args.flags,
					&args.vaddrout);
	if (err) {
		drm_gem_object_put(obj);
		return err;
	}

	m = shim_mapping_add(sf, &sf->mmap_list, args.vaddrout, args.vaddrout, args.size, obj);
	if (IS_ERR(m)) {
		err = PTR_ERR(m);
		goto err_unmap;
	}

	if (copy_to_user(argp, &args, sizeof(args))) {
		m = shim_mapping_detach(sf, &sf->mmap_list, args.vaddrout, args.vaddrout);
		if (m) {
			qda_legacy_ops_table.munmap(sf->session, m->vaddr, m->size);
			shim_mapping_free(m);
		}
		return -EFAULT;
	}

	return 0;

err_unmap:
	qda_legacy_ops_table.munmap(sf->session, args.vaddrout, args.size);
	drm_gem_object_put(obj);

	return err;
}

static long qda_shim_munmap(struct qda_shim_file *sf, void __user *argp)
{
	struct fastrpc_req_munmap args;
	struct qda_shim_mapping *m;
	int err;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	m = shim_mapping_detach(sf, &sf->mmap_list, args.vaddrout, args.vaddrout);
	if (!m)
		return -ENOENT;

	if (m->size != args.size) {
		shim_mapping_attach(sf, &sf->mmap_list, m);
		return -EINVAL;
	}

	err = qda_legacy_ops_table.munmap(sf->session, m->vaddr, m->size);
	if (err) {
		shim_mapping_attach(sf, &sf->mmap_list, m);
		return err;
	}

	shim_mapping_free(m);

	return 0;
}

static long qda_shim_mem_map(struct qda_shim_file *sf, void __user *argp)
{
	struct qda_shim_mapping *m;
	struct fastrpc_mem_map args;
	struct drm_gem_object *obj;
	int err;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	if (args.offset < 0)
		return -EINVAL;

	obj = shim_import_fd(sf->session, args.fd);
	if (IS_ERR(obj))
		return PTR_ERR(obj);
	if (!obj)
		return -EINVAL;

	/* Reserved before the DSP is asked, so the same fd cannot be mapped twice */
	m = shim_mapping_add(sf, &sf->mem_map_list, args.fd, 0, args.length, obj);
	if (IS_ERR(m)) {
		drm_gem_object_put(obj);
		return PTR_ERR(m);
	}

	err = qda_legacy_ops_table.mem_map(sf->session, obj, args.flags, args.attrs, args.offset,
					   args.vaddrin, args.length, args.fd, &args.vaddrout);
	if (err) {
		m = shim_mapping_detach(sf, &sf->mem_map_list, args.fd, 0);
		if (m)
			shim_mapping_free(m);
		return err;
	}

	scoped_guard(mutex, &sf->maps_lock)
		m->vaddr = args.vaddrout;

	if (copy_to_user(argp, &args, sizeof(args))) {
		m = shim_mapping_detach(sf, &sf->mem_map_list, args.fd, args.vaddrout);
		if (m) {
			qda_legacy_ops_table.mem_unmap(sf->session, m->key, m->vaddr, m->size);
			shim_mapping_free(m);
		}
		return -EFAULT;
	}

	return 0;
}

static long qda_shim_mem_unmap(struct qda_shim_file *sf, void __user *argp)
{
	struct fastrpc_mem_unmap args;
	struct qda_shim_mapping *m;
	int err;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	m = shim_mapping_detach(sf, &sf->mem_map_list, args.fd, args.vaddr);
	if (!m)
		return -ENOENT;

	err = qda_legacy_ops_table.mem_unmap(sf->session, m->key, m->vaddr, m->size);
	if (err) {
		shim_mapping_attach(sf, &sf->mem_map_list, m);
		return err;
	}

	shim_mapping_free(m);

	return 0;
}

/*
 * The legacy request IDs and values match the accel ones, but they are
 * translated rather than passed through, so the two ABIs stay independent.
 */
static long qda_shim_set_option(struct qda_shim_file *sf, void __user *argp)
{
	struct fastrpc_ioctl_set_option args;
	int i;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	for (i = 0; i < ARRAY_SIZE(args.reserved); i++)
		if (args.reserved[i])
			return -EINVAL;

	if (args.request_id != FASTRPC_POLL_MODE)
		return -EINVAL;

	if (args.value != FASTRPC_POLL_MODE_ENABLE &&
	    args.value != FASTRPC_POLL_MODE_DISABLE)
		return -EINVAL;

	return qda_legacy_ops_table.set_option(sf->session, QDA_SET_OPTION_POLL_MODE,
					       args.value == FASTRPC_POLL_MODE_ENABLE ?
					       QDA_POLL_MODE_ENABLE : QDA_POLL_MODE_DISABLE);
}

static long qda_shim_get_dsp_info(struct qda_shim_file *sf, void __user *argp)
{
	struct fastrpc_ioctl_capability args;
	int err;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	err = qda_legacy_ops_table.get_dsp_info(sf->session, args.attribute_id, &args.capability);
	if (err)
		return err;

	if (copy_to_user(argp, &args, sizeof(args)))
		return -EFAULT;

	return 0;
}

static long qda_shim_invoke(struct qda_shim_file *sf, void __user *argp)
{
	struct qda_legacy_invoke_arg *largs;
	struct fastrpc_invoke_args *uargs;
	struct fastrpc_invoke args;
	u32 nargs;
	int err;
	int i;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	nargs = QDA_REMOTE_SCALARS_LENGTH(args.sc);
	if (!nargs)
		return qda_legacy_ops_table.invoke(sf->session, args.handle, args.sc, NULL);

	uargs = kcalloc(nargs, sizeof(*uargs), GFP_KERNEL);
	if (!uargs)
		return -ENOMEM;

	if (copy_from_user(uargs, u64_to_user_ptr(args.args), nargs * sizeof(*uargs))) {
		kfree(uargs);
		return -EFAULT;
	}

	largs = kcalloc(nargs, sizeof(*largs), GFP_KERNEL);
	if (!largs) {
		kfree(uargs);
		return -ENOMEM;
	}

	for (i = 0; i < nargs; ++i) {
		struct drm_gem_object *obj;

		largs[i].ptr = uargs[i].ptr;
		largs[i].length = uargs[i].length;
		largs[i].fd = uargs[i].fd;

		obj = shim_import_fd(sf->session, uargs[i].fd);
		if (IS_ERR(obj)) {
			err = PTR_ERR(obj);
			goto err_put_objs;
		}
		largs[i].obj = obj;
	}

	kfree(uargs);

	/* Takes ownership of every largs[i].obj, whether or not it succeeds */
	err = qda_legacy_ops_table.invoke(sf->session, args.handle, args.sc, largs);
	kfree(largs);

	return err;

err_put_objs:
	while (i-- > 0)
		if (largs[i].obj)
			drm_gem_object_put(largs[i].obj);
	kfree(largs);
	kfree(uargs);

	return err;
}

static long qda_shim_dispatch(struct qda_shim_file *sf, unsigned int cmd, void __user *argp)
{
	switch (cmd) {
	case FASTRPC_IOCTL_ALLOC_DMA_BUFF:
		return qda_shim_alloc_dma_buf(sf, argp);
	case FASTRPC_IOCTL_INIT_CREATE:
		return qda_shim_init_create(sf, argp);
	case FASTRPC_IOCTL_INIT_ATTACH:
		return qda_shim_attach(sf, QDA_ATTACH_ROOT_PD);
	case FASTRPC_IOCTL_INIT_ATTACH_SNS:
		return qda_shim_attach(sf, QDA_ATTACH_SENSORS_PD);
	case FASTRPC_IOCTL_INIT_CREATE_STATIC:
		return qda_shim_init_create_static(sf, argp);
	case FASTRPC_IOCTL_MMAP:
		return qda_shim_mmap(sf, argp);
	case FASTRPC_IOCTL_MUNMAP:
		return qda_shim_munmap(sf, argp);
	case FASTRPC_IOCTL_MEM_MAP:
		return qda_shim_mem_map(sf, argp);
	case FASTRPC_IOCTL_MEM_UNMAP:
		return qda_shim_mem_unmap(sf, argp);
	case FASTRPC_IOCTL_INVOKE:
		return qda_shim_invoke(sf, argp);
	case FASTRPC_IOCTL_GET_DSP_INFO:
		return qda_shim_get_dsp_info(sf, argp);
	case FASTRPC_IOCTL_SET_OPTION:
		return qda_shim_set_option(sf, argp);
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
	snode->secure = secure;
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
	struct qda_shim_dev *sdev;
	int err;

	sdev = devm_kzalloc(&auxdev->dev, sizeof(*sdev), GFP_KERNEL);
	if (!sdev)
		return -ENOMEM;

	sdev->qdev = qdev;
	sdev->secure_channel = !of_property_read_bool(qdev->dev->of_node,
						      "qcom,non-secure-domain");

	if (!strncmp(qdev->dsp_name, "cdsp", 4) || !strncmp(qdev->dsp_name, "gdsp", 4)) {
		/* Both nodes, so signed and unsigned PDs are both reachable */
		sdev->unsigned_support = true;
		err = qda_shim_node_register(sdev, 0, true);
		if (!err)
			err = qda_shim_node_register(sdev, 1, false);
	} else if (!strncmp(qdev->dsp_name, "adsp", 4) ||
		   !strncmp(qdev->dsp_name, "mdsp", 4) ||
		   !strncmp(qdev->dsp_name, "sdsp", 4)) {
		err = qda_shim_node_register(sdev, 0, sdev->secure_channel);
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
