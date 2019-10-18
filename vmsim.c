#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "vmsim.h"
#include "assert.h"

counter_t accesses = 0, tlb_hits = 0,
 tlb_misses = 0, page_faults = 0, disk_writes = 0, shutdown_writes = 0;

// function headers
vpn_t vpn_translation(addr_t);
vpn_t vpn_offset(addr_t);

//COUNTERS FOR DEBUGGING
int randCount = 0;
int regCount = 0;

byte_t mem[MEM_SIZE]; //static allocation of memory
// physical address space 2^20
// ---------------------------------------------------------
//                      New Globals
// ---------------------------------------------------------
struct TLB_entry *TLB;
struct FT_entry *frametable;
struct PT_entry *pagetable;
//Page Table Base Register
int PTBR = 0;
// USEFUL MASKS
int dirty_mask = 0x1; // -> 0001
int valid_mask = 0x2; // -> 0010;

// ---------------------------------------------------------

// ---------------------------------------------------------
//                      New Structs
// ---------------------------------------------------------
// Transition Lookaside Buffer
typedef struct TLB_entry {
    ppn_t ppn;
    int dirty;
    int valid;
    vpn_t vpn;
    int age_bit; //pointer to "age bit" array -> # of accesses
} TLB_entry;

// Frame Table Entry
typedef struct FT_entry { //4 BYTES
    uint8_t mapped;     // 8 bits -> 1 if the frame is mapped, 0 if otherwise.
    uint8_t protected;  // 8 bits -> 1 if the frame is protected, 0 if otherwise.
    vpn_t vpn;          // 16 bit -> 2^9 pages (2bytes)
} FT_entry;

// Page Table Entry limited to 2 bytes
typedef struct PT_entry {
    unsigned short ppn_valid_dirty; // 2 bytes -> 16 bits (2 bytes)
    // indexes: 10 - 2  (9 bits)    -> ppn
    // index:   1       (1 bit)     -> valid
    // index:   0       (1 bit)     -> dirty
} PT_entry;
// ---------------------------------------------------------


/* 0. Zero out memory
*  1. Initialize your TLB (do not place FT or PT in TLB)
*  2. Create a frame table and place it into mem[].
*   - The frame table should reside in the first frame of memory
*   - The frame table should occupy no more than one frame, FTE size is up to you
*  3. Create a page table and place it into mem[]
*   - The page table will reside in the sequential frames of memory after the FT
*   - The page table should not reside in the same frame as the frame table
*   - Each PTE is limited to 2 bytes maximum
*  4. Mark all the frames holding the FT and PT as mapped and protected
*
*/
void system_init()
{
    // ---------------------------------------------------------
    // zero out memory
    // ---------------------------------------------------------
    for(int i = 0; i < MEM_SIZE; i++) {
        mem[i] = 0;
    }
    // printf("sizeof FTE: %lu\n", sizeof(FT_entry));
    // ---------------------------------------------------------
    // initialize the TLB to be fully associative with 5 entries
    // (do not palce FT or PT in the TLB)
    // ---------------------------------------------------------
    TLB = (TLB_entry*)malloc(TLB_SIZE * sizeof(TLB_entry));
    for(int j = 0; j < TLB_SIZE; j++) {
        TLB[j].ppn = (uint32_t)malloc(sizeof(uint32_t));
        TLB[j].dirty = (int)malloc(sizeof(int));
        TLB[j].valid = (int)malloc(sizeof(int));
        TLB[j].vpn = (addr_t)malloc(sizeof(addr_t));
        TLB[j].age_bit = (addr_t)malloc(sizeof(addr_t));
    }
    // set all entries to be zero.
    for(int j = 0; j < TLB_SIZE; j++) {
        TLB[j].ppn = 0;
        TLB[j].dirty = 0;
        TLB[j].valid = 0;
        TLB[j].vpn = 0;
        TLB[j].age_bit = 0;
    }
    // ---------------------------------------------------------
    //                  create the Frame Table
    // ---------------------------------------------------------
    // main mem is 2^21 bytes / sizeof(page) 2^12 bytes = 2^9 entries = 512 entries
    // this is calculated as a global.
    // 512 * 4 = 2048 < size of one page (4096 -> 4KiB)
    // set frame table to point to first spot in memory array
    // ---------------------------------------------------------
    // this is a pointer to the frame that the frame_table occurpies
    frametable = (FT_entry*) mem;
    // ---------------------------------------------------------
    //                  create the Page Table
    // ---------------------------------------------------------
    // this will be a pointer to the first frame AFTER the frame table
    pagetable = (PT_entry*) (mem + PAGE_SIZE);
    // make sure that all of the entires in the Frame Table and the
    // Page Table are mapped and protected
    // frame table
    frametable[0].protected = 1;
    frametable[0].mapped = 1;
    // page table -> 2^24 / sizeof(page) 2^12 =
    // 2^12 * 2 bytes = 2 pages
    frametable[1].protected = 1;   // page table
    frametable[1].mapped = 1;      // page table
    frametable[2].protected = 1;   // page table
    frametable[2].protected = 1;   // page table
}

