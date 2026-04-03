.text
.global _start

.extern main
.extern __numos_user_runtime_init
.extern __numos_user_run_constructors
.extern __numos_user_prepare_args
.extern __numos_user_argc
.extern __numos_user_argv

_start:
    mov x29, xzr
    mov x30, xzr

    bl __numos_user_runtime_init
    bl __numos_user_run_constructors
    bl __numos_user_prepare_args

    adrp x9, __numos_user_argc
    add x9, x9, :lo12:__numos_user_argc
    ldr w0, [x9]

    adrp x9, __numos_user_argv
    add x9, x9, :lo12:__numos_user_argv
    ldr x1, [x9]

    bl main

    mov x8, #60
    svc #0

.Lhang:
    wfe
    b .Lhang
