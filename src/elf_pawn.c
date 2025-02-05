/*
 * MIT License
 *
 * Copyright (c) 2020-2023 EntySec
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <elf.h>
#include <fcntl.h>
#include <syscall.h>
#include <unistd.h>
#include <link.h>
#include <string.h>
#include <errno.h>

#include <linux/memfd.h>

#include <sys/types.h>
#include <sys/auxv.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <log.h>
#include <pawn.h>
#include <exec.h>

int pawn_exec(unsigned char *elf, char *argv[], char *env[])
{
    size_t *stack;

    log_debug("* Setting up new stack, page size (%d)\n", PAGE_SIZE);

    stack = (void *)(2047 * PAGE_SIZE + (char *)mmap(0, 2048 * PAGE_SIZE,
            PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE|MAP_GROWSDOWN, -1, 0));

    return exec_with_stack(elf, argv, env, stack);
}

int pawn_exec_fd(unsigned char *elf, char *argv[], char *env[])
{
    int fd;
    int iter;

    size_t end;
    size_t done;

    ElfW(Ehdr) *ehdr;
    ElfW(Phdr) *phdr;

    end = 0;
    done = 0;

    ehdr = (ElfW(Ehdr) *)elf;
    phdr = (ElfW(Phdr) *)(elf + ehdr->e_phoff);

    if (!exec_elf_sanity(ehdr))
    {
        return -1;
    }

    log_debug("* Iterating ELF and searching for PT_LOAD\n");

    for (iter = 0; iter < ehdr->e_phnum; iter++, phdr++)
    {
        if (phdr->p_type == PT_LOAD)
        {
            if (end < phdr->p_offset + phdr->p_filesz)
            {
                end = phdr->p_offset + phdr->p_filesz;
            }
        }
    }

    log_debug("* Creating file descriptor\n");

    fd = syscall(SYS_memfd_create, "", MFD_CLOEXEC);

    if (ftruncate(fd, end) < 0) {
        log_debug("* Unable to write ELF to the file descriptor (%d)\n", fd);
        return -1;
    }

    log_debug("* Writing to the file descriptor (%d)\n", fd);

    while (done < end)
    {
        if (write(fd, elf + done, end - done) < 0)
        {
            return -1;
        }

        done += 1;
    }

    log_debug("* Executing from the file descriptor (%d)\n", fd);

    syscall(SYS_execveat, fd, "", argv, env, 0x1000);
    return 0;
}
