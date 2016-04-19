/*
 *  linux/kernel/fork.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also system_call.s), and some misc functions ('verify_area').
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/mm.c': 'copy_page_tables()'
 */
#include <string.h>
#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>

extern void write_verify(unsigned long address);

long last_pid=0;

void verify_area(void * addr,int size)
{
	unsigned long start;

	start = (unsigned long) addr;
	size += start & 0xfff;
	start &= 0xfffff000;
	start += get_base(current->ldt[2]);
	while (size>0) {
		size -= 4096;
		write_verify(start);
		start += 4096;
	}
}

int copy_mem(int nr,struct task_struct * p)   //这里设置子进程的代码段，数据段及创建，复制子进程的的第一个页表
{
	unsigned long old_data_base,new_data_base,data_limit;
	unsigned long old_code_base,new_code_base,code_limit;
	
	//取子进程的代码，数据段限制长度
	code_limit=get_limit(0x0f);				//0x0f即1111：代码段，LDT， 3特权级别
	data_limit=get_limit(0x17);				//0x17即10111： 数据段，LDT，3特权级
	//获取父进程 （现在是进程0）的代码段，数据段基地址
	old_code_base = get_base(current->ldt[1]);
	old_data_base = get_base(current->ldt[2]);
	if (old_data_base != old_code_base)
		panic("We don't support separate I&D");
	if (data_limit < code_limit)
		panic("Bad data_limit");
	new_data_base = new_code_base = nr * 0x4000000;		//现在nr变成1,0x4000000是64MB
	p->start_code = new_code_base;
	set_base(p->ldt[1],new_code_base);					//设置子进程代码段基地址
	set_base(p->ldt[2],new_data_base);					//设置子进程数据段基地址
	if (copy_page_tables(old_data_base,new_data_base,data_limit)) {
		printk("free_page_tables: from copy_mem\n");
		free_page_tables(new_data_base,data_limit);
		return -ENOMEM;
	}
	return 0;
}

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in it's entirety.
 */
int copy_process(int nr,long ebp,long edi,long esi,long gs,long none,
		long ebx,long ecx,long edx,
		long fs,long es,long ds,
		long eip,long cs,long eflags,long esp,long ss)
	//上面的参数是int 0x80，system_call,sys_fork多次压栈的结果。其顺序是完全一致的
{
	struct task_struct *p;
	int i;
	struct file *f;

	//在16MB内存的最高端获取一页，这里使用强制类型转换就是将这个页当成是task_union来用。
	p = (struct task_struct *) get_free_page();
	if (!p)
		return -EAGAIN;
	task[nr] = p;
	
	// NOTE!: the following statement now work with gcc 4.3.2 now, and you
	// must compile _THIS_ memcpy without no -O of gcc.#ifndef GCC4_3
	*p = *current;	/* NOTE! this doesn't copy the supervisor stack */
	p->state = TASK_UNINTERRUPTIBLE;
	p->pid = last_pid;
	p->father = current->pid;
	p->counter = p->priority;
	p->signal = 0;
	p->alarm = 0;
	p->leader = 0;		/* process leadership doesn't inherit */
	p->utime = p->stime = 0;
	p->cutime = p->cstime = 0;
	p->start_time = jiffies;
	p->tss.back_link = 0;
	p->tss.esp0 = PAGE_SIZE + (long) p;
	p->tss.ss0 = 0x10;
	p->tss.eip = eip;							//重要！ 就是参数的EIP，是int 0x80压栈,指向的是：if（_res>=0)
	p->tss.eflags = eflags;						
	p->tss.eax = 0;								//重要！决定mian()函数中if(！fork())后面的分枝
	p->tss.ecx = ecx;
	p->tss.edx = edx;
	p->tss.ebx = ebx;
	p->tss.esp = esp;
	p->tss.ebp = ebp;
	p->tss.esi = esi;
	p->tss.edi = edi;
	p->tss.es = es & 0xffff;
	p->tss.cs = cs & 0xffff;
	p->tss.ss = ss & 0xffff;
	p->tss.ds = ds & 0xffff;
	p->tss.fs = fs & 0xffff;
	p->tss.gs = gs & 0xffff;
	p->tss.ldt = _LDT(nr); 						//挂接子进程的LDT
	p->tss.trace_bitmap = 0x80000000;
	if (last_task_used_math == current)
		__asm__("clts ; fnsave %0"::"m" (p->tss.i387));
	if (copy_mem(nr,p)) {						//前面定义了copy_mem的函数，这里设置子进程的代码段，数据段及创建，复制子进程的的第一个页表
		task[nr] = NULL;//现在采用的模式已经不会出现这样的情况了。
		free_page((long) p);
		return -EAGAIN;
	}
	for (i=0; i<NR_OPEN;i++)		//下面将父进程相关文件属性的引用计数加1，表示父子进程共享文件
		if ((f=p->filp[i]))
			f->f_count++;
	if (current->pwd)
		current->pwd->i_count++;
	if (current->root)
		current->root->i_count++;
	if (current->executable)
		current->executable->i_count++;
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
	p->state = TASK_RUNNING;	/* do this last, just in case */
	return last_pid;
}

int find_empty_process(void)					//为新创建的进程找到一个闲置的空间。这里面NR_TASK是64
{
	int i;

	repeat:
		if ((++last_pid)<0) last_pid=1;			//如果++后面last_pid溢出，则置1
		for(i=0 ; i<NR_TASKS ; i++)				//现在，++后面last_pid为1.找到有效的last_pid
			if (task[i] && task[i]->pid == last_pid) goto repeat;
	for(i=1 ; i<NR_TASKS ; i++)					//返回第一个空闲的i
		if (!task[i])
			return i;
	return -EAGAIN;								//这里EAGAIN为11
}
