#ifndef F_CPU
#define F_CPU 16000000UL 
#endif

#include "avrkernel.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stddef.h>
#include <avr/pgmspace.h>

extern unsigned int __bss_end;
extern void *__brkval;

// ============= UART Register Compatibility =============
// ATmega328 uses different register names (with _0 suffix)
// ATmega32A uses simpler names without suffix
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328__)
    // ATmega328: Register names with _0 suffix
    #define UBRRH UBRR0H
    #define UBRRL UBRR0L
    #define UCSRA UCSR0A
    #define UCSRB UCSR0B
    #define UCSRC UCSR0C
    #define UDR   UDR0
    #define RXEN  RXEN0
    #define TXEN  TXEN0
    #define UDRE  UDRE0
    #define RXC   RXC0
    #define UCSZ1 UCSZ01
    #define UCSZ0 UCSZ00
#endif
// ======================================================

#define KERNEL_HEAP_SIZE 128
static uint8_t k_heap_pool[KERNEL_HEAP_SIZE];
static block_header_t *heap_start = NULL;

PCB_t pcb_table[MAX_PROCS];
PCB_t *current_proc = &pcb_table[0]; 

uint8_t proc_stacks[MAX_PROCS][PROC_STACK_SZ];

static pid_t next_pid = 1;
volatile uint32_t sleep_ticks = 0;
static uint8_t printed_once = 0;
volatile uint16_t k_intr0_counter = 0;
volatile uint16_t k_intr1_counter = 0;
__attribute__((naked, noreturn))
static void process_exit(void) {
    __asm__ __volatile__ (
        "cli\n\t"           
        "jmp process_exit\n\t"  
    );
    __builtin_unreachable();
}

__attribute__((optimize("O0")))
int k_sys_fork(void (*proc_code)(void)) {
    int i;
    PCB_t *p = 0;

    for (i = 0; i < MAX_PROCS; i++) {
        if (pcb_table[i].state == STATE_UNUSED) {
            p = &pcb_table[i];
            break;
        }
    }

    if (p == 0) return -1;
    p->state = STATE_EMBRYO;
    p->pid = next_pid++;
    uint8_t *stk = &proc_stacks[i][PROC_STACK_SZ - 1];
    uint16_t proc_addr = (uint16_t)proc_code;
    *stk-- = (uint8_t)(proc_addr & 0xFF);        
    *stk-- = (uint8_t)((proc_addr >> 8) & 0xFF);
    *stk-- = 0x00; 
    *stk-- = 0x80;
    for (int r = 1; r <= 31; r++) {
        *stk-- = 0x00;
    }

    p->context_sp = stk;
    p->state = STATE_READY;
    return p->pid;
}

void k_uart_init(uint32_t baud) {
    uint16_t ubrr_val = (F_CPU / (16UL * baud)) - 1;
    UBRRH = (uint8_t)(ubrr_val >> 8);
    UBRRL = (uint8_t)ubrr_val;
    UCSRB = (1 << RXEN) | (1 << TXEN);
    #if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328__)
        UCSRC = (1 << UCSZ1) | (1 << UCSZ0);
    #else
        UCSRC = (1 << URSEL) | (1 << UCSZ1) | (1 << UCSZ0);
    #endif
}

void k_timer_init(void) {
    TCCR0A = 0x00;
    TCCR0B = (1 << CS02) | (1 << CS00);
    TIMSK0 |= (1 << TOIE0);
}

void k_servo_init(void) {
    DDRB |= (1 << PB1);
    TCCR1A = (1 << COM1A1) | (1 << WGM11);
    TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS11); 
    ICR1 = 39999; 
}

void k_servo_write(uint8_t angle) {
    OCR1A = 1000 + (angle * 1000 / 100);
}

void avr_button_init(void) {
    DDRD &= ~(1 << DDD2);
    PORTD |= (1 << PORTD2);
}

int avr_button_read(char *buf, int count) {
    if (count < 1 || buf == NULL) return -1;
    if (!(PIND & (1 << PIND2))) {
        buf[0] = 1; 
    } else {
        buf[0] = 0; 
    }

    return 1; 
}

