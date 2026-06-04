//Ok tệp định nghĩa này là tôi cũng phải dùng AI để tạm thời định nghĩa nhưng mà nó lấy nhiều quá (bằng lỗi) nên phải áp dụng trong avrkernel.c :)))))
//Trừ một số hàm là phải bổ sung liên tục và sửa đổi và bổ sung để biên dịch đc avrkernel.c.
#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stddef.h>

// ============================================================================
// 1. CẤU HÌNH HỆ THỐNG (SYSTEM CONFIGURATION)
// ============================================================================
#define MAX_PROCS         4      // Số lượng tiến trình tối đa hệ thống hỗ trợ
#define PROC_STACK_SZ     64    // Kích thước Kernel Stack cho mỗi tiến trình (bytes)
#define MAX_FD            15      // Số lượng File Descriptor tối đa cho mỗi Proc
#define PIPE_SIZE         16
#define MAX_SYSTEM_FILES  18
#define I2C_BUF_SIZE 32

typedef uint16_t pid_t;

static volatile uint8_t i2c_buffer[I2C_BUF_SIZE];
static volatile uint8_t i2c_head = 0;
static volatile uint8_t i2c_tail = 0;
static volatile uint8_t i2c_busy = 0;
static volatile uint8_t i2c_sla_w = 0x27 << 1; 
static volatile pid_t i2c_blocked_pid = 0;


typedef enum {
    STATE_UNUSED = 0,   
    STATE_EMBRYO,       
    STATE_READY,        
    STATE_RUNNING,      
    STATE_BLOCKED,      
    STATE_SLEEPING
} proc_state_t;

struct file; 

struct file_operations {
    int (*open)(struct file *f, const char *path, int flags);
    int (*read)(struct file *f, char *buf, int count);
    int (*write)(struct file *f, const char *buf, int count);
    int (*close)(struct file *f);
    int (*ioctl)(struct file *f, uint8_t cmd, uint16_t arg);
};

#define IOCTL_UART_SET_BAUD 1
#define IOCTL_SERVO_SET_ANGLE 2
#define IOCTL_BUTTON_GET_PIN 3
#define IOCTL_AVRSPI_CHIP_SELECT 0x40
#define IOCTL_AVRSPI_SET_SPEED 0x41
#define SPI_CS_LOW 0
#define SPI_CS_HIGH 1
#define IOCTL_ADC_SET_CHANNEL    0x50
#define IOCTL_EEPROM_SEEK 0x60
#define IOCTL_EEPROM_CLEAR 0x61
#define IOCTL_EEPROM_GET_SIZE 0x62
#define IOCTL_PWM_INIT         1  
#define IOCTL_PWM_SET_DUTY     2  
#define IOCTL_PWM_DISABLE_CH   3
#define IOCTL_GPIO_REQUEST_PIN   1  
#define IOCTL_GPIO_SET_DIR_OUT   2  
#define IOCTL_GPIO_SET_DIR_IN    3  
#define IOCTL_GPIO_WRITE_HIGH    4  
#define IOCTL_GPIO_WRITE_LOW     5  
#define IOCTL_GPIO_READ_PIN      6  
#define IOCTL_GPIO_RELEASE_PIN   7  
#define IOCTL_WDT_ENABLE      0x10
#define IOCTL_WDT_DISABLE     0x20
#define IOCTL_WDT_KICK        0x30
#define IOCTL_WDT_SET_TIMEOUT 0x40
#define IOCTL_INTR_ENABLE      1
#define IOCTL_INTR_DISABLE     2
#define INTR_MODE_LOW          0x00 
#define INTR_MODE_TOGGLE       0x01 
#define INTR_MODE_FALLING      0x02 
#define INTR_MODE_RISING       0x03 

#define PIN_0   0
#define PIN_1   1
#define PIN_2   2
#define PIN_3   3
#define PIN_4   4
#define PIN_5   5
#define PIN_6   6
#define PIN_7   7

#define RESERVED_PINS_PORTB  0x0E 
#define RESERVED_PINS_PORTD  0x0B

