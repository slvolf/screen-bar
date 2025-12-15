/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2019/10/15
 * Description        : Main program body.
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/*
 *@Note 
 *ADC DMA sampling routines:
 *ADC channel 2 (PA2), the rule group channel obtains ADC conversion data 
 *for 1024 consecutive times through DMA.
 *
*/

#include "debug.h"
#include "string.h"
#include "stdlib.h"
                                                    
// 避免与ch32f10x.h中的HSE_VALUE重定义冲突
#ifndef HSE_VALUE
#define HSE_VALUE        8000000U    // 8MHz外置晶振
#endif
#define SYS_CLK          72000000U   // 目标系统时钟：72MHz

// PWM配置（COB灯带20kHz无频闪）
#define PWM_FREQ         20000U      // PWM频率：20kHz（无频闪）
#define PWM_ARR          899U        // 72MHz/(3+1)/900 = 20kHz
#define PWM_WARM_CH      TIM_Channel_4  // PA3-TIM2 CH4（暖灯）
#define PWM_WHITE_CH     TIM_Channel_1  // PA4-TIM3 CH1（白光）
#define PWM_MIN_DUTY     3           // 最小占空比（微弱光）
#define PWM_MAX_DUTY     100         // 最大占空比
#define PWM_STEP         1           // 调光步进值

// 触控模块（TTP223）配置
#define TOUCH_WARM_PIN   GPIO_Pin_14 // PB14（暖灯触控）
#define TOUCH_WHITE_PIN  GPIO_Pin_15 // PB15（白光触控）
#define TOUCH_PORT       GPIOB
#define TOUCH_DEBOUNCE   20          // 消抖时间（ms）
#define SHORT_PRESS_TIME 500         // 短按阈值（<500ms）
#define LONG_PRESS_TIME  500         // 长按阈值（≥500ms）
#define DIR_FLIP_TIME    2000        // 长按2秒翻转调光方向

// I2C光感模块（BH1750）配置
#define LIGHT_SENSOR_ADDR 0x46      // BH1750 I2C地址（0x23 << 1）
#define AUTO_LIGHT_INTERVAL 500     // 自动调光间隔（ms）

// ESP8266串口通信配置
#define UART_BAUDRATE    9600
#define REPORT_INTERVAL  100        // 状态上报间隔（ms）
#define JSON_BUF_LEN     256        // JSON缓冲区长度

// 易失性关键字兼容定义
#ifndef __IO
#define __IO volatile
#endif

// 全局SysTick计数器（毫秒级）
__IO uint32_t g_systick_counter = 0;

// 全局状态变量
u8 warm_duty = PWM_MIN_DUTY;    // 暖灯当前占空比
u8 white_duty = PWM_MIN_DUTY;   // 白光当前占空比
u8 warm_last_duty = PWM_MIN_DUTY; // 暖灯关闭前的占空比
u8 white_last_duty = PWM_MIN_DUTY;// 白光关闭前的占空比
u8 warm_switch = 1;             // 暖灯开关状态（1=开，0=关）
u8 white_switch = 1;            // 白光开关状态
u8 touch_warm_flag = 0;         // 暖灯触摸状态（0=无，1=触摸中）
u8 touch_white_flag = 0;        // 白光触摸状态
u8 warm_touch_dir = 1;          // 暖灯调光方向（1=调亮，0=调暗）
u8 white_touch_dir = 1;         // 白光调光方向
u32 warm_touch_tick = 0;        // 暖灯触摸计时
u32 white_touch_tick = 0;       // 白光触摸计时
u8 auto_light_enable = 0;       // 自动调光使能（1=开启，0=关闭）
u16 ambient_light = 0;          // 环境光值（lux）
u32 auto_light_tick = 0;        // 自动调光计时

// 函数声明
uint32_t SysTick_GetTick(void);
void SystemClock_Config_8MHz(void);
void PWM_Init(void);
void Set_PWM_Duty(u8 ch, u8 duty);
void Light_Switch(u8 type, u8 state);
void Touch_Init(void);
void Touch_Detect(void);
void TickTimer_Init(void);
void I2C1_Init(void);
u16 Read_Ambient_Light(void);
void Auto_Light_Adjust(void);
void UART1_Init(u32 baudrate);
void Parse_ESP8266_Cmd(char *cmd);
void UART1_Receive_Cmd(void);
void UART1_Send_Status(void);
void RGB_Reserve_Init(void);

