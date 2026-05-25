#include <string.h>
#include <stdint.h>

extern int k_sys_open(const char *path, int flags);
extern int k_sys_read(int fd, char *buf, int count);
extern int k_sys_write(int fd, const char *buf, int count);
extern void k_sys_sleep(uint16_t ms);

#define LCD_BIT_RS        (1 << 0) 
#define LCD_BIT_RW        (1 << 1)
#define LCD_BIT_EN        (1 << 2) 
#define LCD_BIT_BACKLIGHT (1 << 3) 

static void lcd_write_4bits(int fd_lcd, uint8_t value, uint8_t mode) {
    uint8_t pin_state = (value & 0xF0) | mode | LCD_BIT_BACKLIGHT;
    uint8_t pulse_high = pin_state | LCD_BIT_EN;
    k_sys_write(fd_lcd, (char*)&pulse_high, 1);
    uint8_t pulse_low = pin_state & ~LCD_BIT_EN;
    k_sys_write(fd_lcd, (char*)&pulse_low, 1);
}

static void lcd_send(int fd_lcd, uint8_t value, uint8_t mode) {
    uint8_t high_nibble = value & 0xF0;
    uint8_t low_nibble = (value << 4) & 0xF0;
    lcd_write_4bits(fd_lcd, high_nibble, mode);
    lcd_write_4bits(fd_lcd, low_nibble, mode);
}

static void game_lcd_goto(int fd_lcd, int x, int y) {
    uint8_t addr = 0;
    if (y == 0) addr = 0x00 + x;
    if (y == 1) addr = 0x40 + x;
    if (y == 2) addr = 0x14 + x; 
    if (y == 3) addr = 0x54 + x; 
    
    lcd_send(fd_lcd, 0x80 | addr, 0); 
}

// Hàm in chuỗi ký tự ra LCD
static void game_lcd_puts(int fd_lcd, const char *str) {
    while (*str) {
        lcd_send(fd_lcd, (uint8_t)(*str), LCD_BIT_RS); 
        str++;
    }
}

static void game_lcd_put_score(int fd_lcd, unsigned int val) {
    char buf[5];
    buf[0] = '0' + (val / 1000) % 10;
    buf[1] = '0' + (val / 100) % 10;  
    buf[2] = '0' + (val / 10) % 10;   
    buf[3] = '0' + (val % 10);
    buf[4] = '\0';
    
    game_lcd_puts(fd_lcd, buf);
}

void game_lcd_init(int fd_lcd) {
    k_sys_sleep(50); 

    lcd_write_4bits(fd_lcd, 0x30, 0);
    k_sys_sleep(5);
    
    lcd_write_4bits(fd_lcd, 0x30, 0);
    k_sys_sleep(1);
    
    lcd_write_4bits(fd_lcd, 0x30, 0);
    k_sys_sleep(1);


    lcd_write_4bits(fd_lcd, 0x20, 0);
    k_sys_sleep(1);

   
    lcd_send(fd_lcd, 0x28, 0); 
    k_sys_sleep(1);

    lcd_send(fd_lcd, 0x0C, 0); 
    k_sys_sleep(1);

    lcd_send(fd_lcd, 0x06, 0); 
    k_sys_sleep(1);

    lcd_send(fd_lcd, 0x01, 0); 
    k_sys_sleep(5);            
}

void dino_testinit_main(void) {
    int fd_lcd  = k_sys_open("/dev/i2c0", 0);
    int fd_btn  = k_sys_open("/dev/avrbutton0", 0);
    int fd_uart = k_sys_open("/dev/uart0", 0);
    game_lcd_init(fd_lcd);
    
    const char *msginit = "[Test Init] Tro choi duoc tao ra de kiem tra tien trinh Init de tien hanh viet Shell\n";
    k_sys_write(fd_uart, msginit, strlen(msginit));
    
    int dino_y = 3;          
    int dino_is_jumping = 0; 
    int jump_counter = 0;
    int cactus_x = 19;       
    unsigned int score = 0;
    int game_over = 0;
    char btn_state = 0;
    char row2[21];
    char row3[21];

    while (1) {
        if (!game_over) {
            k_sys_read(fd_btn, &btn_state, 1);
            if (btn_state == 1 && !dino_is_jumping) {
                dino_is_jumping = 1;
                dino_y = 2; 
                jump_counter = 0;
            }
            if (dino_is_jumping) {
                jump_counter++;
                if (jump_counter > 2) { 
                    dino_y = 3;         
                    dino_is_jumping = 0;
                }
            }
            cactus_x--; 
            if (cactus_x < 0) {
                cactus_x = 19; 
                score++;       
            }
            if (dino_y == 3 && cactus_x == 2) {
                game_over = 1;
                const char *msginit1 = "[Test Init] CRASH! Game Over.\n";
                k_sys_write(fd_uart, msginit1, strlen(msginit1));
            }

            for (int i = 0; i < 20; i++) { 
                row2[i] = ' ';  
                row3[i] = '_';  
            }
            row2[20] = '\0'; 
            row3[20] = '\0';
            if (dino_y == 2) {
                row2[2] = 'D'; 
            } else {
                row3[2] = 'D'; 
            }
            row3[cactus_x] = 'X'; 
            game_lcd_goto(fd_lcd, 0, 0);
            game_lcd_puts(fd_lcd, "Score: ");
            game_lcd_put_score(fd_lcd, score);
            game_lcd_goto(fd_lcd, 0, 2);
            game_lcd_puts(fd_lcd, row2);
            game_lcd_goto(fd_lcd, 0, 3);
            game_lcd_puts(fd_lcd, row3);

        } else {
            game_lcd_goto(fd_lcd, 5, 1);
            game_lcd_puts(fd_lcd, "Game Over!");
            game_lcd_goto(fd_lcd, 1, 2);
            game_lcd_puts(fd_lcd, "Press BTN to Replay");
            k_sys_read(fd_btn, &btn_state, 1);
            if (btn_state == 1) {
                score = 0;
                cactus_x = 19;
                dino_y = 3;
                game_over = 0;
                lcd_send(fd_lcd, 0x01, 0); 
                k_sys_sleep(5); 
            }
        }

        k_sys_sleep(95); 
    }
}