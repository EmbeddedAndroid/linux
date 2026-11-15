// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xen static shared memory regions as character devices.
 *
 * A domain's static shared memory (xen,domain-shared-memory-v1 in the Xen
 * device tree) shows up in its own device tree as a /reserved-memory child
 * compatible "xen,shared-memory-v1" with a "xen,id" string; Xen 4.19 and
 * later also list it in the domain's /memory. Nothing binds the node, so
 * user space reaches it only through /dev/mem and no device can import it.
 *
 * Each region becomes /dev/xen-shm/<xen,id>: a shared mmap maps the region
 * (the file offset is the offset into it) as normal cacheable memory, and
 * XSHM_IOCTL_EXPORT returns a DMA-BUF for a page aligned slice of it, so a
 * DMA device can import memory another domain shares with this one. Only a
 * region that lies entirely in the kernel's memory map (and so has struct
 * pages) can be exported. Another domain writes the
 * memory without any cache maintenance here, so only DMA-coherent devices
 * may attach. A slice is exported at most once at a time: an export that
 * overlaps a live DMA-BUF fails with -EBUSY until that one is released, so
 * a backend cannot hand out memory a device may still be writing.
 */

#include <linux/dma-buf.h>
#include <linux/dma-map-ops.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/memblock.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/sizes.h>
#include <uapi/xen/static-shm.h>

struct xshm {
	struct miscdevice misc;
	char name[64];
	phys_addr_t base;
	resource_size_t size;
	struct mutex lock;		/* the live list */
	bool has_pages;
	struct list_head live;		/* exported slices not yet released */
};

struct xshm_buf {
	struct xshm *x;
	struct list_head node;
	unsigned long pfn;
	size_t size;
};

static int xshm_attach(struct dma_buf *dmabuf, struct dma_buf_attachment *at)
{
	return dev_is_dma_coherent(at->dev) ? 0 : -EOPNOTSUPP;
}

static struct sg_table *xshm_map_dma_buf(struct dma_buf_attachment *at,
					 enum dma_data_direction dir)
{
	struct xshm_buf *b = at->dmabuf->priv;
	unsigned int n = DIV_ROUND_UP(b->size, SZ_1G), i;
	struct scatterlist *sg;
	struct sg_table *sgt;
	int ret;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-ENOMEM);
	ret = sg_alloc_table(sgt, n, GFP_KERNEL);
	if (ret)
		goto err_free;
	for_each_sgtable_sg(sgt, sg, i) {
		size_t off = (size_t)i * SZ_1G;

		sg_set_page(sg, pfn_to_page(b->pfn + (off >> PAGE_SHIFT)),
			    min_t(size_t, SZ_1G, b->size - off), 0);
	}
	ret = dma_map_sgtable(at->dev, sgt, dir, 0);
	if (ret)
		goto err_table;
	return sgt;

err_table:
	sg_free_table(sgt);
err_free:
	kfree(sgt);
	return ERR_PTR(ret);
}

static void xshm_unmap_dma_buf(struct dma_buf_attachment *at,
			       struct sg_table *sgt, enum dma_data_direction dir)
{
	dma_unmap_sgtable(at->dev, sgt, dir, 0);
	sg_free_table(sgt);
	kfree(sgt);
}

static int xshm_dma_buf_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct xshm_buf *b = dmabuf->priv;
	size_t len = vma->vm_end - vma->vm_start;

	if (!(vma->vm_flags & VM_SHARED) ||
	    vma->vm_pgoff + (len >> PAGE_SHIFT) > b->size >> PAGE_SHIFT)
		return -EINVAL;
	return remap_pfn_range(vma, vma->vm_start, b->pfn + vma->vm_pgoff, len,
			       vma->vm_page_prot);
}

static void xshm_dma_buf_release(struct dma_buf *dmabuf)
{
	struct xshm_buf *b = dmabuf->priv;

	scoped_guard(mutex, &b->x->lock)
		list_del(&b->node);
	kfree(b);
}

static bool xshm_busy(struct xshm *x, unsigned long pfn, size_t size)
{
	unsigned long end = pfn + (size >> PAGE_SHIFT);
	struct xshm_buf *b;

	list_for_each_entry(b, &x->live, node)
		if (pfn < b->pfn + (b->size >> PAGE_SHIFT) && b->pfn < end)
			return true;
	return false;
}

static const struct dma_buf_ops xshm_dma_buf_ops = {
	.attach = xshm_attach,
	.map_dma_buf = xshm_map_dma_buf,
	.unmap_dma_buf = xshm_unmap_dma_buf,
	.mmap = xshm_dma_buf_mmap,
	.release = xshm_dma_buf_release,
};

