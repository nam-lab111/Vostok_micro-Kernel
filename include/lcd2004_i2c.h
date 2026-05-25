#ifndef LCD2004_VFS_H
#define LCD2004_VFS_H

#include <stdint.h>

void lcd_init_sequence(int fd_i2c);
void lcd_print_str(int fd_i2c, const char *str);
void lcd_set_cursor(int fd_i2c, uint8_t row, uint8_t col);

#endif