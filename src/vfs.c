#include "avrkernel.h"
#include <string.h>

extern pipe_t g_pipe;
extern char k_uart_getc(void);

struct file g_file_table[MAX_SYSTEM_FILES] = {0};

const struct file_operations uart_fops;
const struct file_operations pipe_fops;
const struct file_operations i2c_fops;
const struct file_operations button_fops;

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

const struct file_operations uart_fops = {
    .open  = uart_vfs_open,
    .read  = uart_vfs_read,
    .write = uart_vfs_write,
    .close = uart_vfs_close
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
    .close = button_vfs_close
};