/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef L850GL_L850_MUTATION_POLICY_H
#define L850GL_L850_MUTATION_POLICY_H

#include <stdint.h>

#define L850GL_NVM_PRE_RESET_REQUIRED_MATCHES 2U
#define L850GL_NVM_POST_RESET_REQUIRED_MATCHES 1U

typedef enum {
	L850GL_NVM_VERIFY_PRE_RESET = 0,
	L850GL_NVM_VERIFY_POST_RESET,
} L850GLNvmVerifyStage;

typedef enum {
	L850GL_NVM_OBSERVATION_MATCH = 0,
	L850GL_NVM_OBSERVATION_MISMATCH,
	L850GL_NVM_OBSERVATION_INVALID,
} L850GLNvmObservation;

typedef enum {
	L850GL_NVM_DECISION_RETRY = 0,
	L850GL_NVM_DECISION_READY,
	L850GL_NVM_DECISION_FAIL_INVALID,
} L850GLNvmDecision;

typedef struct {
	L850GLNvmVerifyStage stage;
	uint8_t consecutive_matches;
} L850GLNvmVerifier;

void l850gl_nvm_verifier_init(L850GLNvmVerifier *verifier,
			      L850GLNvmVerifyStage stage);
L850GLNvmDecision l850gl_nvm_verifier_observe(
	L850GLNvmVerifier *verifier, L850GLNvmObservation observation);

#endif /* L850GL_L850_MUTATION_POLICY_H */
