#include <avr/io.h>
#include "avrkernel.h"
#include <string.h>
#include <stdio.h>

extern pipe_t g_pipe;
extern char k_uart_getc(void);
extern PCB_t *current_proc;
extern volatile uint16_t k_intr0_counter;
extern volatile uint16_t k_intr1_counter;

struct file g_file_table[MAX_SYSTEM_FILES] = {0};

static uint8_t current_adc_channel = 0;

typedef struct {
    volatile uint8_t *ddr;       
    volatile uint8_t *port;      
    volatile uint8_t *pin;       
    uint8_t reserved_mask;       
    uint8_t allocated_mask;      
    int8_t owner_pid[8];         
} gpio_port_t;

static gpio_port_t g_port_b = { &DDRB, &PORTB, &PINB, RESERVED_PINS_PORTB, 0, {-1, -1, -1, -1, -1, -1, -1, -1} };
static gpio_port_t g_port_d = { &DDRD, &PORTD, &PIND, RESERVED_PINS_PORTD, 0, {-1, -1, -1, -1, -1, -1, -1, -1} };

const struct file_operations uart_fops;
const struct file_operations pipe_fops;
const struct file_operations i2c_fops;
const struct file_operations button_fops;
const struct file_operations servo_fops;
const struct file_operations avrspi_fops;
const struct file_operations adc_fops;
const struct file_operations eepromavr_fops;
const struct file_operations ec0_fops;
const struct file_operations ec1_fops;
const struct file_operations gpio_port_fops;
const struct file_operations watchdog_fops;
const struct file_operations null_fops;
const struct file_operations sysinfo_fops;
const struct file_operations intr0_fops;
const struct file_operations intr1_fops;

static struct file* file_alloc(void) {
    for (int i = 0; i < MAX_SYSTEM_FILES; i++) {
        if (g_file_table[i].ref_count == 0 && g_file_table[i].type == 0) {
            g_file_table[i].ref_count = 1;
            g_file_table[i].offset = 0;
            g_file_table[i].private_data = NULL;
            g_file_table[i].f_ops = NULL;
            return &g_file_table[i];
        }
    }
    return NULL;
}

static int fd_alloc(struct file *f) {
    for (int i = 0; i < MAX_FD; i++) {
        if (current_proc->ofile[i] == NULL) {
            current_proc->ofile[i] = f;
            return i;
        }
    }
    return -1;
}

