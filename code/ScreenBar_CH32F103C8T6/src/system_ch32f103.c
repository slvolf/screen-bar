#include "stm32f1xx.h"  /* CMSIS通用头文件（兼容CH32） */

/* 系统时钟初始化（CH32F103默认使用内部8MHz HSI，此处简化配置） */
/* 已重命名为 SystemInit_CH32，以避免与框架中的 SystemInit 符号冲突。
   若需要在复位初始化阶段执行此函数，请在项目中调用它或替换框架的实现。 */
void SystemInit_CH32(void) {
    // 使能外设时钟（如需配置外部晶振，可在此扩展）
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;  // 使能复用功能时钟（可选）
}