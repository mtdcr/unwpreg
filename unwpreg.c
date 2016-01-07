/*
 * unwpreg - Extracts files from wpreg firmware archives of
 *           LUXTRONIC 2.0 compatible heat pumps.
 *           Supports firmware versions 1.XX.
 *
 * Copyright 2016 Andreas Oberritter
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Example of usage:
 *   make
 *   wget http://www.heatpump24.de/software/wpreg.V1.77.zip
 *   unzip wpreg.V1.77.zip
 *   ./unwpreg wpreg.V1.77
 *   ls -l home/
 */

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

struct fwpart {
	char name[0x74];
	char size[0x0C];
	unsigned char hash[0x10];
	char mode[0x0C];
	char time[0x0C];
	char rsvd[0x0C];
	char path[0x3C];
	char vers[0x10];
	unsigned char payload[];
};

static void _chdir(const char *path)
{
	int ret;

	ret = chdir(path);
	assert(ret == 0);
}

static void _mkdir(const char *path, mode_t mode)
{
	struct stat st;
	int ret;

	assert(path[0] != '\0');
	assert(path[0] != '/');
	assert(strcmp(path, "."));
	assert(strcmp(path, ".."));

	if (stat(path, &st) != 0 && errno == ENOENT) {
		ret = mkdir(path, mode);
		assert(ret == 0);
	} else {
		assert(S_ISDIR(st.st_mode));
	}
}

static void _mkdir_p(char *path, mode_t mode)
{
	char *s;

	s = strchr(path, '/');
	while (s && *s == '/')
		*s++ = 0;

	_mkdir(path, mode);

	if (s != 0) {
		int fd = open(".", O_RDONLY);
		assert(fd >= 0);
		_chdir(path);
		_mkdir_p(s, mode);
		fchdir(fd);
		close(fd);
	}
}

static void mkdir_p_fn(const char *path, mode_t mode)
{
	char *s;
	size_t n;

	n = strlen(path) + 1;

	s = malloc(n);
	assert(s != NULL);
	memcpy(s, path, n);

	_mkdir_p(dirname(s), mode);

	free(s);
}

static void save_buf(const char *path, const unsigned char *buf, size_t size, mode_t mode, time_t time)
{
	FILE *f;

	while (*path == '/')
		path++;

	mkdir_p_fn(path, 0777);

	f = fopen(path, "w");
	assert(f != NULL);
	fwrite(buf, size, 1, f);
	fclose(f);

	chmod(path, mode);

	struct utimbuf times = {
		.actime = time,
		.modtime = time,
	};
	utime(path, &times);
}

static size_t process_buf(const void *buf, size_t count)
{
	const struct fwpart *pt = buf;
	size_t size;
	mode_t mode;
	time_t time;
	size_t rsvd;

	assert(count >= sizeof(struct fwpart));

	size = strtoul(pt->size, NULL, 0);
	mode = strtoul(pt->mode, NULL, 0);
	time = strtoul(pt->time, NULL, 0);
	rsvd = strtoul(pt->rsvd, NULL, 0);

	printf("Name: %s\n", pt->name);
	printf("Size: %#zx\n", size);
	printf("Path: %s\n", pt->path);
	printf("Mode: %#o\n", mode);
	printf("Time: %ld\n", time);
	printf("Rsvd: %#zx\n", rsvd);
	printf("Vers: %s\n", pt->vers);
	printf("\n");

	assert(count >= sizeof(struct fwpart) + size);

	save_buf(pt->path, pt->payload, size, mode, time);

	return sizeof(struct fwpart) + size;
}

static void process_mem(unsigned char *mem, size_t size)
{
	while (size > 0) {
		size_t length = process_buf(mem, size);
		mem += length;
		size -= length;
	}

	assert(size == 0);
}

static bool process_file(int fd)
{
	struct stat st;
	void *mem;

	if (fstat(fd, &st) < 0) {
		perror("fstat");
		return false;
	}

	mem = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (mem == MAP_FAILED) {
		perror("mmap");
		return false;
	}

	process_mem(mem, st.st_size);
	munmap(mem, st.st_size);
	return true;
}

int main(int argc, char *argv[])
{
	bool ok;
	int i;

	for (i = 1; i < argc; i++) {
		int fd = open(argv[i], O_RDONLY);
		if (fd < 0) {
			perror(argv[i]);
			return 1;
		}

		ok = process_file(fd);
		close(fd);
		if (!ok)
			return 1;
	}

	return 0;
}
