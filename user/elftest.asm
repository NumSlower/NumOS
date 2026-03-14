; =============================================================================
; user/elftest.asm  -  NumOS Interactive Shell
;
; Commands:
;   help          print this command list
;   clear         clear the screen
;   echo <text>   print text to the console
;   uptime        show system uptime in seconds and ms
;   pid           show the shell's process ID
;   color         run a colour demo (if framebuffer is active)
;   exit          terminate the shell
; =============================================================================

bits 64
global _start

; ---------------------------------------------------------------------------
; Syscall numbers
; ---------------------------------------------------------------------------
SYS_READ        equ 0
SYS_WRITE       equ 1
SYS_INPUT       equ 207
SYS_EXIT        equ 60
SYS_GETPID      equ 39
SYS_UPTIME_MS   equ 96
SYS_FB_INFO     equ 201
SYS_FB_WRITE    equ 202
SYS_FB_SETCOLOR equ 204
SYS_FB_FILLRECT equ 206

; Standard file descriptors
FD_STDIN        equ 0
FD_STDOUT       equ 1

; Colours (0x00RRGGBB)
COL_WHITE       equ 0x00C8D7EB
COL_CYAN        equ 0x0032C8D2
COL_GREEN       equ 0x003CD264
COL_YELLOW      equ 0x00E6B432
COL_RED         equ 0x00E64646
COL_BLUE        equ 0x005096E6
COL_MAGENTA     equ 0x00C864C8
COL_DIM         equ 0x0050596E
COL_BG          equ 0x000E0E16
COL_TRANSPARENT equ 0xFFFFFFFF

; Input buffer size
IBUF_SIZE       equ 256

; ---------------------------------------------------------------------------
section .rodata
; ---------------------------------------------------------------------------

msg_banner:
    db  0x0A
    db  " +==============================================+", 0x0A
    db  " |   NumOS Shell  v1.0   (x86-64 Ring 3)       |", 0x0A
    db  " |   Type  help  for available commands.        |", 0x0A
    db  " +==============================================+", 0x0A, 0x0A, 0
msg_banner_len equ $ - msg_banner - 1

msg_prompt:     db  "[shell]> ", 0
msg_prompt_len  equ $ - msg_prompt - 1

msg_nl:         db  0x0A, 0
msg_nl_len      equ 1

msg_bs:         db  0x08, 0x20, 0x08, 0
msg_bs_len      equ 3

msg_unknown:
    db  " Unknown command.  Try  help", 0x0A, 0
msg_unknown_len equ $ - msg_unknown - 1

; help text
msg_help:
    db  0x0A
    db  "  Commands:", 0x0A
    db  "  ---------", 0x0A
    db  "  help          print this list", 0x0A
    db  "  clear         clear the screen", 0x0A
    db  "  echo <text>   print text", 0x0A
    db  "  uptime        show uptime in seconds", 0x0A
    db  "  pid           show shell process ID", 0x0A
    db  "  color         colour demo", 0x0A
    db  "  exit          exit shell", 0x0A, 0x0A, 0
msg_help_len equ $ - msg_help - 1

; uptime prefix
msg_uptime_pfx: db  "  Uptime: ", 0
msg_uptime_pfx_len equ $ - msg_uptime_pfx - 1
msg_uptime_s:   db  " s  (", 0
msg_uptime_ms:  db  " ms)", 0x0A, 0

; pid prefix
msg_pid_pfx:    db  "  PID: ", 0
msg_pid_pfx_len equ $ - msg_pid_pfx - 1

; color demo strings
msg_col_hdr:    db  0x0A, "  Colour demo:", 0x0A, 0
msg_col_r:      db  "    [RED]     ", 0
msg_col_g:      db  "    [GREEN]   ", 0
msg_col_y:      db  "    [YELLOW]  ", 0
msg_col_b:      db  "    [BLUE]    ", 0
msg_col_c:      db  "    [CYAN]    ", 0
msg_col_m:      db  "    [MAGENTA] ", 0
msg_col_w:      db  "    [WHITE]   ", 0x0A, 0
msg_col_done:   db  "  Colours reset.", 0x0A, 0

