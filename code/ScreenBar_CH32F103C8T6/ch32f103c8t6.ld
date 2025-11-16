/* CH32F103C8T6：64KB Flash，20KB RAM */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 20K
}

ENTRY(Reset_Handler)  /* 程序入口：复位向量 */

SECTIONS
{
    /* 中断向量表（CMSIS要求放在Flash起始位置） */
    .isr_vector : { KEEP(*(.isr_vector)) } >FLASH

    /* 代码段（包含CMSIS内核代码和用户程序） */
    .text : {
        *(.text)           /* 用户代码 */
        *(.text*)          /* 扩展代码 */
        *(.rodata)         /* 只读数据 */
        *(.rodata*)        /* 扩展只读数据 */
    } >FLASH

    /* 初始化数据段（从Flash复制到RAM） */
    .data : AT(LOADADDR(.text) + SIZEOF(.text))
    {
        /* _sidata 指向 Flash 中 .data 的初始化数据起始处，供 startup 使用 */
        _sidata = LOADADDR(.data);
        _sdata = .;
        *(.data)
        _edata = .;
    } >RAM

    /* 未初始化数据段（BSS） */
    .bss : {
        _sbss = .;
        *(.bss)
        _ebss = .;
    } >RAM

    /* 栈顶地址（RAM末尾，CMSIS依赖此定义） */
    _estack = ORIGIN(RAM) + LENGTH(RAM);
}