/*********************************************************************
 * @函数名    SysTick_GetTick
 * @功能      获取系统运行时间（毫秒）
 * @返回值    当前SysTick计数值
 ********************************************************************/
uint32_t SysTick_GetTick(void) {
  return g_systick_counter;
}

/*********************************************************************
 * @函数名    SystemClock_Config_8MHz
 * @功能      配置系统时钟：8MHz外置晶振 → 72MHz
 * @返回值    无
 ********************************************************************/
void SystemClock_Config_8MHz(void) {
  RCC_DeInit();
  
  // 使能8MHz外部晶振
  RCC_HSEConfig(RCC_HSE_ON);
  while (RCC_GetFlagStatus(RCC_FLAG_HSERDY) == RESET);
  
  // 配置PLL：8MHz × 9 = 72MHz
  RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_9);
  RCC_PLLCmd(ENABLE);
  while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);
  
  // 配置总线分频
  RCC_HCLKConfig(RCC_SYSCLK_Div1);
  RCC_PCLK1Config(RCC_HCLK_Div2);
  RCC_PCLK2Config(RCC_HCLK_Div1);
  
  // 切换系统时钟到PLL
  RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
  while (RCC_GetSYSCLKSource() != 0x08);

  // 更新SystemCoreClock，确保后续延时与打印正确
  SystemCoreClockUpdate();
}

/*********************************************************************
 * @函数名    PWM_Init
 * @功能      初始化20kHz无频闪PWM（PA3暖灯/PA4白光）
 * @返回值    无
 ********************************************************************/
void PWM_Init(void) {
  GPIO_InitTypeDef GPIO_InitStructure = {0};
  TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
  TIM_OCInitTypeDef TIM_OCInitStructure = {0};

  // 使能TIM2/TIM3和GPIOA时钟
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3, ENABLE);
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);

  // 配置PA3(TIM2 CH4)和PA4(TIM3 CH1)为复用推挽输出
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3 | GPIO_Pin_4;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOA, &GPIO_InitStructure);

  // TIM2时基配置（20kHz）
  TIM_TimeBaseStructure.TIM_Prescaler = 3;
  TIM_TimeBaseStructure.TIM_Period = PWM_ARR;
  TIM_TimeBaseStructure.TIM_ClockDivision = 0;
  TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

  // 配置TIM2 CH4（暖灯）PWM模式1
  TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
  TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
  TIM_OCInitStructure.TIM_Pulse = (PWM_MIN_DUTY * PWM_ARR) / 100;
  TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
  TIM_OC4Init(TIM2, &TIM_OCInitStructure);
  TIM_OC4PreloadConfig(TIM2, TIM_OCPreload_Enable);

  // TIM3时基配置（与TIM2相同）
  TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

  // 配置TIM3 CH1（白光）PWM模式1
  TIM_OC1Init(TIM3, &TIM_OCInitStructure);
  TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Enable);

  // 使能TIM2/TIM3
  TIM_ARRPreloadConfig(TIM2, ENABLE);
  TIM_Cmd(TIM2, ENABLE);
  TIM_ARRPreloadConfig(TIM3, ENABLE);
  TIM_Cmd(TIM3, ENABLE);
}

/*********************************************************************
 * @函数名    Set_PWM_Duty
 * @功能      设置PWM占空比（限制5%-100%）
 * @参数      ch - TIM_Channel_4(暖灯)/TIM_Channel_1(白光)
 *            duty - 目标占空比（0-100）
 * @返回值    无
 ********************************************************************/
void Set_PWM_Duty(u8 ch, u8 duty) {
  // 限制占空比范围
  if (duty < PWM_MIN_DUTY) {
    duty = PWM_MIN_DUTY;
  }
  if (duty > PWM_MAX_DUTY) {
    duty = PWM_MAX_DUTY;
  }
  
  u16 ccr_val = (duty * PWM_ARR) / 100;
  if (ch == PWM_WARM_CH) {
    TIM_SetCompare4(TIM2, ccr_val);
    warm_duty = duty;
  } else if (ch == PWM_WHITE_CH) {
    TIM_SetCompare1(TIM3, ccr_val);
    white_duty = duty;
  }
}

/*********************************************************************
 * @函数名    Light_Switch
 * @功能      控制灯的亮灭（灭=PWM置0，亮=恢复上次占空比）
 * @参数      type - 0=暖灯，1=白光
 *            state - 1=开，0=关
 * @返回值    无
 ********************************************************************/