; exit message
msg_exit:       db  0x0A, "  Goodbye.", 0x0A, 0
msg_exit_len    equ $ - msg_exit - 1

; echo prefix (just a space)
msg_echo_pfx:   db  "  ", 0

; command name literals (for comparison, no null)
cmd_help:       db  "help"
cmd_clear:      db  "clear"
cmd_echo:       db  "echo"
cmd_uptime:     db  "uptime"
cmd_pid:        db  "pid"
cmd_color:      db  "color"
cmd_exit:       db  "exit"

; 50-newline clear sequence
msg_cls:        times 50 db 0x0A
msg_cls_len     equ 50

; ---------------------------------------------------------------------------
section .bss
; ---------------------------------------------------------------------------
ibuf:           resb IBUF_SIZE      ; line input buffer
numbuf:         resb 32             ; scratch for number-to-string conversion

; ---------------------------------------------------------------------------
section .text
; ---------------------------------------------------------------------------

; ---------------------------------------------------------------------------
; _start
; ---------------------------------------------------------------------------
_start:
    ; Print banner
    mov  rdi, msg_banner
    call puts

.prompt_loop:
    ; Print prompt in cyan
    call set_col_cyan
    mov  rdi, msg_prompt
    call puts
    call set_col_white

    ; Read one line into ibuf
    mov  rdi, ibuf
    call read_line            ; length returned in rbx

    ; Skip empty lines
    test rbx, rbx
    jz   .prompt_loop

    ; Dispatch
    mov  rdi, ibuf
    mov  rsi, rbx
    call dispatch

    jmp  .prompt_loop

; ---------------------------------------------------------------------------
; dispatch(rdi = buffer, rsi = length)
;   Tries each command name in sequence.
; ---------------------------------------------------------------------------
dispatch:
    push rbp
    mov  rbp, rsp

    ; help
    lea  rdx, [rel cmd_help]
    mov  rcx, 4
    call cmd_match
    jz   .do_help

    ; clear
    lea  rdx, [rel cmd_clear]
    mov  rcx, 5
    call cmd_match
    jz   .do_clear

    ; echo (prefix match - "echo " or just "echo")
    lea  rdx, [rel cmd_echo]
    mov  rcx, 4
    call cmd_prefix_match
    jz   .do_echo

    ; uptime
    lea  rdx, [rel cmd_uptime]
    mov  rcx, 6
    call cmd_match
    jz   .do_uptime

    ; pid
    lea  rdx, [rel cmd_pid]
    mov  rcx, 3
    call cmd_match
    jz   .do_pid

    ; color
    lea  rdx, [rel cmd_color]
    mov  rcx, 5
    call cmd_match
    jz   .do_color

    ; exit
    lea  rdx, [rel cmd_exit]
    mov  rcx, 4
    call cmd_match
    jz   .do_exit

    ; unknown
    call set_col_red
    mov  rax, SYS_WRITE
    mov  rdi, FD_STDOUT
    lea  rsi, [rel msg_unknown]
    mov  rdx, msg_unknown_len
    syscall
    call set_col_white
    jmp  .done

.do_help:
    call cmd_help_fn
    jmp  .done

.do_clear:
    call cmd_clear_fn
    jmp  .done

.do_echo:
    call cmd_echo_fn
    jmp  .done

.do_uptime:
    call cmd_uptime_fn
    jmp  .done

.do_pid:
    call cmd_pid_fn
    jmp  .done

.do_color:
    call cmd_color_fn
    jmp  .done

.do_exit:
    mov  rdi, msg_exit
    call puts
    mov  rax, SYS_EXIT
    xor  rdi, rdi
    syscall

.done:
    pop  rbp
    ret

; ---------------------------------------------------------------------------
; cmd_match(rdi=buf, rsi=buflen, rdx=cmdstr, rcx=cmdlen)
;   Sets ZF if the buffer equals the command exactly (case-sensitive).
;   Clobbers rax, r8, r9.
; ---------------------------------------------------------------------------
cmd_match:
    cmp  rsi, rcx
    jne  .no_match
    mov  r8, rdi
    mov  r9, rdx
    mov  rax, rcx
