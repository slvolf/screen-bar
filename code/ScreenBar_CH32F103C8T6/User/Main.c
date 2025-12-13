/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2019/10/15
 * Description        : Main program body (ADC+DMA + PWM+UART+I2C)
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/*
 *@Note 
 *1. ADC DMA sampling: ADC通道2(PA2)，规则组通过DMA连续采集1024次
 *2. 新增功能：PWM(TIM2 CH1/PA0)、UART1(PA9/PA10)、I2C1(PA6/PA7)
*/

#include "debug.h"

/* 全局变量 */ 
u16 TxBuf[1024]; 
s16 Calibrattion_Val = 0;  

/* 新增：PWM/UART/I2C相关宏 */
#define PWM_TIM        TIM2
#define PWM_CH1_PIN    GPIO_Pin_0
#define PWM_CH1_PORT   GPIOA
#define UART_BAUDRATE  115200
#define I2C_SCL_PIN    GPIO_Pin_6
#define I2C_SDA_PIN    GPIO_Pin_7
#define I2C_SCL_PORT   GPIOA
#define I2C_SDA_PORT   GPIOA

/*********************************************************************
 * @fn      ADC_Function_Init
 *
 * @brief   初始化ADC采集（保留原有逻辑）
 *
 * @return  none
 */
void ADC_Function_Init(void)
{
	ADC_InitTypeDef ADC_InitStructure={0}; 
	GPIO_InitTypeDef GPIO_InitStructure={0};

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE );	  
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE );	 
	RCC_ADCCLKConfig(RCC_PCLK2_Div8);	  
                        
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	ADC_DeInit(ADC1);  
	ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
	ADC_InitStructure.ADC_ScanConvMode = DISABLE;	
	ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;		
	ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;	
	ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;	
	ADC_InitStructure.ADC_NbrOfChannel = 1;	
	ADC_Init(ADC1, &ADC_InitStructure);	 

	ADC_DMACmd(ADC1, ENABLE);  
	ADC_Cmd(ADC1, ENABLE);
	
	ADC_ResetCalibration(ADC1);	
	while(ADC_GetResetCalibrationStatus(ADC1));		
	ADC_StartCalibration(ADC1);	 
	while(ADC_GetCalibrationStatus(ADC1));
	Calibrattion_Val = Get_CalibrationValue(ADC1);
}

/*********************************************************************
 * @fn      Get_ADC_Val
 *
 * @brief   获取ADC单次采样值（保留原有逻辑）
 *
 * @param   ch - ADC通道
 *
 * @return  val - 采样值
 */
u16 Get_ADC_Val(u8 ch)   
{
  u16 val;	
	
	ADC_RegularChannelConfig(ADC1, ch, 1, ADC_SampleTime_239Cycles5 );		  			    
	ADC_SoftwareStartConvCmd(ADC1, ENABLE);			
	 
	while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC ));
	val = ADC_GetConversionValue(ADC1);
	
	return val;
}

/*********************************************************************
 * @fn      DMA_Tx_Init
 *
 * @brief   初始化DMA（保留原有逻辑）
 *
 * @param   DMA_CHx - DMA通道
 *          ppadr - 外设地址
 *          memadr - 内存地址
 *          bufsize - 缓存大小
 *
 * @return  none
 */
void DMA_Tx_Init( DMA_Channel_TypeDef* DMA_CHx, u32 ppadr, u32 memadr, u16 bufsize)
{
	DMA_InitTypeDef DMA_InitStructure={0};

	RCC_AHBPeriphClockCmd( RCC_AHBPeriph_DMA1, ENABLE );
	
	DMA_DeInit(DMA_CHx);	
	DMA_InitStructure.DMA_PeripheralBaseAddr = ppadr;	
	DMA_InitStructure.DMA_MemoryBaseAddr = memadr;	
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;	
  DMA_InitStructure.DMA_BufferSize = bufsize; 
	DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;	
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;	
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;	
	DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;	
	DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;	
	DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;	
	DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;	
	DMA_Init( DMA_CHx, &DMA_InitStructure );	
}

/*********************************************************************
 * @fn      Get_ConversionVal
 *
 * @brief   ADC校准值补偿（保留原有逻辑）
 *
 * @param   val - 原始采样值
 *
 * @return  补偿后的值
 */
u16 Get_ConversionVal(s16 val)
{
	if((val+Calibrattion_Val)<0) return 0;
	if((Calibrattion_Val + val) > 4095||val==4095) return 4095;
	return (val+Calibrattion_Val);
}

/*********************************************************************
 * @fn      PWM_Init
 *
 * @brief   新增：初始化PWM（TIM2 CH1/PA0）
 *
 * @return  none
 */
void PWM_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure={0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure={0};
    TIM_OCInitTypeDef TIM_OCInitStructure={0};

    // 使能时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);

    // 配置PA0为复用推挽输出
    GPIO_InitStructure.GPIO_Pin = PWM_CH1_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PWM_CH1_PORT, &GPIO_InitStructure);

    // TIM2时基配置：72MHz/72=1MHz → 1000级PWM（1kHz频率）
    TIM_TimeBaseStructure.TIM_Period = 999;
    TIM_TimeBaseStructure.TIM_Prescaler = 71;
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

    // PWM模式1：CNT < CCR时输出高电平
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = 500; // 初始50%占空比
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(TIM2, &TIM_OCInitStructure);

    // 使能预装载
    TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM2, ENABLE);

    // 启动TIM2
    TIM_Cmd(TIM2, ENABLE);
}