void Light_Switch(u8 type, u8 state) {
  if (type == 0) {  // 暖灯
    warm_switch = state;
    if (state == 0) {  // 关闭
      warm_last_duty = warm_duty;
      TIM_SetCompare4(TIM2, 0);
    } else {  // 打开
      Set_PWM_Duty(PWM_WARM_CH, warm_last_duty);
    }
  } else {  // 白光
    white_switch = state;
    if (state == 0) {  // 关闭
      white_last_duty = white_duty;
      TIM_SetCompare1(TIM3, 0);
    } else {  // 打开
      Set_PWM_Duty(PWM_WHITE_CH, white_last_duty);
    }
  }
}

/*********************************************************************
 * @函数名    Touch_Init
 * @功能      初始化TTP223触控引脚（PB14/PB15上拉输入）
 * @返回值    无
 ********************************************************************/
void Touch_Init(void) {
  GPIO_InitTypeDef GPIO_InitStructure = {0};
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

  GPIO_InitStructure.GPIO_Pin = TOUCH_WARM_PIN | TOUCH_WHITE_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(TOUCH_PORT, &GPIO_InitStructure);
}

/*********************************************************************
 * @函数名    Touch_Detect
 * @功能      触控检测（短按=开关，长按=调光+方向翻转）
 * @返回值    无
 ********************************************************************/
void Touch_Detect(void) {
  // 每次长按内只翻转一次方向
  static u8 warm_flipped_once = 0;

  // 暖灯触控检测（松开是0，按下是1）
  u8 warm_touch_state = GPIO_ReadInputDataBit(TOUCH_PORT, TOUCH_WARM_PIN);

  if (warm_touch_state == 1) {  // 按下（触摸中）
    touch_warm_flag = 1;
    if (warm_touch_tick == 0) {  // 首次触摸：初始化计时+方向
      warm_touch_tick = SysTick_GetTick();
      warm_flipped_once = 0;
      // 重置调光计时
      static u32 last_adjust_tick = 0;
      last_adjust_tick = warm_touch_tick;
    } else {
      u32 diff = SysTick_GetTick() - warm_touch_tick;
      static u32 last_adjust_tick = 0;
      // 长按≥500ms，首次满足时翻转方向；且距离上次调光≥50ms → 持续步进调光
      if (diff >= LONG_PRESS_TIME && (SysTick_GetTick() - last_adjust_tick) >= 50) {
        if (!warm_flipped_once) {
          warm_touch_dir = !warm_touch_dir;
          warm_flipped_once = 1;
        }
        last_adjust_tick = SysTick_GetTick(); // 更新上次调光时间
        
        u8 new_duty = 0;
        if (warm_touch_dir == 1) {  // 调亮（持续步进）
          new_duty = warm_duty + PWM_STEP;
        } else {  // 调暗（持续步进）
          new_duty = warm_duty - PWM_STEP;
        }
        // 限制占空比范围（5%-100%）
        new_duty = (new_duty < PWM_MIN_DUTY) ? PWM_MIN_DUTY : new_duty;
        new_duty = (new_duty > PWM_MAX_DUTY) ? PWM_MAX_DUTY : new_duty;
        // 更新全局变量（关键！否则下次还是初始值+步进）
        warm_duty = new_duty;
        // 设置PWM占空比
        Set_PWM_Duty(PWM_WARM_CH, new_duty);
      }
    }
  } else {  // 松开（非触摸）：从else对应==0改为对应==1
    if (touch_warm_flag == 1) {
      u32 diff = SysTick_GetTick() - warm_touch_tick;
      if (diff < SHORT_PRESS_TIME) {  // 短按=开关
        Light_Switch(0, !warm_switch);
      }
      // 重置本次触摸所有状态（关键：避免跨触摸周期干扰）
      touch_warm_flag = 0;
      warm_touch_tick = 0;
      static u32 last_adjust_tick = 0;
      last_adjust_tick = 0;
      warm_flipped_once = 0;
    }
  }

  // 白光触控检测（与暖光一致：长按翻转一次方向并持续步进，短按切换开关）
  static u8 white_flipped_once = 0;

  u8 white_touch_state = GPIO_ReadInputDataBit(TOUCH_PORT, TOUCH_WHITE_PIN);

  if (white_touch_state == 1) {  // 按下（触摸中）
    touch_white_flag = 1;
    if (white_touch_tick == 0) {  // 首次触摸：初始化计时+方向
      white_touch_tick = SysTick_GetTick();
      white_flipped_once = 0;
      // 重置调光计时
      static u32 last_white_adjust_tick = 0;
      last_white_adjust_tick = white_touch_tick;
    } else {
      u32 diff = SysTick_GetTick() - white_touch_tick;
      static u32 last_white_adjust_tick = 0;
      // 长按≥500ms，首次满足时翻转方向；且距离上次调光≥50ms → 持续步进调光
      if (diff >= LONG_PRESS_TIME && (SysTick_GetTick() - last_white_adjust_tick) >= 50) {
        if (!white_flipped_once) {
          white_touch_dir = !white_touch_dir;
          white_flipped_once = 1;
        }
        last_white_adjust_tick = SysTick_GetTick();

        u8 new_duty = 0;
        if (white_touch_dir == 1) {  // 调亮（持续步进）
          new_duty = white_duty + PWM_STEP;
        } else {  // 调暗（持续步进）
          new_duty = white_duty - PWM_STEP;
        }
        // 限制占空比范围
        new_duty = (new_duty < PWM_MIN_DUTY) ? PWM_MIN_DUTY : new_duty;
        new_duty = (new_duty > PWM_MAX_DUTY) ? PWM_MAX_DUTY : new_duty;
        // 更新全局变量
        white_duty = new_duty;
        // 设置PWM占空比
        Set_PWM_Duty(PWM_WHITE_CH, new_duty);
      }
    }
  } else {  // 白光松开（非触摸）
    if (touch_white_flag == 1) {
      u32 diff = SysTick_GetTick() - white_touch_tick;
      if (diff < SHORT_PRESS_TIME) {  // 短按=开关
        Light_Switch(1, !white_switch);
      }
      // 重置本次触摸所有状态
      touch_white_flag = 0;
      white_touch_tick = 0;
      static u32 last_white_adjust_tick = 0;
      last_white_adjust_tick = 0;
      white_flipped_once = 0;
    }
  }
}

