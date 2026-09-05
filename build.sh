#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

if [[ $# -eq 0 ]]; then
	printf 'usage: %s <cross-compile-prefix> [make arguments...]\n' "$0" >&2
	exit 1
fi

cross_compile="$1"
shift
output_dir="${O:-${script_dir}/build/sun252i-f101}"
jobs="${JOBS:-$(nproc)}"

if [[ ! -x "${cross_compile}gcc" ]]; then
	printf 'error: compiler not found: %sgcc\n' "${cross_compile}" >&2
	exit 1
fi

make -C "${script_dir}" -j"${jobs}" \
	O="${output_dir}" \
	PLATFORM=generic \
	PLATFORM_DEFCONFIG=sun252i-f101.defconfig \
	CROSS_COMPILE="${cross_compile}" \
	PLATFORM_RISCV_XLEN=32 \
	PLATFORM_RISCV_ABI=ilp32 \
	PLATFORM_RISCV_ISA=rv32imafdcv_zicsr_zifencei_xtheadcmo_xtheadsync \
	FW_DYNAMIC=n \
	FW_JUMP=y \
	FW_PAYLOAD=n \
	FW_TEXT_START=0x40f80000 \
	FW_JUMP_ADDR=0x40000000 \
	FW_JUMP_FDT_OFFSET= \
	"$@"