/*********************************************************************
 * @fn      UART1_Init
 *
 * @brief   新增：初始化UART1（PA9/TX PA10/RX）
 *
 * @param   baudrate - 波特率
 *
 * @return  none
 */
void UART1_Init(u32 baudrate)
{
    GPIO_InitTypeDef GPIO_InitStructure={0};
    USART_InitTypeDef USART_InitStructure={0};

    // 使能时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

    // PA9(TX) 复用推挽输出
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // PA10(RX) 浮空输入
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // UART配置
    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);

    // 使能UART1
    USART_Cmd(USART1, ENABLE);
}

/*********************************************************************
 * @fn      I2C1_Init
 *
 * @brief   新增：初始化I2C1（PA6/SCL PA7/SDA）
 *
 * @return  none
 */
void I2C1_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure={0};
    I2C_InitTypeDef I2C_InitStructure={0};

    // 使能时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

    // PA6/SCL、PA7/SDA 开漏复用输出
    GPIO_InitStructure.GPIO_Pin = I2C_SCL_PIN | I2C_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // I2C配置：100kHz
    I2C_DeInit(I2C1);
    I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_OwnAddress1 = 0x00;
    I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_InitStructure.I2C_ClockSpeed = 100000;
    I2C_Init(I2C1, &I2C_InitStructure);

    // 使能I2C1
    I2C_Cmd(I2C1, ENABLE);
}

/*********************************************************************
 * @fn      Set_PWM_Duty
 *
 * @brief   新增：设置PWM占空比
 *
 * @param   duty - 占空比(0-100)
 *
 * @return  none
 */
void Set_PWM_Duty(u8 duty)
{
    if(duty > 100) duty = 100;
    u16 ccr_val = (duty * 999) / 100; // 对应ARR=999
    TIM_SetCompare1(TIM2, ccr_val);
}

/*********************************************************************
 * @fn      UART1_SendStr
 *
 * @brief   新增：UART1发送字符串
 *
 * @param   str - 字符串指针
 *
 * @return  none
 */
void UART1_SendStr(char *str)
{
    while(*str)
    {
        while(USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
        USART_SendData(USART1, *str++);
    }
}

/*********************************************************************
 * @fn      main
 *
 * @brief   主函数（保留ADC+DMA，新增PWM/UART/I2C初始化和测试）
 *
 * @return  none
 */
int main(void)
{
	u16 i;
    u8 pwm_duty = 50; // 初始PWM占空比
	
	SystemCoreClockUpdate();
	Delay_Init();
	USART_Printf_Init(115200); // 原有调试串口
	printf("SystemClk:%d\r\n",SystemCoreClock);
	printf( "ChipID:%08x\r\n", DBGMCU_GetCHIPID() );

    // 初始化新增功能
    PWM_Init();
    UART1_Init(UART_BAUDRATE);
    I2C1_Init();
    UART1_SendStr("CH32F103 PWM+UART+I2C Init OK\r\n");

	// 原有ADC+DMA初始化
	ADC_Function_Init();
	printf("CalibrattionValue:%d\n", Calibrattion_Val);
	
	DMA_Tx_Init( DMA1_Channel1, (u32)&ADC1->RDATAR, (u32)TxBuf, 1024 );  
	DMA_Cmd( DMA1_Channel1, ENABLE );		
	
	ADC_RegularChannelConfig(ADC1, ADC_Channel_2, 1, ADC_SampleTime_239Cycles5 );	
	ADC_SoftwareStartConvCmd(ADC1, ENABLE);			
    Delay_Ms(50);
	ADC_SoftwareStartConvCmd(ADC1, DISABLE);	
	
	// 打印ADC采样值（原有逻辑）
	for(i=0; i<1024; i++){
		printf( "%04d\r\n", Get_ConversionVal(TxBuf[i]));
		Delay_Ms(10);
	}

    // 主循环：PWM占空比渐变 + UART打印
	while(1)
    {
        // PWM占空比从0到100渐变
        for(pwm_duty=0; pwm_duty<=100; pwm_duty+=5)
        {
            Set_PWM_Duty(pwm_duty);
            UART1_SendStr("PWM Duty: ");
            // 打印占空比到UART1
            char duty_str[16];
            sprintf(duty_str, "%d%%\r\n", pwm_duty);
            UART1_SendStr(duty_str);
            Delay_Ms(500);
        }
        for(pwm_duty=100; pwm_duty>=0; pwm_duty-=5)
        {
            Set_PWM_Duty(pwm_duty);
            UART1_SendStr("PWM Duty: ");
            char duty_str[16];
            sprintf(duty_str, "%d%%\r\n", pwm_duty);
            UART1_SendStr(duty_str);
            Delay_Ms(500);
        }
    }
}