/* At system shutdown, you need to write all dirty
* frames back to disk
* These are different from the dirty pages you write back on an eviction
* Count the number of dirty frames in the physical memory here and store it in
* shutdown_writes
* finally, return mem
*/
byte_t* system_shutdown()
{
    // loop through PT entries -> count the amount of dirty bits
    uint8_t dirty = 0, valid = 0;
    for (int i = 0; i < PAGE_SIZE; i++) {
        dirty = pagetable[i].ppn_valid_dirty & dirty_mask;
        // printf("dirty: %d\n", dirty);
        valid = (pagetable[i].ppn_valid_dirty & valid_mask) >> 1;
        // printf("valid: %d\n", valid);
        if (dirty == 1 & valid == 1) {
            shutdown_writes++;
        }
    }
  return mem;
}

/*
* Check the TLB for an entry
* returns HIT or MISS
* If HIT, returns the physcial address in paddr
* Updates the state of TLB based on write
*
*/
status_t check_TLB(addr_t vaddr, uint write, addr_t* paddr)
{
    // address conversion
    vpn_t vpn = vpn_translation(vaddr);
    vpn_t offset = vpn_offset(vaddr);
    // offset length
    int offset_length = log2(PAGE_SIZE);
    // loop over TLB entries
    for(int j = 0; j < TLB_SIZE; j++) {
        if (TLB[j].valid == 1 & TLB[j].vpn == vpn) {
            // return physical address
            // shift ppn over the offset_length and concat with the offset bits
            regCount++;
            // printf("%d\n", regCount);
            *paddr = TLB[j].ppn << offset_length | offset;
            // hit
            tlb_hits++;
            // if this is a write -> make the entry dirty
            if (write) {
                TLB[j].dirty = 1;
            }
            // return HIT
            return HIT;
        }
    }
    tlb_misses++;
    // do not return the pyhsical address
    return MISS;
}

