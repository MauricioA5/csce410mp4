#include "assert.H"
#include "console.H"
#include "paging_low.H"
#include "page_table.H"
#include "vm_pool.H"

PageTable * PageTable::current_page_table = nullptr;
unsigned int PageTable::paging_enabled = 0;
ContFramePool * PageTable::kernel_mem_pool = nullptr;
ContFramePool * PageTable::process_mem_pool = nullptr;
unsigned long PageTable::shared_size = 0;

void PageTable::init_paging(ContFramePool * _kernel_mem_pool,
                          ContFramePool * _process_mem_pool,
                          const unsigned long _shared_size)
{
    assert(_kernel_mem_pool && _process_mem_pool);
    assert(_shared_size == 4UL * 1024 * 1024);
    kernel_mem_pool = _kernel_mem_pool;
    process_mem_pool = _process_mem_pool;
    shared_size = _shared_size;
}

PageTable::PageTable() : pools(nullptr)
{
    // Set up the page directory before enabling paging.
    assert(!paging_enabled);
    unsigned long directory_frame = process_mem_pool->get_frames(1);
    unsigned long table_frame = process_mem_pool->get_frames(1);
    assert(directory_frame && table_frame);
    page_directory = (unsigned long *)(directory_frame * PAGE_SIZE);
    unsigned long * table = (unsigned long *)(table_frame * PAGE_SIZE);
    for (unsigned int i = 0; i < ENTRIES_PER_PAGE; ++i) {
        page_directory[i] = 2;
        table[i] = i * PAGE_SIZE | 3;
    }
    page_directory[0] = table_frame * PAGE_SIZE | 3;
    page_directory[1023] = directory_frame * PAGE_SIZE | 3;
}

void PageTable::load()
{
    current_page_table = this;
    // member retains a physical address, even after paging is enabled.
    write_cr3((unsigned long)page_directory);
}

void PageTable::enable_paging()
{
    assert(current_page_table);
    write_cr0(read_cr0() | 0x80000000UL);
    paging_enabled = 1;
}

unsigned long * PageTable::PDE_address(unsigned long addr)
{
    return (unsigned long *)(0xFFFFF000UL + (addr >> 22) * 4);
}

unsigned long * PageTable::PTE_address(unsigned long addr)
{
    return (unsigned long *)(0xFFC00000UL + (addr >> 12) * 4);
}

void PageTable::handle_fault(REGS * r)
{
    unsigned long addr = read_cr2();
    PageTable * pt = current_page_table;
    if (!pt || (r->err_code & 9) || addr >= 0xFFC00000UL) {
        Console::puts("Invalid page fault\n");
        assert(false);
        for (;;) {}
    }
    VMPool * owner = pt->pools;
    while (owner && !owner->is_legitimate(addr)) owner = owner->next;
    if (!owner) {
        Console::puts("Segmentation fault: unallocated virtual address\n");
        assert(false);
        for (;;) {}
    }
    unsigned long * pde = pt->PDE_address(addr);
    if (!(*pde & 1)) {
        unsigned long frame = process_mem_pool->get_frames(1);
        assert(frame);
        *pde = frame * PAGE_SIZE | 3;
        write_cr3(read_cr3());
        // The new table is now reachable through the recursive window.
        unsigned long * table = (unsigned long *)(0xFFC00000UL + (addr >> 22) * PAGE_SIZE);
        for (unsigned int i = 0; i < ENTRIES_PER_PAGE; ++i) table[i] = 2;
    }
    unsigned long * pte = pt->PTE_address(addr);
    assert(!(*pte & 1));
    unsigned long frame = owner->frame_pool->get_frames(1);
    assert(frame);
    *pte = frame * PAGE_SIZE | 3;
}

void PageTable::register_pool(VMPool * pool)
{
    assert(pool && pool->page_table == this);
    for (VMPool * p = pools; p; p = p->next) {
        assert(p != pool);
        assert(pool->base_address + pool->pool_size <= p->base_address ||
               p->base_address + p->pool_size <= pool->base_address);
    }
    pool->next = pools;
    pools = pool;
}

void PageTable::free_page(unsigned long page_no)
{
    assert(this == current_page_table);
    assert(page_no >= shared_size / PAGE_SIZE && page_no < 0xFFC00UL);
    unsigned long addr = page_no * PAGE_SIZE;
    if (!(*PDE_address(addr) & 1)) return;
    unsigned long * pte = PTE_address(addr);
    if (!(*pte & 1)) return; // An untouched reservation has no physical frame.
    unsigned long frame = (*pte & 0xFFFFF000UL) / PAGE_SIZE;
    *pte = 2;
    write_cr3(read_cr3()); // Invalidate stale translations before frame reuse.
    ContFramePool::release_frames(frame);
}
