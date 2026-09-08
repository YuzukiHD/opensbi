#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

usage()
{
	printf 'usage: %s [--arch rv32|rv64] <cross-compile-prefix> [make arguments...]\n' "$0" >&2
}

arch="${RISCV_ARCH:-rv32}"

while [[ $# -gt 0 ]]; do
	case "$1" in
		-a|--arch)
			if [[ $# -lt 2 ]]; then
				usage
				exit 1
			fi
			arch="$2"
			shift 2
			;;
		--arch=*)
			arch="${1#--arch=}"
			shift
			;;
		-h|--help)
			usage
			exit 0
			;;
		--)
			shift
			break
			;;
		-*)
			printf 'error: unknown option: %s\n' "$1" >&2
			usage
			exit 1
			;;
		*)
			break
			;;
	esac
done

if [[ $# -eq 0 ]]; then
	usage
	exit 1
fi

case "${arch}" in
	rv32)
		riscv_xlen=32
		riscv_abi=ilp32
		riscv_isa=rv32imafdcv_zicsr_zifencei_xtheadcmo_xtheadsync
		default_output_dir="${script_dir}/build/sun252i-f101"
		;;
	rv64)
		riscv_xlen=64
		riscv_abi=lp64
		riscv_isa=rv64imafdcv_zicsr_zifencei_xtheadcmo_xtheadsync
		default_output_dir="${script_dir}/build/sun252i-f101-rv64"
		;;
	*)
		printf 'error: unsupported architecture: %s (expected rv32 or rv64)\n' "${arch}" >&2
		exit 1
		;;
esac

cross_compile="$1"
shift
output_dir="${O:-${default_output_dir}}"
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
	PLATFORM_RISCV_XLEN="${riscv_xlen}" \
	PLATFORM_RISCV_ABI="${riscv_abi}" \
	PLATFORM_RISCV_ISA="${riscv_isa}" \
	FW_DYNAMIC=n \
	FW_JUMP=y \
	FW_PAYLOAD=n \
	FW_TEXT_START=0x40f80000 \
	FW_JUMP_ADDR=0x40000000 \
	FW_JUMP_FDT_OFFSET= \
	"$@"
