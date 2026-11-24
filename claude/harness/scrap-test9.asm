; Host clipboard accumulation check. PutScrap "AAAA", ZeroScrap, PutScrap "BBBB".
; After a correct ZeroScrap the host clipboard reads "BBBB"; without it, "AAAABBBB".
; Guest text is set from the monitor: $00:6000 = C1 C1 C1 C1 (AAAA), $00:6008 = C2 C2 C2 C2 (BBBB).
; Data avoids $0400-$07FF, which is the text page and gets overwritten by screen output.
        .cpu "65816"
        * = $0300
start:  php
        sei
        clc
        xce
        rep #$30
        .al
        .xl
        ldx #$0201          ; TLStartUp
        jsl $E10000
        ldx #$0203          ; MTStartUp
        jsl $E10000
        pea $0000           ; GetNewID result
        pea $1000           ; idTag
        ldx #$2003          ; GetNewID
        jsl $E10000
        pla
        ldx #$0216          ; ScrapStartUp
        jsl $E10000

        pea $0000           ; PutScrap numBytes hi
        pea $0004           ; numBytes lo (4)
        pea $0000           ; scrapType text
        pea $0000           ; srcPtr bank
        pea $6000           ; srcPtr address
        ldx #$0C16          ; PutScrap "AAAA"
        jsl $E10000

        ldx #$0B16          ; ZeroScrap
        jsl $E10000

        pea $0000           ; PutScrap numBytes hi
        pea $0004           ; numBytes lo (4)
        pea $0000           ; scrapType text
        pea $0000           ; srcPtr bank
        pea $6008           ; srcPtr address
        ldx #$0C16          ; PutScrap "BBBB"
        jsl $E10000

        sec
        xce
        plp
        jmp $ff69