int k_sys_open(const char *path, int flags) {
    k_lock();
    struct file *f = file_alloc();
    if (f == NULL) {
        k_unlock();
        return -1;
    }

    int fd = fd_alloc(f);
    if (fd == -1) {
        f->ref_count = 0;
        k_unlock();
        return -2;
    }

    if (strcmp(path, "/dev/uart0") == 0) {
        f->f_ops = &uart_fops;
        f->type = 1; 
    }
    else if (strcmp(path, "/dev/pipe0") == 0) {
        f->f_ops = &pipe_fops;
        f->type = 2; 
    }
    else if (strcmp(path, "/dev/i2c0") == 0) {
        f->f_ops = &i2c_fops;
        f->type = 1; 
    }
    else if (strcmp(path, "/dev/avrbutton0") == 0) {
        f->f_ops = &button_fops;
        f->type = 1; 
    }
    else if (strcmp(path, "/dev/avrservo0") == 0) {
        f->f_ops = &servo_fops;
        f->type = 1; 
    }
    else if (strcmp(path, "/dev/avrspi0") == 0) {
        f->f_ops = &avrspi_fops;
        f->type = 1;
    }
    else if (strcmp(path, "/dev/adc0") == 0) {
        f->f_ops = &adc_fops; 
        f->type = 1; 
    }
    //Copyright (c) Le Khanh Nam 2026, All right reserved.
    //Đây là node VFS duy nhất dành cho eeprom nội bộ của ATmega - This is the only VFS node dedicated to the ATmega's internal EEPROM.
    else if (strcmp(path, "/dev/eeprom") == 0) {
        f->f_ops = &eepromavr_fops;
        f->type = 1;
    }
    else if (strcmp(path, "/dev/ec0") == 0) {
        f->f_ops = &ec0_fops;
        f->type = 1;
    }
    else if (strcmp(path, "/dev/ec1") == 0) {
        f->f_ops = &ec1_fops;
        f->type = 1;
    }
    else if (strcmp(path, "/dev/portb") == 0) {
        f->f_ops = &gpio_port_fops;
        f->type = 1;
        f->private_data = &g_port_b; 
    }
    else if (strcmp(path, "/dev/portd") == 0) {
        f->f_ops = &gpio_port_fops;
        f->type = 1;
        f->private_data = &g_port_d; 
    }
    else if (strcmp(path, "/dev/watchdog") == 0) {
        f->f_ops = &watchdog_fops;
        f->type = 1;
    }
    else if (strcmp(path, "/dev/null") == 0) {
        f->f_ops = &null_fops;
        f->type = 1;
    }
    else if (strcmp(path, "sysinfo") == 0) {
        f->f_ops = &sysinfo_fops;
        f->type = 1;
    }
    else if (strcmp(path, "/dev/avrintr0") == 0) {
        f->f_ops = &intr0_fops;
        f->type = 1;
    }
    else if (strcmp(path, "/dev/avrintr1") == 0) {
        f->f_ops = &intr1_fops;
        f->type = 1;
    }
    else {
        current_proc->ofile[fd] = NULL;
        f->ref_count = 0;
        k_unlock();
        return -3;
    }

    if (f->f_ops && f->f_ops->open) {
        if (f->f_ops->open(f, path, flags) < 0) {
            current_proc->ofile[fd] = NULL;
            f->ref_count = 0;
            f->type = 0;
            k_unlock();
            return -4;
        }
    }

    k_unlock();
    return fd;
}

int k_sys_read(int fd, char *buf, int count) {
    if (fd < 0 || fd >= MAX_FD || current_proc->ofile[fd] == NULL) {
        return -1;
    }

    struct file *f = current_proc->ofile[fd];
    if (f->f_ops && f->f_ops->read) {
        return f->f_ops->read(f, buf, count);
    }
    
    return -2;
}

int k_sys_write(int fd, const char *buf, int count) {
    if (fd < 0 || fd >= MAX_FD || current_proc->ofile[fd] == NULL) {
        return -1;
    }

    struct file *f = current_proc->ofile[fd];
    if (f->f_ops && f->f_ops->write) {
        return f->f_ops->write(f, buf, count);
    }
    
    return -2;
}

int k_sys_close(int fd) {
    if (fd < 0 || fd >= MAX_FD || current_proc->ofile[fd] == NULL) {
        return -1;
    }

    k_lock();
    struct file *f = current_proc->ofile[fd];
    current_proc->ofile[fd] = NULL;

    if (f->ref_count > 0) {
        f->ref_count--;
        if (f->ref_count == 0) {
            if (f->f_ops && f->f_ops->close) {
                f->f_ops->close(f);
            }
            f->type = 0;
            f->offset = 0;
            f->private_data = NULL;
            f->f_ops = NULL;
        }
    }

    k_unlock();
    return 0;
}
 

int pipe_vfs_open(struct file *f, const char *path, int flags) {
    f->private_data = &g_pipe; 
    return 0;
}

int pipe_vfs_read(struct file *f, char *buf, int count) {
    pipe_t *p = (pipe_t *)f->private_data;
    uint8_t data;
    int bytes_read = 0;

    while (bytes_read < count) {
        if (pipe_read(p, &data) == 0) {
            buf[bytes_read] = data;
            bytes_read++;
        } else {
            break; 
        }
    }
    return bytes_read; 
}