int8_t k_process_spawn(void (*proc_code)(void), const char *proc_name) {
    k_uart_puts_P(PSTR("[Kernel] Spawning "));
    k_uart_puts(proc_name);
    k_uart_puts("...\n");
    int pid = k_sys_fork(proc_code);
    if (pid < 0) {
        k_uart_puts_P(PSTR("[Kernel] [Error] Failed to spawn "));
        k_uart_puts(proc_name);
        k_uart_puts_P(PSTR(" (No empty PCB slot available!)\n"));
        return -1;
    }
    k_uart_puts_P(PSTR("[Kernel] Process "));
    k_uart_puts(proc_name);
    k_uart_puts_P(PSTR(" spawned successfully (PID: "));
    char pid_char = '0' + (uint8_t)pid;
    k_uart_putc(pid_char);
    k_uart_puts_P(PSTR(").\n"));
    return 0;
}

void k_uart_putc(char c) {
    while (!(UCSRA & (1 << UDRE)));
    UDR = c;
}

void k_uart_puts(const char *s) {
    uint8_t sreg = SREG; 
    __asm__ __volatile__ ("cli"); 
    while (*s) {
        if (*s == '\n') {
            k_uart_putc('\r');
        }
        k_uart_putc(*s++);
    }
    SREG = sreg; 
}

void k_uart_puts_P(const char *pgm_s) {
    uint8_t sreg = SREG;
    __asm__ __volatile__ ("cli");

    char c;
    while ((c = pgm_read_byte(pgm_s++))) {
        if (c == '\n') {
            k_uart_putc('\r');
        }
        k_uart_putc(c);
    }

    SREG = sreg;
}

char k_uart_getc(void) {
    while (!(UCSRA & (1 << RXC)));
    return UDR;
}

void k_uart_put_num(uint16_t num) {
    if (num == 0) {
        k_uart_putc('0');
        return;
    }
    char buf[5]; 
    int8_t i = 0;
    while (num > 0) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }
    for (int8_t j = i - 1; j >= 0; j--) {
        k_uart_putc(buf[j]);
    }
}

void k_i2c_init(void) {
    TWSR = 0x00; 
    TWBR = 72;   
    TWCR = (1 << TWEN); 
    k_uart_puts_P(PSTR("[Kernel] I2C Hardware Interface initialized (100kHz).\n"));
}

int k_i2c_write_async(uint8_t data) {
    while (1) {
        k_lock();
        uint8_t next_head = (i2c_head + 1) % I2C_BUF_SIZE;
        if (next_head == i2c_tail) {
            current_proc->state = STATE_BLOCKED;
            i2c_blocked_pid = current_proc->pid;
            k_unlock();
            TCNT0 = 0xFF; 
            __asm__ __volatile__ ("nop");
            continue; 
        }
        i2c_buffer[i2c_head] = data;
        i2c_head = next_head;
        if (!i2c_busy) {
            i2c_busy = 1;
            TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN) | (1 << TWIE);
        }
        
        k_unlock();
        return 0; 
    }
}

void k_panic(const char *msg) {
    k_uart_puts_P(PSTR("\n!!! Kernel Panic !!!\n"));
    k_uart_puts(msg);
    k_uart_puts_P(PSTR("\nSystem Halted.\n"));
    while (1);
}

void k_sys_sleep(uint16_t ticks) {
    if (ticks == 0) return;
    uint8_t sreg = SREG;
    __asm__ __volatile__ ("cli"); 
    current_proc->sleep_ticks = ticks;
    current_proc->state = STATE_SLEEPING; 
    TCNT0 = 0xFF; 
    __asm__ __volatile__ ("sei");
    __asm__ __volatile__ ("nop"); 
    __asm__ __volatile__ ("cli"); 
    SREG = sreg; 
}

