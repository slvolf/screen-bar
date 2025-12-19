/********************************** (C) COPYRIGHT *******************************
 * File Name          : ch32f10x_it.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2024/01/06
 * Description        : Main Interrupt Service Routines.
 *********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#include "ch32f10x_it.h" 
#include "ch32f10x.h"

// 白光软件PWM：由Main.c提供占空比与引脚定义
extern volatile u8 g_white_soft_duty;

/*********************************************************************
 * @fn      NMI_Handler
 *
 * @brief   This function handles NMI exception.
 *
 * @return  none
 */
void NMI_Handler(void)
{
  while (1)
  {
  }
}

/*********************************************************************
 * @fn      HardFault_Handler
 *
 * @brief   This function handles Hard Fault exception.
 *
 * @return  none
 */
void HardFault_Handler(void)
{
  while (1)
  {
  }
}

/*********************************************************************
 * @fn      MemManage_Handler
 *
 * @brief   This function handles Memory Manage exception.
 *
 * @return  none
 */
void MemManage_Handler(void)
{
  while (1)
  {
  }
}

/*********************************************************************
 * @fn      BusFault_Handler
 *
 * @brief   This function handles Bus Fault exception.
 *
 * @return  none
 */
void BusFault_Handler(void)
{
  while (1)
  {
  }
}

/*********************************************************************
 * @fn      UsageFault_Handler
 *
 * @brief   This function handles Usage Fault exception.
 *
 * @return  none
 */
void UsageFault_Handler(void)
{
  while (1)
  {
  }
}

/*********************************************************************
 * @fn      SVC_Handler
 *
 * @brief   This function handles SVCall exception.
 *
 * @return  none
 */
void SVC_Handler(void)
{
}

/*********************************************************************
 * @fn      DebugMon_Handler
 *
 * @brief   This function handles Debug Monitor exception.
 *
 * @return  none
 */
void DebugMon_Handler(void)
{
}

/*********************************************************************
 * @fn      PendSV_Handler
 *
 * @brief   This function handles PendSVC exception.
 *
 * @return  none
 */
void PendSV_Handler(void)
{
}

/*********************************************************************
 * @fn      SysTick_Handler
 *
 * @brief   This function handles SysTick Handler.
 *
 * @return  none
 */
static __IO uint32_t TimingDelay = 0;
void TimingDelay_Decrement(void) {
  if (TimingDelay != 0x00) {
    TimingDelay--;
  }
}
extern __IO uint32_t g_systick_counter;
void TimingDelay_Decrement(void);
void SysTick_Handler(void)
{
  g_systick_counter++;
  TimingDelay_Decrement();
}


/*********************************************************************
 * @fn      TIM4_IRQHandler
 *
 * @brief   TIM4 更新中断：用于产生1ms系统节拍。
 *
 * @return  none
 */
void TIM4_IRQHandler(void)
{
  if (TIM_GetITStatus(TIM4, TIM_IT_Update) != RESET)
  {
    TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
    g_systick_counter++;
  }
}

/*********************************************************************
 * @fn      TIM3_IRQHandler
 *
 * @brief   TIM3中断：用于PA4白光软件PWM(10kHz)
 *          - Update中断：每个周期开始拉高(或保持低/高)
 *          - CC1中断：到达占空比关断点拉低
 *
 * @return  none
 */
void TIM3_IRQHandler(void)
{
  // 周期开始
  if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
  {
    TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
    // 避免旧的CC1挂起影响本周期
    TIM_ClearITPendingBit(TIM3, TIM_IT_CC1);

    u8 duty = g_white_soft_duty;
    if (duty == 0)
    {
      GPIO_ResetBits(GPIOA, GPIO_Pin_4);
      TIM_ITConfig(TIM3, TIM_IT_CC1, DISABLE);
    }
    else if (duty >= 100)
    {
      GPIO_SetBits(GPIOA, GPIO_Pin_4);
      TIM_ITConfig(TIM3, TIM_IT_CC1, DISABLE);
    }
    else
    {
      // 关断点 = duty% * 周期ticks，避免CCR=0导致立即比较
      u16 arr = (u16)TIM3->ATRLR;
      u16 period_ticks = (u16)(arr + 1U);
      u16 on_ticks = (u16)(((u32)duty * (u32)period_ticks) / 100U);
      if (on_ticks < 1U) on_ticks = 1U;
      if (on_ticks > arr) on_ticks = arr;

      GPIO_SetBits(GPIOA, GPIO_Pin_4);
      TIM_SetCompare1(TIM3, on_ticks);
      TIM_ITConfig(TIM3, TIM_IT_CC1, ENABLE);
    }
  }

  // 关断点
  if (TIM_GetITStatus(TIM3, TIM_IT_CC1) != RESET)
  {
    TIM_ClearITPendingBit(TIM3, TIM_IT_CC1);
    GPIO_ResetBits(GPIOA, GPIO_Pin_4);
  }
}





