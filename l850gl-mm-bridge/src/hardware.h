/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef L850GL_HARDWARE_H
#define L850GL_HARDWARE_H

#include <stdbool.h>

/*
 * Attest that physdev names the exact L850-GL MBIM USB device currently
 * represented by sysfs. physdev is expected to come from
 * mm_modem_get_physdev(); callers must not accept it from an RPC client.
 *
 * The result is deliberately fail-closed and carries no path or diagnostic
 * data. A false result covers an unsupported device as well as every I/O or
 * validation failure.
 */
bool l850gl_hardware_attest_l850_mbim(const char *physdev);

#ifdef L850GL_HARDWARE_TESTING
/* Test-only fixture entry point; it is absent from production builds. */
bool l850gl_hardware_attest_l850_mbim_at(int sysfs_root_fd,
					 const char *physdev);
#endif

#endif
