/*
 * Copyright 2014 Linaro Ltd.
 * Copyright (C) 2014 ZTE Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MACH_ZX_CORE_H
#define __MACH_ZX_CORE_H

extern struct smp_operations zx_smp_ops;

void zx_secondary_startup(void);

#endif /* __MACH_ZX_CORE_H */
