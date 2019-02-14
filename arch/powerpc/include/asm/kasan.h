/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_KASAN_H
#define __ASM_KASAN_H

#ifndef __ASSEMBLY__

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pgtable-types.h>

#define KASAN_SHADOW_SCALE_SHIFT	3

#define KASAN_SHADOW_OFFSET	(KASAN_SHADOW_START - \
				 (PAGE_OFFSET >> KASAN_SHADOW_SCALE_SHIFT))
#define KASAN_SHADOW_END	(KASAN_SHADOW_START + KASAN_SHADOW_SIZE)


#ifdef CONFIG_PPC32
#include <asm/fixmap.h>
#define KASAN_SHADOW_START	(ALIGN_DOWN(FIXADDR_START - KASAN_SHADOW_SIZE, \
					    PGDIR_SIZE))
#define KASAN_SHADOW_SIZE	((~0UL - PAGE_OFFSET + 1) >> KASAN_SHADOW_SCALE_SHIFT)

void kasan_early_init(void);

#endif /* CONFIG_PPC32 */

#ifdef CONFIG_PPC_BOOK3E_64
#define KASAN_SHADOW_START VMEMMAP_BASE
#define KASAN_SHADOW_SIZE	(KERN_VIRT_SIZE >> KASAN_SHADOW_SCALE_SHIFT)

extern struct static_key_false powerpc_kasan_enabled_key;
#define check_return_arch_not_ready() \
	do {								\
		if (!static_branch_likely(&powerpc_kasan_enabled_key))	\
			return;						\
	} while (0)

extern unsigned char kasan_zero_page[PAGE_SIZE];
static inline void *kasan_mem_to_shadow_book3e(const void *addr)
{
	if ((unsigned long)addr >= KERN_VIRT_START &&
		(unsigned long)addr < (KERN_VIRT_START + KERN_VIRT_SIZE)) {
		return (void *)kasan_zero_page;
	}

	return (void *)((unsigned long)addr >> KASAN_SHADOW_SCALE_SHIFT)
		+ KASAN_SHADOW_OFFSET;
}
#define kasan_mem_to_shadow kasan_mem_to_shadow_book3e

static inline void *kasan_shadow_to_mem_book3e(const void *shadow_addr)
{
	/*
	 * We map the entire non-linear virtual mapping onto the zero page so if
	 * we are asked to map the zero page back just pick the beginning of that
	 * area.
	 */
	if (shadow_addr >= (void *)kasan_zero_page &&
		shadow_addr < (void *)(kasan_zero_page + PAGE_SIZE)) {
		return (void *)KERN_VIRT_START;
	}

	return (void *)(((unsigned long)shadow_addr - KASAN_SHADOW_OFFSET)
		<< KASAN_SHADOW_SCALE_SHIFT);
}
#define kasan_shadow_to_mem kasan_shadow_to_mem_book3e

static inline bool kasan_addr_has_shadow_book3e(const void *addr)
{
	/*
	 * We want to specifically assert that the addresses in the 0x8000...
	 * region have a shadow, otherwise they are considered by the kasan
	 * core to be wild pointers
	 */
	if ((unsigned long)addr >= KERN_VIRT_START &&
		(unsigned long)addr < (KERN_VIRT_START + KERN_VIRT_SIZE)) {
		return true;
	}
	return (addr >= kasan_shadow_to_mem((void *)KASAN_SHADOW_START));
}
#define kasan_addr_has_shadow kasan_addr_has_shadow_book3e

#endif /* CONFIG_PPC_BOOK3E_64 */

void kasan_init(void);

#endif /* CONFIG_KASAN */
#endif
