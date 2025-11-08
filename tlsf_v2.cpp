#include <bitset>
#include <iostream>
#include <utility>
#include <cmath>

#define SUB_BIN_COUNT 4
#define BIN_COUNT 8
#define POOL_COUNT 10
#define HEAP_SIZE 1<<15
#define MIN_BLOCK_SIZE 32
#define BLOCK_FREE_BIT 0x1                                                              //Set bit indicates that the block is in the free list

class TLSF
{
private:
    struct MemoryPool
    {
        void* startPointer {};
        void* endPointer {};                                                            //Both are inclusive
    };

    struct EntryHeader
    {
        EntryHeader* physical_prev;
        EntryHeader* free_list_next;
        EntryHeader* free_list_prev;
        size_t size_of_block;                                                   //The LSB of the size is used as a flag to indicate whether or not the block is in the
    };                                                                          //free list
    

    struct SecondBin
    {
        EntryHeader* free_list_head { };
    };

    struct FirstBin
    {
        SecondBin sub_bins[SUB_BIN_COUNT];
        std::bitset<SUB_BIN_COUNT> sub_bin_status;                                      //here, 0 would mean that the sub-bin is full
        bool fb_full{true};                                                             //will be used to maintain the bitset at the tlsf
    };

    template <size_t SIZE>
    int ffs(std::bitset<SIZE>& status, int index){
        index++;
        while(!status[index] && index < SIZE)
            index++;
        return index;
    }

    bool suffSpace(size_t size, EntryHeader* list_head){
        EntryHeader* ptr = list_head;
        while (ptr){
            if(ptr->size_of_block >= size)
                return true;
        }
        return false;
    }

    void insertMemIntoPool(char* pool_head_ptr){
        if(used_pool_count >= POOL_COUNT){
            std::cerr << "Heap memory is a out of space; reached maximum number of pools possible";
            exit;           
        }                                                            
        pools[used_pool_count].startPointer = pool_head_ptr;
        pools[used_pool_count].endPointer = pool_head_ptr + static_cast<size_t>(HEAP_SIZE) - 1;
        used_pool_count++;
        std::cout<< "Used pool count: " << used_pool_count<<"\n";
    }

    void expandHeap(){                                                                  //expands the heap by HEAP_SIZE each time that it is called
        EntryHeader* expanded_heap = (EntryHeader*)std::malloc(HEAP_SIZE);
        *expanded_heap = {nullptr,nullptr,nullptr,HEAP_SIZE ^ BLOCK_FREE_BIT};
        auto& sub_bin_involved = bins[BIN_COUNT - 1].sub_bins[SUB_BIN_COUNT - 1];
        EntryHeader* cur_head = sub_bin_involved.free_list_head;

        sub_bin_involved.free_list_head = expanded_heap;
        expanded_heap->free_list_next = cur_head;

        if(cur_head) cur_head->free_list_prev = expanded_heap;
        
        updateBitset(BIN_COUNT - 1, SUB_BIN_COUNT - 1);
        insertMemIntoPool(reinterpret_cast<char*>(expanded_heap));

        return;
    }

    std::pair<int,int> findIndex(size_t size){
        int bin_index = std::min(BIN_COUNT - 1,static_cast<int>(log(size)));
        int bin_interval = 1 << bin_index;
        int sub_bin_interval = bin_interval/SUB_BIN_COUNT;
        int sub_bin_index = std::min(SUB_BIN_COUNT - 1,(static_cast<int>(size) - bin_interval)/sub_bin_interval);
        return {bin_index, sub_bin_index};
    }


    std::pair<int,int> findBucket(size_t size){
        auto [bin_index, sub_bin_index] = findIndex(size);
        while (bin_index < BIN_COUNT){
            while (sub_bin_index < SUB_BIN_COUNT){
                if(suffSpace(size, bins[bin_index].sub_bins[sub_bin_index].free_list_head))
                    return {bin_index, sub_bin_index};
                sub_bin_index = ffs(bins[bin_index].sub_bin_status, sub_bin_index);
            }
            sub_bin_index = 0;
            bin_index = ffs(bin_status, bin_index);
        }
        expandHeap();
        return {BIN_COUNT - 1, SUB_BIN_COUNT - 1};
    }

    void updateBitset(int bin_index, int sub_bin_index) {
        auto& bin_involved = bins[bin_index];
        auto& sub_bin_involved = bin_involved.sub_bins[sub_bin_index];
        auto& free_list_involved = sub_bin_involved.free_list_head;
        if(free_list_involved){
            bin_involved.fb_full = false;
            bin_involved.sub_bin_status.set(sub_bin_index);
            tlsf_full = false;
            bin_status.set(bin_index);
        } else {
            bin_involved.sub_bin_status.reset(sub_bin_index);
            if(bin_involved.sub_bin_status.none()){
                bin_involved.fb_full = true;
                bin_status.reset(bin_index);
                if(bin_status.none()){
                    tlsf_full = true;
                }
            }
        }
    }

    bool blockIsFree(EntryHeader* block){
        return block->size_of_block & BLOCK_FREE_BIT;
    }

