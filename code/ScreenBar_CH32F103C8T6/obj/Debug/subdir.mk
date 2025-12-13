################################################################################
# MRS Version: 2.3.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Debug/debug.c 

C_DEPS += \
./Debug/debug.d 

OBJS += \
./Debug/debug.o 

DIR_OBJS += \
./Debug/*.o \

DIR_DEPS += \
./Debug/*.d \

DIR_EXPANDS += \
./Debug/*.233r.expand \


# Each subdirectory must supply rules for building sources it contributes
Debug/%.o: ../Debug/%.c
	@	arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -mthumb-interwork -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -I"c:/Users/slvolf/Documents/MyFiles/大学/电子综合设计/大二上台灯/screen-bar/code/ScreenBar_CH32F103C8T6/CH32F103C8T/Core" -I"c:/Users/slvolf/Documents/MyFiles/大学/电子综合设计/大二上台灯/screen-bar/code/ScreenBar_CH32F103C8T6/CH32F103C8T/Debug" -I"c:/Users/slvolf/Documents/MyFiles/大学/电子综合设计/大二上台灯/screen-bar/code/ScreenBar_CH32F103C8T6/CH32F103C8T/Peripheral/inc" -I"c:/Users/slvolf/Documents/MyFiles/大学/电子综合设计/大二上台灯/screen-bar/code/ScreenBar_CH32F103C8T6/CH32F103C8T/Peripheral/src" -I"c:/Users/slvolf/Documents/MyFiles/大学/电子综合设计/大二上台灯/screen-bar/code/ScreenBar_CH32F103C8T6/CH32F103C8T/User" -std=gnu11 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

