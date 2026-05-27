#include <avr/io.h>
#include "avrkernel.h"
#include <string.h>

extern pipe_t g_pipe;
extern char k_uart_getc(void);

struct file g_file_table[MAX_SYSTEM_FILES] = {0};

static uint8_t current_adc_channel = 0;

const struct file_operations uart_fops;
const struct file_operations pipe_fops;
const struct file_operations i2c_fops;
const struct file_operations button_fops;
const struct file_operations servo_fops;
const struct file_operations avrspi_fops;
const struct file_operations adc_fops;

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

