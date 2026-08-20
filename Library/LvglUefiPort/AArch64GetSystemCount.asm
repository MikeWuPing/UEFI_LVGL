; AArch64GetSystemCount.asm - read CNTPCT_EL0 (system counter).
; UEFI applications run at EL1: EL1 reads of CNTPCT_EL0 are not gated by
; CNTKCTL_EL1.EL0PCTEN, so this cannot trap. Same Stall-calibrated tick
; pattern as the X64 TSC branch (TickTimer.c).
    EXPORT AArch64GetSystemCount
    AREA .text, CODE, READONLY
AArch64GetSystemCount
    MRS X0, CNTPCT_EL0
    RET
    END