.loop:
    test rax, rax
    jz   .match
    movzx r10, byte [r8]
    movzx r11, byte [r9]
    cmp  r10, r11
    jne  .no_match
    inc  r8
    inc  r9
    dec  rax
    jmp  .loop
.match:
    xor  rax, rax   ; ZF = 1
    ret
.no_match:
    mov  rax, 1     ; ZF = 0
    test rax, rax
    ret

; ---------------------------------------------------------------------------
; cmd_prefix_match(rdi=buf, rsi=buflen, rdx=cmdstr, rcx=cmdlen)
;   Sets ZF if the buffer starts with the command (buf[0..cmdlen-1] matches).
;   The buffer may be longer (allows "echo hello" to match "echo").
; ---------------------------------------------------------------------------
cmd_prefix_match:
    cmp  rsi, rcx
    jb   .no_match
    mov  r8, rdi
    mov  r9, rdx
    mov  rax, rcx
.loop:
    test rax, rax
    jz   .match
    movzx r10, byte [r8]
    movzx r11, byte [r9]
    cmp  r10, r11
    jne  .no_match
    inc  r8
    inc  r9
    dec  rax
    jmp  .loop
.match:
    xor  rax, rax
    ret
.no_match:
    mov  rax, 1
    test rax, rax
    ret

; ---------------------------------------------------------------------------
; Command handlers
; ---------------------------------------------------------------------------

cmd_help_fn:
    call set_col_cyan
    mov  rdi, msg_help
    call puts
    call set_col_white
    ret

; ---- clear -----------------------------------------------------------------
cmd_clear_fn:
    mov  rax, SYS_WRITE
    mov  rdi, FD_STDOUT
    lea  rsi, [rel msg_cls]
    mov  rdx, msg_cls_len
    syscall
    ret

; ---- echo ------------------------------------------------------------------
; ibuf layout: "echo<space>rest..." or just "echo"
cmd_echo_fn:
    ; Print "  " prefix
    call set_col_green
    mov  rdi, msg_echo_pfx
    call puts

    ; Find start of argument: skip "echo" (4 bytes) and optional space
    lea  rsi, [rel ibuf]
    add  rsi, 4             ; skip "echo"
    movzx eax, byte [rsi]
    cmp  al, 0x20
    jne  .no_space
    inc  rsi                ; skip the space
.no_space:
    ; rsi now points at the argument (or NUL if none)
    mov  rdi, rsi
    call puts
    ; newline
    mov  rax, SYS_WRITE
    mov  rdi, FD_STDOUT
    lea  rsi, [rel msg_nl]
    mov  rdx, msg_nl_len
    syscall
    call set_col_white
    ret

; ---- uptime ----------------------------------------------------------------
cmd_uptime_fn:
    call set_col_yellow
    ; Print "  Uptime: "
    mov  rax, SYS_WRITE
    mov  rdi, FD_STDOUT
    lea  rsi, [rel msg_uptime_pfx]
    mov  rdx, msg_uptime_pfx_len
    syscall

    ; Get uptime in ms
    mov  rax, SYS_UPTIME_MS
    syscall
    mov  r12, rax           ; save ms

    ; Convert ms -> seconds
    mov  rax, r12
    xor  rdx, rdx
    mov  rcx, 1000
    div  rcx                ; rax = seconds, rdx = remainder ms

    ; Print seconds
    mov  rdi, rax
    call print_uint

    ; Print " s  ("
    mov  rdi, msg_uptime_s
    call puts

    ; Print raw ms
    mov  rdi, r12
    call print_uint

    ; Print " ms)\n"
    mov  rdi, msg_uptime_ms
    call puts

    call set_col_white
    ret

