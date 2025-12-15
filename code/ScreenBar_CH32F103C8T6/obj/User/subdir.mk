################################################################################
# MRS Version: 2.3.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../User/Main.c \
../User/ch32f10x_it.c \
../User/system_ch32f10x.c 

C_DEPS += \
./User/Main.d \
./User/ch32f10x_it.d \
./User/system_ch32f10x.d 

OBJS += \
./User/Main.o \
./User/ch32f10x_it.o \
./User/system_ch32f10x.o 

DIR_OBJS += \
./User/*.o \

DIR_DEPS += \
./User/*.d \

DIR_EXPANDS += \
./User/*.233r.expand \


# Each subdirectory must supply rules for building sources it contributes
User/%.o: ../User/%.c
	@	arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -mthumb-interwork -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -I"c:/Users/slvolf/Documents/MyFiles/大学/电子综合设计/大二上台灯/screen-bar/code/ScreenBar_CH32F103C8T6/Core" -I"c:/Users/slvolf/Documents/MyFiles/大学/电子综合设计/大二上台灯/screen-bar/code/ScreenBar_CH32F103C8T6/Debug" -I"c:/Users/slvolf/Documents/MyFiles/大学/电子综合设计/大二上台灯/screen-bar/code/ScreenBar_CH32F103C8T6/Peripheral/inc" -I"c:/Users/slvolf/Documents/MyFiles/大学/电子综合设计/大二上台灯/screen-bar/code/ScreenBar_CH32F103C8T6/Peripheral/src" -I"c:/Users/slvolf/Documents/MyFiles/大学/电子综合设计/大二上台灯/screen-bar/code/ScreenBar_CH32F103C8T6/User" -std=gnu11 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

