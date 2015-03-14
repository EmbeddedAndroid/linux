/*
 * Copyright 2014 Linaro Ltd.
 * Copyright (C) 2014 ZTE Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "core.h"

static void __init zx_l2x0_init(void)
{
	void __iomem *base;
	struct device_node *np;
	unsigned int val;

	np = of_find_compatible_node(NULL, NULL, "arm,pl310-cache");
	if (!np)
		goto out;

	base = of_iomap(np, 0);
	if (!base) {
		of_node_put(np);
		goto out;
	}

	val = readl_relaxed(base + L310_PREFETCH_CTRL);
	val |= 0x70800000;
	writel_relaxed(val, base + L310_PREFETCH_CTRL);

	writel_relaxed(L310_DYNAMIC_CLK_GATING_EN | L310_STNDBY_MODE_EN,
		       base + L310_POWER_CTRL);

	iounmap(base);
	of_node_put(np);

out:
	l2x0_of_init(0x7c433C01, 0x8000c3fe);
}

static void __init zx296702_init_machine(void)
{
	zx_l2x0_init();
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *zx296702_dt_compat[] __initconst = {
	"zte,zx296702",
	NULL,
};

DT_MACHINE_START(ZX, "ZTE ZX296702 (Device Tree)")
	.smp		= smp_ops(zx_smp_ops),
	.dt_compat	= zx296702_dt_compat,
	.init_machine	= zx296702_init_machine,
MACHINE_END
