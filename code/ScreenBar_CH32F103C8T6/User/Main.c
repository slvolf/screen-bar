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

#include "debug.h"
#include "string.h"
#include "stdlib.h"
                                                    
// 避免与ch32f10x.h中的HSE_VALUE重定义冲突
#ifndef HSE_VALUE
#define HSE_VALUE        8000000U    // 8MHz外置晶振
#endif
#define SYS_CLK          72000000U   // 目标系统时钟：72MHz

// PWM配置
// 暖灯：TIM2硬件PWM(PA3)
// 白光：PA4无硬件PWM复用，使用TIM3中断软件PWM
#define PWM_FREQ         10000U      // 目标PWM频率：10kHz
#define PWM_ARR          1799U       // 72MHz/(3+1)/1800 = 10kHz (TIM2)
#define PWM_WARM_CH      TIM_Channel_4  // PA3-TIM2 CH4（暖灯）
#define PWM_WHITE_CH     TIM_Channel_1  // 白光通道标识（软件PWM）
#define PWM_MIN_DUTY     3           // 最小占空比（微弱光）
#define PWM_MAX_DUTY     100         // 最大占空比
#define PWM_STEP         1           // 调光步进值

// 白光软件PWM参数：TIM3 1MHz计数(1us/tick)，周期100us(10kHz)
#define WHITE_SOFTPWM_TIM       TIM3
#define WHITE_SOFTPWM_IRQn      TIM3_IRQn
#define WHITE_SOFTPWM_PSC       (72 - 1)   // 72MHz/72 = 1MHz
#define WHITE_SOFTPWM_ARR       (100 - 1)  // 100us周期 -> 10kHz
#define WHITE_GPIO_PORT         GPIOA
#define WHITE_GPIO_PIN          GPIO_Pin_4

// 触控模块（TTP223）配置
#define TOUCH_WARM_PIN   GPIO_Pin_14 // PB14（暖灯触控）
#define TOUCH_WHITE_PIN  GPIO_Pin_15 // PB15（白光触控）
#define TOUCH_PORT       GPIOB
#define TOUCH_DEBOUNCE   20          // 消抖时间（ms）
#define SHORT_PRESS_TIME 500         // 短按阈值（<500ms）
#define LONG_PRESS_TIME  500         // 长按阈值（≥500ms）
#define DIR_FLIP_TIME    2000        // 长按2秒翻转调光方向

// I2C光感模块（BH1750）配置
// 改用软件模拟I2C，引脚定义如下：
// 标准I2C1引脚：PB6-SCL, PB7-SDA
#define I2C_SCL_PIN      GPIO_Pin_6
#define I2C_SDA_PIN      GPIO_Pin_7
#define I2C_GPIO_PORT    GPIOB
#define I2C_RCC_PORT     RCC_APB2Periph_GPIOB

#define LIGHT_SENSOR_ADDR_7BIT 0x23 // ADDR=LOW→0x23，ADDR=HIGH→0x5C
#define LIGHT_SENSOR_ADDR      (LIGHT_SENSOR_ADDR_7BIT << 1)
#define AUTO_LIGHT_INTERVAL 500     // 自动调光间隔（ms）

// 软件I2C宏定义（经典实现：SCL/SDA为开漏输出，上拉电阻拉高；读ACK/读数据时将SDA切为输入）
#define I2C_SCL_H      GPIO_SetBits(I2C_GPIO_PORT, I2C_SCL_PIN)
#define I2C_SCL_L      GPIO_ResetBits(I2C_GPIO_PORT, I2C_SCL_PIN)
#define I2C_SDA_H      GPIO_SetBits(I2C_GPIO_PORT, I2C_SDA_PIN)
#define I2C_SDA_L      GPIO_ResetBits(I2C_GPIO_PORT, I2C_SDA_PIN)
#define I2C_SDA_READ   GPIO_ReadInputDataBit(I2C_GPIO_PORT, I2C_SDA_PIN)

// BH1750/I2C调试开关
#define BH1750_DEBUG          1
#define BH1750_DEBUG_VERBOSE  1     // 置1会打印更多“成功路径”的细节