    EntryHeader* findPhysicalNext(EntryHeader* block){

        size_t size { block->size_of_block };
        char* ptr { reinterpret_cast<char*>(block) };
        EntryHeader* physical_next {};
        
        for(int idx{}; idx < used_pool_count; idx++){
            void* begin = pools[idx].startPointer;
            void* end = pools[idx].endPointer;
            if( ptr >= begin and ptr + size <= end){
                physical_next = reinterpret_cast<EntryHeader*>(ptr+size);
                break;
            }
        }

        return physical_next;
    }

    EntryHeader* findPhysicalPrevious(EntryHeader* block)
    {
        return block->physical_prev;
    }

    void coalesce( EntryHeader* first, EntryHeader* second ){                                  //This is called only once both blocks are not in the free list

        std::cout << "Size of the first block: " << first->size_of_block << "\n";
        std::cout << "Size of the second block: " << second->size_of_block << "\n";

        first->size_of_block += second->size_of_block;
        
        EntryHeader* next = findPhysicalNext(second);
        if( next ){
            next->physical_prev = first;
        }

    }

    void split(EntryHeader* cur_block, size_t req_size)                         //The block is no longer in the free list when split is called
    {
        char* ptr = reinterpret_cast<char*>(cur_block);
        ptr += req_size;
        EntryHeader* extra_block { reinterpret_cast<EntryHeader*>(ptr) };

        size_t extra_size { cur_block->size_of_block - req_size };
        cur_block->size_of_block = req_size;
        extra_block->size_of_block = extra_size;

        extra_block->physical_prev = cur_block;

        deallocate(extra_block);

    }

    void removeFromFreeList(EntryHeader* block_to_remove){                              //Will be called exclusively when block is certainly on the list
        EntryHeader* prev_list_entry { block_to_remove->free_list_next };
        EntryHeader* next_list_entry { block_to_remove -> free_list_next };
        auto [block_bin, block_sub_bin] = findIndex(block_to_remove->size_of_block);

        if(prev_list_entry)
            prev_list_entry->free_list_next = next_list_entry;
        else{
            bins[block_bin].sub_bins[block_sub_bin].free_list_head = next_list_entry;
        }
        if(next_list_entry)
            next_list_entry->free_list_prev = prev_list_entry;

        block_to_remove->size_of_block ^= BLOCK_FREE_BIT;
        updateBitset(block_bin, block_sub_bin);
        
        return;
    }
    
    bool tlsf_full {true};
    FirstBin bins[BIN_COUNT];
    MemoryPool pools[POOL_COUNT];
    std::bitset<BIN_COUNT> bin_status;
    int used_pool_count { 0 };

public:
    void* allocate(size_t req_size){

        auto [bin_index, sub_bin_index] = findBucket(req_size);
        auto& sub_bin_involved = bins[bin_index].sub_bins[sub_bin_index];
        EntryHeader* list_ptr = sub_bin_involved.free_list_head;

        while(list_ptr->size_of_block < req_size)
            list_ptr = list_ptr->free_list_next;

        removeFromFreeList(list_ptr);
        
        EntryHeader* allocated_block = list_ptr;

        if(allocated_block->size_of_block - req_size > MIN_BLOCK_SIZE)
            split(allocated_block, req_size);
        return allocated_block;

    }

    void deallocate(void* ptr)
    {
        EntryHeader* block { reinterpret_cast<EntryHeader*>(ptr) };
        size_t block_size {block->size_of_block};
        EntryHeader* physical_prev { findPhysicalPrevious(block) };
        EntryHeader* physical_next { findPhysicalNext(block) };

        if( physical_next && blockIsFree(physical_next) ){
            removeFromFreeList(physical_next);
            coalesce(block, physical_next);
        }

        if( physical_prev && blockIsFree(physical_prev) ){
            removeFromFreeList(physical_prev);
            coalesce(physical_prev, block);
            block = physical_prev;
        }

        auto [fl_index, sl_index] = findIndex(block->size_of_block);
        
        EntryHeader* old_head = bins[fl_index].sub_bins[sl_index].free_list_head;
        bins[fl_index].sub_bins[sl_index].free_list_head = block;
        block->free_list_prev = nullptr;
        block->free_list_next = old_head;
        block->size_of_block |= BLOCK_FREE_BIT;

        if(old_head)
            old_head->free_list_prev = block;
        
        updateBitset(fl_index, sl_index);

        return;

    }

    friend std::ostream& operator<<(std::ostream& out, const TLSF& tlsf);
};

std::ostream& operator<<(std::ostream& out, const TLSF& tlsf){
    for(int bin_index { 0 }; bin_index < BIN_COUNT; bin_index++){
        std::cout << bin_index <<": \n";
        for (int sub_bin_index {0}; sub_bin_index < SUB_BIN_COUNT; sub_bin_index++){
            std::cout << "\t" << sub_bin_index;
            TLSF::EntryHeader* list_ptr = tlsf.bins[bin_index].sub_bins[sub_bin_index].free_list_head;
            while(list_ptr){
                std::cout << "->" << list_ptr->size_of_block;
                list_ptr = list_ptr->free_list_next;
            }
            std::cout << "\n";
        }
    }
    return out;
}


int main(){
    TLSF tlsf{};
    std::cout << tlsf;
    std::cout << "___________\n";
    for (int i {}; i<10; ++i){
        tlsf.allocate(1<<10);
    }
    std::cout << tlsf;
}