; ---- pid -------------------------------------------------------------------
cmd_pid_fn:
    call set_col_magenta
    mov  rax, SYS_WRITE
    mov  rdi, FD_STDOUT
    lea  rsi, [rel msg_pid_pfx]
    mov  rdx, msg_pid_pfx_len
    syscall

    mov  rax, SYS_GETPID
    syscall
    mov  rdi, rax
    call print_uint

    mov  rdi, msg_nl
    call puts
    call set_col_white
    ret

; ---- color -----------------------------------------------------------------
cmd_color_fn:
    ; Check if framebuffer is available (fb_info field 3)
    mov  rax, SYS_FB_INFO
    mov  rdi, 3
    syscall
    test rax, rax
    jnz  .fb_active

    ; Fallback: VGA colour codes via escape-like writes
    mov  rdi, msg_col_hdr
    call puts

    call set_col_red
    mov  rdi, msg_col_r
    call puts
    call set_col_green
    mov  rdi, msg_col_g
    call puts
    call set_col_yellow
    mov  rdi, msg_col_y
    call puts
    call set_col_blue
    mov  rdi, msg_col_b
    call puts
    call set_col_cyan
    mov  rdi, msg_col_c
    call puts
    call set_col_magenta
    mov  rdi, msg_col_m
    call puts
    call set_col_white
    mov  rdi, msg_col_w
    call puts
    call set_col_white
    ret

.fb_active:
    ; Framebuffer colour demo: draw a row of coloured rectangles
    mov  rdi, msg_col_hdr
    call puts

    ; 7 colour swatches, each 120x30 pixels, starting at (20, 100)
    ; fb_fillrect(x, y, w, h, color)
    %macro draw_swatch 3   ; x, width, colour
        mov  rax, SYS_FB_FILLRECT
        mov  rdi, %1        ; x
        mov  rsi, 100       ; y
        mov  rdx, %2        ; w
        mov  r10, 30        ; h
        mov  r8,  %3        ; colour
        syscall
    %endmacro

    draw_swatch  20,  100, COL_RED
    draw_swatch  130, 100, COL_GREEN
    draw_swatch  240, 100, COL_YELLOW
    draw_swatch  350, 100, COL_BLUE
    draw_swatch  460, 100, COL_CYAN
    draw_swatch  570, 100, COL_MAGENTA
    draw_swatch  680, 100, COL_WHITE

    mov  rdi, msg_col_done
    call puts
    call set_col_white
    ret

; ---------------------------------------------------------------------------
; read_line(rdi = buffer ptr)
;   Reads characters from stdin until newline or IBUF_SIZE-1 chars.
;   Echoes each character.  Handles backspace.
;   Returns length in rbx.  Buffer is NUL-terminated.
; ---------------------------------------------------------------------------
read_line:
    push rbp
    mov  rbp, rsp
    sub  rsp, 16            ; local: [rbp-8] = single char

    mov  r12, rdi           ; buffer pointer
    xor  rbx, rbx           ; count

.loop:
    ; read one byte
    mov  rax, SYS_INPUT
    lea  rdi, [rbp-8]        ; buf
    mov  rsi, 1              ; count
    syscall
    test rax, rax
    jle  .done              ; EOF or error

    movzx eax, byte [rbp-8]

    ; Enter / Return
    cmp  al, 0x0A
    je   .done
    cmp  al, 0x0D
    je   .done

    ; Backspace (0x08 or 0x7F)
    cmp  al, 0x08
    je   .backspace
    cmp  al, 0x7F
    je   .backspace

    ; Buffer full check
    cmp  rbx, IBUF_SIZE-2
    jge  .loop              ; discard when full

    ; Store and echo
    mov  [r12 + rbx], al
    inc  rbx

    mov  rax, SYS_WRITE
    mov  rdi, FD_STDOUT
    lea  rsi, [rbp-8]
    mov  rdx, 1
    syscall
    jmp  .loop

.backspace:
    test rbx, rbx
    jz   .loop
    dec  rbx
    mov  rax, SYS_WRITE
    mov  rdi, FD_STDOUT
    lea  rsi, [rel msg_bs]
    mov  rdx, msg_bs_len
    syscall
    jmp  .loop