/*********************************************************************
 * @函数名    TickTimer_Init
 * @功能      使用TIM4产生1ms节拍，更新g_systick_counter供状态机计时
 * @返回值    无
 ********************************************************************/
void TickTimer_Init(void) {
  TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
  NVIC_InitTypeDef NVIC_InitStructure = {0};

  // 72MHz(APB1*2) → 1kHz更新：预分频7200-1，自动重装10-1
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);

  TIM_TimeBaseStructure.TIM_Prescaler = 7200 - 1;   // 72MHz/7200 = 10kHz
  TIM_TimeBaseStructure.TIM_Period = 10 - 1;         // 10kHz/10 = 1kHz (1ms)
  TIM_TimeBaseStructure.TIM_ClockDivision = 0;
  TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInit(TIM4, &TIM_TimeBaseStructure);

  TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
  TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);
  TIM_Cmd(TIM4, ENABLE);

  NVIC_InitStructure.NVIC_IRQChannel = TIM4_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);
}

/*********************************************************************
 * @函数名    I2C1_Init
 * @功能      初始化I2C1（PB6=SCL，PB7=SDA）
 * @返回值    无
 ********************************************************************/
void I2C1_Init(void) {
  GPIO_InitTypeDef GPIO_InitStructure = {0};
  I2C_InitTypeDef I2C_InitStructure = {0};

  // 使能GPIOB和I2C1时钟
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

  // 配置PB6(SCL)和PB7(SDA)为开漏复用输出
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOB, &GPIO_InitStructure);

  // I2C1配置（100kHz）
  I2C_DeInit(I2C1);
  I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
  I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;
  I2C_InitStructure.I2C_OwnAddress1 = 0x00;
  I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
  I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
  I2C_InitStructure.I2C_ClockSpeed = 100000;
  I2C_Init(I2C1, &I2C_InitStructure);
  I2C_Cmd(I2C1, ENABLE);
}

/*********************************************************************
 * @函数名    Read_Ambient_Light
 * @功能      读取BH1750环境光值（lux）
 * @返回值    环境光值（lux）
 ********************************************************************/