int pipe_vfs_write(struct file *f, const char *buf, int count) {
    pipe_t *p = (pipe_t *)f->private_data;
    int bytes_written = 0;

    while (bytes_written < count) {
        int res = pipe_write(p, buf[bytes_written]);
        if (res == -1) {
            return -1; 
        }
        bytes_written++;
    }
    return bytes_written;
}

int pipe_vfs_close(struct file *f) {
    return 0;
}

const struct file_operations pipe_fops = {
    .open  = pipe_vfs_open,
    .read  = pipe_vfs_read,
    .write = pipe_vfs_write,
    .close = pipe_vfs_close
};


int uart_vfs_open(struct file *f, const char *path, int flags) {
    return 0;
}

int uart_vfs_read(struct file *f, char *buf, int count) {
    int bytes_read = 0;
    
    while (bytes_read < count) {
        buf[bytes_read] = k_uart_getc(); 
        k_uart_putc(buf[bytes_read]); // Echo kỹ thuật lên Terminal
        
        if (buf[bytes_read] == '\r' || buf[bytes_read] == '\n') {
            bytes_read++;
            break;
        }
        bytes_read++;
    }
    return bytes_read; 
}

int uart_vfs_write(struct file *f, const char *buf, int count) {
    k_lock(); 
    for (int i = 0; i < count; i++) {
        if (buf[i] == '\n') {
            k_uart_putc('\r'); 
        }
        k_uart_putc(buf[i]);  
    }
    k_unlock(); 
    return count; 
}

int uart_vfs_close(struct file *f) {
    return 0;
}

int uart_vfs_ioctl(struct file *f, uint8_t cmd, uint16_t arg) {
    if (cmd == IOCTL_UART_SET_BAUD) {
        uint32_t baud = (uint32_t)arg;
        uint16_t ubrr_val;
        
        k_lock(); 
        while (!(UCSR0A & (1 << TXC0))); 
        UCSR0A |= (1 << TXC0);
        if (baud > 9600) {
            UCSR0A |= (1 << U2X0); 
            ubrr_val = (F_CPU / (8UL * baud)) - 1;
        } else {
            UCSR0A &= ~(1 << U2X0); 
            ubrr_val = (F_CPU / (16UL * baud)) - 1;
        }
        UBRR0H = (uint8_t)(ubrr_val >> 8);
        UBRR0L = (uint8_t)ubrr_val;

        k_unlock(); 
        return 0;
    }
    return -1; 
}

const struct file_operations uart_fops = {
    .open  = uart_vfs_open,
    .read  = uart_vfs_read,
    .write = uart_vfs_write,
    .close = uart_vfs_close,
    .ioctl = uart_vfs_ioctl
};


int i2c_vfs_open(struct file *f, const char *path, int flags) {
    return 0;
}

int i2c_vfs_read(struct file *f, char *buf, int count) {
    return 0; 
}

int i2c_vfs_write(struct file *f, const char *buf, int count) {
    int bytes_written = 0;
    while (bytes_written < count) {
        k_i2c_write_async((uint8_t)buf[bytes_written]);
        bytes_written++;
    }

    return bytes_written; 
}

int i2c_vfs_close(struct file *f) {
    return 0;
}

const struct file_operations i2c_fops = {
    .open  = i2c_vfs_open,
    .read  = i2c_vfs_read,
    .write = i2c_vfs_write,
    .close = i2c_vfs_close
};

int button_vfs_open(struct file *f, const char *path, int flags) {
    avr_button_init(); 
    return 0;
}

int button_vfs_read(struct file *f, char *buf, int count) {
    return avr_button_read(buf, count); 
}

int button_vfs_close(struct file *f) {
    return 0; 
}

const struct file_operations button_fops = {
    .open  = button_vfs_open,
    .read  = button_vfs_read,
    .write = NULL,             
    .close = button_vfs_close,
    .ioctl = NULL
};