.done:
    ; NUL-terminate
    mov  byte [r12 + rbx], 0

    ; Echo newline
    mov  rax, SYS_WRITE
    mov  rdi, FD_STDOUT
    lea  rsi, [rel msg_nl]
    mov  rdx, msg_nl_len
    syscall

    add  rsp, 16
    pop  rbp
    ret

; ---------------------------------------------------------------------------
; puts(rdi = NUL-terminated string)
;   Writes the string to stdout.  Clobbers rax, rsi, rdx, rcx.
; ---------------------------------------------------------------------------
puts:
    push rbx
    push rdi
    mov  rbx, rdi
    ; measure length
    xor  rcx, rcx
.len_loop:
    cmp  byte [rbx + rcx], 0
    je   .write
    inc  rcx
    jmp  .len_loop
.write:
    test rcx, rcx
    jz   .skip
    mov  rax, SYS_WRITE
    mov  rdi, FD_STDOUT
    mov  rsi, rbx
    mov  rdx, rcx
    syscall
.skip:
    pop  rdi
    pop  rbx
    ret

; ---------------------------------------------------------------------------
; print_uint(rdi = uint64)
;   Converts rdi to decimal and writes it to stdout.
;   Uses numbuf in .bss.  Clobbers rax, rbx, rcx, rdx, r8.
; ---------------------------------------------------------------------------
print_uint:
    push rbp
    mov  rbp, rsp

    lea  rbx, [rel numbuf]
    add  rbx, 31
    mov  byte [rbx], 0
    dec  rbx
    mov  rax, rdi
    mov  r8, rbx            ; save end pointer

    test rax, rax
    jnz  .convert
    mov  byte [rbx], '0'
    jmp  .write

.convert:
    test rax, rax
    jz   .write
    xor  rdx, rdx
    mov  rcx, 10
    div  rcx                ; rax = quotient, rdx = remainder
    add  dl, '0'
    mov  [rbx], dl
    dec  rbx
    jmp  .convert

.write:
    inc  rbx                ; point to first digit
    ; calculate length
    mov  rcx, r8
    sub  rcx, rbx
    inc  rcx

    mov  rax, SYS_WRITE
    mov  rdi, FD_STDOUT
    mov  rsi, rbx
    mov  rdx, rcx
    syscall

    pop  rbp
    ret

; ---------------------------------------------------------------------------
; Colour helpers (framebuffer setcolor syscall)
;   fg in rdi, bg in rsi  (COL_TRANSPARENT for no background change)
; ---------------------------------------------------------------------------
%macro set_color_macro 2
    mov  rax, SYS_FB_SETCOLOR
    mov  rdi, %1
    mov  rsi, %2
    syscall
%endmacro

set_col_white:
    push rax
    push rdi
    push rsi
    set_color_macro COL_WHITE, COL_TRANSPARENT
    pop  rsi
    pop  rdi
    pop  rax
    ret

set_col_cyan:
    push rax
    push rdi
    push rsi
    set_color_macro COL_CYAN, COL_TRANSPARENT
    pop  rsi
    pop  rdi
    pop  rax
    ret

set_col_green:
    push rax
    push rdi
    push rsi
    set_color_macro COL_GREEN, COL_TRANSPARENT
    pop  rsi
    pop  rdi
    pop  rax
    ret

set_col_yellow:
    push rax
    push rdi
    push rsi
    set_color_macro COL_YELLOW, COL_TRANSPARENT
    pop  rsi
    pop  rdi
    pop  rax
    ret

set_col_red:
    push rax
    push rdi
    push rsi
    set_color_macro COL_RED, COL_TRANSPARENT
    pop  rsi
    pop  rdi
    pop  rax
    ret

set_col_blue:
    push rax
    push rdi
    push rsi
    set_color_macro COL_BLUE, COL_TRANSPARENT
    pop  rsi
    pop  rdi
    pop  rax
    ret

set_col_magenta:
    push rax
    push rdi
    push rsi
    set_color_macro COL_MAGENTA, COL_TRANSPARENT
    pop  rsi
    pop  rdi
    pop  rax
    ret
