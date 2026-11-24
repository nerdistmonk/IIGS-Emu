; Keystroke skip test. Polls $C000 for bit 7, stores each key, then strobes
; $C010 once (normal guest behavior). Results: $1000-$103D, one byte per key,
; in arrival order. Paste 62 chars; any missing byte in the results is a skip.
; $1000 is used because $0400-$07FF is text page 1 and gets overwritten.
        .cpu "65816"
        * = $0300
start:  php
        sei
        clc
        xce
        rep #$30
        .al
        .xl
        ldx #$0000
        sep #$20
        .as
loop:   lda $C000
        bpl loop
        and #$7f
        sta $1000,x
        lda $C010
        inx
        cpx #$003e
        bne loop
        sec
        xce
        plp
        jmp $ff69
