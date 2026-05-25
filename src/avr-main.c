#include <avr/io.h>
#include "avrkernel.h"
#include "lcd2004_i2c.h"
#include "avr/pgmspace.h"
#include <string.h>

pipe_t g_pipe;

//static uint8_t count = 0;
//static uint8_t user2_is_alive = 1; 
extern void dino_testinit_main(void);

void init_process(void) {
    dino_testinit_main();
    while (1) {
        k_sys_sleep(40);
    }
}

void user_process2(void) {
    int fd_uart = k_sys_open("/dev/uart0", 0);
    if (fd_uart < 0) {
        k_uart_puts_P(PSTR("[User Process 2] [Error] Khong the mo UART qua VFS!\n"));
        while(1);
    }
    const char *msg2 = "[User Process 2] Da khoi dong thanh cong\n";
    k_sys_write(fd_uart, msg2, strlen(msg2));
    
    while (1) {
        PORTB ^= (1 << PB0);
        k_sys_sleep(50);
    }
}

void user_process3(void) {
    k_sys_exit();
}

int main(void) {
    k_kernel_init();
    k_process_spawn(init_process, "Init Process");
    k_process_spawn(user_process2, "User Process 2");
    k_process_spawn(user_process3, "User Process 3");
    avr_button_init();
    k_timer_init();
    k_servo_init();
    DDRB |= (1 << PB5);
    DDRB |= (1 << PB0);
    DDRD |= (1 << PD7);
    k_uart_puts_P(PSTR("[Kernel] Entering Preemptive Scheduling...\n"));
    __asm__ __volatile__ ("sei");
    while(1) {
        __asm__ __volatile__ ("nop");  
    }
    
    return 0;
}