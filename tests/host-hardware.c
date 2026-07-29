/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hardware.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static void
make_directory(const char *path)
{
	assert(mkdir(path, 0700) == 0);
}

static void
write_value(const char *path, const char *value)
{
	size_t length = strlen(value);
	size_t written = 0;
	ssize_t count;
	int fd;

	fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	assert(fd >= 0);
	while (written < length) {
		count = write(fd, value + written, length - written);
		assert(count > 0);
		written += (size_t)count;
	}
	assert(close(fd) == 0);
}

static void
make_device(const char *root, const char *name, const char *vendor,
	    const char *product)
{
	char path[PATH_MAX];

	assert(snprintf(path, sizeof(path), "%s/devices/%s", root, name) > 0);
	make_directory(path);
	assert(snprintf(path, sizeof(path), "%s/devices/%s/idVendor", root,
			name) > 0);
	write_value(path, vendor);
	assert(snprintf(path, sizeof(path), "%s/devices/%s/idProduct", root,
			name) > 0);
	write_value(path, product);
}

static bool
attest(int root_fd, const char *physdev)
{
	return l850gl_hardware_attest_l850_mbim_at(root_fd, physdev);
}

int
main(void)
{
	char template[PATH_MAX];
	char overlong[4112];
	char path[PATH_MAX];
	char target[PATH_MAX];
	const char *temporary_directory;
	const char *root;
	int root_fd;

	temporary_directory = getenv("TMPDIR");
	if (temporary_directory == NULL || temporary_directory[0] == '\0')
		temporary_directory = ".";
	assert(snprintf(template, sizeof(template),
			"%s/l850gl-hardware-test.XXXXXX",
			temporary_directory) > 0);
	root = mkdtemp(template);
	assert(root != NULL);
	assert(snprintf(path, sizeof(path), "%s/devices", root) > 0);
	make_directory(path);

	make_device(root, "good", "2cb7\n", "0007\n");
	make_device(root, "good-no-newline", "2cb7", "0007");
	make_device(root, "wrong-vendor", "1234\n", "0007\n");
	make_device(root, "wrong-product", "2cb7\n", "0008\n");
	make_device(root, "upper", "2CB7\n", "0007\n");
	make_device(root, "extra", "2cb7 \n", "0007\n");

	root_fd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	assert(root_fd >= 0);

	assert(attest(root_fd, "/sys/devices/good"));
	assert(attest(root_fd, "/sys/devices/good-no-newline"));
	assert(!attest(root_fd, "/sys/devices/wrong-vendor"));
	assert(!attest(root_fd, "/sys/devices/wrong-product"));
	assert(!attest(root_fd, "/sys/devices/upper"));
	assert(!attest(root_fd, "/sys/devices/extra"));
	assert(!attest(root_fd, "/sys/devices/missing"));
	assert(!attest(root_fd, NULL));

	/* Non-canonical paths must never be normalized on the caller's behalf. */
	assert(!attest(root_fd, "devices/good"));
	assert(!attest(root_fd, "/sys/devices"));
	assert(!attest(root_fd, "/sys/devices/good/"));
	assert(!attest(root_fd, "/sys/devices//good"));
	assert(!attest(root_fd, "/sys/devices/./good"));
	assert(!attest(root_fd, "/sys/devices/../devices/good"));
	assert(!attest(root_fd, "/sys/devices-good/good"));

	/* Directory symlinks cannot redirect traversal to an otherwise valid ID. */
	assert(snprintf(path, sizeof(path), "%s/devices/good-link", root) > 0);
	assert(symlink("good", path) == 0);
	assert(!attest(root_fd, "/sys/devices/good-link"));

	/* Attribute symlinks are rejected even if their target text is valid. */
	assert(snprintf(path, sizeof(path), "%s/devices/symlink-attribute", root) >
	       0);
	make_directory(path);
	assert(snprintf(target, sizeof(target), "%s/vendor-target", root) > 0);
	write_value(target, "2cb7\n");
	assert(snprintf(path, sizeof(path),
			"%s/devices/symlink-attribute/idVendor", root) > 0);
	assert(symlink(target, path) == 0);
	assert(snprintf(path, sizeof(path),
			"%s/devices/symlink-attribute/idProduct", root) > 0);
	write_value(path, "0007\n");
	assert(!attest(root_fd, "/sys/devices/symlink-attribute"));

	/* A FIFO attribute cannot block the bridge and cannot pass fstat checks. */
	assert(snprintf(path, sizeof(path), "%s/devices/fifo-attribute", root) > 0);
	make_directory(path);
	assert(snprintf(path, sizeof(path), "%s/devices/fifo-attribute/idVendor",
			root) > 0);
	assert(mkfifo(path, 0600) == 0);
	assert(snprintf(path, sizeof(path), "%s/devices/fifo-attribute/idProduct",
			root) > 0);
	write_value(path, "0007\n");
	assert(!attest(root_fd, "/sys/devices/fifo-attribute"));

	memset(overlong, 'a', sizeof(overlong));
	memcpy(overlong, "/sys/devices/", sizeof("/sys/devices/") - 1U);
	overlong[sizeof(overlong) - 1U] = '\0';
	assert(!attest(root_fd, overlong));

	assert(close(root_fd) == 0);
	assert(snprintf(path, sizeof(path), "rm -rf -- '%s'", root) > 0);
	assert(system(path) == 0);

	puts("hardware attestation tests passed");
	return 0;
}