int servo_vfs_open(struct file *f, const char *path, int flags) {
    static uint8_t initialized = 0;
    if (!initialized) {
        k_servo_init();
        initialized = 1;
    }
    return 0;
}

int servo_vfs_write(struct file *f, const char *buf, int count) {
    if (count < 1) return -1; 
    uint8_t angle = (uint8_t)buf[0];
    k_servo_write(angle);
    return 1; 
}

int servo_vfs_close(struct file *f) {
    return 0; 
}

int servo_vfs_ioctl(struct file *f, uint8_t cmd, uint16_t arg) {
    if (cmd == IOCTL_SERVO_SET_ANGLE) {
        k_servo_write((uint8_t)arg);
        return 0;
    }
    return -1; 
}

const struct file_operations servo_fops = {
    .open  = servo_vfs_open,
    .read  = NULL,             
    .write = servo_vfs_write,
    .close = servo_vfs_close,
    .ioctl = servo_vfs_ioctl
};

int avrspi_vfs_open(struct file *f, const char *path, int flags) {
    spi_init_master();
    return 0;
}

int avrspi_vfs_close(struct file *f) {
    return 0;
}

int avrspi_vfs_write(struct file *f, const char *buf, int count) {
    for (int i = 0; i < count; i++) {
        spi_transceive((uint8_t)buf[i]);
    }
    return count;
}

int avrspi_vfs_read(struct file *f, char *buf, int count) {
    for (int i = 0; i < count; i++) {
        buf[i] = (char)spi_transceive(0xFF); 
    }
    return count;
}

int avrspi_vfs_ioctl(struct file *f, uint8_t cmd, uint16_t arg) {
    struct spi_cs_pkt *cs = (struct spi_cs_pkt *)arg;
    
    if (cmd != IOCTL_AVRSPI_CHIP_SELECT || cs == NULL) {
        return -1; 
    }
    switch (cs->port) {
        case 'B':
            DDRB |= (1 << cs->pin);
            if (cs->status == SPI_CS_LOW) {
                PORTB &= ~(1 << cs->pin); 
                if (cs->pin == 2) {
                    SPCR |= (1 << MSTR); 
                }
            } else {
                PORTB |= (1 << cs->pin);  
            }
            break;

        case 'C':
            DDRC |= (1 << cs->pin);
            if (cs->status == SPI_CS_LOW) {
                PORTC &= ~(1 << cs->pin);
            } else {
                PORTC |= (1 << cs->pin);
            }
            break;

        case 'D':
            DDRD |= (1 << cs->pin);
            if (cs->status == SPI_CS_LOW) {
                PORTD &= ~(1 << cs->pin);
            } else {
                PORTD |= (1 << cs->pin);
            }
            break;

        default:
            return -2; 
    }
    return 0; 
}

const struct file_operations avrspi_fops = {
    .open  = avrspi_vfs_open,
    .read  = avrspi_vfs_read,
    .write = avrspi_vfs_write,
    .close = avrspi_vfs_close,
    .ioctl = avrspi_vfs_ioctl
};

int avradc_vfs_open(struct file *f, const char *path, int flags) {
    adc_init_hardware();
    return 0;
}

int avradc_vfs_close(struct file *f) {
    return 0;
}

int avradc_vfs_read(struct file *f, char *buf, int count) {
    if (count < 2) return -1; 
    k_lock(); 
    uint16_t raw_val = adc_read_raw(current_adc_channel);
    k_unlock();
    buf[0] = raw_val & 0xFF;        
    buf[1] = (raw_val >> 8) & 0xFF; 

    return 2;
}

int avradc_vfs_write(struct file *f, const char *buf, int count) {
    return -1; 
}

int avradc_vfs_ioctl(struct file *f, uint8_t cmd, uintptr_t arg) {
    if (cmd != IOCTL_ADC_SET_CHANNEL) {
        return -1; 
    }

    uint8_t target_channel = (uint8_t)arg;

    if (target_channel > 7) {
        return -2; 
    }

    if (target_channel == 4 || target_channel == 5) {
        if (i2c_busy) return -3; 
    }

    k_lock();
    current_adc_channel = target_channel; 
    k_unlock();

    return 0;
}

