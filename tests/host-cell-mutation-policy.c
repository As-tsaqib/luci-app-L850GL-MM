/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: Apache-2.0
 */

#include "l850_mutation_policy.h"

#include <assert.h>
#include <stdio.h>

static void
test_pre_reset_requires_two_matches(void)
{
	L850GLNvmVerifier verifier;

	l850gl_nvm_verifier_init(&verifier, L850GL_NVM_VERIFY_PRE_RESET);
	assert(verifier.consecutive_matches == 0U);
	assert(l850gl_nvm_verifier_observe(&verifier,
		L850GL_NVM_OBSERVATION_MATCH) == L850GL_NVM_DECISION_RETRY);
	assert(verifier.consecutive_matches == 1U);
	assert(l850gl_nvm_verifier_observe(&verifier,
		L850GL_NVM_OBSERVATION_MATCH) == L850GL_NVM_DECISION_READY);
	assert(verifier.consecutive_matches == 2U);
	assert(l850gl_nvm_verifier_observe(&verifier,
		L850GL_NVM_OBSERVATION_MATCH) == L850GL_NVM_DECISION_READY);
	assert(verifier.consecutive_matches == 2U);
}

static void
test_mismatch_resets_pre_reset_streak(void)
{
	L850GLNvmVerifier verifier;

	l850gl_nvm_verifier_init(&verifier, L850GL_NVM_VERIFY_PRE_RESET);
	assert(l850gl_nvm_verifier_observe(&verifier,
		L850GL_NVM_OBSERVATION_MATCH) == L850GL_NVM_DECISION_RETRY);
	assert(l850gl_nvm_verifier_observe(&verifier,
		L850GL_NVM_OBSERVATION_MISMATCH) == L850GL_NVM_DECISION_RETRY);
	assert(verifier.consecutive_matches == 0U);
	assert(l850gl_nvm_verifier_observe(&verifier,
		L850GL_NVM_OBSERVATION_MATCH) == L850GL_NVM_DECISION_RETRY);
	assert(verifier.consecutive_matches == 1U);
	assert(l850gl_nvm_verifier_observe(&verifier,
		L850GL_NVM_OBSERVATION_MATCH) == L850GL_NVM_DECISION_READY);
}

static void
test_delayed_commit_sequence(void)
{
	L850GLNvmVerifier verifier;

	l850gl_nvm_verifier_init(&verifier, L850GL_NVM_VERIFY_PRE_RESET);
	assert(l850gl_nvm_verifier_observe(&verifier,
		L850GL_NVM_OBSERVATION_MISMATCH) == L850GL_NVM_DECISION_RETRY);
	assert(l850gl_nvm_verifier_observe(&verifier,
		L850GL_NVM_OBSERVATION_MISMATCH) == L850GL_NVM_DECISION_RETRY);
	assert(l850gl_nvm_verifier_observe(&verifier,
		L850GL_NVM_OBSERVATION_MATCH) == L850GL_NVM_DECISION_RETRY);
	assert(l850gl_nvm_verifier_observe(&verifier,
		L850GL_NVM_OBSERVATION_MATCH) == L850GL_NVM_DECISION_READY);
}

static void
test_invalid_observation_fails_closed(void)
{
	L850GLNvmVerifier verifier;

	l850gl_nvm_verifier_init(&verifier, L850GL_NVM_VERIFY_PRE_RESET);
	assert(l850gl_nvm_verifier_observe(&verifier,
		L850GL_NVM_OBSERVATION_INVALID) ==
		L850GL_NVM_DECISION_FAIL_INVALID);
	assert(verifier.consecutive_matches == 0U);
	assert(l850gl_nvm_verifier_observe(&verifier,
		L850GL_NVM_OBSERVATION_MATCH) == L850GL_NVM_DECISION_RETRY);
	assert(l850gl_nvm_verifier_observe(&verifier,
		L850GL_NVM_OBSERVATION_INVALID) ==
		L850GL_NVM_DECISION_FAIL_INVALID);
	assert(verifier.consecutive_matches == 0U);
}

static void
test_post_reset_accepts_one_match(void)
{
	L850GLNvmVerifier verifier;

	l850gl_nvm_verifier_init(&verifier, L850GL_NVM_VERIFY_POST_RESET);
	assert(l850gl_nvm_verifier_observe(&verifier,
		L850GL_NVM_OBSERVATION_MISMATCH) == L850GL_NVM_DECISION_RETRY);
	assert(verifier.consecutive_matches == 0U);
	assert(l850gl_nvm_verifier_observe(&verifier,
		L850GL_NVM_OBSERVATION_MATCH) == L850GL_NVM_DECISION_READY);
	assert(verifier.consecutive_matches == 1U);

	l850gl_nvm_verifier_init(&verifier, L850GL_NVM_VERIFY_POST_RESET);
	assert(l850gl_nvm_verifier_observe(&verifier,
		L850GL_NVM_OBSERVATION_INVALID) ==
		L850GL_NVM_DECISION_FAIL_INVALID);
}

static void
test_reinitialization_and_invalid_inputs(void)
{
	L850GLNvmVerifier verifier;

	l850gl_nvm_verifier_init(&verifier, L850GL_NVM_VERIFY_PRE_RESET);
	assert(l850gl_nvm_verifier_observe(&verifier,
		L850GL_NVM_OBSERVATION_MATCH) == L850GL_NVM_DECISION_RETRY);
	l850gl_nvm_verifier_init(&verifier, L850GL_NVM_VERIFY_POST_RESET);
	assert(verifier.stage == L850GL_NVM_VERIFY_POST_RESET);
	assert(verifier.consecutive_matches == 0U);
	assert(l850gl_nvm_verifier_observe(&verifier,
		L850GL_NVM_OBSERVATION_MATCH) == L850GL_NVM_DECISION_READY);

	l850gl_nvm_verifier_init(&verifier, (L850GLNvmVerifyStage)99);
	assert(l850gl_nvm_verifier_observe(&verifier,
		L850GL_NVM_OBSERVATION_MATCH) ==
		L850GL_NVM_DECISION_FAIL_INVALID);
	assert(l850gl_nvm_verifier_observe(&verifier,
		(L850GLNvmObservation)99) ==
		L850GL_NVM_DECISION_FAIL_INVALID);
	assert(l850gl_nvm_verifier_observe(NULL,
		L850GL_NVM_OBSERVATION_MATCH) ==
		L850GL_NVM_DECISION_FAIL_INVALID);
	l850gl_nvm_verifier_init(NULL, L850GL_NVM_VERIFY_PRE_RESET);
}

int
main(void)
{
	assert(L850GL_NVM_PRE_RESET_REQUIRED_MATCHES == 2U);
	assert(L850GL_NVM_POST_RESET_REQUIRED_MATCHES == 1U);
	test_pre_reset_requires_two_matches();
	test_mismatch_resets_pre_reset_streak();
	test_delayed_commit_sequence();
	test_invalid_observation_fails_closed();
	test_post_reset_accepts_one_match();
	test_reinitialization_and_invalid_inputs();
	puts("L850 cell mutation policy tests passed");
	return 0;
}
