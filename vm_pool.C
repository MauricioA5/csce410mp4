#include "vm_pool.H"
#include "page_table.H"
#include "assert.H"

VMPool::VMPool(unsigned long base, unsigned long size,
               ContFramePool * frames, PageTable * table)
    : base_address(base), pool_size(size), frame_pool(frames),
      page_table(table), next(nullptr), regions((Region *)base), region_count(1)
{
    assert(frames && table);
    assert(base >= 0x400000UL && base < 0xFFC00000UL);
    assert(base % Machine::PAGE_SIZE == 0 && size % Machine::PAGE_SIZE == 0);
    assert(size >= Machine::PAGE_SIZE && size <= 0xFFC00000UL - base);
    // Register BEFORE writing metadata. The first write faults; legitimacy of
    // the reserved metadata page must not itself read the uninitialized array.
    page_table->register_pool(this);
    regions[0].start = base;
    regions[0].size = Machine::PAGE_SIZE;
}

unsigned long VMPool::allocate(unsigned long size)
{
    if (!size || size > pool_size - Machine::PAGE_SIZE ||
        region_count == REGION_CAPACITY) return 0;
    unsigned long rounded = ((size - 1) / Machine::PAGE_SIZE + 1) * Machine::PAGE_SIZE;
    unsigned long candidate = base_address + Machine::PAGE_SIZE;
    unsigned long pos = 1;
    // First fit over gaps between sorted allocation records. Free regions are
    // implicit, so adjacent holes naturally merge without another metadata array.
    for (; pos < region_count; ++pos) {
        if (rounded <= regions[pos].start - candidate) break;
        candidate = regions[pos].start + regions[pos].size;
    }
    if (rounded > base_address + pool_size - candidate) return 0;
    for (unsigned long i = region_count; i > pos; --i) regions[i] = regions[i - 1];
    regions[pos].start = candidate;
    regions[pos].size = rounded;
    ++region_count;
    return candidate;
}

void VMPool::release(unsigned long start)
{
    unsigned long pos = 1;
    while (pos < region_count && regions[pos].start != start) ++pos;
    assert(pos < region_count);
    if (pos == region_count) return;
    unsigned long pages = regions[pos].size / Machine::PAGE_SIZE;
    for (unsigned long i = 0; i < pages; ++i)
        page_table->free_page(start / Machine::PAGE_SIZE + i);
    for (unsigned long i = pos + 1; i < region_count; ++i) regions[i - 1] = regions[i];
    --region_count;
}

bool VMPool::is_legitimate(unsigned long addr)
{
    if (addr < base_address || addr - base_address >= pool_size) return false;
    if (addr - base_address < Machine::PAGE_SIZE) return true;
    for (unsigned long i = 1; i < region_count; ++i)
        if (addr >= regions[i].start && addr - regions[i].start < regions[i].size)
            return true;
    return false;
}