const struct file_operations adc_fops = {
    .open  = avradc_vfs_open,
    .read  = avradc_vfs_read,
    .write = avradc_vfs_write,
    .close = avradc_vfs_close,
    .ioctl = avradc_vfs_ioctl
};

int eepromavr_open(struct file *f, const char *path, int flags) {
    f->offset = 0;
    return 0;
}

int eepromavr_close(struct file *f) {
    return 0;
}

int eepromavr_read(struct file *f, char *buf, int count) {
    int bytesread = 0;
    while (bytesread < count) {
        if (f->offset >= 1024) break;
        buf[bytesread] = eepromatmega_read((uint16_t)f->offset);
        f->offset++;
        bytesread++;
    }
    return bytesread;
}

int eepromavr_write(struct file *f, const char *buf, int count) {
    int byteswritten = 0;
    while (byteswritten < count) {
        if (f->offset >= 1024) break;
        eepromatmega_write((uint16_t)f->offset, (uint8_t)buf[byteswritten]);
        f->offset++;
        byteswritten++;
    }
    return byteswritten;
}

int eepromavr_ioctl(struct file *f, uint8_t cmd, uint16_t arg) {
    switch (cmd) {
        case IOCTL_EEPROM_SEEK:
            if (arg >= 1024) {
                return -1;
            }
            f->offset = arg;
            return 0;
        case IOCTL_EEPROM_CLEAR:
            for (uint16_t i =0; i < 1024; i++) {
                eepromatmega_write(i, 0xFF);
            }
            f->offset = 0;
            return 0;
        case IOCTL_EEPROM_GET_SIZE:
            return 1024;
        default:
            return -1;
    }
}

const struct file_operations eepromavr_fops = {
    .open  = eepromavr_open,
    .read  = eepromavr_read,
    .write = eepromavr_write,
    .close = eepromavr_close,
    .ioctl = eepromavr_ioctl
};

int pwm_vfs_open(struct file *f, const char *path, int flags) {
    uint8_t timer_idx = (f->f_ops == &ec1_fops) ? 1 : 0;
    avr_pwm_raw_init(timer_idx);
    return 0;
}

int pwm_vfs_close(struct file *f) {
    return 0;
}

int pwm_vfs_read(struct file *f, char *buf, int count) {
    return -1; 
}

int pwm_vfs_write(struct file *f, const char *buf, int count) {
    if (count < 1 || buf == NULL) return -1;
    uint8_t timer_idx = (f->f_ops == &ec1_fops) ? 1 : 0;
    if (timer_idx == 0) {
        uint8_t duty = (uint8_t)buf[0];
        avr_pwm_raw_set_duty(0, 1, duty); 
        return 1;
    } 
    else {
        int bytes_written = 0;
        if (count >= 1) {
            avr_pwm_raw_set_duty(1, 0, (uint8_t)buf[0]); 
            bytes_written++;
        }
        if (count >= 2) {
            avr_pwm_raw_set_duty(1, 1, (uint8_t)buf[1]); 
            bytes_written++;
        }
        return bytes_written;
    }
}

