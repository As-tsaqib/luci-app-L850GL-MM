/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "hardware.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FIBOCOM_SYSFS_ROOT "/sys"
#define FIBOCOM_SYSFS_PREFIX "/sys/devices/"
#define FIBOCOM_PHYSDEV_MAX 4096U
#define FIBOCOM_COMPONENT_MAX 255U
#define FIBOCOM_USB_ID_LEN 4U

static bool
physdev_length(const char *physdev, size_t *length)
{
	size_t i;

	if (physdev == NULL || length == NULL)
		return false;

	for (i = 0; i <= FIBOCOM_PHYSDEV_MAX; i++) {
		if (physdev[i] == '\0') {
			if (i == 0 || i > FIBOCOM_PHYSDEV_MAX)
				return false;
			*length = i;
			return true;
		}
	}

	return false;
}

static bool
component_is_canonical(const char *component, size_t length)
{
	if (length == 0 || length > FIBOCOM_COMPONENT_MAX)
		return false;

	if (length == 1 && component[0] == '.')
		return false;

	if (length == 2 && component[0] == '.' && component[1] == '.')
		return false;

	return true;
}

static int
open_physdev_at(int sysfs_root_fd, const char *physdev)
{
	const size_t prefix_length = sizeof(FIBOCOM_SYSFS_PREFIX) - 1U;
	char component[FIBOCOM_COMPONENT_MAX + 1U];
	size_t path_length;
	size_t offset;
	int current_fd = -1;
	struct stat root_stat;

	if (fstat(sysfs_root_fd, &root_stat) != 0 || !S_ISDIR(root_stat.st_mode))
		return -1;

	if (!physdev_length(physdev, &path_length) ||
	    path_length <= prefix_length ||
	    memcmp(physdev, FIBOCOM_SYSFS_PREFIX, prefix_length) != 0 ||
	    physdev[path_length - 1U] == '/')
		return -1;

	/* The injected root represents /sys, so traversal starts at devices. */
	offset = sizeof("/sys/") - 1U;
	while (offset < path_length) {
		size_t end = offset;
		size_t component_length;
		int next_fd;

		while (end < path_length && physdev[end] != '/')
			end++;
		component_length = end - offset;
		if (!component_is_canonical(physdev + offset,
					    component_length))
			goto fail;

		memcpy(component, physdev + offset, component_length);
		component[component_length] = '\0';
		next_fd = openat(current_fd >= 0 ? current_fd : sysfs_root_fd,
				 component,
				 O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
		if (next_fd < 0)
			goto fail;

		if (current_fd >= 0)
			close(current_fd);
		current_fd = next_fd;

		if (end == path_length)
			break;

		/* A repeated slash would produce a non-canonical empty component. */
		offset = end + 1U;
	}

	return current_fd;

fail:
	if (current_fd >= 0)
		close(current_fd);
	return -1;
}

static bool
read_exact_usb_id(int device_fd, const char *attribute,
		  const char expected[FIBOCOM_USB_ID_LEN + 1U])
{
	char value[FIBOCOM_USB_ID_LEN + 2U];
	size_t used = 0;
	struct stat attribute_stat;
	ssize_t count;
	int attribute_fd;
	bool matches = false;

	attribute_fd = openat(device_fd, attribute,
			      O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (attribute_fd < 0)
		return false;

	if (fstat(attribute_fd, &attribute_stat) != 0 ||
	    !S_ISREG(attribute_stat.st_mode))
		goto out;

	while (used < sizeof(value)) {
		count = read(attribute_fd, value + used, sizeof(value) - used);
		if (count < 0) {
			if (errno == EINTR)
				continue;
			goto out;
		}
		if (count == 0)
			break;
		used += (size_t)count;
	}

	/* Accept the four sysfs digits with either EOF or exactly one LF. */
	if ((used == FIBOCOM_USB_ID_LEN ||
	     (used == FIBOCOM_USB_ID_LEN + 1U &&
	      value[FIBOCOM_USB_ID_LEN] == '\n')) &&
	    memcmp(value, expected, FIBOCOM_USB_ID_LEN) == 0)
		matches = true;

out:
	close(attribute_fd);
	return matches;
}

static bool
attest_l850_mbim_at(int sysfs_root_fd, const char *physdev)
{
	int device_fd;
	bool matches;

	device_fd = open_physdev_at(sysfs_root_fd, physdev);
	if (device_fd < 0)
		return false;

	matches = read_exact_usb_id(device_fd, "idVendor", "2cb7") &&
		  read_exact_usb_id(device_fd, "idProduct", "0007");
	close(device_fd);
	return matches;
}

bool
fibocom_hardware_attest_l850_mbim(const char *physdev)
{
	int sysfs_root_fd;
	bool matches;

	sysfs_root_fd = open(FIBOCOM_SYSFS_ROOT,
			     O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (sysfs_root_fd < 0)
		return false;

	matches = attest_l850_mbim_at(sysfs_root_fd, physdev);
	close(sysfs_root_fd);
	return matches;
}

#ifdef FIBOCOM_HARDWARE_TESTING
bool
fibocom_hardware_attest_l850_mbim_at(int sysfs_root_fd, const char *physdev)
{
	return attest_l850_mbim_at(sysfs_root_fd, physdev);
}
#endif
