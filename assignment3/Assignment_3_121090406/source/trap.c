#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fcntl.h"


struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  }
  // // TODO: page fault handling
  // else if(r_scause() == 13 || r_scause() == 15) {
  //   uint64 va = r_stval();//get virtual address
  //   struct vma* vma = 0;

  //   if(va>=p->sz || va<=p->trapframe->sp) //chekc if va is in heap
  //     goto err;

  //   for(int i=0; i < VMASIZE; i++) // find the vma
  //   {
  //     if(va>=p->vma[i].addr && va < p->vma[i].addr + p->vma[i].len)
  //     {
  //       vma = &p->vma[i];
  //       break;
  //     }
  //   }

  //   if(!vma) goto err;// not found

  //   va = PGROUNDDOWN(va);// adjust according to page size

  //   // allocate memory
  //   char* mem = kalloc();
  //   if(mem == 0) goto err;

  //   memset(mem, 0, PGSIZE);//initialize
  //   mapfile(vma->f, mem,va-vma->addr+vma->offset);// copy file in disk to virtual memory

  //   //set protection level
  //   int flags = PTE_U;
  //   if(vma->prot & PROT_READ) flags |= PTE_R;
  //   if(vma->prot & PROT_WRITE) flags |= PTE_W;
  //   if(vma->prot & PROT_EXEC) flags |= PTE_X;
  //   // Create page table entries for virtual addresses starting at va that refer to physical addresses starting at mem
  //   if(mappages(p->pagetable, va, PGSIZE, (uint64)mem, flags) != 0)// map pages
  //     kfree(mem);// fail to create, and free the page of physical memory
  // }
  else if(r_scause() == 13 || r_scause() == 15) {
    // uint64 va = r_stval();
    // printf("Now, after mmap, we get a page fault\n");
    uint64 va = r_stval();  // 获取导致异常的虚拟地址
    struct vma* vma = 0;
    // 1. 查找有效的 VMA（虚拟内存区域）
    for (int i = 0; i < VMASIZE; i++) {
        if (p->vma[i].used && va >= p->vma[i].addr && va < p->vma[i].addr + p->vma[i].length) {
            vma = &p->vma[i];
            break;
        }
    }

    if (!vma) {
        // 如果没有找到对应的 VMA，发生错误
        goto err;
    }

    // 2. 读取文件中的一页数据到物理内存
    // 由于发生了页面错误，意味着该页未被映射，所以下面我们需要从文件中读取数据
    char* mem = kalloc(); // 分配物理内存
    if (mem == 0) {
        // 如果内存不足，结束进程
        goto err;
    }

    memset(mem, 0, PGSIZE); // 将物理内存初始化为 0
    int offset = va - vma->addr + vma->offset;  // 计算文件的偏移量
    mapfile(vma->file, mem, offset); // 从文件中读取数据到内存

    // 3. 将内存页映射到用户地址空间
    // 设置页面的权限
    int flags = PTE_U; // 用户可访问
    if (vma->prot & PROT_READ) {
      flags |= PTE_R;  // 如果有读权限，设置读权限
    }
    if (vma->prot & PROT_WRITE) {
      flags |= PTE_W; // 如果有写权限，设置写权限
    }
    if (vma->prot & PROT_EXEC) {
      flags |= PTE_X;  // 如果有执行权限，设置执行权限
    }

    // 将虚拟地址 va 映射到物理地址 mem，并设置权限
    if (mappages(p->pagetable, va, PGSIZE, (uint64)mem, flags) == -1) {
        // 如果映射失败，释放分配的内存
        kfree(mem); //avoid memory leak
        goto err;
    }

  }
  else {
  err:
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to userret in trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

