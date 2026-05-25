//cái này phải dùng tạm trợ lý AI để kiếm tài liệu và ghép chúng lại :))))). Nguyên đoạn này tôi phải đấu trí với AI và tệp PDF
#define __SFR_OFFSET 0
#include <avr/io.h>

.global TIMER0_OVF_vect

.extern current_proc
.extern schedule_preemptive

; ============================================================================
; TRẠM TRUNG CHUYỂN NGẮT CỨNG PREEMPTIVE (LƯU TOÀN BỘ 32 THANH GHI)
; ============================================================================
TIMER0_OVF_vect:
    ; Khi nhảy vào đây, phần cứng đã tự động PUSH PC (2 bytes)
    
    ; --- BƯỚC 1: LƯU TOÀN BỘ NGỮ CẢNH (R0-R31 + SREG) ---
    PUSH    r0
    IN      r0, _SFR_IO_ADDR(SREG)
    PUSH    r0
    
    PUSH    r1
    PUSH    r2
    PUSH    r3
    PUSH    r4
    PUSH    r5
    PUSH    r6
    PUSH    r7
    PUSH    r8
    PUSH    r9
    PUSH    r10
    PUSH    r11
    PUSH    r12
    PUSH    r13
    PUSH    r14
    PUSH    r15
    PUSH    r16
    PUSH    r17
    PUSH    r18
    PUSH    r19
    PUSH    r20
    PUSH    r21
    PUSH    r22
    PUSH    r23
    PUSH    r24
    PUSH    r25
    PUSH    r26
    PUSH    r27
    PUSH    r28
    PUSH    r29
    PUSH    r30
    PUSH    r31

    ; --- BƯỚC 2: CẤT CON TRỎ STACK VÀO PCB HIỆN TẠI ---
    IN      r24, _SFR_IO_ADDR(SPL)
    IN      r25, _SFR_IO_ADDR(SPH)
    
    LDS     r30, current_proc
    LDS     r31, current_proc + 1
    
    STD     Z+0, r24
    STD     Z+1, r25

    ; --- BƯỚC 3: GỌI BỘ LẬP LỊCH TÌM TIẾN TRÌNH TIẾP THEO ---
    CALL    schedule_preemptive

    ; --- BƯỚC 4: TRÁO CON TRỎ STACK SANG TIẾN TRÌNH MỚI ---
    LDS     r30, current_proc
    LDS     r31, current_proc + 1
    
    LDD     r24, Z+0
    LDD     r25, Z+1
    
    OUT     _SFR_IO_ADDR(SPL), r24
    OUT     _SFR_IO_ADDR(SPH), r25

    ; --- BƯỚC 5: KHÔI PHỤC NGỮ CẢNH CỦA TIẾN TRÌNH MỚI ---
    POP     r31
    POP     r30
    POP     r29
    POP     r28
    POP     r27
    POP     r26
    POP     r25
    POP     r24
    POP     r23
    POP     r22
    POP     r21
    POP     r20
    POP     r19
    POP     r18
    POP     r17
    POP     r16
    POP     r15
    POP     r14
    POP     r13
    POP     r12
    POP     r11
    POP     r10
    POP     r9
    POP     r8
    POP     r7
    POP     r6
    POP     r5
    POP     r4
    POP     r3
    POP     r2
    POP     r1
    
    POP     r0
    OUT     _SFR_IO_ADDR(SREG), r0
    POP     r0

    RETI