// ESP8266串口通信配置
#define UART_BAUDRATE    9600
#define REPORT_INTERVAL  100        // 状态上报间隔（ms）
#define JSON_BUF_LEN     256        // JSON缓冲区长度
#define UART1_RX_BUF_SIZE 256       // UART1 环形缓冲区长度

// 易失性关键字兼容定义
#ifndef __IO
#define __IO volatile
#endif

// 全局SysTick计数器（毫秒级）
__IO uint32_t g_systick_counter = 0;
// UART1 最近一次接收字节的时间戳（ms），用于在接收期间暂缓发送，避免TX打断RX
__IO uint32_t uart1_last_rx_tick = 0;

// UART1 环形接收缓冲区（中断填充，主循环读取）
static volatile u8 uart1_rx_buf[UART1_RX_BUF_SIZE] = {0};
static volatile u16 uart1_rx_head = 0;
static volatile u16 uart1_rx_tail = 0;

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

// 白光软件PWM：中断读取该占空比(0-100)
volatile u8 g_white_soft_duty = PWM_MIN_DUTY;

#if BH1750_DEBUG
#define BH1750_LOG(fmt, ...) \
  do { \
    printf("[BH1750 %lu] " fmt "\r\n", (unsigned long)SysTick_GetTick(), ##__VA_ARGS__); \
  } while (0)
#else
#define BH1750_LOG(...) do { } while (0)
#endif

// 软件I2C延时 (放慢速度，确保信号稳定)
static void Soft_I2C_Delay(void) {
    Delay_Us(50); // 增加延时到50us (约10kHz)，适应10k弱上拉
}

static void Soft_I2C_SDA_OutOD(void) {
  GPIO_InitTypeDef GPIO_InitStructure = {0};
  GPIO_InitStructure.GPIO_Pin = I2C_SDA_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(I2C_GPIO_PORT, &GPIO_InitStructure);
}

static void Soft_I2C_SDA_InPU(void) {
  GPIO_InitTypeDef GPIO_InitStructure = {0};
  GPIO_InitStructure.GPIO_Pin = I2C_SDA_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(I2C_GPIO_PORT, &GPIO_InitStructure);
}

static void Soft_I2C_GPIO_Init(void) {
  GPIO_InitTypeDef GPIO_InitStructure = {0};

  RCC_APB2PeriphClockCmd(I2C_RCC_PORT, ENABLE);

  // SCL：开漏输出
  GPIO_InitStructure.GPIO_Pin = I2C_SCL_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(I2C_GPIO_PORT, &GPIO_InitStructure);

  // SDA：默认开漏输出（读ACK/读数据时会切换为输入）
  Soft_I2C_SDA_OutOD();

  // 释放总线为高
  I2C_SCL_H;
  I2C_SDA_H;
  Soft_I2C_Delay();
}

// 软件I2C起始信号
static void Soft_I2C_Start(void) {
  Soft_I2C_SDA_OutOD();
    I2C_SDA_H;
    I2C_SCL_H;
    Soft_I2C_Delay();
    I2C_SDA_L;
    Soft_I2C_Delay();
    I2C_SCL_L;
}

// 软件I2C停止信号
static void Soft_I2C_Stop(void) {
  Soft_I2C_SDA_OutOD();
    I2C_SDA_L;
    I2C_SCL_H;
    Soft_I2C_Delay();
    I2C_SDA_H;
    Soft_I2C_Delay();
}

// 软件I2C等待应答
static u8 Soft_I2C_WaitAck(void) {
    u8 ack = 0;
  Soft_I2C_SDA_InPU(); // 释放SDA
    Soft_I2C_Delay();
    I2C_SCL_H;
    Soft_I2C_Delay();
    if (I2C_SDA_READ) {
        ack = 1; // NACK
    } else {
        ack = 0; // ACK
    }
    I2C_SCL_L;
  Soft_I2C_SDA_OutOD();
    return ack;
}

// 软件I2C发送字节
static void Soft_I2C_SendByte(u8 byte) {
    u8 i;
  Soft_I2C_SDA_OutOD();
    for (i = 0; i < 8; i++) {
        if (byte & 0x80) {
            I2C_SDA_H;
        } else {
            I2C_SDA_L;
        }
        byte <<= 1;
        Soft_I2C_Delay();
        I2C_SCL_H;
        Soft_I2C_Delay();
        I2C_SCL_L;
        Soft_I2C_Delay();
    }
}

// 软件I2C读取字节
static u8 Soft_I2C_ReadByte(u8 ack) {
    u8 i, byte = 0;
  Soft_I2C_SDA_InPU();
    for (i = 0; i < 8; i++) {
        I2C_SCL_H;
        Soft_I2C_Delay(); // 等待SCL上升
        byte <<= 1;
        if (I2C_SDA_READ) {
            byte |= 0x01;
        }
        I2C_SCL_L;
        Soft_I2C_Delay();
    }
    
    // 发送应答
  Soft_I2C_SDA_OutOD();
    if (ack) {
        I2C_SDA_L; // ACK
    } else {
        I2C_SDA_H; // NACK
    }
    Soft_I2C_Delay();
    I2C_SCL_H;
    Soft_I2C_Delay();
    I2C_SCL_L;
    I2C_SDA_H; // 释放
    
    return byte;
}

// 全局变量：探测到的8位地址（写地址）
static u8 g_bh1750_addr = LIGHT_SENSOR_ADDR;

static int BH1750_Ping(u8 addr) {
  Soft_I2C_Start();
  Soft_I2C_SendByte(addr);
  if (Soft_I2C_WaitAck()) {
    Soft_I2C_Stop();
    return 0;
  }
  Soft_I2C_Stop();
  return 1;
}

static int BH1750_WriteCmd(u8 cmd) {
  Soft_I2C_Start();
  Soft_I2C_SendByte(g_bh1750_addr);
  if (Soft_I2C_WaitAck()) { Soft_I2C_Stop(); return 0; }
  Soft_I2C_SendByte(cmd);
  if (Soft_I2C_WaitAck()) { Soft_I2C_Stop(); return 0; }
  Soft_I2C_Stop();
  return 1;
}

static int BH1750_ReadRaw(u16 *raw) {
  u8 hi = 0, lo = 0;
  Soft_I2C_Start();
  Soft_I2C_SendByte(g_bh1750_addr | 0x01);
  if (Soft_I2C_WaitAck()) { Soft_I2C_Stop(); return 0; }
  hi = Soft_I2C_ReadByte(1);
  lo = Soft_I2C_ReadByte(0);
  Soft_I2C_Stop();
  *raw = ((u16)hi << 8) | lo;
  return 1;
}

static int BH1750_Detect(void) {
  if (BH1750_Ping(0x23 << 1)) { g_bh1750_addr = (0x23 << 1); return 1; }
  if (BH1750_Ping(0x5C << 1)) { g_bh1750_addr = (0x5C << 1); return 1; }
  return 0;
}

static void BH1750_SelfTestOnce(void) {
  if (!BH1750_Detect()) {
    BH1750_LOG("FAIL: No sensor found!");
    return;
  }
  BH1750_LOG("FOUND Sensor addr=0x%02X", g_bh1750_addr >> 1);
}

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
 * @功能      初始化PWM：暖灯TIM2硬件PWM(PA3)，白光PA4软件PWM(TIM3中断)
 * @返回值    无
 ********************************************************************/
void PWM_Init(void) {
  GPIO_InitTypeDef GPIO_InitStructure = {0};
  TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
  TIM_OCInitTypeDef TIM_OCInitStructure = {0};
  NVIC_InitTypeDef NVIC_InitStructure = {0};

  // 使能TIM2/TIM3和GPIOA时钟
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3, ENABLE);
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);

  // PA3：TIM2 CH4 硬件PWM(复用推挽)
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOA, &GPIO_InitStructure);

  // PA4：白光软件PWM(普通推挽输出)
  GPIO_InitStructure.GPIO_Pin = WHITE_GPIO_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(WHITE_GPIO_PORT, &GPIO_InitStructure);
  GPIO_ResetBits(WHITE_GPIO_PORT, WHITE_GPIO_PIN);

  // TIM2时基配置（暖灯硬件PWM）
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

  // TIM3：白光软件PWM时基(1MHz计数，10kHz周期)
  TIM_DeInit(WHITE_SOFTPWM_TIM);
  TIM_TimeBaseStructure.TIM_Prescaler = WHITE_SOFTPWM_PSC;
  TIM_TimeBaseStructure.TIM_Period = WHITE_SOFTPWM_ARR;
  TIM_TimeBaseStructure.TIM_ClockDivision = 0;
  TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInit(WHITE_SOFTPWM_TIM, &TIM_TimeBaseStructure);

  // 使用CH1比较中断作为“关断点”，不需要输出到引脚
  TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_Timing;
  TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Disable;
  TIM_OCInitStructure.TIM_Pulse = 0;
  TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
  TIM_OC1Init(WHITE_SOFTPWM_TIM, &TIM_OCInitStructure);

  TIM_ClearITPendingBit(WHITE_SOFTPWM_TIM, TIM_IT_Update | TIM_IT_CC1);
  TIM_ITConfig(WHITE_SOFTPWM_TIM, TIM_IT_Update, ENABLE);
  TIM_ITConfig(WHITE_SOFTPWM_TIM, TIM_IT_CC1, ENABLE);

  NVIC_InitStructure.NVIC_IRQChannel = WHITE_SOFTPWM_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);

  // 使能TIM2/TIM3
  TIM_ARRPreloadConfig(TIM2, ENABLE);
  TIM_Cmd(TIM2, ENABLE);
  TIM_Cmd(WHITE_SOFTPWM_TIM, ENABLE);
}

