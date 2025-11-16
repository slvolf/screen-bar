#include "stm32f1xx.h"
#include <stdint.h>

void delay_ms(uint32_t ms) {
    // 注意：时钟频率变为72MHz后，原延时循环需要调整（可按比例修改乘数）
    // 72MHz下约为 ms * 72000（粗略估算，实际需精确校准）
    for (volatile uint32_t i = 0; i < ms * 72000; i++);
}

int main(void) {
    SystemInit_CH32();  // 初始化外部晶振和系统时钟（必须放在最前面）

    // 使能GPIOC时钟
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;

    // 配置PC13为推挽输出（50MHz）
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;  // MODE13=10（50MHz）

    // 循环翻转PC13电平
    while (1) {
        GPIOC->ODR ^= GPIO_ODR_ODR13;
        delay_ms(500);
    }
}