void k_kernel_init(void) {
    k_wdt_core_disable();
    k_uart_init(9600); 
    k_uart_puts_P(PSTR("\n========================================================\n"));
    k_uart_puts_P(PSTR("Vostok MPKernel for AVR 8bit Architecture Version I\n"));
    k_uart_puts_P(PSTR("Kernel AVR Monolithic 2026.0.2\n"));
    k_uart_puts_P(PSTR("Copyright (c) 2026, Le Khanh Nam, All rights reserved.\n"));
    k_uart_puts_P(PSTR("========================================================\n"));
    k_uart_puts_P(PSTR("[Kernel] Initializing Scheduler (reserving PCB[0] for kernel)...\n"));
    scheduler_init();
    k_mem_init();
    k_i2c_init();
    k_wdt_core_enable(WDT_2S);
    k_uart_puts_P(PSTR("[Kernel] Watchdog Protection Core enabled.\n"));
}

static uint8_t current_idx = 0; 

void scheduler_init(void) {
    pcb_table[0].pid = 0;
    pcb_table[0].state = STATE_READY;
    pcb_table[0].context_sp = &proc_stacks[0][PROC_STACK_SZ - 1];
    current_idx = 0;
    current_proc = &pcb_table[0];
    pcb_table[0].state = STATE_RUNNING;
}

void schedule(void) {
}


void schedule_preemptive(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (pcb_table[i].state == STATE_SLEEPING) {
            if (pcb_table[i].sleep_ticks > 0) {
                pcb_table[i].sleep_ticks--; 
            }
            if (pcb_table[i].sleep_ticks == 0) {
                pcb_table[i].state = STATE_READY;
            }
        }
    }

    uint8_t start_idx = current_idx;
    uint8_t next_idx = current_idx;
    PCB_t *old_proc = &pcb_table[current_idx];
    PCB_t *new_proc = NULL;

    if (old_proc->state == STATE_RUNNING) {
        old_proc->state = STATE_READY;
    }
    do {
        next_idx = (next_idx + 1) % MAX_PROCS;
        if (pcb_table[next_idx].state == STATE_READY) {
            new_proc = &pcb_table[next_idx];
            break;
        }
    } while (next_idx != start_idx);

    if (new_proc == NULL) {
        next_idx = 0;
        new_proc = &pcb_table[0];
    }

    if (old_proc != new_proc) {
        if (!printed_once) {
            k_uart_puts_P(PSTR("[Kernel] First context switch occurred.\n"));
            printed_once = 1;
        }

        new_proc->state = STATE_RUNNING;
        current_idx = next_idx;
        current_proc = new_proc;
    } else {
        old_proc->state = STATE_RUNNING;
    }
}

void k_lock(void) {
    __asm__ __volatile__ ("cli");
}

void k_unlock(void) {
    __asm__ __volatile__ ("sei");
}

void k_uart_puts_safe(const char *s) {
    k_lock();
    k_uart_puts(s);
    k_unlock();
}

void k_sys_exit(void) {
    k_lock();
    current_proc->state = STATE_UNUSED;
    current_proc->pid = 0;
    k_unlock();
    TCNT0 = 0xFF; 
    while(1) {
        __asm__ __volatile__ ("nop"); 
    }
}

int k_sys_kill(pid_t pid) {
    if (pid == 0 || pid == current_proc->pid) {
        return -1; 
    }
    k_lock();
    for (int i = 0; i < MAX_PROCS; i++) {
        if (pcb_table[i].pid == pid && pcb_table[i].state != STATE_UNUSED) {
            pcb_table[i].state = STATE_UNUSED;
            pcb_table[i].pid = 0;
            k_uart_puts_P(PSTR("[Kernel] Process killed successfully.\n"));
            k_unlock();
            return 0; 
        }
    }
    
    k_unlock();
    return -2; 
}

