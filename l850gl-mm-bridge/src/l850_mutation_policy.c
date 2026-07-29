/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "l850_mutation_policy.h"

#include <stddef.h>

static uint8_t
required_matches(L850GLNvmVerifyStage stage)
{
	switch (stage) {
	case L850GL_NVM_VERIFY_PRE_RESET:
		return L850GL_NVM_PRE_RESET_REQUIRED_MATCHES;
	case L850GL_NVM_VERIFY_POST_RESET:
		return L850GL_NVM_POST_RESET_REQUIRED_MATCHES;
	default:
		return 0U;
	}
}

void
l850gl_nvm_verifier_init(L850GLNvmVerifier *verifier,
			 L850GLNvmVerifyStage stage)
{
	if (verifier == NULL)
		return;
	verifier->stage = stage;
	verifier->consecutive_matches = 0U;
}

L850GLNvmDecision
l850gl_nvm_verifier_observe(L850GLNvmVerifier *verifier,
			    L850GLNvmObservation observation)
{
	uint8_t required;

	if (verifier == NULL)
		return L850GL_NVM_DECISION_FAIL_INVALID;
	required = required_matches(verifier->stage);
	if (required == 0U || observation == L850GL_NVM_OBSERVATION_INVALID) {
		verifier->consecutive_matches = 0U;
		return L850GL_NVM_DECISION_FAIL_INVALID;
	}
	if (observation == L850GL_NVM_OBSERVATION_MISMATCH) {
		verifier->consecutive_matches = 0U;
		return L850GL_NVM_DECISION_RETRY;
	}
	if (observation != L850GL_NVM_OBSERVATION_MATCH) {
		verifier->consecutive_matches = 0U;
		return L850GL_NVM_DECISION_FAIL_INVALID;
	}
	if (verifier->consecutive_matches < required)
		verifier->consecutive_matches++;
	return verifier->consecutive_matches >= required ?
		L850GL_NVM_DECISION_READY : L850GL_NVM_DECISION_RETRY;
}
