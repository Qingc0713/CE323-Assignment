#include "mbed.h"
#include "TextLCD.h"
#include <string.h>
#include <stdbool.h>

// --- Hardware Initialization ---
TextLCD lcd(p15, p16, p17, p18, p19, p20);

// Shared Bus for Keypad (rows 0-3) and Switches (rows 4-5)
BusOut rows(p26, p25, p24);
BusIn cols(p14, p13, p12, p11);

// LED SPI Driver
DigitalOut lat(p8);
SPI sw(p5, p6, p7);

// --- State Variables ---
Timer blink_timer;
bool led_blink_state = false;
float blink_rates[4] = {0.2, 0.5, 1.0, 2.0};
int rate_index = 0; // Starts at 0.2s

char Keytable[][4] = {
    {'1', '2', '3', 'F'},
    {'4', '5', '6', 'E'},
    {'7', '8', '9', 'D'},
    {'A', '0', 'B', 'C'}
};

char stored_code[5] = "1234"; // The hardcoded stored code for verification
char input_code[5] = {0};
int code_size = 0;

// --- Task 1: LED & Switch Management ---
void process_leds() {
    // 1. Toggle blink state based on selected timer rate
    if (blink_timer.read() >= blink_rates[rate_index]) {
        led_blink_state = !led_blink_state;
        blink_timer.reset();
    }

    // 2. Read the 8 switches (Logic High = ON)
    rows = 0b100; // Access 4 leftmost switches
    wait_us(10);  // Settle time
    int left_4 = cols;
    
    rows = 0b101; // Access 4 rightmost switches
    wait_us(10);
    int right_4 = cols;
    
    int switches = (left_4 << 4) | right_4;

    // 3. Write to LEDs
    int led_bits = 0;
    for (int i = 7; i >= 0; i--) {
        led_bits <<= 2;
        if (switches & (1 << i)) {
            // If switch is ON, LED mimics the blink state
            if (led_blink_state) {
                led_bits |= 2; // 2 is GREEN, 1 is RED, 3 is ORANGE
            }
        }
    }
    sw.write(led_bits);
    lat = 1;
    lat = 0;
}

// --- Task 2: Keypad Management (加入了消抖机制) ---
char getKey() {
    for (int i = 0; i < 4; i++) {
        rows = i; // 选择 Keypad 的行
        wait_us(10); // 硬件电平稳定时间
        
        for (int j = 0; j < 4; j++) {
            // 当按键被按下时，对应列被拉低 (读取为 0)
            if (~cols & (1 << j)) {
                char detected = Keytable[j][3 - i];

                // 1. 按下消抖：检测到信号后等待 20ms，再次确认是否真的被按下
                wait_us(20000); 
                rows = i;       // 重新写入行选信号
                wait_us(10);
                if ((~cols & (1 << j)) == 0) {
                    continue;   // 如果 20ms 后信号没了，说明是杂波或抖动，忽略
                }

                // 2. 等待按键释放（并处理释放时的抖动）
                bool released = false;
                while (!released) {
                    // 即使在死循环等待期间，也必须保持 LED 的闪烁和刷新！
                    process_leds(); 
                    
                    rows = i; // 重新写入行选信号
                    wait_us(10);
                    
                    if ((~cols & (1 << j)) == 0) { 
                        // 初步检测到按键被释放了
                        wait_us(20000); // 释放消抖：等待 20ms 确认真的松手了
                        rows = i; 
                        wait_us(10);
                        if ((~cols & (1 << j)) == 0) { 
                            released = true; // 确认释放，跳出循环
                        }
                    }
                }
                return detected; // 安全返回检测到的字符
            }
        }
    }
    return ' '; // 没有按键按下
}

// --- UI Management ---
// status: 0 = Normal, 1 = "Press B to set", 2 = "CE323" Success
void refresh_lcd(int status) {
    lcd.cls();
    
    // Line 1 logic
    if (status == 1) {
        lcd.locate(0, 0);
        lcd.printf("Press B to set");
    } else if (status == 2) {
        lcd.locate(0, 0);
        lcd.printf("CE323");
    }
    // If status == 0, Line 1 is intentionally left empty

    // Line 2 logic: "Code: _ _ _ _" formatted dynamically
    lcd.locate(0, 1);
    char c1 = (code_size > 0) ? input_code[0] : '_';
    char c2 = (code_size > 1) ? input_code[1] : '_';
    char c3 = (code_size > 2) ? input_code[2] : '_';
    char c4 = (code_size > 3) ? input_code[3] : '_';
    
    lcd.printf("Code: %c %c %c %c", c1, c2, c3, c4);
}

// --- Main Loop ---
int main() {
    // Setup SPI
    sw.format(16, 0);
    sw.frequency(1000000);
    lat = 0;

    // Initialize Timer & LCD
    blink_timer.start();
    refresh_lcd(0); // Starts with an empty first line and "Code: _ _ _ _"

    while (1) {
        process_leds(); // Updates LED blinking and switch states continuously
        
        char key = getKey(); // Non-blocking scan; safely blocks on release

        if (key != ' ') {
            // --- Feature: Change Blink Rate ---
            // Key 'A' cycles through the 4 available blink rates
            if (key == 'A') {
                rate_index = (rate_index + 1) % 4;
                blink_timer.reset(); 
            } 
            // --- Feature: Entering the Code ---
            else if (code_size < 4) {
                if (key == 'C') { // Delete character
                    if (code_size > 0) {
                        code_size--;
                        input_code[code_size] = '\0';
                        refresh_lcd(0);
                    }
                } else if (key != 'B') { // Any standard key input
                    input_code[code_size] = key;
                    code_size++;
                    input_code[code_size] = '\0';

                    if (code_size == 4) {
                        refresh_lcd(1); // Show "Press B to set"
                    } else {
                        refresh_lcd(0);
                    }
                }
            } 
            // --- Feature: Verifying the Code ---
            else { // code_size == 4
                if (key == 'B') {
                    if (strcmp(input_code, stored_code) == 0) {
                        // Success
                        code_size = 0;
                        input_code[0] = '\0';
                        refresh_lcd(2); // Shows CE323 and resets line 2
                    } else {
                        // Failure
                        code_size = 0;
                        input_code[0] = '\0';
                        refresh_lcd(0); // Empties line 1 and resets line 2
                    }
                } else if (key == 'C') { // Allow backspace even if full
                    code_size--;
                    input_code[code_size] = '\0';
                    refresh_lcd(0);
                }
            }
        }
        wait_us(10000); // 10ms loop delay to reduce CPU thrashing
    }
}