#include <onion/log.h>
#include <errno.h>
#include <unistd.h>

#include "../include/injector.h"
#include "ps5/klog.h"
#include <ps5/kernel.h>

int attached = false;
intptr_t remote_malloc = 0;
intptr_t remote_pthread_create = 0;
void* remote_pthread_join = NULL;
SCEFunctions sce_functions = {0};

/*
 * Scoped PTRACE_AUTHID for the inject_elf() window (not DEBUG_AUTHID).
 * Elevate once via set_ucred_to_ptrace(); restore on every exit. No per-call
 * flip inside sys_ptrace (see libonion_elfldr pt.c).
 */
static int read_self_authid(uint64_t *authid_out)
{
    if (!authid_out) {
        return -1;
    }

    *authid_out = kernel_get_ucred_authid(getpid());
    return (*authid_out != 0) ? 0 : -1;
}

static int write_self_authid(uint64_t authid)
{
    return kernel_set_ucred_authid(getpid(), authid);
}


int __attribute__((section(".stager_shellcode$1")))  stager(SCEFunctions* functions)
{
    pthread_t thread;
    functions->pthread_create_ptr(&thread, 0, (void *(*)(void *)) functions->elf_main, functions->payload_args);

    asm("int3");

    return 0;
}

//
// Just used to calculate stager size
//
int __attribute__((section(".stager_shellcode$2"))) stager_end()
{
    return 0;
}

//
// Poor man function size counter, temp stuff
//
uint32_t get_shellcode_size()
{
    return &stager_end - &stager;
}


//
// Init all remote function pointers needed for injection
//
void init_remote_function_pointers(pid_t pid)
{
    if (!attached)
    {
        if (pt_attach(pid) < 0)
        {
            LOG_ERROR("Error attaching PID %d! aborting...", pid);
            return;
        }
    }

    char nid[12] = {0};
    //
    // Injector/loader specifics
    //
    nid_encode("malloc", nid);
    remote_malloc = pt_resolve(pid, nid);
    nid_encode("pthread_create", nid);
    remote_pthread_create = pt_resolve(pid, nid);
    nid_encode("nid_pthread_join", nid);
    remote_pthread_join = (void*) pt_resolve(pid, nid);

    //
    // Shellcode function pointers
    //
    nid_encode("sceKernelDebugOutText", nid);
    sce_functions.sceKernelDebugOutText = (void*) pt_resolve(pid, nid);
    sce_functions.pthread_create_ptr = (void*) remote_pthread_create;

}


