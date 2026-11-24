; Scrap Manager ZeroScrap isolation, fixed stack handling (single pop after GetNewID).
; Each call stores P (bit 0 = carry = tool error) and A (error code when carry is set):
;   $0A30/$0A32  ScrapStartUp
;   $0A34/$0A36  ZeroScrap on an empty scrap
;   $0A38/$0A3A  PutScrap (16 bytes, type 5)
;   $0A3C/$0A3E  ZeroScrap after PutScrap
;   $0A40/$0A42  GetScrapSize (type 5) after the second ZeroScrap (LongInt, low word first)
;   $0A90        userID from GetNewID
;   $0A84/$0A86  SP at entry / exit (must match)
        .cpu "65816"
        * = $0300

save    .macro pa, aa
        sta \aa
        php
        sep #$20
        .as
        pla
        rep #$30
        .al
        .xl
        and #$00ff
        sta \pa
        .endm

start:  php
        sei
        clc
        xce
        rep #$30
        .al
        .xl
        tsc
        sta $0A84
        ldx #$0201          ; TLStartUp
        jsl $E10000
        ldx #$0203          ; MTStartUp
        jsl $E10000
        pea $0000           ; GetNewID result (word)
        pea $1000           ; idTag: type 1 (application), auxID 0
        ldx #$2003          ; GetNewID
        jsl $E10000
        pla                 ; the one result word
        sta $0A90

        ldx #$0216          ; ScrapStartUp (no parameters)
        jsl $E10000
        save $0A30, $0A32

        ldx #$0B16          ; ZeroScrap on an empty scrap
        jsl $E10000
        save $0A34, $0A36

        pea $0000           ; PutScrap numBytes hi
        pea $0010           ; numBytes lo (16)
        pea $0005           ; scrapType 5
        pea $0000           ; srcPtr hi
        pea $0300           ; srcPtr lo
        ldx #$0C16          ; PutScrap
        jsl $E10000
        save $0A38, $0A3A

        ldx #$0B16          ; ZeroScrap after PutScrap
        jsl $E10000
        save $0A3C, $0A3E

        pea $0000           ; GetScrapSize result (LongInt)
        pea $0000
        pea $0005           ; scrapType 5
        ldx #$0F16          ; GetScrapSize
        jsl $E10000
        pla
        sta $0A40
        pla
        sta $0A42

        rep #$20
        .al
        tsc
        sta $0A86
        sep #$20
        .as
        sec
        xce
        plp
        jmp $ff69
