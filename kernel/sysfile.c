//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "proc.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd);
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;
  
  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  argaddr(1, &st);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0){
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if(dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if(type == T_DIR){
    // now that success is guaranteed:
    dp->nlink++;  // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

 fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  argint(1, &major);
  argint(2, &minor);
  if((argstr(0, path, MAXPATH)) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if(argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray);
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

uint64 sys_mmap(void) 
{
    uint64 addr;
    int length, prot, flags, fd, offset;
    struct proc *p = myproc();

    argaddr(0, &addr);
    if (addr != 0)
        panic("sys_mmap only supports that addr must be 0");
    argint(1, &length);
    if (length <= 0)
        return -1;
    argint(2, &prot);
    argint(3, &flags);
    argint(4, &fd);
    if (fd < 0 || fd >= NOFILE || (p->ofile[fd] == 0))
        return -1;
    argint(5, &offset);
    // get file structure corresponding to fd
    struct file *f = p->ofile[fd];

    if (flags == MAP_SHARED && (prot & PROT_WRITE) && (f->writable == 0)) {
        // cannot map a file as writable if the file is not opened as writable
        printf("DEBUG: sys_mmap failed: file fd %d not opened as writable\n", fd);
        return -1;
    }

    // Increase file reference count
    f = filedup(f);

    // find a enough and free virtual address to map
    if ((addr = findFreeVMA(p->pagetable, length, p->sz)) == 0) {
        printf("DEBUG: sys_mmap failed to find free VMA\n");
        return -1;
    }

    // Add one entry to mmapvmas
    // Do I need lock?
    for (int i = 0; i < NMMAPVMA; i++) {
        if (p->mmapvmas[i].length == 0) {
            p->mmapvmas[i].addr = addr;
            p->mmapvmas[i].length = length;
            p->mmapvmas[i].prot = prot;
            p->mmapvmas[i].flags = flags;
            p->mmapvmas[i].file = f;
            p->mmapvmas[i].fd = fd;
            p->mmapvmas[i].offset = offset;
            break;
        }
        if (i == NMMAPVMA - 1) {
            // No free mmapvma entry
            return -1;
        }
    }
    printf("------- sys_mmap called -------\n");
    printf("sys_mmap mapped addr 0x%lx length %d\n", addr, length);
    return addr;
}

// BUG: type cannot be unsigned.
int min(int a, int b) {
    return a < b ? a : b;
}

int max(int a, int b) {
    return a > b ? a : b;
}

int munmap(uint64 addr, int length) {
    printf("------- sys_munmap called -------\n");
    printf("sys_munmap unmapping addr 0x%lx length %d\n", addr, length);
    int idx;
    struct proc *p = myproc();
    struct mmapvma *vma = p->mmapvmas;
    if (length < 0)
        return -1; 
    else if (length == 0)
        return 0;

    // find the mmap VMA corresponding to addr
    for(idx = 0; idx < NMMAPVMA; idx++) {
        if (vma[idx].addr <= addr && addr < vma[idx].addr + vma[idx].length)
            break;
    }
    if (idx == NMMAPVMA) {
        // No such mmap VMA
        printf("sys_munmap: no such mmap VMA for address 0x%lx\n", addr);
        return -1;
    }

    // check if addr is at the start or end of the VMA
    if (addr != vma[idx].addr && 
        addr + length < vma[idx].addr + vma[idx].length) {
        // not at start or end
        printf("sys_munmap: addr 0x%lx is not at start or end of VMA\n", addr);
        return -1;
    }
    printf("DEBUG: sys_munmap found mmap VMA: addr 0x%lx length %ld bytes\n", 
            vma[idx].addr, vma[idx].length);
    printf("DEBUG: sys_munmap unmapping addr 0x%lx length %d bytes\n", addr, length);

    // correct the length
    length = min(length, vma[idx].addr + vma[idx].length - addr);

    // Write back to file if MAP_SHARED
    if(vma[idx].flags & MAP_SHARED) {
        // BUG: When writing back, we cann't exceed the file size.
        // If the file is smaller than the mapped region, we need to adjust the length.
        // We only support file offset zero now.
        int offsetInFile = (int)(addr - vma[idx].addr + vma[idx].offset);
        ilock(vma[idx].file->ip);
        int filesize = vma[idx].file->ip->size;
        iunlock(vma[idx].file->ip);
        printf("Offset in file: %d, file size: %d bytes\n", offsetInFile, filesize);
        int writeFilelength = min(length, filesize - offsetInFile);
        writeFilelength = max(writeFilelength, 0);
        printf("write file length: %d bytes\n", writeFilelength);

        // if(writePartialFile(vma[idx].file, addr,
        //         writeFilelength, offsetInFile) < 0) {
        //     printf("sys_munmap: writePartialFile failed for addr 0x%lx length %d bytes\n", addr, length);
        //     return -1;
        // }

        // Write back page by page
        // skip all not mapped pages
        for (uint64 a = addr;  writeFilelength > 0; a += PGSIZE) {
            if (walkaddr(p->pagetable, a) == 0) {
                offsetInFile += PGSIZE;
                writeFilelength -= PGSIZE;
                continue;
            }
            printf("add %lx, size %d, off %d\n", a, min(PGSIZE, writeFilelength), offsetInFile);
            if(writePartialFile(vma[idx].file, a,
                    min(PGSIZE, writeFilelength), offsetInFile) < 0) {
                printf("sys_munmap: writePartialFile failed for addr 0x%lx length %d bytes\n", addr, length);
                return -1;
            }
            offsetInFile += PGSIZE;
            writeFilelength -= PGSIZE;
        }

    }
    // unmap the pages from page table
    // skip all not mapped pages
    for (uint64 a = addr; a < addr + length; a += PGSIZE) {
        if (walkaddr(p->pagetable, a) == 0) {
            continue;
        }
        uvmunmap(p->pagetable, a, 1, 1);
    }

    ilock(vma[idx].file->ip);
    int fileSize = vma[idx].file->ip->size;
    iunlock(vma[idx].file->ip);
    printf("FILE SIZE AFTER MUNMAP: %d bytes\n", fileSize);

    if (length < vma[idx].length) {
        // adjust the VMA
        if (addr == vma[idx].addr) {
            // unmap at start
            vma[idx].addr += length;
            vma[idx].length -= length;
            vma[idx].offset += length;
        } else
            vma[idx].length -= length;
    } else {
        // remove the VMA and decrease file reference count
        fileclose(vma[idx].file);
        vma[idx].addr = 0;
        vma[idx].length = 0;
        vma[idx].prot = 0;
        vma[idx].flags = 0;
        vma[idx].file = 0;
        vma[idx].fd = -1;
        vma[idx].offset = 0;
    }

    printf("sys_munmap completed unmapping new addr 0x%lx new length %ld bytes\n", vma[idx].addr, vma[idx].length);

    return 0;
}


// NOTE: munmap only support unmap at the start, 
// or at the end, or the whole region 
// (but not punch a hole in the middle of a region).
// Since this will lead to fragmentation of mmapvmas array.
// For simplicity, we will leave this for future work.
uint64 sys_munmap(void) 
{
    uint64 addr;
    int length;

    argaddr(0, &addr);
    argint(1, &length);
    return munmap(addr, length);
}