int pipe_write(pipe_t *p, uint8_t data) {
    while (1) {
        k_lock();
        if (p->count >= PIPE_SIZE && p->blocked_reader_pid == 0) {
        }

        if (p->count < PIPE_SIZE) {
            p->buffer[p->head] = data;
            p->head = (p->head + 1) % PIPE_SIZE;
            p->count++;
            if (p->blocked_reader_pid != 0) {
                for (int i = 0; i < MAX_PROCS; i++) {
                    if (pcb_table[i].pid == p->blocked_reader_pid) {
                        pcb_table[i].state = STATE_READY; 
                        p->blocked_reader_pid = 0;         
                        break;
                    }
                }
            }
            k_unlock();
            return 0; 
        }
        uint8_t reader_alive = 0;
        for (int i = 0; i < MAX_PROCS; i++) {
            if (pcb_table[i].pid != 0 && pcb_table[i].pid != current_proc->pid) {
                if (pcb_table[i].pid == 2 && pcb_table[i].state != STATE_UNUSED) {
                    reader_alive = 1;
                }
            }
        }

        if (!reader_alive) {
            k_unlock();
            return -1; 
        }
        current_proc->state = STATE_BLOCKED;
        p->blocked_writer_pid = current_proc->pid;
        k_unlock();
        TCNT0 = 0xFF;
        __asm__ __volatile__ ("nop");
    }
}

int pipe_read(pipe_t *p, uint8_t *data) {
    while (1) {
        k_lock();
        if (p->count > 0) {
            *data = p->buffer[p->tail];
            p->tail = (p->tail + 1) % PIPE_SIZE;
            p->count--;
            if (p->blocked_writer_pid != 0) {
                for (int i = 0; i < MAX_PROCS; i++) {
                    if (pcb_table[i].pid == p->blocked_writer_pid) {
                        pcb_table[i].state = STATE_READY;
                        p->blocked_writer_pid = 0;
                        break;
                    }
                }
            }
            k_unlock();
            return 0; 
        }
        current_proc->state = STATE_BLOCKED;
        p->blocked_reader_pid = current_proc->pid;
        k_unlock();
        TCNT0 = 0xFF; 
    
        __asm__ __volatile__ ("nop"); 
    }
}

void sys_yield(void) {
}

ISR(TWI_vect) {
    uint8_t status = TWSR & 0xF8; 
    switch(status) {
        case 0x08: 
            TWDR = i2c_sla_w; 
            TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWIE); 
            break;
        case 0x18: 
        case 0x28: 
            if (i2c_tail != i2c_head) {
                TWDR = i2c_buffer[i2c_tail];
                i2c_tail = (i2c_tail + 1) % I2C_BUF_SIZE;
                TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWIE);
                if (i2c_blocked_pid != 0) {
                    for (int i = 0; i < MAX_PROCS; i++) {
                        if (pcb_table[i].pid == i2c_blocked_pid) {
                            pcb_table[i].state = STATE_READY;
                            i2c_blocked_pid = 0;
                            break;
                        }
                    }
                }
            } else {
                TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
                i2c_busy = 0; 
            }
            break;

        default:
            TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
            i2c_busy = 0;
            break;
    }
}

int k_sys_ioctl(int fd, uint8_t cmd, uint16_t arg) {
    if (fd < 0 || fd >= MAX_FD) {
        return -1; 
    }
    struct file *f = current_proc->ofile[fd];
    if (f == NULL || f->f_ops == NULL || f->f_ops->ioctl == NULL) {
        return -2; 
    }
    return f->f_ops->ioctl(f, cmd, arg);
}

void spi_init_master(void) {
    DDRB |= (1 << DDB3) | (1 << DDB5) | (1 << DDB2);
    DDRB &= ~(1 << DDB4);
    SPCR = (1 << SPE) | (1 << MSTR) | (1 << SPR1) | (1 << SPR0);
    SPSR &= ~(1 << SPI2X);
    PORTB |= (1 << PORTB2);
}

uint8_t spi_transceive(uint8_t data) {
    SPDR = data;
    while (!(SPSR & (1 << SPIF)));
    return SPDR;
}

void adc_init_hardware(void) {
    ADMUX = (1 << REFS0); 
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

uint16_t adc_read_raw(uint8_t channel) {
    channel &= 0x07;
    ADMUX = (ADMUX & 0xF0) | channel;
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC));
    return ADC;
}

