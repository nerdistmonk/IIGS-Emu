; GetScrap shim, assembled at $FB0000 in the host-only RAM bank (src/clipboard.c).
; A JSL $E10000 with X=$0D16 and text scrap type is redirected here.  On entry
; the stack holds the caller's return address (3 bytes) and the GetScrap
; parameters (scrapType, destHandle).  The shim saves them, runs ZeroScrap and
; PutScrap on the host text staged at $FB1000, then runs the real GetScrap with
; the caller's parameters and returns with the stack as the caller expects.
; Assemble with: 64tass -a -o clipboard_shim.bin clipboard_shim.asm
        .cpu "65816"
RET     = $fb0100
ARG     = $fb0104
PSAVE   = $fb010c
LEN     = $fb0110

        * = $fb0000
        php
        sep #$20
        .as
        pla
        sta PSAVE
        pla
        sta RET
        pla
        sta RET+1
        pla
        sta RET+2
        pla
        sta ARG
        pla
        sta ARG+1
        pla
        sta ARG+2
        pla
        sta ARG+3
        pla
        sta ARG+4
        pla
        sta ARG+5

        rep #$10
        .xl
        ldx #$0816
        jsl $e10000

        pea $0000
        rep #$20
        .al
        lda LEN
        pha
        sep #$20
        .as
        pea $0000
        pea $00fb
        pea $1000
        rep #$10
        .xl
        ldx #$0c16
        jsl $e10000

        sep #$20
        .as
        lda ARG+5
        pha
        lda ARG+4
        pha
        lda ARG+3
        pha
        lda ARG+2
        pha
        lda ARG+1
        pha
        lda ARG
        pha
        rep #$10
        .xl
        ldx #$0d16
        jsl $e10000

        sep #$20
        .as
        lda RET+2
        pha
        lda RET+1
        pha
        lda RET
        pha
        lda PSAVE
        pha
        plp
        rtl