u16 Read_Ambient_Light(void) {
  u8 buf[2] = {0};
  u16 lux = 0;

  // 发送测量指令（连续高分辨率模式）
  I2C_GenerateSTART(I2C1, ENABLE);
  while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_MODE_SELECT));

  I2C_Send7bitAddress(I2C1, LIGHT_SENSOR_ADDR, I2C_Direction_Transmitter);
  while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED));

  I2C_SendData(I2C1, 0x10);
  while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED));
  I2C_GenerateSTOP(I2C1, ENABLE);

  Delay_Ms(18);  // 等待测量完成

  // 读取测量数据
  I2C_GenerateSTART(I2C1, ENABLE);
  while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_MODE_SELECT));

  I2C_Send7bitAddress(I2C1, LIGHT_SENSOR_ADDR, I2C_Direction_Receiver);
  while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED));

  // 读取第一个字节
  while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_RECEIVED));
  buf[0] = I2C_ReceiveData(I2C1);
  I2C_AcknowledgeConfig(I2C1, ENABLE);

  // 读取第二个字节
  while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_RECEIVED));
  buf[1] = I2C_ReceiveData(I2C1);
  I2C_AcknowledgeConfig(I2C1, DISABLE);
  I2C_GenerateSTOP(I2C1, ENABLE);

  // 转换为lux值（BH1750公式：(buf[0]<<8 | buf[1])/1.2）
  lux = (u16)((buf[0] << 8 | buf[1]) / 1.2);
  return lux;
}

/*********************************************************************
 * @函数名    Auto_Light_Adjust
 * @功能      基于环境光自动调光（光越暗，灯越亮）
 * @返回值    无
 ********************************************************************/
void Auto_Light_Adjust(void) {
  // 关闭自动调光或触控中时，跳过调整
  if (auto_light_enable == 0 || touch_warm_flag || touch_white_flag) {
    return;
  }

  if ((SysTick_GetTick() - auto_light_tick) > AUTO_LIGHT_INTERVAL) {
    ambient_light = Read_Ambient_Light();
    // 环境光映射PWM：0-1000lux → PWM 100%-5%（线性映射）
    u8 target_warm = 0, target_white = 0;
    if (ambient_light > 1000) {
      ambient_light = 1000;
    }
    target_warm = 100 - (ambient_light / 10);
    target_white = 100 - (ambient_light / 10);
    
    // 仅灯开启时调整占空比
    if (warm_switch == 1) {
      Set_PWM_Duty(PWM_WARM_CH, target_warm);
    }
    if (white_switch == 1) {
      Set_PWM_Duty(PWM_WHITE_CH, target_white);
    }
    
    auto_light_tick = SysTick_GetTick();
  }
}

/*********************************************************************
 * @函数名    UART1_Init
 * @功能      初始化UART1（PA9=TX，PA10=RX）
 * @参数      baudrate - 串口波特率
 * @返回值    无
 ********************************************************************/
void UART1_Init(u32 baudrate) {
  GPIO_InitTypeDef GPIO_InitStructure = {0};
  USART_InitTypeDef USART_InitStructure = {0};

  // 使能USART1和GPIOA时钟
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

  // 配置PA9(TX)为复用推挽输出，PA10(RX)为浮空输入
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOA, &GPIO_InitStructure);

  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
  GPIO_Init(GPIOA, &GPIO_InitStructure);

  // UART1配置（8N1）
  USART_InitStructure.USART_BaudRate = baudrate;
  USART_InitStructure.USART_WordLength = USART_WordLength_8b;
  USART_InitStructure.USART_StopBits = USART_StopBits_1;
  USART_InitStructure.USART_Parity = USART_Parity_No;
  USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
  USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
  USART_Init(USART1, &USART_InitStructure);
  USART_Cmd(USART1, ENABLE);
}

/*********************************************************************
 * @函数名    Parse_ESP8266_Cmd
 * @功能      解析ESP8266下发的JSON指令
 * @参数      cmd - 指令字符串
 * @返回值    无
 ********************************************************************/
void Parse_ESP8266_Cmd(char *cmd) {
  char *p = NULL;
  // 解析设置PWM指令：{"cmd":"set_pwm","warm":20,"white":30}
  if (strstr(cmd, "\"cmd\":\"set_pwm\"")) {
    p = strstr(cmd, "\"warm\":");
    if (p) {
      u8 warm = atoi(p + 7);
      if (warm_switch == 1) {
        Set_PWM_Duty(PWM_WARM_CH, warm);
      }
      warm_last_duty = warm;
    }
    p = strstr(cmd, "\"white\":");
    if (p) {
      u8 white = atoi(p + 8);
      if (white_switch == 1) {
        Set_PWM_Duty(PWM_WHITE_CH, white);
      }
      white_last_duty = white;
    }
  }
  // 解析自动调光指令：{"cmd":"auto_light","enable":1}
  else if (strstr(cmd, "\"cmd\":\"auto_light\"")) {
    p = strstr(cmd, "\"enable\":");
    if (p) {
      auto_light_enable = atoi(p + 9);
    }
  }
  // 解析灯开关指令：{"cmd":"light_switch","warm":1,"white":0}
  else if (strstr(cmd, "\"cmd\":\"light_switch\"")) {
    p = strstr(cmd, "\"warm\":");
    if (p) {
      Light_Switch(0, atoi(p + 7));
    }
    p = strstr(cmd, "\"white\":");
    if (p) {
      Light_Switch(1, atoi(p + 8));
    }
  }
}

