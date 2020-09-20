/*
 * YAFFS: Yet another Flash File System . A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2018 Aleph One Ltd.
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1 as
 * published by the Free Software Foundation.
 *
 * Note: Only YAFFS headers are LGPL, YAFFS C code is covered by GPL.
 */

#ifndef __NAMEVAL_H__
#define __NAMEVAL_H__

#include "yportenv.h"

struct yaffs_dev;

int nval_del(struct yaffs_dev *dev, YCHAR *xb, int xb_size, const YCHAR * name);
int nval_set(struct yaffs_dev *dev,
	     YCHAR *xb, int xb_size, const YCHAR * name, const YCHAR *buf,
	     int bsize, int flags);
int nval_get(struct yaffs_dev *dev,
	     const YCHAR *xb, int xb_size, const YCHAR * name, YCHAR *buf,
	     int bsize);
int nval_list(struct yaffs_dev *dev,
	      const YCHAR *xb, int xb_size, YCHAR *buf, int bsize);
int nval_hasvalues(struct yaffs_dev *dev, const YCHAR *xb, int xb_size);
#endif
