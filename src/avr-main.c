#include <avr/io.h>
#include "avrkernel.h"
#include "lcd2004_i2c.h"
#include "avr/pgmspace.h"
#include <string.h>

pipe_t g_pipe;

//static uint8_t count = 0;
//static uint8_t user2_is_alive = 1; 
extern void dino_testinit_main(void);

#define AVR_CMD_PROG_ENABLE     0xAC
#define AVR_CMD_READ_SIGNATURE  0x30

uint8_t vostok_isp_transaction(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    spi_transceive(a);
    spi_transceive(b);
    spi_transceive(c);
    return spi_transceive(d); // Trả về kết quả của byte thứ 4
}

void init_process(void) {
    int fd_uart = k_sys_open("/dev/uart0", 0);
    int fd_spi  = k_sys_open("/dev/avrspi0", 0); // Mở để Driver tự động chạy spi_init_master()
    
    if (fd_spi < 0 || fd_uart < 0) {
        k_uart_puts_P(PSTR("[Init] [Error] Khong the mo UART0 hoac SPI0!\n"));
        while(1);
    }

    const char *welcome_msg = "\n=========================================\n"
                             "[Vostok OS] Kich hoat Task Test SPI ISP V2\n"
                             "=========================================\n";
    k_sys_write(fd_uart, welcome_msg, strlen(welcome_msg));

    struct spi_cs_pkt sram_cs = {
        .port = 'B',
        .pin = 2,
        .status = SPI_CS_HIGH
    };

    char log_buf[64];
    uint8_t chip_id[3] = {0, 0, 0};

    /* ========================================================
       CRITICAL TIMING: ĐƯA CHIP VÀO CHẾ ĐỘ ISP (DỊCH TỪ ARDUINO ISP)
       ======================================================== */
    PORTB &= ~(1 << PORTB5);
    k_sys_sleep(2); 
    sram_cs.status = SPI_CS_HIGH;
    k_sys_ioctl(fd_spi, IOCTL_AVRSPI_CHIP_SELECT, (uintptr_t)&sram_cs);
    k_sys_sleep(1); 
    sram_cs.status = SPI_CS_LOW;
    k_sys_ioctl(fd_spi, IOCTL_AVRSPI_CHIP_SELECT, (uintptr_t)&sram_cs);
    k_sys_sleep(5); 
    vostok_isp_transaction(0xAC, 0x53, 0x00, 0x00);
    k_sys_sleep(2); 
    chip_id[0] = vostok_isp_transaction(0x30, 0x00, 0x00, 0x00);
    chip_id[1] = vostok_isp_transaction(0x30, 0x00, 0x01, 0x00);
    chip_id[2] = vostok_isp_transaction(0x30, 0x00, 0x02, 0x00);
    sram_cs.status = SPI_CS_HIGH;
    k_sys_ioctl(fd_spi, IOCTL_AVRSPI_CHIP_SELECT, (uintptr_t)&sram_cs);
    strcpy(log_buf, "[Vostok SPI Target ID] RAW HEX: ");
    k_sys_write(fd_uart, log_buf, strlen(log_buf));

    for(uint8_t i = 0; i < 3; i++) {
        char hex[4];
        uint8_t high = (chip_id[i] >> 4) & 0x0F;
        uint8_t low  = chip_id[i] & 0x0F;
        hex[0] = (high < 10) ? ('0' + high) : ('A' + high - 10);
        hex[1] = (low < 10) ? ('0' + low) : ('A' + low - 10);
        hex[2] = ' ';
        hex[3] = '\0';
        k_sys_write(fd_uart, hex, 3);
    }
    k_sys_write(fd_uart, "\n", 1);

    if (chip_id[0] == 0x1E && chip_id[1] == 0x95 && chip_id[2] == 0x0F) {
        const char *success_msg = "[Result] Phát hiện chính xác chip ATmega328P Target.\n";
        k_sys_write(fd_uart, success_msg, strlen(success_msg));
    } else {
        const char *fail_msg = "[Result] IC khac khong dung Target.\n";
        k_sys_write(fd_uart, fail_msg, strlen(fail_msg));
    }

    while (1) {
        k_sys_sleep(100);
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
        k_sys_sleep(20);
    }
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
    DDRB |= (1 << PB5);
    DDRB |= (1 << PB0);
    k_uart_puts_P(PSTR("[Kernel] Entering Preemptive Scheduling...\n"));
    __asm__ __volatile__ ("sei");
    while(1) {
        __asm__ __volatile__ ("nop");  
    }
    
    return 0;
}