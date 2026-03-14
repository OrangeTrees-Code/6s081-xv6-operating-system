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
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "memlayout.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int* pfd, struct file** pf) {
  int fd;
  struct file* f;

  if (argint(n, &fd) < 0)
    return -1;
  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
    return -1;
  if (pfd)
    *pfd = fd;
  if (pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file* f) {
  int fd;
  struct proc* p = myproc();

  for (fd = 0; fd < NOFILE; fd++) {
    if (p->ofile[fd] == 0) {
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void) {
  struct file* f;
  int fd;

  if (argfd(0, 0, &f) < 0)
    return -1;
  if ((fd = fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void) {
  struct file* f;
  int n;
  uint64 p;

  if (argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void) {
  struct file* f;
  int n;
  uint64 p;

  if (argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void) {
  int fd;
  struct file* f;

  if (argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void) {
  struct file* f;
  uint64 st; // user pointer to struct stat

  if (argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void) {
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode* dp, * ip;

  if (argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if ((ip = namei(old)) == 0) {
    end_op();
    return -1;
  }

  ilock(ip);
  if (ip->type == T_DIR) {
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if ((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if (dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0) {
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
isdirempty(struct inode* dp) {
  int off;
  struct dirent de;

  for (off = 2 * sizeof(de); off < dp->size; off += sizeof(de)) {
    if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if (de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void) {
  struct inode* ip, * dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if (argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if ((dp = nameiparent(path, name)) == 0) {
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if (namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if ((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if (ip->nlink < 1)
    panic("unlink: nlink < 1");
  if (ip->type == T_DIR && !isdirempty(ip)) {
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if (ip->type == T_DIR) {
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
create(char* path, short type, short major, short minor) {
  struct inode* ip, * dp;
  char name[DIRSIZ];

  if ((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if ((ip = dirlookup(dp, name, 0)) != 0) {
    iunlockput(dp);
    ilock(ip);
    if (type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if ((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if (type == T_DIR) {  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if (dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64
sys_open(void) {
  char path[MAXPATH];
  int fd, omode;
  struct file* f;
  struct inode* ip;
  int n;

  if ((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if (omode & O_CREATE) {
    ip = create(path, T_FILE, 0, 0);
    if (ip == 0) {
      end_op();
      return -1;
    }
  }
  else {
    if ((ip = namei(path)) == 0) {
      end_op();
      return -1;
    }
    ilock(ip);
    if (ip->type == T_DIR && omode != O_RDONLY) {
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if (ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)) {
    iunlockput(ip);
    end_op();
    return -1;
  }

  if ((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0) {
    if (f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if (ip->type == T_DEVICE) {
    f->type = FD_DEVICE;
    f->major = ip->major;
  }
  else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if ((omode & O_TRUNC) && ip->type == T_FILE) {
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void) {
  char path[MAXPATH];
  struct inode* ip;

  begin_op();
  if (argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0) {
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void) {
  struct inode* ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if ((argstr(0, path, MAXPATH)) < 0 ||
    argint(1, &major) < 0 ||
    argint(2, &minor) < 0 ||
    (ip = create(path, T_DEVICE, major, minor)) == 0) {
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void) {
  char path[MAXPATH];
  struct inode* ip;
  struct proc* p = myproc();

  begin_op();
  if (argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0) {
    end_op();
    return -1;
  }
  ilock(ip);
  if (ip->type != T_DIR) {
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
sys_exec(void) {
  char path[MAXPATH], * argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if (argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0) {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for (i = 0;; i++) {
    if (i >= NELEM(argv)) {
      goto bad;
    }
    if (fetchaddr(uargv + sizeof(uint64) * i, (uint64*)&uarg) < 0) {
      goto bad;
    }
    if (uarg == 0) {
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if (argv[i] == 0)
      goto bad;
    if (fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

bad:
  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void) {
  uint64 fdarray; // user pointer to array of two integers
  struct file* rf, * wf;
  int fd0, fd1;
  struct proc* p = myproc();

  if (argaddr(0, &fdarray) < 0)
    return -1;
  if (pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if ((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0) {
    if (fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if (copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
    copyout(p->pagetable, fdarray + sizeof(fd0), (char*)&fd1, sizeof(fd1)) < 0) {
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

void sort_vmas(struct vma* arr) {
  for (int i = 1; i < MAXVMA; i++) {
    if (!arr[i].valid) continue;
    struct vma key = arr[i];
    int j = i - 1;
    while (j >= 0 && arr[j].valid && arr[j].addr < key.addr) {
      arr[j + 1] = arr[j];
      j--;
    }
    arr[j + 1] = key;
  }
}

union vmaOrSize {
  struct vma* vma;
  uint64 size;
};

struct vmaInfoAndAddr {
  union vmaOrSize vmaOrSize;
  uint64 addr;
};



struct vmaInfoAndAddr computeSpace(int index, struct proc* p) {
  int up, down;
  up = down = index;
  while (up >= 0 && p->vmas[up].valid == 0) {
    up--;
  }
  while (down < MAXVMA && p->vmas[down].valid == 0) {
    down++;
  }
  uint64 ceiling = (up == -1 ? TRAPFRAME : p->vmas[up].addr);
  uint64 floor = (down == MAXVMA ? p->sz : (p->vmas[down].addr + p->vmas[down].length));

  struct vmaInfoAndAddr res;
  if (ceiling <= floor) {
    res.vmaOrSize.size = 0;
  }
  else {
    res.vmaOrSize.size = ceiling - floor;
  }
  // res.vmaOrSize.size = ceiling - floor;
  res.addr = ceiling;
  return res;
}

struct vmaInfoAndAddr vmaAlloc(uint64 size) {
  struct vmaInfoAndAddr res;
  res.addr = 0;
  struct proc* p = myproc();
  for (int i = 0;i < MAXVMA;i++) {
    if (p->vmas[i].valid == 0) {
      struct vmaInfoAndAddr info = computeSpace(i, p);
      if (info.vmaOrSize.size >= size) {
        res.vmaOrSize.vma = &(p->vmas[i]);
        res.addr = PGROUNDDOWN(info.addr - size);
        return res;
      }
    }
  }
  return res;
}

void vmaInit(struct vmaInfoAndAddr* info, uint64 addr, uint64 length, int prot, int flags, off_t offset, int fd, struct file* fp) {
  struct vma* vma = info->vmaOrSize.vma;
  vma->valid = 1;

  if (addr == 0) {
    // allocate from top down; addr in info is ceiling
    vma->addr = PGROUNDDOWN(info->addr);
  }
  else {
    vma->addr = PGROUNDDOWN(addr);
  }
  vma->fd = fd;
  vma->flags = flags;
  vma->fp = fp;
  vma->length = length;
  vma->offset = offset;
  vma->prot = prot;

  filedup(fp);

  sort_vmas(myproc()->vmas);
}



//void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
uint64
sys_mmap(void) {
  uint64 addr;
  uint64 length;
  int prot, flags;
  off_t offset;
  int fd;
  struct file* fp;
  if (argaddr(0, &addr) < 0 || argaddr(1, &length) < 0 || argint(2, &prot) < 0 || argint(3, &flags) < 0 || argfd(4, &fd, &fp) < 0 || argint(5, &offset) < 0) {
    return 0xffffffffffffffff;
  }

  if ((flags & MAP_SHARED) && (flags & MAP_PRIVATE)) {
    return 0xffffffffffffffff;
  }

  if ((flags & MAP_SHARED) && (prot & PROT_WRITE) && fp->readable != 0 && fp->writable == 0){
    return 0xffffffffffffffff;
  }

  struct vmaInfoAndAddr info = vmaAlloc(length);

  if (info.addr == 0) {
    return 0xffffffffffffffff;
  }

  vmaInit(&info, addr, length, prot, flags, offset, fd, fp);

  return info.addr;
}

struct vma* findVmaByAddr(uint64 addr) {
  struct proc* p = myproc();
  struct vma* vma = 0;

  for (int i = 0; i < MAXVMA; i++) {
    vma = &(p->vmas[i]);
    if (vma->valid == 1 && vma->addr <= addr && addr < (uint64)vma->addr + vma->length) {
      return vma;
    }
  }

  return 0;
}


int write_back(struct vma* vma, uint64 addr, uint64 length) {
  pte_t* pte;
  char* mem;
  struct proc* p = myproc();
  uint64 va;
  struct inode* ip;

  uint64 end = addr + length;

  for (uint64 va = addr; va < end; va += PGSIZE) {

    if ((pte = walk(p->pagetable, va, 0)) == 0) {
      return -1;
    }
    if ((*pte & PTE_V) && (*pte & PTE_D)) {
      uint n = PGSIZE;
      if (va + PGSIZE > end)
        n = end - va;
  
      ip = vma->fp->ip;
  
      if ((mem = walkaddr(p->pagetable, (uint64)va)) == 0) {
        continue;
      }
  
      begin_op();
      ilock(ip);
      writei(ip, 0, mem, vma->offset + (va - (uint64)(vma->addr)), n);
      iunlock(ip);
      end_op();
    }

  }

  return 0;

}

void vmaShrink(struct vma* vma, uint64 addr, uint64 length) {
  if (vma->addr == addr && vma->length == length) {
    fileclose(vma->fp);
    vma->valid = 0;
    return;
  }

  else if (vma->addr == addr) {
    vma->addr += length;
    vma->length -= length;
    vma->offset += length;
    return;
  }

  else {
    vma->length -= length;
    return;
  }
}

int do_munmap(uint64 addr,uint64 length) {
  struct vma* vma;
  if ((vma = findVmaByAddr(addr)) == 0) {
    return -1;
  }

  if (vma->flags & MAP_SHARED) {
    write_back(vma, addr, length);
  }

  int npage = PGROUNDUP(length) / PGSIZE;

  uvmunmap(myproc()->pagetable, PGROUNDDOWN(addr), npage, 1);

  vmaShrink(vma, addr, length);

  return 0;
}

uint64
sys_munmap(void) {
  uint64 addr, length;
  if (argaddr(0, &addr) < 0 || argaddr(1, &length) < 0) {
    return -1;
  }

  if (do_munmap(addr, length) < 0) {
    return -1;
  }

  return 0;
}

int vma_pagefault_handler(struct proc* p, uint64 va, struct vma* vma) {
  char* mem;
  if ((mem = kalloc()) == 0) {
    return -1;
  }
  struct inode* ip = vma->fp->ip;

  ilock(ip);
  int got = readi(ip, 0, mem, vma->offset + ((uint64)PGROUNDDOWN(va) - (uint64)vma->addr), PGSIZE);
  iunlock(ip);

  if (got < 0) {
    kfree(mem);
    return -1;
  }
  if (got < PGSIZE) {
    memset(mem + got, 0, PGSIZE - got);
  }

  int perm = PTE_U;
  if (vma->prot & PROT_READ)  perm |= PTE_R;
  if (vma->prot & PROT_WRITE) perm |= PTE_W;
  if (mappages(p->pagetable, PGROUNDDOWN(va), PGSIZE, (uint64)mem, perm) < 0) {
    kfree(mem);
    return -1;
  }

  return 0;
}
