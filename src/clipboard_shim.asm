; GetScrap shim, assembled at $FB0000 in the host-only RAM bank (src/clipboard.c).
; A JSL $E10000 for GetScrap, GetScrapHandle, or GetScrapSize with text scrap
; type is redirected here, and TOOL holds the tool number.  On entry the stack
; holds the caller's return address (3 bytes) and the six parameter bytes
; (result space and scrapType).  The shim saves them, runs ZeroScrap and
; PutScrap on the host text staged at $FB1000, then runs the real tool with
; the caller's parameters and returns with the stack as the caller expects.
; Assemble with: 64tass -a -o clipboard_shim.bin clipboard_shim.asm
        .cpu "65816"
RET     = $fb0100
ARG     = $fb0104
PSAVE   = $fb010c
LEN     = $fb0110
TOOL    = $fb0114

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
        ldx #$0b16
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
        rep #$30
        .al
        .xl
        lda TOOL
        tax
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
