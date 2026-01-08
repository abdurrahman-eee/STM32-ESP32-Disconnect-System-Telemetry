#include "stm32f1xx.h"
#include <stdio.h>
#include <string.h>

// --- TIMING ---
volatile uint32_t ticks_ms = 0;
void SysTick_Handler(void) { ticks_ms++; }
void delay_init(void) { SysTick_Config(8000000 / 1000); }
uint32_t millis(void) { return ticks_ms; }
void delay_us(volatile uint32_t x) { x*=6; while(x--) __NOP(); }

// --- UART (To ESP32) ---
void usart_init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN | RCC_APB2ENR_IOPAEN;
    // PA9 (TX) -> Alt Func Push-Pull
    GPIOA->CRH &= ~(0xF << 4); GPIOA->CRH |= (0xB << 4);
    // PA10 (RX) -> Input Pull-Up (Prevents noise)
    GPIOA->CRH &= ~(0xF << 8); GPIOA->CRH |= (0x8 << 8); GPIOA->ODR |= (1 << 10); 
    // Baud 115200 @ 8MHz
    USART1->BRR = 0x45; 
    USART1->CR1 |= USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}
void usart_send(char c) { while (!(USART1->SR & USART_SR_TXE)); USART1->DR = c; }
void usart_print(char *str) { while (*str) usart_send(*str++); }
int usart_available(void) { return (USART1->SR & USART_SR_RXNE); }
char usart_read(void) { return (char)(USART1->DR); }

// --- HARDWARE ---
void gpio_init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPCEN;
    // PA1 IR (Input Pullup)
    GPIOA->CRL &= ~(0xF<<4); GPIOA->CRL |= (0x8<<4); GPIOA->ODR |= (1<<1);
    // PC2 Btn (Input Pullup)
    GPIOC->CRL &= ~(0xF<<8); GPIOC->CRL |= (0x8<<8); GPIOC->ODR |= (1<<2);
    // LEDs PC0, PC3
    GPIOC->CRL &= ~(0xF<<0); GPIOC->CRL |= (0x2<<0); 
    GPIOC->CRL &= ~(0xF<<12); GPIOC->CRL |= (0x2<<12);
}
void adc_init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN; ADC1->CR2 |= ADC_CR2_ADON; 
    for(int i=0;i<1000;i++); ADC1->CR2 |= ADC_CR2_CAL; while(ADC1->CR2 & ADC_CR2_CAL);
}
uint16_t adc_read(void) {
    ADC1->SQR3=0; ADC1->CR2|=ADC_CR2_ADON; while(!(ADC1->SR & ADC_SR_EOC)); return ADC1->DR;
}
void servo_init(void) {
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN; 
    GPIOB->CRL &= ~(0xF<<28); GPIOB->CRL |= (0xA<<28); // PB7
    TIM4->PSC = 7; TIM4->ARR = 19999;
    TIM4->CCMR1 = (6<<12)|TIM_CCMR1_OC2PE; TIM4->CCER|=TIM_CCER_CC2E; TIM4->CR1|=TIM_CR1_CEN;
}
void set_servo(int a) { if(a<0)a=0; if(a>180)a=180; TIM4->CCR2=600+(a*1800/180); }

// --- RAM MONITOR ---
extern uint32_t _estack;
extern uint32_t _Min_Stack_Size;
extern uint32_t _end;  // End of BSS section

uint32_t get_free_ram(void) {
    uint32_t stack_ptr;
    __asm volatile ("mov %0, sp" : "=r" (stack_ptr));
    // Calculate free RAM between heap and stack
    return stack_ptr - (uint32_t)&_end;
}

// --- MAIN ---
int main(void) {
    delay_init(); gpio_init(); adc_init(); servo_init(); usart_init();

    // State Variables
    uint8_t engaged = 0;
    uint8_t auto_mode = 1;  // Auto engagement based on pot threshold
    uint16_t engage_threshold = 2048;  // Mid-point default (adjustable)
    
    uint32_t last_toggle_time = 0; 
    uint32_t last_send_time = 0;
    
    char buffer[64];
    int current_angle = 0;
    uint16_t pot_val = 0;

    GPIOC->ODR |= (1 << 0); // Start Yellow ON (Disengaged)

    while (1) {
        uint32_t now = millis();

        // ===== 1. ESP32 COMMAND PROCESSING =====
        if (usart_available()) {
            char cmd = usart_read();
            switch(cmd) {
                case '1': 
                    engaged = 1; 
                    auto_mode = 0; 
                    last_toggle_time = now; 
                    break;
                case '0': 
                    engaged = 0; 
                    auto_mode = 0; 
                    last_toggle_time = now; 
                    break;
                case 'A': 
                    auto_mode = 1; 
                    break;  // Auto mode enabled
            }
        }

        // ===== 2. LOCAL INPUTS (IR / BUTTON) - EDGE DETECTION =====
        static uint8_t last_ir_state = 0;
        static uint8_t last_btn_state = 0;
        
        uint8_t ir_active = !(GPIOA->IDR & (1 << 1));
        uint8_t btn_active = !(GPIOC->IDR & (1 << 2));

        if (((ir_active && !last_ir_state) || (btn_active && !last_btn_state)) && 
            (now - last_toggle_time > 300)) {
            engaged ^= 1; 
            auto_mode = 0;  // Manual override disables auto mode
            last_toggle_time = now;
        }
        
        last_ir_state = ir_active;
        last_btn_state = btn_active;

        // ===== 3. POTENTIOMETER READING =====
        pot_val = adc_read();

        // ===== 4. AUTO ENGAGEMENT LOGIC =====
        // When in auto mode: pot > threshold = engage, pot < threshold = disengage
        // Hysteresis band prevents chattering around threshold
        if (auto_mode) {
            if (pot_val > engage_threshold + 200) {  // Upper threshold
                engaged = 1;
            } else if (pot_val < engage_threshold - 200) {  // Lower threshold
                engaged = 0;
            }
            // Between thresholds: maintain current state (hysteresis)
        }

        // ===== 5. SERVO CONTROL & LED INDICATION =====
        if (engaged) {
            current_angle = (pot_val * 180) / 4095;  // Map pot to 0-180°
            GPIOC->ODR &= ~(1<<0); GPIOC->ODR |= (1<<3);  // Red LED ON (Active)
        } else {
            current_angle = 0;  // Return to home position
            GPIOC->ODR |= (1<<0); GPIOC->ODR &= ~(1<<3);  // Yellow LED ON (Standby)
        }
        set_servo(current_angle);

        // ===== 6. ENHANCED DATA TRANSMISSION (Every 200ms) =====
        if (now - last_send_time >= 200) {
            uint32_t free_ram = get_free_ram();
            // Protocol: engaged,angle,pot_raw,auto_mode,ram_bytes
            sprintf(buffer, "%d,%d,%d,%d,%lu\n", 
                    engaged, current_angle, pot_val, auto_mode, free_ram);
            usart_print(buffer);
            last_send_time = now;
        }

        delay_us(1000); // 1ms loop delay for stability (1000Hz loop rate)
    }
}