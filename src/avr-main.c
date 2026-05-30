#include <avr/io.h>
#include "avrkernel.h"
#include "lcd2004_i2c.h"
#include "avr/pgmspace.h"
#include <string.h>
#include <util/delay.h>

pipe_t g_pipe;

//static uint8_t count = 0;
//static uint8_t user2_is_alive = 1; 
extern void dino_testinit_main(void);

#define AVR_CMD_PROG_ENABLE     0xAC //keep - no delete, because kernel is not already
#define AVR_CMD_READ_SIGNATURE  0x30 //keep - no delete, because kernel is not already

void init_process(void) {
    int fd0 = k_sys_open("/dev/avrintr0", 0);
    int fd1 = k_sys_open("/dev/avrintr1", 0);

    if (fd0 < 0 || fd1 < 0) {
        k_uart_puts_P(PSTR("[User] Lỗi: Không thể mở /dev/intr0 hoặc /dev/intr1!\n"));
        k_sys_exit();
    }
    k_sys_ioctl(fd0, IOCTL_INTR_ENABLE, INTR_MODE_FALLING);
    k_sys_ioctl(fd1, IOCTL_INTR_ENABLE, INTR_MODE_FALLING);

    k_uart_puts_P(PSTR("[User] Tiến trình test Ngắt đã chạy! Hãy nhấn nút ở chân PD2 và PD3.\n"));

    uint16_t count0 = 0;
    uint16_t count1 = 0;
    uint16_t last_count0 = 0;
    uint16_t last_count1 = 0;

    while (1) {
        int r0 = k_sys_read(fd0, (char *)&count0, sizeof(uint16_t));
        int r1 = k_sys_read(fd1, (char *)&count1, sizeof(uint16_t));
        if (count0 != last_count0 || count1 != last_count1) {
            k_uart_puts_P(PSTR("[OK] INT0: "));
            k_uart_put_num(count0);
            k_uart_puts_P(PSTR(" phát | INT1: "));
            k_uart_put_num(count1);
            k_uart_puts_P(PSTR(" phát\n"));

            last_count0 = count0;
            last_count1 = count1;
        }
        k_sys_sleep(50); 
    }
}

const char msg_sys_header[] PROGMEM = "\nMPKernel Version 0.2 VostokMPK 2026.0.2\n";
const char msg_sys_tasks[]  PROGMEM = "Active Tasks: ";
const char msg_sys_ram[]    PROGMEM = "\nReal Free RAM: "; 
const char msg_sys_bytes[]  PROGMEM = " Bytes";
const char msg_sys_footer[] PROGMEM = "\n-------------------------\n";

void user_process2(void) {
    k_sys_exit();
}

void user_process3(void) {
    int uart_fd = k_sys_open("/dev/uart0", 0);
    int servo_fd = k_sys_open("/dev/avrservo0", 0);
    if (uart_fd < 0) {
        k_uart_puts_P(PSTR("[User Process 3] [Error] Khong the mo UART qua VFS!\n"));
        while(1);
    }
    const char *msg3 = "[User Process 3] Servo da duoc dieu khien\n";
    k_sys_write(uart_fd, msg3, strlen(msg3));
    while (1) {
        k_sys_ioctl(servo_fd, IOCTL_SERVO_SET_ANGLE, 0);
        k_sys_sleep(100);
        k_sys_ioctl(servo_fd, IOCTL_SERVO_SET_ANGLE, 90);
        k_sys_sleep(100);
    }
    k_sys_sleep(30);
}

int main(void) {
    k_kernel_init();
    k_process_spawn(init_process, "Init Process");
    k_process_spawn(user_process2, "User Process 2");
    k_process_spawn(user_process3, "User Process 3");
    avr_button_init();
    k_timer_init();
    k_servo_init();
    k_uart_puts_P(PSTR("[Kernel] Entering Preemptive Scheduling...\n"));
    __asm__ __volatile__ ("sei");
    while(1) {
        __asm__ __volatile__ ("nop");  
    }
    
    return 0;
}