#define WDT_15MS   (1 << WDE)
#define WDT_500MS  (1 << WDE) | (1 << WDP2) | (1 << WDP0)
#define WDT_1S     (1 << WDE) | (1 << WDP2) | (1 << WDP1)
#define WDT_2S     (1 << WDE) | (1 << WDP2) | (1 << WDP1) | (1 << WDP0)
#define WDT_4S     (1 << WDE) | (1 << WDP3)
#define WDT_8S     (1 << WDE) | (1 << WDP3) | (1 << WDP0)

struct spi_cs_pkt {
    char port;        
    uint8_t pin;     
    uint8_t status;   
};

struct file {
    uint8_t type;                           
    uint32_t offset;                        
    uint8_t ref_count;
    const struct file_operations *f_ops;    
    void *private_data;
};

extern struct file g_file_table[MAX_SYSTEM_FILES];

typedef struct {
    uint8_t *context_sp;        
    pid_t pid;                  
    proc_state_t state;         
    uint16_t sleep_ticks;
    
    // Lưu các con trỏ trỏ tới struct file của VFS
    struct file *ofile[MAX_FD]; 
} PCB_t;

extern PCB_t pcb_table[MAX_PROCS];
extern PCB_t *current_proc;

typedef struct {
    uint8_t buffer[PIPE_SIZE];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    uint8_t readers;          
    uint8_t writers;           
    pid_t blocked_reader_pid;
    pid_t blocked_writer_pid;
} pipe_t;

typedef struct {
    volatile uint8_t *tccra;
    volatile uint8_t *tccrb;
    volatile void    *ocr_a;   
    volatile void    *ocr_b;
    volatile uint8_t *ddr_a;
    volatile uint8_t *ddr_b;
    uint8_t pin_a;
    uint8_t pin_b;
} avr_pwm_hardware_t;

struct sysinfo_data {
    uint32_t uptime;
    uint8_t  active;
    uint16_t free_ram;
};

typedef struct block_header {
    uint16_t size;
    struct block_header *next;
} block_header_t;

#define BLOCK_FREE_MASK 0x8000
#define BLOCK_SIZE_MASK 0x7FFF

void schedule(void);
void scheduler_init(void);
void schedule_preemptive(void);
void swtch(uint8_t **old_sp, uint8_t *new_sp);
int k_sys_fork(void (*proc_code)(void)); 
void k_sys_sleep(uint16_t ticks);
void k_sys_exit(void);
void sys_yield(void);
void k_uart_init(uint32_t baud);
void k_uart_putc(char c);
void k_uart_puts(const char *s);
void k_uart_puts_P(const char *pgm_s);
void k_panic(const char *msg);
void k_timer_init(void);  
void k_lock(void);
void k_unlock(void);
void k_uart_puts_safe(const char *s);
int pipe_write(pipe_t *p, uint8_t data);
int pipe_read(pipe_t *p, uint8_t *data);
int k_sys_open(const char *path, int flags);
int k_sys_read(int fd, char *buf, int count);
int k_sys_write(int fd, const char *buf, int count);
int k_sys_close(int fd);
char k_uart_getc(void);
void k_servo_init(void);
void k_servo_write(uint8_t angle);
int8_t k_process_spawn(void (*proc_code)(void), const char *proc_name);
void k_kernel_init(void);
void k_i2c_init(void);
int k_i2c_write_async(uint8_t data);
void avr_button_init(void) ;
int avr_button_read(char *buf, int count);
int k_sys_ioctl(int fd, uint8_t cmd, uintptr_t arg);
void spi_init_master(void);
uint8_t spi_transceive(uint8_t data);
void adc_init_hardware(void);
uint16_t adc_read_raw(uint8_t channel);
uint8_t eepromatmega_read (uint16_t addr);
void eepromatmega_write(uint16_t addr, uint8_t data);
void avr_pwm_raw_init(uint8_t timer_idx);
void avr_pwm_raw_set_duty(uint8_t timer_idx, uint8_t channel, uint8_t duty);
void k_wdt_core_enable(uint8_t timeout_val);
void k_wdt_core_disable(void);
static inline void k_wdt_core_kick(void) {
    __asm__ __volatile__("wdr");
}
void k_uart_put_num(uint16_t num);
void k_mem_init(void);
void* k_malloc(size_t size);
uint16_t k_get_free_heap(void);
void k_free(void *ptr);

#endif // KERNEL_H