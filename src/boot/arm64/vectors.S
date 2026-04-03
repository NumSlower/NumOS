.section .text.boot, "ax"

.global arm64_exception_vectors

.macro vector_slot
    b arm64_vector_hang
    .balign 0x80
.endm

arm64_exception_vectors:
    vector_slot
    vector_slot
    vector_slot
    vector_slot
    vector_slot
    vector_slot
    vector_slot
    vector_slot
    vector_slot
    vector_slot
    vector_slot
    vector_slot
    vector_slot
    vector_slot
    vector_slot
    vector_slot

arm64_vector_hang:
    wfe
    b arm64_vector_hang