int pwm_vfs_ioctl(struct file *f, uint8_t cmd, uint16_t arg) {
    if (f == NULL || f->f_ops == NULL) return -1;
    uint8_t timer_idx = (f->f_ops == &ec1_fops) ? 1 : 0;
    extern const avr_pwm_hardware_t pwm_timers[];
    const avr_pwm_hardware_t *hw = &pwm_timers[timer_idx];
    uint8_t channel = (arg >> 8) & 0xFF;
    uint8_t duty = arg & 0xFF;

    switch (cmd) {
        case IOCTL_PWM_INIT:
            avr_pwm_raw_init(timer_idx);
            break;

        case IOCTL_PWM_SET_DUTY:
            avr_pwm_raw_set_duty(timer_idx, channel, duty);
            break;

        case IOCTL_PWM_DISABLE_CH:
            if (arg == 0) { 
                if (timer_idx == 0) {
                    return -2; 
                } else {
                    *(hw->tccra) &= ~(1 << COM2A1);
                    *(hw->ddr_a) &= ~(1 << hw->pin_a); 
                }
            } 
            else {         
                if (timer_idx == 0) {
                    *(hw->tccra) &= ~(1 << COM1B1);
                    *(hw->ddr_b) &= ~(1 << hw->pin_b);
                } else {
                    *(hw->tccra) &= ~(1 << COM2B1);
                    *(hw->ddr_b) &= ~(1 << hw->pin_b);
                }
            }
            break;
        default:
            return -3; 
    }
    return 0; 
}

const struct file_operations ec0_fops = {
    .open  = pwm_vfs_open,
    .read  = pwm_vfs_read,
    .write = pwm_vfs_write,
    .close = pwm_vfs_close,
    .ioctl = pwm_vfs_ioctl
};

const struct file_operations ec1_fops = {
    .open  = pwm_vfs_open,
    .read  = pwm_vfs_read,
    .write = pwm_vfs_write,
    .close = pwm_vfs_close,
    .ioctl = pwm_vfs_ioctl
};

int gpio_vfs_open(struct file *f, const char *path, int flags) {
    if (f == NULL || f->private_data == NULL) return -1;
    return 0;
}

int gpio_vfs_read(struct file *f, char *buf, int count) {
    if (f == NULL || f->private_data == NULL || buf == NULL || count < 1) return -1;
    gpio_port_t *port = (gpio_port_t *)f->private_data;
    uint8_t pin_num = buf[0] & 0x07;
    uint8_t pin_mask = (1 << pin_num);
    if (!(pin_mask & port->allocated_mask) || port->owner_pid[pin_num] != current_proc->pid) {
        return -2;
    }
    buf[1] = (*(port->pin) & pin_mask) ? 1 : 0;
    return 2; 
}

int gpio_vfs_write(struct file *f, const char *buf, int count) {
    if (f == NULL || f->private_data == NULL || buf == NULL || count < 2) return -1;
    gpio_port_t *port = (gpio_port_t *)f->private_data;
    uint8_t pin_num = buf[0] & 0x07;
    uint8_t level = buf[1];
    uint8_t pin_mask = (1 << pin_num);
    if (!(pin_mask & port->allocated_mask) || port->owner_pid[pin_num] != current_proc->pid) {
        return -2; 
    }

    if (level) {
        *(port->port) |= pin_mask;  
    } else {
        *(port->port) &= ~pin_mask; 
    }
    return count;
}

int gpio_vfs_close(struct file *f) {
    if (f == NULL || f->private_data == NULL) return -1;
    gpio_port_t *port = (gpio_port_t *)f->private_data;
    for (uint8_t i = 0; i < 8; i++) {
        if (port->owner_pid[i] == current_proc->pid) {
            uint8_t pin_mask = (1 << i);
            port->allocated_mask &= ~pin_mask;
            port->owner_pid[i] = -1;     
            *(port->ddr) &= ~pin_mask;  
            *(port->port) &= ~pin_mask; 
        }
    }
    return 0;
}

