#define move_to_user_mode() \
__asm__ ("movl %%esp,%%eax\n\t" \ 
	"pushl $1f\n\t" \
	"iret\n" \
	"1:\tmovl $0x17,%%eax\n\t" \
	"movw %%ax,%%ds\n\t" \
	"movw %%ax,%%es\n\t" \
	"movw %%ax,%%fs\n\t" \
	"movw %%ax,%%gs" \
	:::"ax")

#define sti() __asm__ ("sti"::)
#define cli() __asm__ ("cli"::)
#define nop() __asm__ ("nop"::)

#define iret() __asm__ ("iret"::)

#define _set_gate(gate_addr,type,dpl,addr) \
__asm__ ("movw %%dx,%%ax\n\t" \
	"movw %0,%%dx\n\t" \
	"movl %%eax,%1\n\t" \
	"movl %%edx,%2" \
	: \
	: "i" ((short) (0x8000+(dpl<<13)+(type<<8))), \  //立即数
	"o" (*((char *) (gate_addr))), \				 //中断描述前4个字符的地址
	"o" (*(4+(char *) (gate_addr))), \				 //中断描述后4个字符的地址
	"d" ((char *) (addr)),"a" (0x00080000))			//"d" 对应edx。"a" 对应eax

#define set_intr_gate(n,addr) \
	_set_gate(&idt[n],14,0,addr)//这里以上的是给中断描述符拼写的一部分

#define set_trap_gate(n,addr) \
	_set_gate(&idt[n],15,0,addr)

#define set_system_gate(n,addr) \
	_set_gate(&idt[n],15,3,addr)

#define _set_seg_desc(gate_addr,type,dpl,base,limit) {\
	*(gate_addr) = ((base) & 0xff000000) | \
		(((base) & 0x00ff0000)>>16) | \
		((limit) & 0xf0000) | \
		((dpl)<<13) | \
		(0x00408000) | \
		((type)<<8); \
	*((gate_addr)+1) = (((base) & 0x0000ffff)<<16) | \
		((limit) & 0x0ffff); }

#define _set_tssldt_desc(n,addr,type) \      // 嵌入式汇编，参考trap_init的注释
__asm__ ("movw $104,%1\n\t" \				 //  将104 1101000 存入描述符的第1,2字节
	"movw %%ax,%2\n\t" \					 // 将tss 或者ldt的基地址的低16位存入描述符的第3,4字节
	"rorl $16,%%eax\n\t" \					 //循环右移16位，将高低字互换
	"movb %%al,%3\n\t" \					 //将互换完的的第一字节，即地址的第三字节存入第五字节
	"movb $" type ",%4\n\t" \				 //将0x89或者0x82存入第六字节
	"movb $0x00,%5\n\t" \					 // 将0x00存入第七字节
	"movb %%ah,%6\n\t" \					 // 将互换完的第二字节即地址的第四字节存入第八字节
	"rorl $16,%%eax" \						 //复原eax
	::"a" (addr), "m" (*(n)), "m" (*(n+2)), "m" (*(n+4)), \
	 "m" (*(n+5)), "m" (*(n+6)), "m" (*(n+7)) \
	)										//"m"(*(n))  是gdt第n项描述符地址开始的内存单元
											// n:gdt 的项值。 addr：tss或者ldt的地址。 0x89 对应tss，0x82对应ldt
#define set_tss_desc(n,addr) _set_tssldt_desc(((char *) (n)),((int)(addr)),"0x89")
#define set_ldt_desc(n,addr) _set_tssldt_desc(((char *) (n)),((int)(addr)),"0x82")

