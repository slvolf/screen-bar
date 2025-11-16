#include "stm32f1xx.h"  /* CMSIS提供的外设寄存器结构体（兼容CH32） */
#include <stdint.h>

/* 延时函数（约1ms） */
void delay_ms(uint32_t ms) {
    for (volatile uint32_t i = 0; i < ms * 8000; i++);
}

int main(void) {
    /* 1. 使能GPIOC时钟（使用CMSIS的RCC结构体） */
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;

    /* 2. 配置PC13为推挽输出（50MHz）
       - GPIOx_CRH：端口配置高寄存器（引脚8-15）
       - PC13对应bit20-23：MODE13=10（50MHz），CNF13=00（推挽输出）
    */  
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);  // 清除原配置
    GPIOC->CRH |= GPIO_CRH_MODE13_1;  // MODE13=10（50MHz）
    // CNF13默认00（推挽输出），无需额外配置

    /* 3. 循环翻转PC13电平（LED闪烁） */
    while (1) {
        GPIOC->ODR ^= GPIO_ODR_ODR13;  // 翻转PC13（使用CMSIS的位定义）
        delay_ms(500);
    }
}