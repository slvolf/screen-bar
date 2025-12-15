################################################################################
# MRS Version: 2.3.0
# Automatically-generated file. Do not edit!
################################################################################
-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include User/subdir.mk
-include Startup/subdir.mk
-include Peripheral/src/subdir.mk
-include Debug/subdir.mk
-include Core/subdir.mk
-include subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(ASM_DEPS)),)
-include $(ASM_DEPS)
endif
ifneq ($(strip $(ASM_UPPER_DEPS)),)
-include $(ASM_UPPER_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 
SECONDARY_FLASH += \
ScreenBar_CH32F103C8T6.hex \

SECONDARY_LIST += \
ScreenBar_CH32F103C8T6.lst \

SECONDARY_SIZE += \
ScreenBar_CH32F103C8T6.siz \


# All Target
all: 
	$(MAKE) --no-print-directory main-build 

main-build: ScreenBar_CH32F103C8T6.elf secondary-outputs

# Tool invocations
ScreenBar_CH32F103C8T6.elf: $(OBJS) $(USER_OBJS_ESCAPE)
	@	arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -mthumb-interwork -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -T "c:/Users/slvolf/Documents/MyFiles/大学/电子综合设计/大二上台灯/screen-bar/code/ScreenBar_CH32F103C8T6/LD/CH32F103C6T6.ld" -nostartfiles -Xlinker --gc-sections -Wl,-Map,"ScreenBar_CH32F103C8T6.map" --specs=nano.specs --specs=nosys.specs -o "ScreenBar_CH32F103C8T6.elf" $(OBJS) $(LIBS)
ScreenBar_CH32F103C8T6.hex: ScreenBar_CH32F103C8T6.elf
	@	arm-none-eabi-objcopy -O ihex "ScreenBar_CH32F103C8T6.elf" "ScreenBar_CH32F103C8T6.hex"
ScreenBar_CH32F103C8T6.lst: ScreenBar_CH32F103C8T6.elf
	@	arm-none-eabi-objdump --source --all-headers --demangle --disassemble "ScreenBar_CH32F103C8T6.elf" > "ScreenBar_CH32F103C8T6.lst"
ScreenBar_CH32F103C8T6.siz: ScreenBar_CH32F103C8T6.elf
	arm-none-eabi-size --format=berkeley "ScreenBar_CH32F103C8T6.elf"

# Other Targets
clean:
	-$(RM) $(DIR_OBJS) $(SECONDARY_FLASH)$(SECONDARY_LIST)$(SECONDARY_SIZE) ScreenBar_CH32F103C8T6.elf
	-$(RM) $(DIR_EXPANDS) $(CALLGRAPH_DOT)
	-$(RM) $(DIR_DEPS)
secondary-outputs: $(SECONDARY_FLASH) $(SECONDARY_LIST) $(SECONDARY_SIZE)

.PHONY: all clean dependents

-include ../makefile.targets