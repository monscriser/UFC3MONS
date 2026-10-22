#pragma once
#include "ufc_linux_native.h"
#include <elf.h>
#include <sys/procfs.h>
#include <sys/user.h>
#include <sys/uio.h>
namespace ufc_linux_crash {
inline bool WriteAll(int fd,const void* data,size_t length){
 auto* bytes=static_cast<const char*>(data);while(length){auto n=write(fd,bytes,length);if(n<0&&errno==EINTR)continue;if(n<=0)return false;bytes+=n;length-=size_t(n);}return true;
}
// Native ELF core snapshot: faulting thread registers, signal and up to 64 KiB
// of its stack. Separate .maps companion records modules. No fatal fault is
// swallowed and no ptrace or system-wide core-dump setting is changed.
inline bool Capture(const char* path,const ufc_linux::Fault& fault){
 if(!path||!*path||!fault.context||!fault.info)return false;
 int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);if(fd<0)return false;
 elf_prstatus status{};status.pr_cursig=SIGSEGV;status.pr_pid=ufc_linux::ThreadId();status.pr_ppid=getppid();
 auto& g=fault.context->uc_mcontext.gregs;user_regs_struct regs{};
 regs.r15=g[REG_R15];regs.r14=g[REG_R14];regs.r13=g[REG_R13];regs.r12=g[REG_R12];regs.rbp=g[REG_RBP];regs.rbx=g[REG_RBX];
 regs.r11=g[REG_R11];regs.r10=g[REG_R10];regs.r9=g[REG_R9];regs.r8=g[REG_R8];regs.rax=g[REG_RAX];regs.rcx=g[REG_RCX];
 regs.rdx=g[REG_RDX];regs.rsi=g[REG_RSI];regs.rdi=g[REG_RDI];regs.rip=g[REG_RIP];regs.rsp=g[REG_RSP];regs.eflags=g[REG_EFL];regs.orig_rax=uint64_t(-1);
 regs.cs=uint64_t(g[REG_CSGSFS])&65535;regs.gs=(uint64_t(g[REG_CSGSFS])>>16)&65535;regs.fs=(uint64_t(g[REG_CSGSFS])>>32)&65535;
 static_assert(sizeof(regs)==sizeof(status.pr_reg));std::memcpy(status.pr_reg,&regs,sizeof(regs));
 char notes[2048]{};size_t note_size=0;
 auto note=[&](uint32_t type,const void* data,size_t size){Elf64_Nhdr n{5,uint32_t(size),type};std::memcpy(notes+note_size,&n,sizeof(n));note_size+=sizeof(n);std::memcpy(notes+note_size,"CORE",5);note_size+=8;std::memcpy(notes+note_size,data,size);note_size+=(size+3)&~size_t(3);};
 note(NT_PRSTATUS,&status,sizeof(status));note(NT_SIGINFO,fault.info,sizeof(*fault.info));
 uintptr_t stack=regs.rsp&~uintptr_t(4095);size_t stack_size=0;ufc_linux::Region region;
 if(ufc_linux::Query(reinterpret_cast<void*>(stack),&region,sizeof(region))&&(region.Protect&ufc_linux::Read))stack_size=std::min<size_t>(65536,uintptr_t(region.BaseAddress)+region.RegionSize-stack);
 Elf64_Ehdr header{};std::memcpy(header.e_ident,ELFMAG,SELFMAG);header.e_ident[EI_CLASS]=ELFCLASS64;header.e_ident[EI_DATA]=ELFDATA2LSB;header.e_ident[EI_VERSION]=EV_CURRENT;header.e_ident[EI_OSABI]=ELFOSABI_LINUX;
 header.e_type=ET_CORE;header.e_machine=EM_X86_64;header.e_version=EV_CURRENT;header.e_ehsize=sizeof(header);header.e_phoff=sizeof(header);header.e_phentsize=sizeof(Elf64_Phdr);header.e_phnum=stack_size?2:1;
 Elf64_Phdr segments[2]{};segments[0].p_type=PT_NOTE;segments[0].p_offset=sizeof(header)+header.e_phnum*sizeof(Elf64_Phdr);segments[0].p_filesz=note_size;segments[0].p_align=4;
 segments[1].p_type=PT_LOAD;segments[1].p_offset=4096;segments[1].p_vaddr=stack;segments[1].p_filesz=stack_size;segments[1].p_memsz=stack_size;segments[1].p_flags=PF_R|PF_W;segments[1].p_align=4096;
 bool ok=WriteAll(fd,&header,sizeof(header))&&WriteAll(fd,segments,header.e_phnum*sizeof(Elf64_Phdr))&&WriteAll(fd,notes,note_size);
 if(stack_size&&lseek(fd,4096,SEEK_SET)<0)ok=false;
 char buffer[4096];
 for(size_t offset=0;ok&&offset<stack_size;offset+=sizeof(buffer)){
  size_t count=std::min(sizeof(buffer),stack_size-offset);iovec local{buffer,count},remote{reinterpret_cast<void*>(stack+offset),count};
  auto n=process_vm_readv(getpid(),&local,1,&remote,1,0);if(n!=ssize_t(count)){ok=false;break;}ok=WriteAll(fd,buffer,count);
 }
 close(fd);
 // Keep module evidence even when the stack could not be copied completely.
 char map_path[4096];size_t length=strnlen(path,sizeof(map_path)-6);if(length<sizeof(map_path)-6){std::memcpy(map_path,path,length);std::memcpy(map_path+length,".maps",6);int maps=open("/proc/self/maps",O_RDONLY|O_CLOEXEC);int output=open(map_path,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);if(maps>=0&&output>=0){ssize_t n;while((n=read(maps,buffer,sizeof(buffer)))>0)if(!WriteAll(output,buffer,size_t(n)))break;}if(maps>=0)close(maps);if(output>=0)close(output);}
 return ok;
}
}
