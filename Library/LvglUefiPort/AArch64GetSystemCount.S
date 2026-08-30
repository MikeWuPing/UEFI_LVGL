#------------------------------------------------------------------------------
#
# AArch64GetSystemCount() - read CNTPCT_EL0 (system counter).
#
# Build 133: GAS twin of AArch64GetSystemCount.asm - the CLANGPDB (GCC
# family) toolchain builds .S sources; MSFT-only .asm left the symbol
# undefined when the app was linked with -t CLANGPDB. UEFI applications
# run at EL1: EL1 reads of CNTPCT_EL0 are not gated by
# CNTKCTL_EL1.EL0PCTEN, so this cannot trap.
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
#------------------------------------------------------------------------------

.text
.p2align 2
GCC_ASM_EXPORT(AArch64GetSystemCount)

#UINT64
#EFIAPI
#AArch64GetSystemCount (
#  VOID
#  );
#
ASM_PFX(AArch64GetSystemCount):
  AARCH64_BTI(c)
  mrs   x0, cntpct_el0
  ret
