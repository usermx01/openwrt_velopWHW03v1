/*
 * Linksys boot counter reset code for mtd
 *
 * Copyright (C) 2013 Jonas Gorski <jogo@openwrt.org>
 * Portions Copyright (c) 2019, Jeff Kletsky
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License v2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <endian.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <syslog.h>

#include <sys/ioctl.h>

#include "mtd.h"

#define BOOTCOUNT_MAGIC	0x20110811

#ifndef BLKDISCARD
#define BLKDISCARD	_IO(0x12,119)
#endif
#ifndef BLKGETSIZE
#define BLKGETSIZE 	_IO(0x12,96) 
#endif

/*
 * EA6350v3, and potentially other NOR-boot devices,
 * use an offset increment of 16 between records,
 * not mtd_info_user.writesize (often 1 on NOR devices).
 */

#define BC_OFFSET_INCREMENT_MIN 16
#define WRITESIZE 512



#define DLOG_OPEN()

#define DLOG_ERR(...) do {						       \
		fprintf(stderr, "ERROR: " __VA_ARGS__); fprintf(stderr, "\n"); \
	} while (0)

#define DLOG_NOTICE(...) do {						\
		fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n");	\
	} while (0)

#define DLOG_DEBUG(...)



struct bootcounter {
	uint32_t magic;
	uint32_t count;
	uint32_t checksum;
};

static char page[2048];

int mtd_resetbc(const char *mtd)
{
	//struct mtd_info_user mtd_info;
	int emmc_size;
	int flags = O_RDWR | O_SYNC;
	struct bootcounter *curr = (struct bootcounter *)page;
	unsigned int i;
	unsigned int bc_offset_increment;
	int last_count = 0;
	int num_bc;
	int fd;
	int ret;
	int retval = 0;

	DLOG_OPEN();

	fd = open(mtd, flags);

	if (fd < 0) {
		fprintf(stderr, "Could not open device: %s\n", mtd);
		return -1;
	}
	//fd = mtd_check_open(mtd);

	if (ioctl(fd, BLKGETSIZE, &emmc_size) < 0) {
		DLOG_ERR("Unable to obtain mtd_info for given partition name.");

		retval = -1;
		goto out;
	}


	/* Detect need to override increment (for EA6350v3) */

	/*if (mtd_info.writesize < BC_OFFSET_INCREMENT_MIN) {

		bc_offset_increment = BC_OFFSET_INCREMENT_MIN;
		DLOG_DEBUG("Offset increment set to %i for writesize of %i",
			   bc_offset_increment, mtd_info.writesize);
	} else {

		bc_offset_increment = mtd_info.writesize;
	}*/

	num_bc = emmc_size / bc_offset_increment;

	for (i = 0; i < num_bc; i++) {
		pread(fd, curr, sizeof(*curr), i * bc_offset_increment);

		/* Existing code assumes erase is to 0xff; left as-is (2019) */

		if (curr->magic != BOOTCOUNT_MAGIC &&
		    curr->magic != 0xffffffff) {
			DLOG_ERR("Unexpected magic %08x at offset %08x; aborting.",
				 curr->magic, i * bc_offset_increment);

			retval = -2;
			goto out;
		}

		if (curr->magic == 0xffffffff)
			break;

		last_count = curr->count;
	}


	if (last_count == 0) {	/* bootcount is already 0 */

		retval = 0;
		goto out;
	}


	if (i == num_bc) {
		DLOG_NOTICE("Boot-count log full with %i entries; erasing (expected occasionally).",
			    i);

		uint64_t range[2];
		range[0] = 0;
		range[1] = emmc_size;

		ret = ioctl(fd, BLKDISCARD, &range);
		if (ret < 0) {
			DLOG_ERR("Failed to erase boot-count log eMMC; ioctl() BLKDISCARD returned %i",
				 ret);

			retval = -3;
			goto out;
		}

		i = 0;
	}

	memset(curr, 0xff, bc_offset_increment);

	curr->magic = BOOTCOUNT_MAGIC;
	curr->count = 0;
	curr->checksum = BOOTCOUNT_MAGIC;

	/* Assumes bc_offset_increment is a multiple of mtd_info.writesize */

	ret = pwrite(fd, curr, bc_offset_increment, i * bc_offset_increment);
	if (ret < 0) {
		DLOG_ERR("Failed to write boot-count log entry; pwrite() returned %i",
			 errno);
		retval = -4;
		goto out;

	} else {
		sync();

		DLOG_NOTICE("Boot count sucessfully reset to zero.");

		retval = 0;
		goto out;
	}

out:
	close(fd);
	return retval;
}
