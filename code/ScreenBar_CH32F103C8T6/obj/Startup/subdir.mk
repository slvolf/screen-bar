################################################################################
# MRS Version: 2.3.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
S_UPPER_SRCS += \
../Startup/startup_ch32f10x.S 

S_UPPER_DEPS += \
./Startup/startup_ch32f10x.d 

OBJS += \
./Startup/startup_ch32f10x.o 

DIR_OBJS += \
./Startup/*.o \

DIR_DEPS += \
./Startup/*.d \

DIR_EXPANDS += \
./Startup/*.233r.expand \


# Each subdirectory must supply rules for building sources it contributes
Startup/%.o: ../Startup/%.S
	@	arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -mthumb-interwork -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -x assembler-with-cpp -I"c:/Users/slvolf/Documents/MyFiles/大学/电子综合设计/大二上台灯/screen-bar/code/ScreenBar_CH32F103C8T6/CH32F103C8T/Startup" -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