static long xshm_export(struct xshm *x, struct xshm_export __user *uarg)
{
	DEFINE_DMA_BUF_EXPORT_INFO(info);
	struct xshm_export arg;
	struct xshm_buf *b;
	struct dma_buf *dmabuf;
	int fd;

	if (copy_from_user(&arg, uarg, sizeof(arg)))
		return -EFAULT;
	if (arg.flags || !arg.size || !PAGE_ALIGNED(arg.offset | arg.size) ||
	    arg.offset >= x->size || arg.size > x->size - arg.offset)
		return -EINVAL;
	if (!x->has_pages)
		return -EOPNOTSUPP;

	b = kzalloc(sizeof(*b), GFP_KERNEL);
	if (!b)
		return -ENOMEM;
	b->x = x;
	b->pfn = PHYS_PFN(x->base + arg.offset);
	b->size = arg.size;

	info.ops = &xshm_dma_buf_ops;
	info.size = arg.size;
	info.flags = O_RDWR | O_CLOEXEC;
	info.priv = b;
	scoped_guard(mutex, &x->lock) {
		if (xshm_busy(x, b->pfn, b->size)) {
			kfree(b);
			return -EBUSY;
		}
		dmabuf = dma_buf_export(&info);
		if (IS_ERR(dmabuf)) {
			kfree(b);
			return PTR_ERR(dmabuf);
		}
		list_add(&b->node, &x->live);
	}
	fd = dma_buf_fd(dmabuf, O_CLOEXEC);
	if (fd < 0) {
		dma_buf_put(dmabuf);
		return fd;
	}
	arg.fd = fd;
	if (copy_to_user(uarg, &arg, sizeof(arg)))
		return -EFAULT;	/* the fd stays installed, as for DRM PRIME */
	return 0;
}

static struct xshm *xshm_of(struct file *f)
{
	return container_of(f->private_data, struct xshm, misc);
}

static long xshm_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct xshm *x = xshm_of(f);

	switch (cmd) {
	case XSHM_IOCTL_INFO: {
		struct xshm_info info = { .size = x->size };

		return copy_to_user((void __user *)arg, &info, sizeof(info)) ? -EFAULT : 0;
	}
	case XSHM_IOCTL_EXPORT:
		return xshm_export(x, (struct xshm_export __user *)arg);
	}
	return -ENOTTY;
}

static int xshm_mmap(struct file *f, struct vm_area_struct *vma)
{
	struct xshm *x = xshm_of(f);
	size_t len = vma->vm_end - vma->vm_start;

	if (!(vma->vm_flags & VM_SHARED) || vma->vm_pgoff > x->size >> PAGE_SHIFT ||
	    len >> PAGE_SHIFT > (x->size >> PAGE_SHIFT) - vma->vm_pgoff)
		return -EINVAL;
	vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
	return remap_pfn_range(vma, vma->vm_start, PHYS_PFN(x->base) + vma->vm_pgoff,
			       len, vma->vm_page_prot);
}

static const struct file_operations xshm_fops = {
	.unlocked_ioctl = xshm_ioctl,
	.mmap = xshm_mmap,
};

static int __init xshm_add(struct device_node *np)
{
	struct resource r;
	const char *id;
	struct xshm *x;
	int ret;

	if (of_property_read_string(np, "xen,id", &id))
		return -EINVAL;
	ret = of_address_to_resource(np, 0, &r);
	if (ret)
		return ret;
	if (!PAGE_ALIGNED(r.start) || !PAGE_ALIGNED(resource_size(&r)))
		return -EINVAL;

	x = kzalloc(sizeof(*x), GFP_KERNEL);
	if (!x)
		return -ENOMEM;
	x->base = r.start;
	x->size = resource_size(&r);
	x->has_pages = memblock_is_region_memory(x->base, x->size);
	mutex_init(&x->lock);
	INIT_LIST_HEAD(&x->live);
	snprintf(x->name, sizeof(x->name), "xen-shm/%s", id);
	x->misc.minor = MISC_DYNAMIC_MINOR;
	x->misc.name = x->name;
	x->misc.fops = &xshm_fops;
	x->misc.mode = 0600;
	ret = misc_register(&x->misc);
	if (ret) {
		kfree(x);
		return ret;
	}
	pr_info("xen-static-shm: %s %pa size %pa%s\n", id, &x->base, &x->size,
		x->has_pages ? "" : " (not exportable)");
	return 0;
}

static int __init xshm_init(void)
{
	struct device_node *np;

	for_each_compatible_node(np, NULL, "xen,shared-memory-v1")
		if (xshm_add(np))
			pr_warn("xen-static-shm: %pOF not added\n", np);
	return 0;
}
device_initcall(xshm_init);