/*********************************************************************
 * @函数名    Set_PWM_Duty
 * @功能      设置PWM占空比（限制3%-100%）
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
  
  if (ch == PWM_WARM_CH) {
    u16 ccr_val = (duty * PWM_ARR) / 100;
    TIM_SetCompare4(TIM2, ccr_val);
    warm_duty = duty;
  } else if (ch == PWM_WHITE_CH) {
    // 白光：软件PWM仅更新占空比，由TIM3中断驱动PA4
    g_white_soft_duty = duty;
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
      g_white_soft_duty = 0;
      GPIO_ResetBits(WHITE_GPIO_PORT, WHITE_GPIO_PIN);
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
  } else {  // 松开（非触摸）
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

  // 白光触控检测
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


void I2C1_Init(void) {
  Soft_I2C_GPIO_Init();
  BH1750_LOG("Soft I2C Init OK");
}

/*********************************************************************
 * @函数名    Read_Ambient_Light
 * @功能      读取BH1750环境光值（lux）
 * @返回值    环境光值（lux）
 ********************************************************************/
u16 Read_Ambient_Light(void) {
  u8 buf[2] = {0};
  u16 lux = 0;

  if (!BH1750_Detect()) {
    return ambient_light;
  }

  // 1. Power On
  if (!BH1750_WriteCmd(0x01)) return ambient_light;
  Delay_Ms(10);

  // 2. Reset
  if (!BH1750_WriteCmd(0x07)) return ambient_light;
  Delay_Ms(10);

  // 3. Continuous H-Res Mode
  if (!BH1750_WriteCmd(0x10)) return ambient_light;
  Delay_Ms(200);

  // 4. Read
  u16 raw = 0;
  if (!BH1750_ReadRaw(&raw)) return ambient_light;
  buf[0] = (u8)(raw >> 8);
  buf[1] = (u8)(raw & 0xFF);

  // 转换为lux值
  lux = (u16)((((u16)buf[0] << 8) | buf[1]) / 1.2);

#if BH1750_DEBUG_VERBOSE
  BH1750_LOG("Read OK raw=%02X %02X lux=%u", buf[0], buf[1], lux);
#endif
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
    // 环境光映射PWM：0-1000lux → PWM 100%-3%（线性映射）
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
  NVIC_InitTypeDef NVIC_InitStructure = {0};

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
  USART_ITConfig(USART1, USART_IT_RXNE, ENABLE); // 使能接收中断

  NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);

  USART_Cmd(USART1, ENABLE);
}

