extern "C"{
#include <onion/log.h>
#include <onion/proc.h>
}

struct proc* get_proc_by_pid(pid_t pid)
{
    uintptr_t next = 0;

    kernel_copyout(KERNEL_ADDRESS_ALLPROC, &next, sizeof(uintptr_t));
    struct proc* proc =  (struct proc*) malloc(sizeof(struct proc));
    do
    {
        kernel_copyout(next, proc, sizeof(struct proc));

        if (proc->pid == pid)
            return proc;

        kernel_copyout(next, &next, sizeof(uint64_t));

    } while (next);

    free(proc);
    return NULL;
}



module_info_t* get_module_info(pid_t pid, const char* module_name)
{
    size_t num_handles = 0;
    syscall(SYS_dl_get_list, pid, NULL, 0, &num_handles);
    
    if (num_handles)
    {
        uintptr_t* handles = (uintptr_t*) calloc(num_handles, sizeof(uintptr_t));
        syscall(SYS_dl_get_list, pid, handles, num_handles, &num_handles);

        module_info_t* mod_info = (module_info_t*) malloc(sizeof(module_info_t));
        
        for (size_t i = 0; i < num_handles; ++i)
        {
            bzero(mod_info, sizeof(module_info_t));
            syscall(SYS_dl_get_info_2, pid, 1, handles[i], mod_info);
            if (!strcmp(mod_info->filename, module_name))
            {
                return mod_info;
            }
        }
        
        free(handles);
        free(mod_info);
    }

    return NULL;
}


int get_module_handle(pid_t pid, const char* module_name)
{
    module_info_t* mod_info = get_module_info(pid, module_name);
    return mod_info ? mod_info->handle : 0; 
}





