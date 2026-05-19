#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
OUT="${ROOT}/build/agent_debugboard_unit"

mkdir -p "${OUT}"

cc -std=c11 -Wall -Wextra -Werror \
	-I"${ROOT}/apps/agent_debugboard/src" \
	"${ROOT}/apps/agent_debugboard/src/debugboard_model.c" \
	"${ROOT}/apps/agent_debugboard/tests/model_host/test_debugboard_model.c" \
	-o "${OUT}/debugboard_model_test"

"${OUT}/debugboard_model_test"
(cd "${ROOT}" && go test ./...)
