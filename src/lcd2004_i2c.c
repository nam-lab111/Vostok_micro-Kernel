#include "lcd2004_i2c.h"
#include "avrkernel.h" 

#define LCD_BACKLIGHT 0x08
#define LCD_ENABLE    0x04
#define LCD_REG_SELECT 0x01
extern int k_sys_write(int fd, const char *buf, int count);

void lcd_send_nibble(int fd_i2c, uint8_t nibble, uint8_t mode) {
    uint8_t data_high = (nibble & 0xF0) | mode | LCD_BACKLIGHT;
    uint8_t seq[2];
    
    seq[0] = data_high | LCD_ENABLE;  
    seq[1] = data_high & ~LCD_ENABLE; 
    
    k_sys_write(fd_i2c, (char *)seq, 2);
}

void lcd_send_byte(int fd_i2c, uint8_t val, uint8_t mode) {
    lcd_send_nibble(fd_i2c, val & 0xF0, mode);        
    lcd_send_nibble(fd_i2c, (val << 4) & 0xF0, mode); 
}

void lcd_init_sequence(int fd_i2c) {
    k_sys_sleep(3); 
    lcd_send_nibble(fd_i2c, 0x30, 0);
    k_sys_sleep(1); 
    lcd_send_nibble(fd_i2c, 0x30, 0);
    k_sys_sleep(1); 
    lcd_send_nibble(fd_i2c, 0x30, 0);
    
    lcd_send_nibble(fd_i2c, 0x20, 0); 
    
    lcd_send_byte(fd_i2c, 0x28, 0); 
    lcd_send_byte(fd_i2c, 0x0C, 0); 
    lcd_send_byte(fd_i2c, 0x06, 0); 
    lcd_send_byte(fd_i2c, 0x01, 0); 
    k_sys_sleep(1); 
}

void lcd_print_str(int fd_i2c, const char *str) {
    while (*str) {
        lcd_send_byte(fd_i2c, (uint8_t)*str++, LCD_REG_SELECT);
    }
}

void lcd_set_cursor(int fd_i2c, uint8_t row, uint8_t col) {
    uint8_t row_offsets[] = {0x00, 0x40, 0x14, 0x54}; 
    lcd_send_byte(fd_i2c, 0x80 | (col + row_offsets[row]), 0);
}