int inject_elf(struct proc* proc, void* elf)
{
    int status = true;
    uint64_t original_authid = 0;
    int ptrace_authid_changed = 0;
    uint64_t sce_ptr_mem;
    uint64_t shellcode_size = get_shellcode_size();

    LOG_DEBUG("[+] Elevating for ptrace (PTRACE_AUTHID)...[+]");

    if (proc == NULL || elf == NULL)
    {
        LOG_ERROR("[-] inject_elf: invalid args proc=%p elf=%p",
                    (void*)proc, elf);
        status = false;
        goto exit;
    }

    /* Scoped PTRACE_AUTHID for attach/load only (not DEBUG_AUTHID). */
    if (read_self_authid(&original_authid) == 0)
    {
        ptrace_authid_changed = 1;
        LOG_DEBUG("[+] inject: backup authid=0x%lx",
                    (unsigned long)original_authid);
    }
    else
    {
        LOG_ERROR("[-] inject: authid backup failed (continuing)");
    }
    set_ucred_to_ptrace();

    if (pt_attach(proc->pid) < 0)
    {
        LOG_ERROR("Error attaching into PID: %d errno=%d", proc->pid, errno);
        status = false;
        goto exit;
    }

    LOG_DEBUG("[+] Attached to %d! [+]", proc->pid);
    attached = true;

    init_remote_function_pointers(proc->pid);

    LOG_DEBUG("[+] Loading ELF on %d...[+]", proc->pid);
    /* Crash window: remote mmap/mprotect via pt_syscall while ShellUI is stopped. */
    intptr_t entry = elfldr_load(proc->pid, (uint8_t*) elf);

    if (entry <= 0)
    {
        LOG_ERROR("[-] Failed to load ELF! [-]");
        status = false;
        goto detach;
    }
    LOG_DEBUG("[+] elfldr_load ok entry=%#lx", (unsigned long)entry);

    LOG_DEBUG("[+] Allocating payload args on %d...[+]", proc->pid);
    intptr_t args = elfldr_payload_args(proc->pid);
    LOG_DEBUG("[+] ELF entrypoint: %#02lx [+]\n[+] Payload Args: %#02lx [+]", entry, args);
    if (args <= 0)
    {
        LOG_ERROR("[-] Failed to allocate payload args! [-]");
        status = false;
        goto detach;
    }

    //
    // Copy shellcode thread parameters & boot code
    //
    sce_functions.elf_main = (void*) entry;
    sce_functions.payload_args = (void*) args;

    uint64_t bootstrap = pt_mmap(proc->pid, 0, shellcode_size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    /* MAP_FAILED is (void*)-1; also reject NULL. (kylin-core) */
    if (!bootstrap || bootstrap == (uint64_t)-1)
    {
        LOG_ERROR("Unable to allocate bootstrap code, injection aborted!");
        status = false;
        goto detach;
    }

    //
    // Make it executable
    //
    if (kernel_mprotect(proc->pid, bootstrap, shellcode_size, PROT_EXEC|PROT_WRITE|PROT_READ) != 0)
    {
        LOG_ERROR("[-] bootstrap mprotect failed pid=%d addr=%#lx",
                    proc->pid, (unsigned long)bootstrap);
        status = false;
        goto detach;
    }
    if (pt_copyin(proc->pid, stager, bootstrap, shellcode_size) != 0)
    {
        LOG_ERROR("[-] bootstrap copyin failed pid=%d addr=%#lx",
                    proc->pid, (unsigned long)bootstrap);
        status = false;
        goto detach;
    }

    LOG_DEBUG("[+] Bootstrap code allocated at %#02lx [+]", bootstrap);
    //
    // Write the sce functions data
    //
    sce_ptr_mem = pt_mmap(proc->pid, 0, sizeof(sce_functions), PROT_READ|PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (!sce_ptr_mem || sce_ptr_mem == (uint64_t)-1)
    {
        LOG_ERROR("[-] sce_functions mmap failed pid=%d", proc->pid);
        status = false;
        goto detach;
    }
    if (pt_copyin(proc->pid, &sce_functions, sce_ptr_mem, sizeof(SCEFunctions)) != 0)
    {
        LOG_ERROR("[-] sce_functions copyin failed pid=%d", proc->pid);
        status = false;
        goto detach;
    }

    LOG_DEBUG("[+] Triggering entrypoint... [+]");
    //
    // Call until hit a breakpoint (stager ends with int3; pt_call2 waits)
    //
    if (pt_call2(proc->pid, bootstrap, sce_ptr_mem) == -1)
    {
        LOG_ERROR("[-] pt_call2 failed pid=%d errno=%d", proc->pid, errno);
        status = false;
        goto detach;
    }

detach:
    if (pt_detach(proc->pid, 0) == 0)
    {
        attached = false;
    }
    else
    {
        LOG_ERROR("[-] pt_detach failed pid=%d errno=%d", proc->pid, errno);
    }

    LOG_DEBUG("[+] ELF injection finished! [+]");
    LOG_DEBUG("[+] Detached [+]");
exit:
    if (ptrace_authid_changed)
    {
        if (write_self_authid(original_authid) == 0)
        {
            LOG_DEBUG("[+] inject: restored authid=0x%lx",
                        (unsigned long)original_authid);
        }
        else
        {
            LOG_ERROR("[-] inject: authid restore failed authid=0x%lx",
                        (unsigned long)original_authid);
        }
    }
    return status;
}
