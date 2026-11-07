; Defines the absolute symbol ts2_rva_0 = the game image base (kGameBase in src/game/game.h).
; GameAddress(rva) compiles to `ts2_rva_0 + rva`, a link-time constant. No game bytes involved.
.386
.model flat
PUBLIC _ts2_rva_0
_ts2_rva_0 EQU 0A0000000h
END