/*
* Check the Page Table for an entry
* returns HIT or MISS
* If HIT, returns the physcial address in paddr
* Updates the state of Page Table based on write
*/
status_t check_PT(addr_t vaddr, uint write, addr_t* paddr)
{
    // address conversion
    vpn_t vpn = vpn_translation(vaddr);
    vpn_t offset = vpn_offset(vaddr);
    // get the valid bit from the page table entry
    int valid = pagetable[vpn].ppn_valid_dirty & valid_mask;
    // if not valid -> increment counters
    // return this?
    int ppn_mask = 0x7FC; // -> 0000011111111100
    //shfit
    int offset_length = log2(PAGE_SIZE);
    // physical address
    // paddr_t phy_addr;
    // new ppn
    ppn_t ppn;
    if (valid) {
        // printf("HERE");
        // build the physical address
        // paddr_t phy_addr = (paddr_t) ((pfn << OFFSET_LEN) | offset);
        // printf("HERE");
        *paddr = (((ppn_mask) & pagetable[vpn].ppn_valid_dirty) << (offset_length - 2)) | offset;
        // return HIT
        return HIT;
    } else {
        page_faults++;
        // get a new page
        if (write) {
            // make dirty
            pagetable[vpn].ppn_valid_dirty = pagetable[vpn].ppn_valid_dirty | dirty_mask;
        }
        ppn = page_fault(vaddr, write);
        // i++;
        // printf("\nvirt add: %lu", vaddr);
        // printf("\npage num: %d", ppn);
        *paddr = ppn << offset_length | offset;
        // printf("address: %lu\n", *paddr);
        // printf("\ni: %d", i);
        return MISS;
    }
}
// ---------------------------------------------------------
//                    Helper Methods
/*  --------------------make-------------------------------------
    Extract the virual page number from the virtual address
    ---------------------------------------------------------
*/
vpn_t vpn_translation(addr_t vaddr) {
    int offset = log2(PAGE_SIZE);
    return vaddr >> offset;
}
/*  ---------------------------------------------------------
    Extract the offset from the virtual address;
    same mapping to physical address.
    ---------------------------------------------------------
*/
vpn_t vpn_offset(addr_t vaddr) {
    int offset = log2(PAGE_SIZE);
    addr_t mask = 0xFFFFFFFF;
    mask = mask << offset;
    mask = ~mask;
    // printf("MASK: %d\n", mask);
    // printf("vaddr: %lu\n", vaddr);
    // printf("vpn: %lu\n", (vaddr & mask));
    return (vaddr & mask) >> offset;
}
// ---------------------------------------------------------
/*
* Updates the TLB
* If you hit in the tlb, you still need to update LRU status
* as well as dirty status:
*   Remember, if the dirty bit is newly set, you should go to memory and
*   mark the corresponding PTE dirty
* If you miss in the TLB, you need to replace:
*   First, check the TLB for any invalid entries
*   If one is found, install the vaddr mapping into it
*   Otherwise, consult an LRU replacement policy
*
* When a TLB entry is kicked out, what should you do? Anything?
*/
void update_TLB(addr_t vaddr, uint write, addr_t paddr, status_t tlb_access)
{
    // address conversion
    vpn_t vpn = vpn_translation(vaddr);
    vpn_t offset = vpn_offset(vaddr);
    int offset_length = log2(PAGE_SIZE);
    // new ppn to store on the miss -> whether found invalid or replace:
    ppn_t ppn = paddr >> offset_length;
    // TLB ACCESSING
    int foundInvalid = 0;
    vpn_t vpnOld = 0;
    int valid = 0;
    uint8_t dirty = 0;
    if (tlb_access == HIT) { // HIT
        for(int j = 0; j < TLB_SIZE; j++) {
            if (TLB[j].vpn == vpn && TLB[j].valid == 1) {
                TLB[j].age_bit = accesses; //increment the age bit
                                        // you update the PAGE TABLE
                // extract the dirty bit
                if (write) {
                    // update TLB
                    TLB[j].dirty = 1;
                    // update
                    // concat this to override -> mark the corresponding PTE dirty
                    pagetable[vpn].ppn_valid_dirty = pagetable[vpn].ppn_valid_dirty | 0x1;
                }
            }
        }
    } else { // MISS
        // printf("HERE");
        // data to confirm the min access age_bit
        int min = TLB[0].age_bit;
        int minIndex = 0;
        // check for invalid entries
        for(int j = 0; j < TLB_SIZE; j++) {
            if (TLB[j].valid == 0) {
                // update valid, VPN and ppn
                TLB[j].valid = 1;
                TLB[j].vpn = vpn;
                TLB[j].ppn = ppn;
                TLB[j].age_bit = accesses;
                foundInvalid = 1;
                // write through
                vpnOld = TLB[j].vpn;
                // write MISS
                if (write) {
                    TLB[j].dirty = 1;
                    dirty = TLB[j].dirty;
                } else { // read miss
                    TLB[j].dirty = 0;
                    dirty = TLB[j].dirty;
                }
                valid = TLB[j].valid;
                // printf("here: %lu\n", vpnOld);
                // write through!!
                pagetable[vpnOld].ppn_valid_dirty = (TLB[j].ppn << 2) | valid << 1 | dirty;

                break;
            }
        }
        if (!foundInvalid) {
        // look for the smallest age bit
            for(int j = 1; j < TLB_SIZE; j++) {
                if (TLB[j].age_bit < min) {
                    min = TLB[j].age_bit;
                    minIndex = j;
                }
            }
            // LRU REPLACEMENT POLICY
            // KICK OUT
            vpnOld = TLB[minIndex].vpn;
            dirty = TLB[minIndex].dirty;
            valid = TLB[minIndex].valid;
            // printf("here: %lu\n", vpnOld);
            pagetable[vpnOld].ppn_valid_dirty = (TLB[minIndex].ppn << 2) | valid << 1 | dirty ;
            // printf("here: %hu\n", pagetable[vpnOld].ppn_valid_dirty);
            // update with new
            TLB[minIndex].valid = 1;
            TLB[minIndex].vpn = vpn;
            TLB[minIndex].ppn = ppn;
            // update the accsess
            if (write) {
                // printf("WRITE\n");
                TLB[minIndex].dirty = 1;
            } else {
                // printf("READ\n");
                TLB[minIndex].dirty = 0;
            }
            TLB[minIndex].age_bit = accesses;
        }
    }
}