/*********************************************************************
 * @函数名    UART1_Receive_Cmd
 * @功能      接收ESP8266指令（带缓冲区）
 * @返回值    无
 ********************************************************************/
void UART1_Receive_Cmd(void) {
  static char cmd_buf[JSON_BUF_LEN] = {0};
  static u8 buf_idx = 0;

  if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) != RESET) {
    u8 data = USART_ReceiveData(USART1);
    // 指令结束符（换行/回车）
    if ((data == '\n' || data == '\r') && buf_idx > 0) {
      cmd_buf[buf_idx] = '\0';
      Parse_ESP8266_Cmd(cmd_buf);                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        
      buf_idx = 0;  // 重置缓冲区
    } else if (buf_idx < JSON_BUF_LEN - 1) {
      cmd_buf[buf_idx++] = data;
    }
  }
}

/*********************************************************************
 * @函数名    UART1_Send_Status
 * @功能      向ESP8266上报系统状态（JSON格式）
 * @返回值    无
 ********************************************************************/
void UART1_Send_Status(void) {
  char json[JSON_BUF_LEN] = {0};
  // 构造JSON字符串
  sprintf(json,
          "{\"pwm\":{\"warm\":%d,\"white\":%d},"
          "\"ambient\":%d,"
          "\"touch\":{\"warm\":%d,\"white\":%d},"
          "\"switch\":{\"warm\":%d,\"white\":%d},"
          "\"auto_light\":%d}\r\n",
          warm_duty, white_duty,
          ambient_light,
          touch_warm_flag, touch_white_flag,
          warm_switch, white_switch,
          auto_light_enable);

  // 逐字节发送JSON数据
  for (u8 i = 0; i < strlen(json); i++) {
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, json[i]);
  }
}

/*********************************************************************
 * @函数名    RGB_Reserve_Init
 * @功能      初始化预留RGB引脚（PA14/PA15）
 * @返回值    无
 ********************************************************************/
void RGB_Reserve_Init(void) {
  GPIO_InitTypeDef GPIO_InitStructure = {0};
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

  // 配置PA14/PA15为推挽输出
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_14 | GPIO_Pin_15;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOA, &GPIO_InitStructure);

  // 初始电平置高
  GPIO_SetBits(GPIOA, GPIO_Pin_14 | GPIO_Pin_15);
}

/*********************************************************************
 * @函数名    main
 * @功能      主函数（整合所有功能）
 * @返回值    无
 ********************************************************************/
int main(void) {
  u32 report_tick = 0;

  // 1. 配置系统时钟（8MHz → 72MHz）
  SystemClock_Config_8MHz();
  // 2. 初始化基础外设
  Delay_Init();
  USART_Printf_Init(UART_BAUDRATE);  // 调试串口（USART2）
  printf("CH32F103 智能灯带控制系统 V2.0\r\n");
  printf("系统时钟：%d\r\n", SystemCoreClock);

  // 3. 初始化功能外设
  PWM_Init();          // COB灯带PWM
  Touch_Init();        // 触控模块
  TickTimer_Init();    // 1ms系统节拍计时
  I2C1_Init();         // 光感I2C
  UART1_Init(UART_BAUDRATE);  // ESP8266通信
  RGB_Reserve_Init();  // 预留RGB引脚

  // 4. 初始状态：灯开启，占空比3%
  Light_Switch(0, 1);
  Light_Switch(1, 1);

  // 主循环
  while (1) {
    // 检测触控 → 调整PWM/开关
    Touch_Detect();
    
    // 自动调光（仅开启且无触控时生效）
    Auto_Light_Adjust();
    
    // 接收ESP8266指令
    UART1_Receive_Cmd();
    
    // 定时向ESP8266上报状态
    if ((SysTick_GetTick() - report_tick) > REPORT_INTERVAL) {
      if (auto_light_enable == 1) {
        ambient_light = Read_Ambient_Light();
      }
      UART1_Send_Status();
      report_tick = SysTick_GetTick();
    }

    Delay_Ms(10);
  }
}
