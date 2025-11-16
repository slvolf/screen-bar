#include "stm32f1xx.h"  /* CMSIS通用头文件（兼容CH32） */

/* 系统时钟初始化（使用外部8MHz无源晶振，配置为72MHz系统时钟） */
void SystemInit_CH32(void) {
    // 1. 使能外部高速时钟HSE，并等待稳定
    RCC->CR |= RCC_CR_HSEON;  // 使能HSE（外部8MHz晶振）
    while (!(RCC->CR & RCC_CR_HSERDY));  // 等待HSE稳定

    // 2. 配置PLL（锁相环）：HSE作为PLL输入，倍频9倍（8MHz * 9 = 72MHz）
    RCC->CFGR &= ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL);  // 清除PLL配置
    RCC->CFGR |= RCC_CFGR_PLLSRC;  // PLL输入源为HSE（外部晶振）
    RCC->CFGR |= RCC_CFGR_PLLMULL9;  // 倍频系数9

    // 3. 使能PLL，并等待锁定
    RCC->CR |= RCC_CR_PLLON;  // 使能PLL
    while (!(RCC->CR & RCC_CR_PLLRDY));  // 等待PLL锁定

    // 4. 配置系统时钟源为PLL，同时配置总线时钟分频
    RCC->CFGR &= ~RCC_CFGR_SW;  // 清除系统时钟源配置
    RCC->CFGR |= RCC_CFGR_SW_PLL;  // 系统时钟源切换为PLL（72MHz）

    // 配置总线时钟：AHB=72MHz，APB1=36MHz（最大36MHz），APB2=72MHz
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1;    // AHB不分频（HCLK = SYSCLK = 72MHz）
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;   // APB1分频2（PCLK1 = 36MHz）
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV1;   // APB2不分频（PCLK2 = 72MHz）

    // 等待系统时钟切换完成
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);

    // 使能复用功能时钟（如需使用其他外设可扩展）
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;
}