int gpio_vfs_ioctl(struct file *f, uint8_t cmd, uint16_t arg) {
    if (f == NULL || f->private_data == NULL) return -1;
    
    gpio_port_t *port = (gpio_port_t *)f->private_data;
    uint8_t pin_num = arg & 0x07; 
    uint8_t pin_mask = (1 << pin_num);

    switch (cmd) {
        case IOCTL_GPIO_REQUEST_PIN:
            if ((pin_mask & port->reserved_mask) || (pin_mask & port->allocated_mask)) {
                return -2; 
            }
            port->allocated_mask |= pin_mask;     
            port->owner_pid[pin_num] = current_proc->pid;
            break;

        case IOCTL_GPIO_SET_DIR_OUT:
            if (!(pin_mask & port->allocated_mask) || port->owner_pid[pin_num] != current_proc->pid) return -3;
            *(port->ddr) |= pin_mask; 
            break;

        case IOCTL_GPIO_SET_DIR_IN:
            if (!(pin_mask & port->allocated_mask) || port->owner_pid[pin_num] != current_proc->pid) return -3;
            *(port->ddr) &= ~pin_mask; 
            *(port->port) |= pin_mask; 
            break;

        case IOCTL_GPIO_RELEASE_PIN:
            if (!(pin_mask & port->allocated_mask) || port->owner_pid[pin_num] != current_proc->pid) return -3;
            port->allocated_mask &= ~pin_mask;
            port->owner_pid[pin_num] = -1;
            *(port->ddr) &= ~pin_mask;  
            *(port->port) &= ~pin_mask; 
            break;

        default:
            return -4; 
    }
    return 0;
}

const struct file_operations gpio_port_fops = {
    .open  = gpio_vfs_open,
    .read  = gpio_vfs_read,
    .write = gpio_vfs_write,
    .close = gpio_vfs_close,
    .ioctl = gpio_vfs_ioctl
};

int vfs_wdt_open(struct file *f, const char *path, int flags) {
    return 0;
}

int vfs_wdt_close(struct file *f) {
    return 0;
}

int vfs_wdt_ioctl(struct file *f, uint8_t cmd, uint16_t arg) {
    switch (cmd) {
        case IOCTL_WDT_ENABLE:
            k_wdt_core_enable(WDT_2S);
            break;

        case IOCTL_WDT_DISABLE:
            k_wdt_core_disable();
            break;

        case IOCTL_WDT_KICK:
            k_wdt_core_kick();
            break;

        case IOCTL_WDT_SET_TIMEOUT:
            k_wdt_core_enable((uint8_t)arg);
            break;
        default:
            return -1; 
    }
    return 0; 
}
const struct file_operations watchdog_fops = {
    .open = vfs_wdt_open,
    .read = NULL,
    .write = NULL,
    .close = vfs_wdt_close,
    .ioctl = vfs_wdt_ioctl
};

int null_open(struct file *f, const char *path, int flags) {
    return 0;
}

int null_read(struct file *f, char *buf, int count) {
    return 0; 
}

int null_write(struct file *f, const char *buf, int count) {
    return count; 
}

int null_close(struct file *f) {
    return 0;
}

const struct file_operations null_fops = {
    .open  = null_open,
    .read  = null_read,
    .write = null_write,
    .close = null_close,
    .ioctl = NULL
};

//-----------------------------------------------------------------------------------------------------------//
extern uint16_t k_get_free_ram(void);
extern volatile uint32_t sleep_ticks;

uint16_t get_free_ram_automated(void) {
    extern unsigned int __bss_end;
    extern void *__brkval;
    uint16_t used_static_ram = (uint16_t)&__bss_end;
    if (__brkval != 0) {
        used_static_ram = (uint16_t)__brkval;
    }
    uint16_t used_stack_ram = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        if (pcb_table[i].state != STATE_UNUSED) {
            used_stack_ram += PROC_STACK_SZ; 
        }
    }

    return (2048 - (used_static_ram + used_stack_ram));
}

int sysinfo_open(struct file *f, const char *path, int flags) {
    return 0; 
}

