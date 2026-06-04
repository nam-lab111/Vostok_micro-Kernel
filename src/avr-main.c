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

void user_process_2(void); 
void user_process_3(void);

typedef struct {
    void (*proc_code)(void);
    const char *name;
} boot_node_t;

void vostok_init_task(void) {
    k_uart_puts_P(PSTR("\n====================================================\n"));
    k_uart_puts_P(PSTR("[init (PID 1)] Đã chiếm quyền điều khiển User-space!\n"));
    k_uart_puts_P(PSTR("====================================================\n"));
    boot_node_t *boot_list = (boot_node_t *)k_malloc(sizeof(boot_node_t) * 2);
    if (boot_list == NULL) {
        k_panic("Init Panic: Không đủ Heap để khởi tạo danh sách boot_list!");
    }
    boot_list[0].proc_code = user_process_2;
    boot_list[0].name = "User_Process_2";
    boot_list[1].proc_code = user_process_3;
    boot_list[1].name = "User_Process_3";
    for (uint8_t i = 0; i < 2; i++) {
        k_uart_puts_P(PSTR("[init] Chuẩn bị gọi nhân sinh task: "));
        k_uart_puts(boot_list[i].name);
        k_uart_puts_P(PSTR("\n"));
        k_process_spawn(boot_list[i].proc_code, boot_list[i].name);
        k_sys_sleep(10); 
    }
    k_free(boot_list);
    k_uart_puts_P(PSTR("[init] Đã dọn dẹp bộ nhớ boot_list. Heap đã được phục hồi!\n"));
    while (1) {
        k_sys_sleep(250); 
    }
}

void user_process_2(void) {
    k_uart_puts_P(PSTR("[User] Tiến trình test PWM đã chạy!\n"));
    int fd_ec0 = k_sys_open("/dev/ec0", 0); 
    int fd_ec1 = k_sys_open("/dev/ec1", 0); 
    if (fd_ec0 < 0 || fd_ec1 < 0) {
        k_uart_puts_safe("[Test PWM] Lỗi: Không thể mở Node /dev/ec0 hoặc /dev/ec1!\n");
        while(1);
    }
    k_sys_ioctl(fd_ec0, IOCTL_PWM_INIT, 0);
    k_sys_ioctl(fd_ec1, IOCTL_PWM_INIT, 0);
    
    k_uart_puts_safe("[Test PWM] Đã kích hoạt luồng băm xung độc lập cho Chân 10 và Chân 11...\n");

    uint8_t duty_led = 0;
    int8_t direction = 1; 

    char buf_ec0[1];
    char buf_ec1[2];

    while (1) {
        buf_ec0[0] = duty_led;
        k_sys_write(fd_ec0, buf_ec0, 1);
        buf_ec1[0] = duty_led; 
        buf_ec1[1] = duty_led; 
        k_sys_write(fd_ec1, buf_ec1, 2);
        duty_led += direction;
        if (duty_led == 255) {
            direction = -1;
        } else if (duty_led == 0) {
            direction = 1;
        }

        k_sys_sleep(2); 
    }
}

void user_process_3(void) {
    k_sys_exit();
}

int main(void) {
    k_kernel_init();
    k_uart_puts_P(PSTR("[Kernel] Initiating the original Init process...\n"));
    k_process_spawn(vostok_init_task, "init");
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