uint8_t eepromatmega_read (uint16_t addr) {
    while (EECR & (1 << EEPE));
    EEARH = (uint8_t)(addr >> 8);
    EEARL = (uint8_t)(addr & 0xFF);
    EECR |= (1 << EERE);
    return EEDR;
}

void eepromatmega_write(uint16_t addr, uint8_t data) {
    while (EECR & (1 << EEPE));
    EEARH = (uint8_t)(addr >> 8);
    EEARL = (uint8_t)(addr & 0xFF);
    EEDR = data;
    uint8_t sreg = SREG;
    k_lock();
    EECR |= (1 << EEMPE);
    EECR |= (1 << EEPE);
    SREG = sreg;
}

const avr_pwm_hardware_t pwm_timers[] = {
    [0] = { // Timer1 - Kênh B duy nhất (Chân 10 - Chỉ băm xung tần số thấp/Servo 2)
        .tccra = (volatile uint8_t *)&TCCR1A,
        .tccrb = (volatile uint8_t *)&TCCR1B,
        .ocr_a = (volatile uint16_t *)&OCR1A, // Vẫn giữ để tham chiếu nếu cần
        .ocr_b = (volatile uint16_t *)&OCR1B, // Ghi duty vào đây để xuất ra chân 10
        .ddr_a = (volatile uint8_t *)&DDRB,   
        .ddr_b = (volatile uint8_t *)&DDRB,   // Chân 10 nằm ở PORTB
        .pin_a = PB1, 
        .pin_b = PB2  // Chân 10
    },
    [1] = { // Timer2 - Nguyên vẹn hoàn toàn (Quản lý chân 11 và chân 3)
        .tccra = (volatile uint8_t *)&TCCR2A,
        .tccrb = (volatile uint8_t *)&TCCR2B,
        .ocr_a = (volatile uint16_t *)&OCR2A, // Ghi duty Kênh A -> xuất ra chân 11
        .ocr_b = (volatile uint16_t *)&OCR2B, // Ghi duty Kênh B -> xuất ra chân 3
        .ddr_a = (volatile uint8_t *)&DDRB,   // Chân 11 nằm ở PORTB
        .ddr_b = (volatile uint8_t *)&DDRD,   // Chân 3 nằm ở PORTD
        .pin_a = PB3, // Chân 11
        .pin_b = PD3  // Chân 3
    }
};

void avr_pwm_raw_init(uint8_t timer_idx) {
    if (timer_idx >= 2) return;
    const avr_pwm_hardware_t *hw = &pwm_timers[timer_idx];
    if (timer_idx == 0) {
        *(hw->tccra) |= (1 << COM1B1) | (1 << WGM10);
        *(hw->tccrb) |= (1 << WGM12) | (1 << CS11); 
        *(volatile uint16_t *)(hw->ocr_b) = 0;
    } 
    else if (timer_idx == 1) {
        *(hw->tccra) = (1 << COM2A1) | (1 << COM2B1) | (1 << WGM21) | (1 << WGM20);
        *(hw->tccrb) = (1 << CS21); 
        *(volatile uint8_t *)(hw->ocr_a) = 0;
        *(volatile uint8_t *)(hw->ocr_b) = 0;
    }
}

void avr_pwm_raw_set_duty(uint8_t timer_idx, uint8_t channel, uint8_t duty) {
    if (timer_idx >= 2) return;
    const avr_pwm_hardware_t *hw = &pwm_timers[timer_idx];
    if (channel == 0) { 
        if (timer_idx == 0) {
            return; 
        } else {
            *(hw->ddr_a) |= (1 << hw->pin_a);
            *(volatile uint8_t *)(hw->ocr_a) = duty;
        }
    } 
    else {            
        if (timer_idx == 0) {
            *(hw->ddr_b) |= (1 << hw->pin_b);
            *(volatile uint16_t *)(hw->ocr_b) = (uint16_t)duty;
        } else {
            *(hw->ddr_b) |= (1 << hw->pin_b);
            *(volatile uint8_t *)(hw->ocr_b) = duty;
        }
    }
}

