#ifndef __ASM_OFFSETS_H__
#define __ASM_OFFSETS_H__
/*
 * DO NOT MODIFY.
 *
 * This file was generated by arch/arm26/Makefile
 *
 */

#define TSK_ACTIVE_MM 96 /* offsetof(struct task_struct, active_mm) */

#define VMA_VM_MM 0 /* offsetof(struct vm_area_struct, vm_mm) */
#define VMA_VM_FLAGS 20 /* offsetof(struct vm_area_struct, vm_flags) */

#define VM_EXEC 4 /* VM_EXEC */


#define PAGE_PRESENT 1 /* L_PTE_PRESENT */
#define PAGE_READONLY 95 /* PAGE_READONLY */
#define PAGE_NOT_USER 3 /* PAGE_NONE */
#define PAGE_OLD 3 /* PAGE_NONE */
#define PAGE_CLEAN 128 /* L_PTE_DIRTY */

#define PAGE_SZ 32768 /* PAGE_SIZE */

#define SYS_ERROR0 10420224 /* 0x9f0000 */

#endif