/* 将远端占空比写入并按需立即更新 */
static void Apply_Remote_PWM(u8 warm, u8 white) {
  if (warm > PWM_MAX_DUTY) warm = PWM_MAX_DUTY;
  if (warm < PWM_MIN_DUTY) warm = PWM_MIN_DUTY;
  if (white > PWM_MAX_DUTY) white = PWM_MAX_DUTY;
  if (white < PWM_MIN_DUTY) white = PWM_MIN_DUTY;

  warm_last_duty = warm;
  white_last_duty = white;

  if (warm_switch) {
    Set_PWM_Duty(PWM_WARM_CH, warm);
  }
  if (white_switch) {
    Set_PWM_Duty(PWM_WHITE_CH, white);
  }

  // printf("[OK] warm=%u white=%u\r\n", warm, white);
}

/*********************************************************************
 * @函数名    Parse_ESP8266_Cmd
 * @功能      解析ESP8266下发的指令（JSON或简化PWM格式）
 * @参数      cmd - 指令字符串
 * @返回值    无
 ********************************************************************/
void Parse_ESP8266_Cmd(char *cmd) {
  char *p = NULL;

  // 解析设置PWM指令：{"cmd":"set_pwm","warm":20,"white":30}
  if (strstr(cmd, "\"cmd\":\"set_pwm\"")) {
    u8 warm = warm_last_duty;
    u8 white = white_last_duty;
    p = strstr(cmd, "\"warm\":");
    if (p) {
      warm = (u8)atoi(p + 7);
    }
    p = strstr(cmd, "\"white\":");
    if (p) {
      white = (u8)atoi(p + 8);
    }
    Apply_Remote_PWM(warm, white);
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
  // 新增简化格式：PWM:<warm>,<white> 例："PWM:35,80"
  else if (strncmp(cmd, "PWM:", 4) == 0) {
    int warm = 0, white = 0;
    if (sscanf(cmd + 4, "%d,%d", &warm, &white) == 2) {
      Apply_Remote_PWM((u8)warm, (u8)white);
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

  // 从环形缓冲读取（由 USART1_IRQHandler 填充），确保不中断丢字节
  // u16 batch_len = 0;
  while (uart1_rx_head != uart1_rx_tail) {
    u8 data = uart1_rx_buf[uart1_rx_tail];
    uart1_rx_tail = (uart1_rx_tail + 1) % UART1_RX_BUF_SIZE;
    // batch_len++;

    if (data == '\r' || data == '\n') {
      if (buf_idx > 0) {
        cmd_buf[buf_idx] = '\0';
        Parse_ESP8266_Cmd(cmd_buf);
        buf_idx = 0;
      }
      continue;
    }

    if (buf_idx < (JSON_BUF_LEN - 1)) {
      cmd_buf[buf_idx++] = data;
    } else {
      // 缓冲溢出则丢弃本条指令
      buf_idx = 0;
    }
  }

  // if (batch_len > 0) {
  //   printf("[U1RX] batch=%u buf_len=%u\r\n", (unsigned)batch_len, (unsigned)buf_idx);
  // }
}

/*********************************************************************
 * @函数名    UART1_Send_Status
 * @功能      向ESP8266上报系统状态（JSON格式）
 * @返回值    无
 ********************************************************************/
void UART1_Send_Status(void) {
  char json[JSON_BUF_LEN] = {0};
  // 接收优先：若最近250ms内有接收活动，则跳过本次上报，避免TX打断RX
  if ((SysTick_GetTick() - uart1_last_rx_tick) < 250) {
    return;
  }
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
  BH1750_SelfTestOnce();// BH1750/I2C自检（建议断开ESP8266，避免与UART1混线）
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

// USART1 接收中断：填充环形缓冲，避免主循环阻塞导致丢字节
void USART1_IRQHandler(void) {
  if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
    u8 data = USART_ReceiveData(USART1);
    uart1_last_rx_tick = SysTick_GetTick();

    u16 next_head = (uart1_rx_head + 1) % UART1_RX_BUF_SIZE;
    if (next_head != uart1_rx_tail) {
      uart1_rx_buf[uart1_rx_head] = data;
      uart1_rx_head = next_head;
    } else {
      // 缓冲满则丢弃最旧数据，确保不阻塞硬件
      uart1_rx_tail = (uart1_rx_tail + 1) % UART1_RX_BUF_SIZE;
      uart1_rx_buf[uart1_rx_head] = data;
      uart1_rx_head = next_head;
    }
  }
}