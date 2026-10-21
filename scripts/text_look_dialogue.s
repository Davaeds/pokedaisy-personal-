@ TEXT LOOK (companion/data/TextLook.kt): FireRed's dialogue box frame drawn in Emerald. Emerald's own
@ WindowFunc_DrawDialogueFrame draws every middle row of the box with the same tiles, while FireRed's box is a pill
@ whose round ends change row by row - so this replaces Emerald's (both the standard one and the one taking its tile
@ and palette from sTileNum / sPaletteNum) through the pointers the game passes to CallWindowFunction.
@
@ Called as WindowFunc(bg, left, top, width, height, paletteNum), like Emerald's. Draws FireRed's layout
@ (pokefirered's WindowFunc_DrawDialogueFrame) one column narrower than the window - Emerald's window is a tile wider than
@ FireRed's and its last column is drawn over by the window itself - with any height: the top row, the first middle
@ row, the second kind of middle row for the top half, then the same flipped, the bottom row flipped. Tiles are slots of
@ the box as TextLook packs it: top row 0-4, middle rows 5-8 and 9-12 (left outer, left inner, right inner, right outer).
@
@ The three words at the end are filled in per ROM: FillBgTilemapBufferRect, and where the tile base (u16) and the
@ palette (u8) are read from (Emerald's sTileNum / sPaletteNum, or two constants in the ROM).
@
@ Assemble: arm-none-eabi-as -mcpu=arm7tdmi -mthumb text_look_dialogue.s -o t.o && arm-none-eabi-objcopy -O binary t.o t.bin
@ (the bytes are TextLook.DIALOGUE_CODE).

    .syntax unified
    .thumb
    .text

@ One cell: FillBgTilemapBufferRect(bg, base + slot + flip, x, y, w, 1, palette).
@ xmode 0: x = left + dx; 1: x = left + W + dx (W = width - 1). wide: w = W, else 1.
    .macro CELL slot, xmode, dx, wide=0
    .if \wide
    str r6, [sp]
    .else
    movs r0, #1
    str r0, [sp]
    .endif
    mov r1, r9
    adds r1, #\slot
    ldr r0, [sp, #16]
    adds r1, r0
    .if \xmode == 0
    .if \dx < 0
    subs r2, r4, #-(\dx)
    .else
    adds r2, r4, #\dx
    .endif
    .else
    adds r2, r4, r6
    .if \dx > 0
    adds r2, #\dx
    .endif
    .endif
    adds r3, r7, #0
    mov r0, r10
    mov r12, r0
    mov r0, r8
    bl call_ip
    .endm

    .global LookDialogueFrame
LookDialogueFrame:
    push {r4-r7, lr}
    mov r4, r8
    mov r5, r9
    mov r6, r10
    mov r7, r11
    push {r4-r7}
    sub sp, #24                 @ [0] w, [4] h = 1, [8] palette, [12] row, [16] flip
    mov r8, r0                  @ bg
    adds r4, r1, #0             @ left
    adds r5, r2, #0             @ top
    subs r6, r3, #1             @ W
    ldr r0, [sp, #60]           @ height (the 5th argument, past 36 bytes pushed and the 24 above)
    mov r11, r0
    ldr r0, p_base
    ldrh r0, [r0]
    mov r9, r0                  @ tile base
    ldr r0, p_pal
    ldrb r0, [r0]
    str r0, [sp, #8]
    ldr r0, p_fn
    mov r10, r0
    movs r0, #1
    str r0, [sp, #4]

    @ The top row.
    movs r0, #0
    str r0, [sp, #16]
    subs r7, r5, #1
    CELL 0, 0, -2
    CELL 1, 0, -1
    CELL 2, 0, 0, 1
    CELL 3, 1, 0
    CELL 4, 1, 1

    @ The bottom row: the top one flipped.
    movs r0, #1
    lsls r0, r0, #11
    str r0, [sp, #16]
    adds r7, r5, #0
    add r7, r11
    CELL 0, 0, -2
    CELL 1, 0, -1
    CELL 2, 0, 0, 1
    CELL 3, 1, 0
    CELL 4, 1, 1

    @ The middle rows.
    movs r0, #0
    str r0, [sp, #12]
row:
    ldr r0, [sp, #12]
    mov r1, r11
    cmp r0, r1
    blt more
    b done
more:
    adds r7, r5, r0             @ y = top + row
    movs r2, #0                 @ flip
    cmp r0, #0
    beq first
    subs r1, #1
    cmp r0, r1
    beq last
    lsls r1, r0, #1
    mov r3, r11
    cmp r1, r3
    blt inner
    movs r2, #1
    lsls r2, r2, #11
inner:
    str r2, [sp, #16]
    CELL 9, 0, -2
    CELL 10, 0, -1
    CELL 11, 1, 0
    CELL 12, 1, 1
    b next
last:
    movs r2, #1
    lsls r2, r2, #11
first:
    str r2, [sp, #16]
    CELL 5, 0, -2
    CELL 6, 0, -1
    CELL 7, 1, 0
    CELL 8, 1, 1
next:
    ldr r0, [sp, #12]
    adds r0, #1
    str r0, [sp, #12]
    b row
done:
    add sp, #24
    pop {r4-r7}
    mov r8, r4
    mov r9, r5
    mov r10, r6
    mov r11, r7
    pop {r4-r7}
    pop {r0}
    bx r0

call_ip:
    bx ip

    .align 2
p_fn:
    .word 0xF00DF00D
p_base:
    .word 0xBA5EBA5E
p_pal:
    .word 0x9A1E9A1E
