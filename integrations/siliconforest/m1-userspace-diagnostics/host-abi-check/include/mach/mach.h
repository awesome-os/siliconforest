#ifndef HOST_ABI_MACH_H
#define HOST_ABI_MACH_H

#include <stdint.h>

#ifndef FIX_VM_ADDRESS_T
#error FIX_VM_ADDRESS_T must name the vm_address_t fixture typedef
#endif
#ifndef FIX_VM_SIZE_T
#error FIX_VM_SIZE_T must name the vm_size_t fixture typedef
#endif

typedef FIX_VM_ADDRESS_T vm_address_t;
typedef FIX_VM_SIZE_T vm_size_t;
typedef int kern_return_t;
typedef int vm_prot_t;
typedef int boolean_t;
typedef int vm_map_t;

#define FALSE 0
#define VM_PROT_NONE 0
#define VM_PROT_READ 1
#define VM_PROT_WRITE 2

extern vm_size_t vm_page_size;
extern vm_size_t vm_page_mask;
extern int vm_page_shift;
extern vm_size_t vm_kernel_page_size;
extern vm_size_t vm_kernel_page_mask;
extern int vm_kernel_page_shift;

kern_return_t vm_protect(vm_map_t, vm_address_t, vm_size_t, boolean_t, vm_prot_t);
kern_return_t host_page_size(vm_map_t, vm_size_t *);
vm_map_t mach_host_self(void);
vm_map_t mach_task_self(void);

#endif
