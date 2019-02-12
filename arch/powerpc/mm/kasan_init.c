// SPDX-License-Identifier: GPL-2.0

#define DISABLE_BRANCH_PROFILING

#include <linux/kasan.h>
#include <linux/printk.h>
#include <linux/memblock.h>
#include <linux/sched/task.h>
#include <asm/pgalloc.h>

void __init kasan_early_init(void)
{
	unsigned long addr = KASAN_SHADOW_START;
	unsigned long end = KASAN_SHADOW_END;
	unsigned long next;
	pmd_t *pmd = pmd_offset(pud_offset(pgd_offset_k(addr), addr), addr);
	int i;
	phys_addr_t pa = __pa(kasan_early_shadow_page);

	BUILD_BUG_ON(KASAN_SHADOW_START & ~PGDIR_MASK);

	if (early_mmu_has_feature(MMU_FTR_HPTE_TABLE))
		panic("KASAN not supported with Hash MMU\n");

	for (i = 0; i < PTRS_PER_PTE; i++)
		__set_pte_at(&init_mm, (unsigned long)kasan_early_shadow_page,
			     kasan_early_shadow_pte + i,
			     pfn_pte(PHYS_PFN(pa), PAGE_KERNEL), 0);

	do {
		next = pgd_addr_end(addr, end);
		pmd_populate_kernel(&init_mm, pmd, kasan_early_shadow_pte);
	} while (pmd++, addr = next, addr != end);
}

static void __init kasan_init_region(struct memblock_region *reg)
{
	void *start = __va(reg->base);
	void *end = __va(reg->base + reg->size);
	unsigned long k_start, k_end, k_cur, k_next;
	pmd_t *pmd;
	void *block;

	if (start >= end)
		return;

	k_start = (unsigned long)kasan_mem_to_shadow(start);
	k_end = (unsigned long)kasan_mem_to_shadow(end);
	pmd = pmd_offset(pud_offset(pgd_offset_k(k_start), k_start), k_start);

	for (k_cur = k_start; k_cur != k_end; k_cur = k_next, pmd++) {
		k_next = pgd_addr_end(k_cur, k_end);
		if ((void *)pmd_page_vaddr(*pmd) == kasan_early_shadow_pte) {
			pte_t *new = pte_alloc_one_kernel(&init_mm);

			if (!new)
				panic("kasan: pte_alloc_one_kernel() failed");
			memcpy(new, kasan_early_shadow_pte, PTE_TABLE_SIZE);
			pmd_populate_kernel(&init_mm, pmd, new);
		}
	};

	block = memblock_alloc(k_end - k_start, PAGE_SIZE);
	for (k_cur = k_start; k_cur < k_end; k_cur += PAGE_SIZE) {
		void *va = block ? block + k_cur - k_start :
				   memblock_alloc(PAGE_SIZE, PAGE_SIZE);
		pte_t pte = pfn_pte(PHYS_PFN(__pa(va)), PAGE_KERNEL);

		if (!va)
			panic("kasan: memblock_alloc() failed");
		pmd = pmd_offset(pud_offset(pgd_offset_k(k_cur), k_cur), k_cur);
		pte_update(pte_offset_kernel(pmd, k_cur), ~0, pte_val(pte));
	}
	flush_tlb_kernel_range(k_start, k_end);
}

static void __init kasan_remap_early_shadow_ro(void)
{
	unsigned long k_cur;
	phys_addr_t pa = __pa(kasan_early_shadow_page);
	int i;

	for (i = 0; i < PTRS_PER_PTE; i++)
		ptep_set_wrprotect(&init_mm, 0, kasan_early_shadow_pte + i);

	for (k_cur = PAGE_OFFSET & PAGE_MASK; k_cur; k_cur += PAGE_SIZE) {
		pmd_t *pmd = pmd_offset(pud_offset(pgd_offset_k(k_cur), k_cur), k_cur);
		pte_t *ptep = pte_offset_kernel(pmd, k_cur);

		if ((void *)pmd_page_vaddr(*pmd) == kasan_early_shadow_pte)
			continue;
		if ((pte_val(*ptep) & PAGE_MASK) != pa)
			continue;

		ptep_set_wrprotect(&init_mm, k_cur, ptep);
	}
	flush_tlb_mm(&init_mm);
}

void __init kasan_init(void)
{
	struct memblock_region *reg;

	for_each_memblock(memory, reg)
		kasan_init_region(reg);

	kasan_remap_early_shadow_ro();

	clear_page(kasan_early_shadow_page);

	/* At this point kasan is fully initialized. Enable error messages */
	init_task.kasan_depth = 0;
	pr_info("KASAN init done\n");
}
