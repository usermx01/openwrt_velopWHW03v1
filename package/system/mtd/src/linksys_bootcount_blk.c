/*
 * Linksys boot counter reset code for blk
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
#include <linux/fs.h>  

#define BOOTCOUNT_MAGIC	0x20110811

#define BC_OFFSET_INCREMENT_MIN 16
#define WRITESIZE 512
#define ERASE_MAGICK 0x00000000



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

int blk_getbc(const char *blk)
{
	uint64_t blk_size;
	int flags = O_RDWR | O_SYNC;
	struct bootcounter *curr = (struct bootcounter *)page;
	unsigned int i;
	unsigned int bc_offset_increment = BC_OFFSET_INCREMENT_MIN;
	int last_count = 0;
	int num_bc;
	int fd;
	int ret;
	int retval = 0;

	DLOG_OPEN();

	fd = open(blk, flags);

	if (fd < 0) {
		DLOG_ERR("Could not open device: %s\n", blk);
		return -1;
	}
	
	if (ioctl(fd, BLKGETSIZE64, &blk_size) < 0) {
		DLOG_ERR("Unable to obtain partition size for given partition name: %s\n", blk);

		retval = -1;
		goto out;
	}

	num_bc = blk_size / bc_offset_increment;

	for (i = 0; i < num_bc; i++) {
		pread(fd, curr, sizeof(*curr), i * bc_offset_increment);
		
		if (curr->magic != BOOTCOUNT_MAGIC &&
		    curr->magic != ERASE_MAGICK) {
			DLOG_ERR("Unexpected magic %08x at offset %08x; aborting.",
				 curr->magic, i * bc_offset_increment);

			retval = -2;
			goto out;
		}

		if (curr->magic == ERASE_MAGICK)
			break;

		last_count = curr->count;
		break;
	}

	DLOG_NOTICE("Current number of boot count: %i", last_count);
	goto out;

out:
	close(fd);
	return retval;
}

int blk_resetbc(const char *blk)
{
	uint64_t blk_size;
	int flags = O_RDWR | O_SYNC;
	struct bootcounter *curr = (struct bootcounter *)page;
	unsigned int i;
	unsigned int bc_offset_increment = BC_OFFSET_INCREMENT_MIN;
	int last_count = 0;
	int num_bc;
	int fd;
	int ret;
	int retval = 0;

	DLOG_OPEN();

	fd = open(blk, flags);

	if (fd < 0) {
		DLOG_ERR("Could not open device: %s\n", blk);
		return -1;
	}
	
	if (ioctl(fd, BLKGETSIZE64, &blk_size) < 0) {
		DLOG_ERR("Unable to obtain partition size for given partition name: %s\n", blk);

		retval = -1;
		goto out;
	}

	num_bc = blk_size / bc_offset_increment;

	for (i = 0; i < num_bc; i++) {
		pread(fd, curr, sizeof(*curr), i * bc_offset_increment);
		
		if (curr->magic != BOOTCOUNT_MAGIC &&
		    curr->magic != ERASE_MAGICK) {
			DLOG_ERR("Unexpected magic %08x at offset %08x; aborting.",
				 curr->magic, i * bc_offset_increment);

			retval = -2;
			goto out;
		}

		if (curr->magic == ERASE_MAGICK)
			break;

		last_count = curr->count;
		break;
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
		range[1] = blk_size;

		ret = ioctl(fd, BLKZEROOUT, &range);
		if (ret < 0) {
			DLOG_ERR("Failed to erase boot-count log eMMC; ioctl() BLKZEROOUT returned %i",
				 ret);

			retval = -3;
			goto out;
		}

		i = 0;
	}

	memset(curr, 0x00, bc_offset_increment);

	curr->magic = BOOTCOUNT_MAGIC;
	curr->count = 0;
	curr->checksum = BOOTCOUNT_MAGIC;

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
