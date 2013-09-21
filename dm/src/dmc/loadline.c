// Copyright (C) 1985-1994 by Symantec
// Copyright (C) 2000-2009 by Digital Mars
// All Rights Reserved
// http://www.digitalmars.com
// Written by Walter Bright
/*
 * This source file is made available for personal use
 * only. The license is in /dmd/src/dmd/backendlicense.txt
 * or /dm/src/dmd/backendlicense.txt
 * For any other uses, please contact Digital Mars.
 */

#if __INTSIZE == 2 && !(__SMALL__ || __COMPACT__)
// Put us in the same segment as token.c so we can use near calls
#pragma cseg TOKEN_TEXT
#endif

#include        <stdio.h>
#include        <string.h>
#include        <stdlib.h>
#include        <ctype.h>
#include        "cc.h"
#include        "token.h"
#include        "global.h"
#include        "parser.h"

extern char switch_E;

#if _WIN32

#if __INTSIZE == 2

/****************************************************
 * Read line from stream until we encounter an error.
 * Requires:
 *      S or M model, and BIGBUF
 * Returns:
 *      c
 */

int loadline (FILE *fp,unsigned char **pp,unsigned char *ptop)
{
    _asm
    {
        push    DS

        lds     BX,pp                   ;BX = &p
        mov     DX,01A0Ah               ;DH = ^Z, DL = '\n'
        les     DI,[BX]                 ;ES:DI = p
        mov     CX,word ptr ptop        ;CX = ptop
        sub     CX,DI                   ;CX = # of chars we can still put
        inc     CX                      ; in buffer
        lds     BX,fp                   ;BX = fp
        mov     AX,_cnt[BX]
        lds     SI,_ptr[BX]
        mov     BX,AX
        jmp     short G1

        even
L3:     stosb                           ;store char from I/O buffer into *p
G1:     dec     BX
        js      G2                      ;if out of chars in buffer
G4:     lodsb                           ;get char from buffer (DS:SI)
        cmp     AL,DH
        je      G3                      ;^Z marks end of file
        cmp     AL,DL                   ;is AL a newline?
        loopne  L3
L4:     xor     AH,AH

L1:     mov     CX,BX
        lds     BX,fp
G5:     mov     _cnt[BX],CX
        mov     word ptr _ptr[BX],SI    ;updated I/O buffer pointer
        lds     BX,pp
        mov     [BX],DI                 ;updated p
        pop     DS
    }
    return _AX;

    _asm
    {

G3:     lds     BX,fp
        xor     CX,CX                           ;0 chars left in buffer
        mov     AX,EOF
        or      byte ptr _flag[BX],_IOEOF       ;set EOF flag bit
        jmp     G5

G2:     pop     DS
        push    DS                      ;restore DS to original value
        push    CX
        push    ES                      ;save
    }
    _fillbuf(fp);                       // fill buffer
    _asm
    {
        mov     DX,01A0Ah               ;DH = ^Z, DL = '\n'
        lds     BX,fp
        mov     CX,_cnt[BX]
        lds     SI,_ptr[BX]
        mov     BX,CX
        pop     ES
        pop     CX
        test    AX,AX                   ;EOF?
        jnz     L1                      ;yes
        dec     BX
        jmp     G4
    }
}

/*************************************************
 * Read in identifier and store it in tok_ident[].
 * Input:
 *      xc =    first char of identifier
 * Output:
 *      xc =    first char past identifier
 *      tok_ident[]     holds the identifier
 */

__declspec(naked) void __near inident()
{
    _asm
    {
        push    DI
        mov     DI,offset tok_ident             ;p = tok_ident
        mov     AX,xc                           ;AH is set to 0
        mov     [DI],AL
        inc     DI

        shl     AX,10
        mov     idhash,AX                       ;idhash = xc << 10;

        even
L26:
        call    egchar
        mov     BX,AX
        test    byte ptr _chartype[BX + 1],3    ;if !isidchar(xc)
        jz      L65
L3A:
        cmp     DI,offset tok_ident + IDMAX
        jae     L4E
        mov     [DI],AL
        inc     DI                      ;*p++ = xc

        call    egchar
        mov     BX,AX
        test    byte ptr _chartype[BX + 1],3    ;if !ididchar(xc)
        jz      L65
        cmp     DI,offset tok_ident + IDMAX
        jae     L4E
        mov     [DI],AL
        inc     DI                      ;*p++ = xc
        jmp     L26
    }

L4E:
    lexerr(EM_ident2big);               // identifier is too long
    goto L26;

    _asm
    {
L65:    mov     [DI],AH                 ;AH is 0 (terminate string)

;  idhash += ((p - tok_ident) << 6) + (*(p - 1) & 0x3F);
        mov     AL,-1[DI]
        sub     DI,offset tok_ident
        and     AL,03Fh
        shl     DI,6
        add     AX,DI
        add     idhash,AX
        pop     DI
        ret
    }
}

#else

/****************************************************
 * Read line from stream until we encounter an error.
 * Requires:
 *      S or M model, and BIGBUF
 * Returns:
 *      c
 */

