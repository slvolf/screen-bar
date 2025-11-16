    .syntax unified
    .cpu cortex-m3  /* 适配Cortex-M3（CMSIS要求） */
    .thumb

    /* 中断向量表（CMSIS格式，可扩展其他中断） */
    .section .isr_vector, "a", %progbits
    .align 2
    .long _estack          /* 栈顶地址（来自链接脚本） */
    .long Reset_Handler    /* 复位处理函数 */
    .long NMI_Handler      /* NMI中断（CMSIS标准） */
    .long HardFault_Handler /* 硬件错误中断 */
    /* 其他中断可在此扩展（如SysTick、USART等） */

    /* 复位处理函数：初始化系统并跳转至main（兼容CMSIS） */
    .section .text.Reset_Handler, "ax", %progbits
    .align 2
Reset_Handler:
    /* 1. 复制.data段到RAM（CMSIS要求的初始化） */
    ldr r0, =_sdata
    ldr r1, =_edata
    ldr r2, =_sidata
copy_data:
    cmp r0, r1
    itt lt
    ldrlt r3, [r2], #4
    strlt r3, [r0], #4
    blt copy_data

    /* 2. 清零.bss段 */
    ldr r0, =_sbss
    ldr r1, =_ebss
    mov r2, #0
zero_bss:
    cmp r0, r1
    it lt
    strlt r2, [r0], #4
    blt zero_bss

    /* 3. 调用CMSIS系统初始化（可选，用于时钟配置等） */
    bl SystemInit

    /* 4. 跳转到main函数 */
    bl main
    b .

    /* 空中断处理函数（CMSIS占位） */
    .weak NMI_Handler
    NMI_Handler:
    b .

    .weak HardFault_Handler
    HardFault_Handler:
    b .

    /* 声明链接脚本符号（供CMSIS使用） */
    .extern _sidata
    .extern _sdata
    .extern _edata
    .extern _sbss
    .extern _ebss
    .extern _estack
    .extern SystemInit  /* CMSIS系统初始化函数（用户实现） */

    .end
