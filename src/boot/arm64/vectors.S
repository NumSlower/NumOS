.section .text.boot, "ax"

.global arm64_exception_vectors
.extern arm64_handle_exception

.set FRAME_SIZE, 288
.set OFF_X30, 240
.set OFF_SP_EL0, 248
.set OFF_ELR_EL1, 256
.set OFF_SPSR_EL1, 264
.set OFF_ESR_EL1, 272
.set OFF_FAR_EL1, 280

.macro vector_slot target
    b \target
    .balign 0x80
.endm

.macro save_frame
    sub sp, sp, #FRAME_SIZE
    stp x0,  x1,  [sp, #0]
    stp x2,  x3,  [sp, #16]
    stp x4,  x5,  [sp, #32]
    stp x6,  x7,  [sp, #48]
    stp x8,  x9,  [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #OFF_X30]
    mrs x0, sp_el0
    str x0, [sp, #OFF_SP_EL0]
    mrs x0, elr_el1
    str x0, [sp, #OFF_ELR_EL1]
    mrs x0, spsr_el1
    str x0, [sp, #OFF_SPSR_EL1]
    mrs x0, esr_el1
    str x0, [sp, #OFF_ESR_EL1]
    mrs x0, far_el1
    str x0, [sp, #OFF_FAR_EL1]
.endm

.macro restore_frame
    ldr x0, [sp, #OFF_ELR_EL1]
    msr elr_el1, x0
    ldr x0, [sp, #OFF_SPSR_EL1]
    msr spsr_el1, x0
    ldr x30, [sp, #OFF_X30]
    ldp x0,  x1,  [sp, #0]
    ldp x2,  x3,  [sp, #16]
    ldp x4,  x5,  [sp, #32]
    ldp x6,  x7,  [sp, #48]
    ldp x8,  x9,  [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    add sp, sp, #FRAME_SIZE
.endm

.balign 2048
arm64_exception_vectors:
    vector_slot arm64_vector_sync
    vector_slot arm64_vector_hang
    vector_slot arm64_vector_hang
    vector_slot arm64_vector_hang
    vector_slot arm64_vector_sync
    vector_slot arm64_vector_hang
    vector_slot arm64_vector_hang
    vector_slot arm64_vector_hang
    vector_slot arm64_vector_sync
    vector_slot arm64_vector_hang
    vector_slot arm64_vector_hang
    vector_slot arm64_vector_hang
    vector_slot arm64_vector_sync
    vector_slot arm64_vector_hang
    vector_slot arm64_vector_hang
    vector_slot arm64_vector_hang

arm64_vector_sync:
    save_frame
    mov x0, sp
    bl arm64_handle_exception
    restore_frame
    eret

arm64_vector_hang:
    wfe
    b arm64_vector_hang
