/*
 * dma_memcpy.c
 *
 *  Created on: Jan 28, 2019
 *      Author: root
 */

#if 0
#ifdef __linux__

#define SWITCHTEC_LIB_LINUX
#endif
#endif


#define SWITCHTEC_LIB_LINUX

#include "../inc/switchtec_ioctl.h"

#include <unistd.h>
#include <fcntl.h>
#include <endian.h>
#include <dirent.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/sysmacros.h>
#include <glob.h>
#include <poll.h>

#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>

int switchtec_open_by_path(const char *path)
{
	int fd;

	fd = open(path, O_RDWR | O_CLOEXEC);

	return fd;
}

int main(int argc, char **argv)
{
	struct switchtec_ioctl_dma_memcpy mp;
	int fd;
	int ret;

	fd = switchtec_open_by_path(argv[1]);

	if (fd < 0)
		return fd;

	mp.chan_id = strtoul(argv[2], 0, 0);
	mp.src_addr = strtoul(argv[3], 0, 0);
	mp.dst_addr = strtoul(argv[4], 0, 0);
	mp.mapped = (strtoul(argv[5], 0, 0) != 0);
	ret = ioctl(fd, SWITCHTEC_IOCTL_DMA_MEMCPY, &mp);

	close(fd);

	return ret;

}

//#endif