int sysinfo_read(struct file *f, char *buf, int count) {
    if (f->offset > 0) {
        return 0; 
    }
    if (count < sizeof(struct sysinfo_data)) {
        return -1;
    }

    struct sysinfo_data data;
    data.uptime = sleep_ticks;
    
    data.active = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        if (pcb_table[i].state != STATE_UNUSED) {
            data.active++;
        }
    }
    data.free_ram = k_get_free_ram();
    memcpy(buf, &data, sizeof(struct sysinfo_data));
    f->offset += sizeof(struct sysinfo_data); 
    return sizeof(struct sysinfo_data); 
}

int sysinfo_write(struct file *f, const char *buf, int count) {
    return -1; 
}

int sysinfo_close(struct file *f) {
    f->offset = 0; 
    return 0;
}

const struct file_operations sysinfo_fops = {
    .open  = sysinfo_open,
    .read  = sysinfo_read,
    .write = sysinfo_write,
    .close = sysinfo_close,
    .ioctl = NULL
};

int intr0_vfs_open(struct file *f, const char *path, int flags) {
    DDRD &= ~(1 << DDD2);   
    PORTD |= (1 << PORTD2); 
    f->offset = 0;
    return 0;
}

int intr0_vfs_read(struct file *f, char *buf, int count) {
    if (count < sizeof(uint16_t)) return -1;
    uint16_t local_count;
    
    k_lock(); 
    local_count = k_intr0_counter;
    k_unlock();
    
    memcpy(buf, &local_count, sizeof(uint16_t));
    return sizeof(uint16_t);
}

int intr0_vfs_ioctl(struct file *f, uint8_t cmd, uint16_t arg) {
    switch (cmd) {
        case IOCTL_INTR_ENABLE:
            EICRA &= ~((1 << ISC01) | (1 << ISC00)); 
            if (arg == INTR_MODE_TOGGLE)       EICRA |= (1 << ISC00);
            else if (arg == INTR_MODE_FALLING) EICRA |= (1 << ISC01);
            else if (arg == INTR_MODE_RISING)  EICRA |= (1 << ISC01) | (1 << ISC00);
            
            EIFR |= (1 << INTF0);  
            EIMSK |= (1 << INT0);  
            return 0;
            
        case IOCTL_INTR_DISABLE:
            EIMSK &= ~(1 << INT0); 
            return 0;
            
        default: return -1;
    }
}

const struct file_operations intr0_fops = {
    .open  = intr0_vfs_open,
    .read  = intr0_vfs_read,
    .write = NULL, 
    .close = NULL,
    .ioctl = intr0_vfs_ioctl
};

int intr1_vfs_open(struct file *f, const char *path, int flags) {
    DDRD &= ~(1 << DDD3);   
    PORTD |= (1 << PORTD3); 
    f->offset = 0;
    return 0;
}

int intr1_vfs_read(struct file *f, char *buf, int count) {
    if (count < sizeof(uint16_t)) return -1;
    uint16_t local_count;
    
    k_lock();
    local_count = k_intr1_counter;
    k_unlock();
    
    memcpy(buf, &local_count, sizeof(uint16_t));
    return sizeof(uint16_t);
}

int intr1_vfs_ioctl(struct file *f, uint8_t cmd, uint16_t arg) {
    switch (cmd) {
        case IOCTL_INTR_ENABLE:
            EICRA &= ~((1 << ISC11) | (1 << ISC10));
            if (arg == INTR_MODE_TOGGLE)       EICRA |= (1 << ISC10);
            else if (arg == INTR_MODE_FALLING) EICRA |= (1 << ISC11);
            else if (arg == INTR_MODE_RISING)  EICRA |= (1 << ISC11) | (1 << ISC10);
            
            EIFR |= (1 << INTF1);
            EIMSK |= (1 << INT1);
            return 0;
            
        case IOCTL_INTR_DISABLE:
            EIMSK &= ~(1 << INT1);
            return 0;
            
        default: return -1;
    }
}

const struct file_operations intr1_fops = {
    .open  = intr1_vfs_open,
    .read  = intr1_vfs_read,
    .write = NULL,
    .close = NULL,
    .ioctl = intr1_vfs_ioctl
};