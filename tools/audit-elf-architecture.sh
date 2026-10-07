#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <runtime-root> <x86_64|aarch64>" >&2
  exit 2
fi

runtime_root="$(realpath "$1")"
expected_architecture="$2"

case "${expected_architecture}" in
  x86_64)
    expected_machine='Advanced Micro Devices X86-64'
    ;;
  aarch64)
    expected_machine='AArch64'
    ;;
  *)
    echo "Unsupported ELF architecture: ${expected_architecture}" >&2
    exit 2
    ;;
esac

elf_count=0
while IFS= read -r -d '' entry; do
  if ! file --brief --dereference "${entry}" | grep -q '^ELF '; then
    continue
  fi

  machine="$(readelf --file-header "${entry}" |
    sed -n 's/^[[:space:]]*Machine:[[:space:]]*//p')"
  if [[ "${machine}" != "${expected_machine}" ]]; then
    echo "Unexpected ELF machine in ${entry}: ${machine}; expected ${expected_machine}" >&2
    exit 1
  fi
  ((elf_count += 1))
done < <(find -L "${runtime_root}" -type f -print0)

if [[ "${elf_count}" -eq 0 ]]; then
  echo "No ELF files found under ${runtime_root}" >&2
  exit 1
fi

echo "Audited ${elf_count} ${expected_architecture} ELF files under ${runtime_root}"