int loadline (FILE *fp,unsigned char **pp,unsigned char *ptop)
{
    _asm
    {
        mov     EDX,01A0Ah              ;DH = ^Z, DL = '\n'
        mov     EBX,pp                  ;EBX = &p
        mov     ECX,ptop                ;ECX = ptop
        mov     EDI,[EBX]               ;ES:EDI = p
        mov     EBX,fp                  ;EBX = fp
        sub     ECX,EDI                 ;ECX = # of chars we can still put
        mov     ESI,_ptr[EBX]           ;load offset of disk buffer
        inc     ECX                     ; in buffer
        mov     EBX,_cnt[EBX]
        xor     EAX,EAX                 ;clear high 3 bytes
        dec     EBX
        js      short G2                ;if out of chars in input buffer
        mov     AL,[ESI]                ;get char from buffer (DS:ESI)
        inc     ESI
        cmp     AL,DL
        je      G6                      ;is AL a newline?
        cmp     AL,DH                   ;^Z marks end of file
        je      G3                      ;^Z marks end of file
        dec     ECX
        jz      short G6

        even
L3:     mov     [EDI],AL                ;store char from input buffer into *p
        inc     EDI
G1:     dec     EBX
        js      short G2                ;if out of chars in buffer
        mov     AL,[ESI]                ;get char from buffer (DS:ESI)
        inc     ESI
        cmp     AL,DH
        je      G3                      ;^Z marks end of file
        cmp     AL,DL                   ;is AL a newline?
        loopne  L3
G6:

L1:     mov     ECX,EBX
        mov     EBX,fp
G5:     mov     _cnt[EBX],ECX
        mov     _ptr[EBX],ESI           ;updated input buffer pointer
        mov     EBX,pp                  ;EBX = &p
        pop     ESI
        mov     [EBX],EDI               ;updated p
    }
    return _EAX;

    _asm
    {
G3:     mov     EBX,fp
        or      byte ptr _flag[EBX],_IOEOF       ;set EOF flag bit
        xor     ECX,ECX                          ;0 chars left in buffer
        mov     EAX,EOF
        jmp     G5

G2:     mov     EBX,fp
        push    ECX
        push    EBX                     ;fp
        call    _fillbuf                ;fill buffer (_fillbuf(fp))
        pop     ESI                     ;clean up the stack.
        pop     ECX
        mov     ESI,_ptr[EBX]           ;load offset of disk buffer
        mov     EBX,_cnt[EBX]
        mov     EDX,01A0Ah              ;DH = ^Z, DL = '\n'
        test    EAX,EAX                 ;EOF?
        jz      G1                      ;no
        jmp     L1
    }
}

__declspec(naked) void __near inident()
{
    _asm
    {   push    EBX
        mov     EAX,xc                          ;AH is set to 0

        push    EDI
        mov     EBX,offset tok_ident+IDMAX      ;EBX = &tok_ident[IDMAX]

        mov     tok_ident,AL
        mov     EDI,offset tok_ident+1          ;p = tok_ident

        mov     EDX,0x83
        mov     ECX,btextp

        cmp     switch_E,AH
        jne     L2

        even
L1:
#if 1
        ;Relies on IDMAX being even
        mov     AL,[ECX]
        inc     ECX
        mov     [EDI],AL                        ;*p++ = xc
        inc     EDI
        test    byte ptr _chartype[EAX + 1],DL  ;if !isidchar(xc)
        jle     L1A

        mov     AL,[ECX]
        inc     ECX
        mov     [EDI],AL                        ;*p++ = xc
        inc     EDI
        test    byte ptr _chartype[EAX + 1],DL  ;if !isidchar(xc)
        jle     L1A

        cmp     EDI,EBX
        jb      L1
        jmp     Lerr
#else
        mov     AL,[ECX]
        inc     ECX
        mov     [EDI],AL                        ;*p++ = xc
        inc     EDI
        test    byte ptr _chartype[EAX + 1],DL  ;if !isidchar(xc)
        jle     L1A
        cmp     EDI,EBX
        jbe     L1
        jmp     Lerr
#endif

L1A:    lea     EDI,-1[EDI]
        js      L8                              ;0 or 0xFF
        cmp     AL,'\\'
        je      Lspecial
L10:    mov     byte ptr xc,AL
        mov     [EDI],AH                        ;AH is 0 (terminate string)
        mov     btextp,ECX

        ;  idhash = (xc<<16) + ((p - tok_ident) << 8) + p[-1]
        mov     AL,tok_ident
        shl     EAX,16
        mov     AL,-1[EDI]
        sub     EDI,offset tok_ident
        shl     EDI,8
        add     EAX,EDI
        pop     EDI
        pop     EBX
        mov     idhash,EAX
        ret

        even
L2:     mov     AL,[ECX]
        inc     ECX
        test    byte ptr _chartype[EAX + 1],0x83        ;if !isidchar(xc)
        jle     L4
        cmp     switch_E,AH
        jne     L6
L3:     cmp     EDI,EBX
        jae     Lerr
        mov     [EDI],AL                        ;*p++ = xc
        inc     EDI
        jmp     L2

L4:     js      L8                              ;0 or 0xFF?
        cmp     switch_E,AH
        je      L9
L6:     push    EAX
        push    ECX
        push    EAX
        call    explist
        pop     ECX
        pop     EAX
        jmp     L9

L8:     dec     ECX
        mov     btextp,ECX
        call    egchar2
        mov     ECX,btextp

L9:     cmp     AL,'\\'
        je      Lspecial
        test    byte ptr _chartype[EAX + 1],3   ;if !isidchar(xc)
        jz      L10
        jmp     L3

Lerr:   push    ECX
        push    EM_ident2big
        call    lexerr                          ; identifier is too long
        add     ESP,4
        pop     ECX
        xor     EAX,EAX
        sub     EDI,IDMAX - 3
        jmp     L2

Lspecial:
        mov     byte ptr xc,AL
        mov     btextp,ECX
        push    EDI
        call    inidentX
        pop     EDI
        pop     EBX
        ret
    }
}

#endif
#endif