/*
* Called on a page fault
* First, search the frame table, starting at fte 0 and iterating linearly through all
* the frame table entries looking for unmapped unprotected frame.
* If one is found, use it.
* Otherwise, choose a frame randomly using rand(). Consider all frames possible in rand(),
* even the frame table and page table themselves.
* If the number lands on a protected frame, roll a random number again - repeat until you land
* on an unprotected frame, then replace that frame.
* If the frame being replaced is dirty, increment disk_writes
* If the frame being accessed is in the TLB, mark it invalid in the TLB (here, not in update_TLB)
* Update the frame table with the the VPN as well
*
* returns the PPN of the new mapped frame
*/
uint32_t page_fault(addr_t vaddr, uint write)
{
    // virtual address translation
    vpn_t vpn = vpn_translation(vaddr);
    vpn_t offset = vpn_offset(vaddr);
    // this will be the open page
    int found = 0;
    int foundPage = 0;
    // if the frame at specific index IS NOT mapepd or protected
    // save this index
    for(int i = 0; i < 512; i++) {
        if (frametable[i].mapped == 0 && frametable[i].protected == 0) {
            foundPage = i;
            found = 1;
            break;
        }
    }
    // rand num
    // num = (rand() % (upper – lower + 1)) + lower
    // random index
    int randFrame;
    // page table info --------
    PT_entry newPTE;
    // dirty info
    int dirty = 0;
    // pyhsical page number masked
    int ppn_mask = 0x7FC; // -> 0000011111111100
    // page
    ppn_t ppn;
    // if found -> map it, include the vpn, return the foundIndex
    if (found) { // empty
        // regCount++;
        // set parameters for the frameTable
        frametable[foundPage].mapped = 1;
        frametable[foundPage].vpn = vpn;
        // update the pagetable -> _ _ _ _ _ 1 1 1 1 1 1 1 1 1 0 0
        // save the dirty bits
        dirty = pagetable[vpn].ppn_valid_dirty & dirty_mask;
        // concate it with the page found and make it valid
        // shift it over 2 (valid | dirty)
        // make it valid        -> _ _ _ _ _ 1 1 1 1 1 1 1 1 1 0 1
        // build the page tabel entry -> make it valid and save the dirty
        pagetable[vpn].ppn_valid_dirty = foundPage << 2 | 0x2 | dirty;
        // return
        return foundPage;
    } else { // random
        // randCount++;
        ppn_t randPage = (rand() % 512);
        // continue to search for a not protected index
        while (frametable[randPage].protected) {
            randPage = (rand() % 512);
        }
        // save the old VPN to set the valid bit to 0
        vpn_t vpnOld = frametable[randPage].vpn;
        // old dirty
        dirty = pagetable[vpnOld].ppn_valid_dirty & dirty_mask;
        // now build the page table entry
        // mark the old as invalid
        /// -> 0 0 0 0 0 1 1 1 1 1 1 1 1 1 0 1
        // -> this makes it invalid
        pagetable[vpnOld].ppn_valid_dirty = randPage << 2 | dirty;
        // update the old vpnTemp
        // update the frametable
        frametable[randPage].mapped = 1;
        frametable[randPage].vpn = vpn;
        // is this frame being replaced PTE dirty using the mask.
        //printf("HERE\n");
        dirty = pagetable[vpnOld].ppn_valid_dirty & dirty_mask;
        // printf("dirty: %d", dirty);
        if (dirty) { // if the old is dirty
            disk_writes++;
        }
        // update the pagetable -> _ _ _ _ _ 1 1 1 1 1 1 1 1 1 0 0
        // save the valid and dirty bits
        dirty = pagetable[vpn].ppn_valid_dirty & dirty_mask;
        // concate it with the page found and make it valid
        // shift it over 2 (valid | dirty)
        // make it valid        -> _ _ _ _ _ 1 1 1 1 1 1 1 1 1 1 0
        // build the page table entry
        pagetable[vpn].ppn_valid_dirty = randPage << 2 | 0x2 | dirty;
        // *-------------------------------------------------------------
        // if the new randPage is in the TLB -> make invalid
        // you do not want this mapping to be used.
        // *-------------------------------------------------------------
        for(int j = 0; j < TLB_SIZE; j++) {
            if (TLB[j].ppn == randPage) { 
                TLB[j].valid = 0; // make sure that this mapping cannot be used.
            }
        }
        // *-------------------------------------------------------------
        return randPage;
        // If the frame being accessed is in the TLB, mark it
        // invalid in the TLB (here, not in update_TLB)
    }
    return 0;
}


/* Called on each access
* If the access is a write, update the memory in mem, return data
* If the access is a read, return the value of memory at the physical address
* Perform accessed in the following order
* Address -> read TLB -> read PT -> Update TLB -> Read Memory
*/
byte_t memory_access(addr_t vaddr, uint write, byte_t data)
{
    addr_t paddr;
    // First, we check the TLB
    status_t tlbAccess = check_TLB(vaddr, write, &paddr);
    // access
    accesses++;
    // check the TLB with the enum HIT or MISS
    if (tlbAccess == MISS) {
        status_t pgtblAccess = check_PT(vaddr, write, &paddr);
        // the address gets assigned inside of page fault
    }
    update_TLB(vaddr, write, paddr, tlbAccess); //update TLB after each access
    // Do memory stuff
    if(write) mem[paddr] = data; //Update mem on write
    // printf("address: %lu\n", paddr);
    return mem[paddr];

}

/* You may not change this method in your final submission!!!!!
*   Furthermore, your code should not have any extra print statements
*/
void vm_print_stats()
{
    printf("%llu, %llu, %llu, %llu, %llu, %llu\n", accesses, tlb_hits, tlb_misses, page_faults, disk_writes, shutdown_writes);
}