void k_wdt_core_enable(uint8_t timeout_val) {
    uint8_t sreg = SREG;
    cli();
    __asm__ __volatile__("wdr");
    MCUSR &= ~(1 << WDRF);
    WDTCSR |= (1 << WDCE) | (1 << WDE);
    WDTCSR = timeout_val;
    SREG = sreg;
}

void k_wdt_core_disable(void) {
    uint8_t sreg = SREG;
    cli();
    __asm__ __volatile__("wdr");
    MCUSR &= ~(1 << WDRF);
    WDTCSR |= (1 << WDCE) | (1 << WDE);
    WDTCSR = 0x00; 
    SREG = sreg;
}

uint16_t k_get_free_ram(void) {
    uint16_t static_and_heap_end = (uint16_t)&__bss_end;
    if (__brkval != 0) {
        static_and_heap_end = (uint16_t)__brkval;
    }
    uint16_t total_allocated_stack = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        if (pcb_table[i].state != STATE_UNUSED) {
            total_allocated_stack += PROC_STACK_SZ;
        }
    }
    uint16_t free_ram = 2048 - (static_and_heap_end + total_allocated_stack);    
    return free_ram;
}

uint16_t k_get_free_heap(void) {
    k_lock();
    uint16_t total_free_bytes = 0;
    block_header_t *curr = heap_start;
    while (curr != NULL) {
        if (curr->size & BLOCK_FREE_MASK) {
            total_free_bytes += (curr->size & BLOCK_SIZE_MASK);
        }
        curr = curr->next;
    }
    k_unlock();
    return total_free_bytes;
}

ISR(INT0_vect) {
    k_intr0_counter++; 
}

ISR(INT1_vect) {
    k_intr1_counter++; 
}

void k_mem_init(void) {
    heap_start = (block_header_t *)k_heap_pool;
    heap_start->size = (KERNEL_HEAP_SIZE - sizeof(block_header_t)) | BLOCK_FREE_MASK;
    heap_start->next = NULL;
}

void* k_malloc(size_t size) {
    if (size == 0) return NULL;
    if (size % 2 != 0) size++;
    k_lock();
    block_header_t *curr = heap_start;
    while (curr != NULL) {
        uint16_t curr_size = curr->size & BLOCK_SIZE_MASK;
        uint8_t is_free = (curr->size & BLOCK_FREE_MASK) ? 1 : 0;
        if (is_free && curr_size >= size) {
            if (curr_size >= size + sizeof(block_header_t) + 2) {
                block_header_t *new_block = (block_header_t *)((uint8_t*)curr + sizeof(block_header_t) + size);
                new_block->size = (curr_size - size - sizeof(block_header_t)) | BLOCK_FREE_MASK;
                new_block->next = curr->next;
                curr->size = size; // Xóa cờ FREE, cập nhật size mới gọn hơn
                curr->next = new_block;
            } else {
                curr->size &= BLOCK_SIZE_MASK; 
            }
            k_unlock();
            return (void*)((uint8_t*)curr + sizeof(block_header_t)); // Trả về vùng payload
        }
        curr = curr->next;
    }
    k_unlock();
    return NULL; 
}

void k_free(void *ptr) {
    if (ptr == NULL) return;
    k_lock();
    block_header_t *header = (block_header_t *)((uint8_t*)ptr - sizeof(block_header_t));
    header->size |= BLOCK_FREE_MASK; 
    block_header_t *curr = heap_start;
    while (curr != NULL && curr->next != NULL) {
        uint8_t curr_free = (curr->size & BLOCK_FREE_MASK) ? 1 : 0;
        uint8_t next_free = (curr->next->size & BLOCK_FREE_MASK) ? 1 : 0;
        
        if (curr_free && next_free) {
            uint16_t new_size = (curr->size & BLOCK_SIZE_MASK) + 
                                sizeof(block_header_t) + 
                                (curr->next->size & BLOCK_SIZE_MASK);
            curr->size = new_size | BLOCK_FREE_MASK;
            curr->next = curr->next->next; 
        } else {
            curr = curr->next;
        }
    }
    k_unlock();
}