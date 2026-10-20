#include "guest.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "virtual_memory.c"
typedef struct { SpMemory memory; uint64_t address; size_t requested; } Allocation;
typedef struct { GuestVm *vm; uint64_t address; } GuestRegion;
typedef struct { SpMutex *native; uint64_t address; unsigned depth; int recursive; } GuestMutex;
typedef struct { uint64_t function,argument,dso; } GuestDestructor;
typedef struct {
    SpGuestImage *main,*sdk; const SpGuestProgram *program,*sdk_program;
    const char *sdk_imports[1024]; size_t sdk_import_count;
} Bootstrap;
typedef struct {
    SpGuestImage *image; const SpGuestProgram *program;
    SpMemory stack,tls; jmp_buf escape; int error;
    uint64_t fs_allocate,fs_release;
    Allocation heap[1024]; uint64_t next_address; size_t allocated_bytes;
    GuestMutex mutexes[512];
    Bootstrap *bootstrap;
    GuestDestructor destructors[512]; size_t destructor_count;
    SpTls *tls_slots[64]; uint64_t tls_destructors[64];
    GuestRegion regions[16]; uint64_t next_region;
} Execution;
static Execution *active;
uint64_t sp_recomp_budget;
static char last_error[256];
const char *sp_guest_last_error(void) { return last_error; }
static void fail(int error,const char *what,uint64_t address) {
    snprintf(last_error,sizeof last_error,"%s at 0x%llx",what,(unsigned long long)address);
    sp_log("GUEST STOP: %s",last_error);
    if (active) { active->error=error; longjmp(active->escape,1); }
    abort();
}
void sp_recomp_checkpoint(uint64_t pc) { fail(SP_TIMEOUT,"bootstrap checkpoint budget exhausted",pc); }
void *sp_recomp_access(uint64_t address,size_t size,int write) {
    if (!active) fail(SP_INVALID,"no active guest context",address);
    static unsigned char empty;
    if (!size) return &empty;
    void *pointer=sp_guest_pointer(active->image,address,size,write);
    if (!pointer && active->bootstrap && active->bootstrap->sdk)
        pointer=sp_guest_pointer(active->bootstrap->sdk,address,size,write);
    const uint64_t stack_base=0x7700000000ULL;
    if (!pointer && address>=stack_base && address-stack_base<=active->stack.size &&
        size<=active->stack.size-(address-stack_base)) pointer=(char *)active->stack.data+(address-stack_base);
    const uint64_t tls_base=0x7800000000ULL;
    if (!pointer && address>=tls_base && address-tls_base<=active->tls.size &&
        size<=active->tls.size-(address-tls_base)) pointer=(char *)active->tls.data+(address-tls_base);
    if (!pointer) for (size_t i=0;i<1024;++i) {
        Allocation *allocation=&active->heap[i];
        if (allocation->memory.data && address>=allocation->address && address-allocation->address<=allocation->requested &&
            size<=allocation->requested-(address-allocation->address)) {
            pointer=(char *)allocation->memory.data+(address-allocation->address); break;
        }
    }
    if (!pointer) for (size_t i=0;i<16;++i) {
        GuestRegion *region=&active->regions[i];
        if (region->vm && address>=region->address && address-region->address<=region->vm->requested)
            pointer=vm_pointer(region->vm,(size_t)(address-region->address),size,write);
        if (pointer) break;
    }
    if (!pointer) fail(SP_ADDRESS,write ? "invalid guest write" : "invalid guest read",address);
    return pointer;
}
void guest_trap(const char *what,uint64_t address) { fail(SP_UNSUPPORTED,what,address); }
void guest_unimpl(uint64_t pc,uint32_t instruction,const char *what) {
    (void)instruction; fail(SP_UNSUPPORTED,what,pc);
}
void guest_svc(Ctx *context,uint32_t number) { (void)context; fail(SP_UNSUPPORTED,"unimplemented guest SVC",number); }
void stx_diag(Ctx *context,uint64_t address,uint64_t expected,uint64_t seen,uint64_t value,int bits) {
    (void)context; (void)address; (void)expected; (void)seen; (void)value; (void)bits;
}
static void invoke(Ctx *context,uint64_t address) {
    if (active->bootstrap && address>=0x7600000000ULL && !( (address-0x7600000000ULL)%256 ) &&
        (address-0x7600000000ULL)/256<active->bootstrap->sdk_import_count)
        fail(SP_UNSUPPORTED,active->bootstrap->sdk_imports[(address-0x7600000000ULL)/256],address);
    if (address>=0x7300000000ULL && !( (address-0x7300000000ULL) % 256 ) &&
        (address-0x7300000000ULL)/256<active->program->symbol_count) {
        host_import(context,(int)((address-0x7300000000ULL)/256)); return;
    }
    const SpGuestProgram *program=active->program;
    if (address>=0x7200000000ULL && address<0x7300000000ULL && active->bootstrap && active->bootstrap->sdk_program)
        program=active->bootstrap->sdk_program;
    size_t low=0,high=program->function_count;
    while (low<high) {
        size_t middle=low+(high-low)/2;
        if (program->functions[middle].addr<address) low=middle+1; else high=middle;
    }
    if (low==program->function_count || program->functions[low].addr!=address)
        fail(SP_UNSUPPORTED,"function not statically recompiled",address);
    program->functions[low].fn(context);
}
void call_indirect(Ctx *context,uint64_t address) { if (!active) fail(SP_INVALID,"no guest",address); invoke(context,address); }
static Allocation *heap_find(uint64_t address) {
    for (size_t i=0;i<1024;++i) if (active->heap[i].memory.data && active->heap[i].address==address) return &active->heap[i];
    return NULL;
}
static uint64_t heap_allocate(size_t size,size_t align) {
    if (!align || (align&(align-1)) || align>1024*1024) return 0;
    if (size>128*1024*1024) return 0;
    if (!size) size=1;
    size_t rounded=(size+sp_page_size()-1)/sp_page_size()*sp_page_size();
    if (rounded>128*1024*1024-active->allocated_bytes) return 0;
    for (size_t i=0;i<1024;++i) if (!active->heap[i].memory.data) {
        Allocation *allocation=&active->heap[i];
        if (sp_memory_map(&allocation->memory,size,NULL)) return 0;
        uint64_t address=(active->next_address+align-1)&~(uint64_t)(align-1);
        if (address<0x7400000000ULL || address+allocation->memory.size>=0x7500000000ULL) {
            sp_memory_unmap(&allocation->memory); return 0;
        }
        allocation->address=address; allocation->requested=size;
        active->next_address=address+allocation->memory.size;
        active->allocated_bytes+=allocation->memory.size;
        return address;
    }
    return 0;
}
static void heap_release(uint64_t address) {
    if (!address) return;
    Allocation *allocation=heap_find(address);
    if (!allocation) fail(SP_ADDRESS,"free of unknown allocation",address);
    size_t size=allocation->memory.size;
    if (sp_memory_unmap(&allocation->memory)) fail(SP_SYSTEM,"heap unmap failed",address);
    active->allocated_bytes-=size;
}
static GuestMutex *guest_mutex(uint64_t address,int create,int recursive) {
    GuestMutex *empty=NULL;
    for (size_t i=0;i<512;++i) {
        GuestMutex *mutex=&active->mutexes[i];
        if (mutex->native && mutex->address==address) {
            if (create) fail(SP_INVALID,"mutex already initialized",address);
            return mutex;
        }
        if (!mutex->native && !empty) empty=mutex;
    }
    if (!create || !empty) fail(SP_INVALID,"unknown mutex or registry full",address);
    void *object=sp_recomp_access(address,0x20,1);
    if (sp_mutex_create(&empty->native)) fail(SP_SYSTEM,"native mutex creation failed",address);
    memset(object,0,0x20); empty->address=address; empty->recursive=recursive; empty->depth=0;
    return empty;
}
static GuestRegion *guest_region(uint64_t address,size_t size) {
    for (size_t i=0;i<16;++i) {
        GuestRegion *region=&active->regions[i];
        if (region->vm && address>=region->address && address-region->address<=region->vm->requested &&
            size<=region->vm->requested-(address-region->address)) return region;
    }
    fail(SP_ADDRESS,"unknown virtual memory range",address); return NULL;
}
static void import_named(Ctx *context,const char *name);
void host_import(Ctx *context,int index) {
    if (!active || index<0 || (size_t)index>=active->program->symbol_count) fail(SP_INVALID,"invalid import index",(uint64_t)index);
    const char *name=active->program->symbols[index].name;
    if (!name) fail(SP_INVALID,"null import name",(uint64_t)index);
    if (active->bootstrap && active->bootstrap->sdk_program && active->bootstrap->sdk) {
        Bootstrap *bootstrap=active->bootstrap; uint64_t address=0; unsigned kind=0;
        if (!sp_guest_export(bootstrap->sdk,name,&address,&kind) && kind==2) {
            const SpGuestProgram *program=bootstrap->sdk_program;
            size_t lo=0,hi=program->function_count;
            while (lo<hi) {
                size_t middle=lo+(hi-lo)/2;
                if (program->functions[middle].addr<address) lo=middle+1; else hi=middle;
            }
            if (lo<program->function_count && program->functions[lo].addr==address) { invoke(context,address); return; }
        }
    }
    import_named(context,name);
}
void sp_sdk_host_import(Ctx *context,int index) {
    const SpGuestProgram *program=active && active->bootstrap ? active->bootstrap->sdk_program : NULL;
    if (!program || index<0 || (size_t)index>=program->symbol_count) fail(SP_INVALID,"invalid SDK import",(uint64_t)index);
    import_named(context,program->symbols[index].name);
}
static void import_named(Ctx *context,const char *name) {
    size_t size=(size_t)context->x[2];
    if (!strcmp(name,"malloc") || !strcmp(name,"_Znwm") || !strcmp(name,"_Znam")) {
        context->x[0]=heap_allocate((size_t)context->x[0],16);
        if (!context->x[0] && strcmp(name,"malloc")) fail(SP_UNSUPPORTED,"C++ bad_alloc unwinding unavailable",context->x[30]);
    } else if (!strcmp(name,"calloc")) {
        uint64_t count=context->x[0],width=context->x[1];
        context->x[0]=(width && count>SIZE_MAX/width) ? 0 : heap_allocate((size_t)(count*width),16);
    } else if (!strcmp(name,"free") || !strcmp(name,"_ZdlPv") || !strcmp(name,"_ZdaPv")) {
        heap_release(context->x[0]);
    } else if (!strcmp(name,"aligned_alloc")) {
        size_t align=(size_t)context->x[0],bytes=(size_t)context->x[1];
        context->x[0]=align && !(bytes%align) ? heap_allocate(bytes,align) : 0;
    } else if (!strcmp(name,"posix_memalign")) {
        uint64_t destination=context->x[0]; size_t align=(size_t)context->x[1];
        if (align<sizeof(uint64_t) || (align&(align-1))) context->x[0]=22;
        else {
            uint64_t address=heap_allocate((size_t)context->x[2],align);
            if (address) memcpy(sp_recomp_access(destination,8,1),&address,8);
            context->x[0]=address ? 0 : 12;
        }
    } else if (!strcmp(name,"realloc")) {
        uint64_t old=context->x[0]; size_t bytes=(size_t)context->x[1];
        Allocation *previous=old ? heap_find(old) : NULL;
        if (old && !previous) fail(SP_ADDRESS,"realloc of unknown allocation",old);
        if (!bytes) { heap_release(old); context->x[0]=0; }
        else {
            uint64_t address=heap_allocate(bytes,16);
            if (address && previous) {
                size_t copied=previous->requested<bytes ? previous->requested : bytes;
                memcpy(sp_recomp_access(address,copied,1),previous->memory.data,copied); heap_release(old);
            }
            context->x[0]=address;
        }
    } else if (!strcmp(name,"memcpy") || !strcmp(name,"memmove")) {
        void *destination=sp_recomp_access(context->x[0],size,1);
        void *source=sp_recomp_access(context->x[1],size,0);
        memmove(destination,source,size);
    } else if (!strcmp(name,"memset")) {
        memset(sp_recomp_access(context->x[0],size,1),(int)context->x[1],size);
    } else if (!strcmp(name,"memcmp")) {
        context->x[0]=(uint64_t)(int64_t)memcmp(sp_recomp_access(context->x[0],size,0),sp_recomp_access(context->x[1],size,0),size);
    } else if (!strcmp(name,"strlen")) {
        size_t length=0;
        while (*(unsigned char *)sp_recomp_access(context->x[0]+length,1,0)) {
            if (++length>1024*1024) fail(SP_INVALID,"unterminated string",context->x[0]);
        }
        context->x[0]=length;
    } else if (!strcmp(name,"_ZN2nn2os11GetHostArgcEv") || !strcmp(name,"_ZN2nn2os11GetHostArgvEv")) {
        /* No command-line arguments are supplied by this bootstrap. */
        context->x[0]=0;
    } else if (!strcmp(name,"_ZN2nn2fs12SetAllocatorEPFPvmEPFvS1_mE")) {
        if (!sp_guest_pointer(active->image,context->x[0],4,0) ||
            !sp_guest_pointer(active->image,context->x[1],4,0)) fail(SP_INVALID,"invalid fs allocator callbacks",context->x[0]);
        active->fs_allocate=context->x[0]; active->fs_release=context->x[1];
    } else if (!strcmp(name,"_ZN2nn2os13GetSystemTickEv")) {
        uint64_t time=sp_now_us(); context->x[0]=(time/10)*192+(time%10)*192/10;
    } else if (!strcmp(name,"_ZN2nn2os22GetSystemTickFrequencyEv")) {
        context->x[0]=19200000;
    } else if (!strcmp(name,"_ZN2nn2os21AllocateAddressRegionEPmm")) {
        size_t index=0; while (index<16 && active->regions[index].vm) ++index;
        if (index==16) fail(SP_SYSTEM,"virtual region slots exhausted",context->x[1]);
        size_t size=(size_t)context->x[1];
        if (size>UINT64_C(4)*1024*1024*1024 || active->next_region>0x8900000000ULL-size)
            fail(SP_SYSTEM,"virtual reservation budget exceeded",size);
        GuestRegion *region=&active->regions[index];
        if (vm_reserve(&region->vm,size)) fail(SP_SYSTEM,"native virtual reservation failed",size);
        region->address=active->next_region; active->next_region+=(size+0x1fffff)&~UINT64_C(0x1fffff);
        memcpy(sp_recomp_access(context->x[0],8,1),&region->address,8); context->x[0]=0;
    } else if (!strcmp(name,"_ZN2nn2os19AllocateMemoryPagesEmm") || !strcmp(name,"_ZN2nn2os15FreeMemoryPagesEmm") ||
               !strcmp(name,"_ZN2nn2os19SetMemoryPermissionEmmi")) {
        uint64_t address=context->x[0]; size_t size=(size_t)context->x[1];
        GuestRegion *region=guest_region(address,size); size_t offset=(size_t)(address-region->address);
        int rc;
        if (!strcmp(name,"_ZN2nn2os19AllocateMemoryPagesEmm")) {
            size_t committed=0,extra=0,page=vm_host_page();
            for (size_t i=0;i<16;++i) if (active->regions[i].vm) committed+=active->regions[i].vm->committed_bytes;
            if (!vm_pages(region->vm,offset,size)) fail(SP_INVALID,"unaligned guest commit",address);
            for (size_t at=offset/page*page;at<(offset+size+page-1)/page*page;at+=page)
                if (!region->vm->host_present[at/page]) extra+=page;
            if (extra>512*1024*1024-committed) fail(SP_SYSTEM,"physical guest page budget exceeded",extra);
            rc=vm_commit(region->vm,offset,size);
        } else if (!strcmp(name,"_ZN2nn2os15FreeMemoryPagesEmm")) rc=vm_decommit(region->vm,offset,size);
        else rc=vm_permission(region->vm,offset,size,(unsigned)context->x[2]);
        if (rc) fail(rc,"virtual page operation failed",address);
        context->x[0]=0;
    } else if (!strcmp(name,"_ZN2nn2os17FreeAddressRegionEm")) {
        GuestRegion *region=guest_region(context->x[0],0);
        if (context->x[0]!=region->address) fail(SP_INVALID,"free of interior virtual address",context->x[0]);
        if (vm_close(&region->vm)) fail(SP_SYSTEM,"virtual reservation release failed",context->x[0]);
        context->x[0]=0;
    } else if (!strcmp(name,"_ZN2nn2os18SdkAllocateTlsSlotEPNS0_7TlsSlotEPFvmE") ||
               !strcmp(name,"_ZN2nn2os15AllocateTlsSlotEPNS0_7TlsSlotEPFvmE")) {
        uint32_t slot=0;
        while (slot<64 && active->tls_slots[slot]) ++slot;
        if (slot==64 || sp_tls_create(&active->tls_slots[slot])) fail(SP_SYSTEM,"TLS slots exhausted",context->x[0]);
        active->tls_destructors[slot]=context->x[1];
        memcpy(sp_recomp_access(context->x[0],4,1),&slot,4); context->x[0]=0;
    } else if (!strcmp(name,"_ZN2nn2os11GetTlsValueENS0_7TlsSlotE") ||
               !strcmp(name,"_ZN2nn2os11SetTlsValueENS0_7TlsSlotEm") ||
               !strcmp(name,"_ZN2nn2os11FreeTlsSlotENS0_7TlsSlotE")) {
        uint64_t slot=context->x[0];
        if (slot>=64 || !active->tls_slots[slot]) fail(SP_INVALID,"unknown guest TLS slot",slot);
        if (!strcmp(name,"_ZN2nn2os11GetTlsValueENS0_7TlsSlotE")) context->x[0]=(uint64_t)(uintptr_t)sp_tls_get(active->tls_slots[slot]);
        else if (!strcmp(name,"_ZN2nn2os11SetTlsValueENS0_7TlsSlotEm")) {
            if (sp_tls_set(active->tls_slots[slot],(void *)(uintptr_t)context->x[1])) fail(SP_SYSTEM,"TLS set failed",slot);
        } else if (sp_tls_destroy(&active->tls_slots[slot])) fail(SP_SYSTEM,"TLS release failed",slot);
    } else if (!strcmp(name,"_ZN2nn2os15InitializeMutexEPNS0_9MutexTypeEbi")) {
        guest_mutex(context->x[0],1,!!context->x[1]);
    } else if (!strcmp(name,"_ZN2nn2os9LockMutexEPNS0_9MutexTypeE") ||
               !strcmp(name,"_ZN2nn2os12TryLockMutexEPNS0_9MutexTypeE")) {
        GuestMutex *mutex=guest_mutex(context->x[0],0,0);
        int attempt=!strcmp(name,"_ZN2nn2os12TryLockMutexEPNS0_9MutexTypeE");
        if (mutex->depth && !mutex->recursive) {
            if (!attempt) fail(SP_INVALID,"recursive lock of nonrecursive guest mutex",context->x[0]);
            context->x[0]=0;
        } else {
            if (!mutex->depth && sp_mutex_lock(mutex->native)) fail(SP_SYSTEM,"native lock failed",context->x[0]);
            if (mutex->depth==UINT32_MAX) fail(SP_INVALID,"guest mutex recursion overflow",context->x[0]);
            ++mutex->depth;
            if (attempt) context->x[0]=1;
        }
    } else if (!strcmp(name,"_ZN2nn2os11UnlockMutexEPNS0_9MutexTypeE")) {
        GuestMutex *mutex=guest_mutex(context->x[0],0,0);
        if (!mutex->depth) fail(SP_INVALID,"unlock of unlocked guest mutex",context->x[0]);
        if (!--mutex->depth && sp_mutex_unlock(mutex->native)) fail(SP_SYSTEM,"native unlock failed",context->x[0]);
    } else if (!strcmp(name,"_ZN2nn2os13FinalizeMutexEPNS0_9MutexTypeE")) {
        GuestMutex *mutex=guest_mutex(context->x[0],0,0);
        if (mutex->depth) fail(SP_INVALID,"finalize of locked guest mutex",context->x[0]);
        if (sp_mutex_destroy(&mutex->native)) fail(SP_SYSTEM,"native mutex destroy failed",context->x[0]);
    } else if (!strcmp(name,"_ZN2nn3err18ErrorResultVariantC1Ev") || !strcmp(name,"_ZN2nn3err18ErrorResultVariantC2Ev")) {
        memset(sp_recomp_access(context->x[0],16,1),0,16);
    } else if (!strcmp(name,"_ZN2nn3err18ErrorResultVariantaSERKNS_6ResultE")) {
        uint32_t result; memcpy(&result,sp_recomp_access(context->x[1],4,0),4);
        memcpy(sp_recomp_access(context->x[0],4,1),&result,4);
    } else if (!strcmp(name,"_ZN2nn3ldn15MakeIpv4AddressEhhhh")) {
        context->x[0]=((context->x[0]&255)<<24)|((context->x[1]&255)<<16)|((context->x[2]&255)<<8)|(context->x[3]&255);
    } else if (!strcmp(name,"__cxa_guard_acquire")) {
        unsigned char *guard=sp_recomp_access(context->x[0],8,1);
        if (guard[0]) context->x[0]=0;
        else {
            if (guard[1]) fail(SP_INVALID,"recursive static initialization",context->x[0]);
            guard[1]=1; context->x[0]=1;
        }
    } else if (!strcmp(name,"__cxa_guard_release") || !strcmp(name,"__cxa_guard_abort")) {
        unsigned char *guard=sp_recomp_access(context->x[0],8,1);
        if (!guard[1]) fail(SP_INVALID,"static guard not acquired",context->x[0]);
        if (!strcmp(name,"__cxa_guard_release")) guard[0]=1;
        guard[1]=0;
    } else if (!strcmp(name,"__cxa_atexit")) {
        if (active->destructor_count==512) context->x[0]=(uint64_t)-1;
        else {
            active->destructors[active->destructor_count++]=(GuestDestructor){context->x[0],context->x[1],context->x[2]};
            context->x[0]=0;
        }
    } else if (!strcmp(name,"__cxa_finalize")) {
        uint64_t dso=context->x[0];
        for (size_t i=active->destructor_count;i>0;--i) {
            GuestDestructor destructor=active->destructors[i-1];
            if (destructor.function && (!dso || dso==destructor.dso)) {
                active->destructors[i-1].function=0; context->x[0]=destructor.argument; invoke(context,destructor.function);
            }
        }
    } else fail(SP_UNSUPPORTED,name,context->x[30]);
}
static int execute(SpGuestImage *image,const SpGuestProgram *program,uint64_t entry,Ctx *context,int sequence,Bootstrap *bootstrap) {
    if (!image || !program || !context || active || !program->functions || !program->function_count ||
        program->image_fingerprint!=sp_guest_fingerprint(image)) return SP_INVALID;
    for (size_t i=0;i<program->function_count;++i)
        if (!program->functions[i].fn || (i && program->functions[i-1].addr>=program->functions[i].addr)) return SP_INVALID;
    if (bootstrap && bootstrap->sdk_program) {
        const SpGuestProgram *sdk=bootstrap->sdk_program;
        if (!sdk->functions || !sdk->function_count) return SP_INVALID;
        for (size_t i=0;i<sdk->function_count;++i)
            if (!sdk->functions[i].fn || (i && sdk->functions[i-1].addr>=sdk->functions[i].addr)) return SP_INVALID;
    }
    Execution *execution=calloc(1,sizeof *execution);
    if (!execution) return SP_SYSTEM;
    execution->image=image; execution->program=program; execution->next_address=0x7400000000ULL; execution->next_region=0x7900000000ULL;
    execution->bootstrap=bootstrap;
    if (sp_memory_map(&execution->stack,1024*1024,NULL)) { free(execution); return SP_SYSTEM; }
    if (sp_memory_map(&execution->tls,64*1024,NULL)) { sp_memory_unmap(&execution->stack); free(execution); return SP_SYSTEM; }
    if (!context->x[31]) context->x[31]=0x7700000000ULL+execution->stack.size;
    if (!context->tpidr) context->tpidr=0x7800000000ULL;
    last_error[0]=0; active=execution; sp_recomp_budget=1000000;
    if (!setjmp(execution->escape)) {
        if (sequence) sp_log("GAME PHASE: nninitStartup");
        invoke(context,entry);
        if (sequence) {
            uint64_t array=0; size_t count=0;
            if (sp_guest_initializers(image,&array,&count)) fail(SP_INVALID,"invalid initializer array",array);
            sp_log("GAME PHASE: static constructors (%llu)",(unsigned long long)count);
            for (size_t i=0;i<count;++i) {
                uint64_t target; memcpy(&target,sp_recomp_access(array+i*8,8,0),8);
                if (target) {
                    sp_log("GAME CONSTRUCTOR: %llu at 0x%llx",(unsigned long long)i,(unsigned long long)target);
                    invoke(context,target);
                }
            }
            uint64_t main_entry=0;
            for (size_t i=0;i<program->symbol_count;++i)
                if (program->symbols[i].defined && !strcmp(program->symbols[i].name,"nnMain"))
                    main_entry=0x7100000000ULL+program->symbols[i].value;
            if (!main_entry) fail(SP_UNSUPPORTED,"nnMain export absent",0);
            sp_log("GAME PHASE: nnMain"); invoke(context,main_entry);
            uint64_t main_result=context->x[0];
            for (unsigned pass=0;pass<4;++pass) for (size_t i=0;i<64;++i)
                if (execution->tls_slots[i] && execution->tls_destructors[i]) {
                    uint64_t value=(uint64_t)(uintptr_t)sp_tls_get(execution->tls_slots[i]);
                    if (value) {
                        if (sp_tls_set(execution->tls_slots[i],NULL)) fail(SP_SYSTEM,"TLS destructor clear failed",i);
                        context->x[0]=value; invoke(context,execution->tls_destructors[i]);
                    }
                }
            for (size_t i=execution->destructor_count;i>0;--i) {
                GuestDestructor destructor=execution->destructors[i-1];
                if (destructor.function) {
                    execution->destructors[i-1].function=0;
                    context->x[0]=destructor.argument; invoke(context,destructor.function);
                }
            }
            context->x[0]=main_result;
        }
    }
    active=NULL; int rc=execution->error;
    for (size_t i=0;i<16;++i) if (execution->regions[i].vm) vm_close(&execution->regions[i].vm);
    for (size_t i=0;i<64;++i) if (execution->tls_slots[i]) sp_tls_destroy(&execution->tls_slots[i]);
    for (size_t i=0;i<512;++i) if (execution->mutexes[i].native) {
        if (execution->mutexes[i].depth) sp_mutex_unlock(execution->mutexes[i].native);
        sp_mutex_destroy(&execution->mutexes[i].native);
    }
    for (size_t i=0;i<1024;++i) if (execution->heap[i].memory.data) sp_memory_unmap(&execution->heap[i].memory);
    sp_memory_unmap(&execution->tls); sp_memory_unmap(&execution->stack); free(execution);
    return rc;
}
int sp_guest_run(SpGuestImage *image,const SpGuestProgram *program,uint64_t entry,Ctx *context) {
    return execute(image,program,entry,context,0,NULL);
}
extern const SpGuestProgram sp_smoke_program;
#ifdef SP_GAME_LINKED
typedef struct {
    SpGuestImage *image; const SpGuestProgram *program; uint64_t entry;
    Ctx *context; int sequence,result; Bootstrap *bootstrap;
} BootWork;
static void boot_on_fiber(void *argument) {
    BootWork *work=argument;
    work->result=execute(work->image,work->program,work->entry,work->context,work->sequence,work->bootstrap);
}
static int bootstrap_resolve(const char *name,uint64_t *address,void *argument) {
    Bootstrap *bootstrap=argument; const SpGuestProgram *program=bootstrap->program;
    /* Bind a symbolic thunk even for unsupported imports. The thunk fails on
     * first use, allowing startup before unused SDK APIs are implemented.
     * Binding is not evidence that an API has a working implementation. */
    unsigned kind=0;
    if (bootstrap->sdk && !sp_guest_export(bootstrap->sdk,name,address,&kind)) {
        if (kind==1) return SP_OK;
        if (bootstrap->sdk_program) {
            const SpGuestProgram *sdk=bootstrap->sdk_program;
            size_t lo=0,hi=sdk->function_count;
            while (lo<hi) {
                size_t middle=lo+(hi-lo)/2;
                if (sdk->functions[middle].addr<*address) lo=middle+1; else hi=middle;
            }
            if (lo<sdk->function_count && sdk->functions[lo].addr==*address) return SP_OK;
        }
    }
    if (!sp_guest_export(bootstrap->main,name,address,&kind)) return SP_OK;
    for (size_t i=0;i<program->symbol_count;++i) if (!strcmp(name,program->symbols[i].name)) {
        *address=0x7300000000ULL+i*256; return SP_OK;
    }
    for (size_t i=0;i<bootstrap->sdk_import_count;++i) if (!strcmp(name,bootstrap->sdk_imports[i])) {
        *address=0x7600000000ULL+i*256; return SP_OK;
    }
    if (bootstrap->sdk_import_count==1024) return SP_UNSUPPORTED;
    *address=0x7600000000ULL+bootstrap->sdk_import_count*256;
    bootstrap->sdk_imports[bootstrap->sdk_import_count++]=name;
    return SP_OK;
}
#endif
static void heap_probe(Ctx *context) {
    if (context->x[9]) { context->x[0]=0x7400000100ULL; host_import(context,1); return; }
    uint64_t passed=0;
    context->x[0]=16; host_import(context,0); uint64_t first=context->x[0];
    if (first && !(first%16)) passed|=1;
    memset(sp_recomp_access(first,16,1),0x5a,16);
    context->x[0]=first; context->x[1]=32; host_import(context,2); uint64_t resized=context->x[0];
    unsigned char *bytes=sp_recomp_access(resized,32,0);
    if (bytes[0]==0x5a && bytes[15]==0x5a && !bytes[16]) passed|=2;
    context->x[0]=resized; host_import(context,1);
    context->x[0]=4; context->x[1]=8; host_import(context,3); uint64_t zeroed=context->x[0];
    bytes=sp_recomp_access(zeroed,32,0); int zero=1;
    for (unsigned i=0;i<32;++i) if (bytes[i]) zero=0;
    if (zero) passed|=4;
    context->x[0]=zeroed; host_import(context,1);
    context->x[0]=UINT64_MAX; context->x[1]=16; host_import(context,3);
    if (!context->x[0]) passed|=8;
    context->x[0]=4096; context->x[1]=8192; host_import(context,4); uint64_t aligned=context->x[0];
    if (aligned && !(aligned%4096)) passed|=16;
    context->x[0]=aligned; host_import(context,1);
    context->x[0]=3; context->x[1]=16; host_import(context,4);
    if (!context->x[0]) passed|=32;
    context->x[0]=256*1024*1024; host_import(context,0);
    if (!context->x[0]) passed|=64;
    context->x[0]=passed;
}
static void mutex_probe(Ctx *context) {
    const uint64_t address=0x7100003000ULL; uint64_t passed=0;
    context->x[0]=address; context->x[1]=1; host_import(context,0);
    context->x[0]=address; host_import(context,1);
    context->x[0]=address; host_import(context,1);
    context->x[0]=address; host_import(context,3);
    context->x[0]=address; host_import(context,3);
    context->x[0]=address; host_import(context,4); passed|=1;
    context->x[0]=address; context->x[1]=0; host_import(context,0);
    context->x[0]=address; host_import(context,2); if (context->x[0]==1) passed|=2;
    context->x[0]=address; host_import(context,2); if (context->x[0]==0) passed|=4;
    context->x[0]=address; host_import(context,3);
    context->x[0]=address; host_import(context,4);
    uint64_t tls_value=0x1256789abcdefULL;
    memcpy(sp_recomp_access(context->tpidr+0x1f8,8,1),&tls_value,8);
    uint64_t actual=0; memcpy(&actual,sp_recomp_access(context->tpidr+0x1f8,8,0),8);
    if (actual==tls_value) passed|=8;
    context->x[0]=passed;
}
static void destructor_probe(Ctx *context) { context->x[20]+=context->x[0]; }
static void services_probe(Ctx *context) {
    uint64_t passed=0; const uint64_t slot_address=0x7100003100ULL,guard=0x7100003120ULL;
    context->x[0]=slot_address; context->x[1]=0; host_import(context,0);
    if (!context->x[0]) passed|=1;
    uint32_t slot; memcpy(&slot,sp_recomp_access(slot_address,4,0),4);
    context->x[0]=slot; context->x[1]=0x123456789abcdefULL; host_import(context,1);
    context->x[0]=slot; host_import(context,2);
    if (context->x[0]==0x123456789abcdefULL) passed|=2;
    context->x[0]=slot; host_import(context,3);
    memset(sp_recomp_access(guard,8,1),0,8);
    context->x[0]=guard; host_import(context,4); if (context->x[0]==1) passed|=4;
    context->x[0]=guard; host_import(context,6);
    context->x[0]=guard; host_import(context,4); if (context->x[0]==1) passed|=8;
    context->x[0]=guard; host_import(context,5);
    context->x[0]=guard; host_import(context,4); if (!context->x[0]) passed|=16;
    context->x[0]=0x710000004cULL; context->x[1]=5; context->x[2]=0x111; host_import(context,7);
    context->x[0]=0x710000004cULL; context->x[1]=7; context->x[2]=0x222; host_import(context,7);
    context->x[0]=0x111; host_import(context,8); if (context->x[20]==5) passed|=32;
    context->x[0]=0; host_import(context,8); host_import(context,8);
    if (context->x[20]==12) passed|=64;
    context->x[0]=passed;
}
static void virtual_probe(Ctx *context) {
    uint64_t passed=0,region=0; const uint64_t output=0x7100003180ULL;
    context->x[0]=output; context->x[1]=65536; host_import(context,0);
    if (!context->x[0]) passed|=1;
    memcpy(&region,sp_recomp_access(output,8,0),8);
    context->x[0]=region+4096; context->x[1]=4096; host_import(context,1);
    uint32_t value=0x12345678; memcpy(sp_recomp_access(region+4096,4,1),&value,4);
    if (context->x[9]==2) { sp_recomp_access(region,4,0); return; }
    context->x[0]=region+4096; context->x[1]=4096; context->x[2]=1; host_import(context,2);
    if (context->x[9]==1) { sp_recomp_access(region+4096,4,1); return; }
    uint32_t actual=0; memcpy(&actual,sp_recomp_access(region+4096,4,0),4);
    if (actual==value) passed|=2;
    context->x[0]=region+4096; context->x[1]=4096; host_import(context,3);
    context->x[0]=region+4096; context->x[1]=4096; host_import(context,1);
    memcpy(&actual,sp_recomp_access(region+4096,4,0),4); if (!actual) passed|=4;
    context->x[0]=region; host_import(context,4); passed|=8;
    context->x[0]=passed;
}
int sp_recomp_test(const char *path) {
    unsigned checks=0,failures=0;
#define VERIFY(expr) do { ++checks; if (!(expr)) { ++failures; sp_log("RECOMP FAIL %d: %s",__LINE__,#expr); } } while (0)
    SpGuestImage *image=NULL;
    VERIFY(sp_guest_load(&image,path)==SP_OK);
    if (image) {
        size_t missing=99;
        VERIFY(sp_guest_relocate(image,NULL,NULL,&missing)==SP_OK && !missing);
        uint64_t *value=sp_guest_pointer(image,0x7100002000ULL,24,1);
        VERIFY(value && value[0]==7 && value[2]==0x7100000020ULL);
        VERIFY(!sp_guest_pointer(image,0x7100000020ULL,8,1));
        VERIFY(!sp_guest_pointer(image,UINT64_MAX,16,0));
        VERIFY(!sp_guest_pointer(image,0x710000005aULL,8,0));
        uint64_t export_address=0; unsigned export_kind=0;
        VERIFY(sp_guest_export(image,"probe",&export_address,&export_kind)==SP_OK && export_address==0x7100000044ULL && export_kind==2);
        VERIFY(sp_guest_export(image,"does_not_exist",&export_address,&export_kind)==SP_UNSUPPORTED);
        SpGuestImage *sdk=NULL;
        VERIFY(sp_guest_load_at(&sdk,path,0x7200000000ULL)==SP_OK);
        if (sdk) {
            VERIFY(sp_guest_export(sdk,"probe",&export_address,&export_kind)==SP_OK && export_address==0x7200000044ULL);
            VERIFY(sp_guest_relocate(sdk,NULL,NULL,&missing)==SP_OK);
            uint64_t relocated=0; void *p=sp_guest_pointer(sdk,0x7200002010ULL,8,0);
            if (p) memcpy(&relocated,p,8);
            VERIFY(relocated==0x7200000020ULL && !sp_guest_pointer(sdk,0x7100002010ULL,8,0));
            VERIFY(sp_guest_close(&sdk)==SP_OK);
        }
        if (value) {
            for (unsigned i=0;i<100;++i) {
                value[0]=(uint64_t)i*7919; value[1]=0;
                Ctx context={0}; context.x[0]=0x7100002000ULL;
                VERIFY(sp_guest_run(image,&sp_smoke_program,0x7100000020ULL,&context)==SP_OK);
                VERIFY(context.x[0]==value[0]+10 && value[1]==value[0]+5);
            }
            Ctx context={0};
            VERIFY(sp_guest_run(image,&sp_smoke_program,0x7100000044ULL,&context)==SP_OK && context.x[0]==1234);
            VERIFY(sp_guest_run(image,&sp_smoke_program,0x710000004cULL,&context)==SP_UNSUPPORTED);
            context.x[0]=0xdeadbeef;
            VERIFY(sp_guest_run(image,&sp_smoke_program,0x7100000054ULL,&context)==SP_ADDRESS);
            VERIFY(sp_guest_run(image,&sp_smoke_program,0x7100000080ULL,&context)==SP_UNSUPPORTED);
            VERIFY(sp_guest_run(image,&sp_smoke_program,0x710000005cULL,&context)==SP_TIMEOUT);
            SpGuestProgram wrong=sp_smoke_program; wrong.image_fingerprint^=1;
            VERIFY(sp_guest_run(image,&wrong,0x7100000020ULL,&context)==SP_INVALID);
            const FnEntry heap_functions[]={{0x7100000044ULL,heap_probe}};
            const DynSym heap_symbols[]={{"malloc",0,0},{"free",0,0},{"realloc",0,0},{"calloc",0,0},{"aligned_alloc",0,0}};
            SpGuestProgram heap_program={heap_functions,1,heap_symbols,5,sp_guest_fingerprint(image)};
            memset(&context,0,sizeof context);
            VERIFY(sp_guest_run(image,&heap_program,0x7100000044ULL,&context)==SP_OK && context.x[0]==127);
            context.x[9]=1;
            VERIFY(sp_guest_run(image,&heap_program,0x7100000044ULL,&context)==SP_ADDRESS);
            const FnEntry mutex_functions[]={{0x7100000044ULL,mutex_probe}};
            const DynSym mutex_symbols[]={
                {"_ZN2nn2os15InitializeMutexEPNS0_9MutexTypeEbi",0,0},
                {"_ZN2nn2os9LockMutexEPNS0_9MutexTypeE",0,0},
                {"_ZN2nn2os12TryLockMutexEPNS0_9MutexTypeE",0,0},
                {"_ZN2nn2os11UnlockMutexEPNS0_9MutexTypeE",0,0},
                {"_ZN2nn2os13FinalizeMutexEPNS0_9MutexTypeE",0,0}};
            SpGuestProgram mutex_program={mutex_functions,1,mutex_symbols,5,sp_guest_fingerprint(image)};
            memset(&context,0,sizeof context);
            VERIFY(sp_guest_run(image,&mutex_program,0x7100000044ULL,&context)==SP_OK && context.x[0]==15);
            const FnEntry service_functions[]={{0x7100000044ULL,services_probe},{0x710000004cULL,destructor_probe}};
            const DynSym service_symbols[]={
                {"_ZN2nn2os18SdkAllocateTlsSlotEPNS0_7TlsSlotEPFvmE",0,0},
                {"_ZN2nn2os11SetTlsValueENS0_7TlsSlotEm",0,0},{"_ZN2nn2os11GetTlsValueENS0_7TlsSlotE",0,0},
                {"_ZN2nn2os11FreeTlsSlotENS0_7TlsSlotE",0,0},{"__cxa_guard_acquire",0,0},
                {"__cxa_guard_release",0,0},{"__cxa_guard_abort",0,0},{"__cxa_atexit",0,0},{"__cxa_finalize",0,0}};
            SpGuestProgram service_program={service_functions,2,service_symbols,9,sp_guest_fingerprint(image)};
            memset(&context,0,sizeof context);
            VERIFY(sp_guest_run(image,&service_program,0x7100000044ULL,&context)==SP_OK && context.x[0]==127);
            const FnEntry virtual_functions[]={{0x7100000044ULL,virtual_probe}};
            const DynSym virtual_symbols[]={
                {"_ZN2nn2os21AllocateAddressRegionEPmm",0,0},{"_ZN2nn2os19AllocateMemoryPagesEmm",0,0},
                {"_ZN2nn2os19SetMemoryPermissionEmmi",0,0},{"_ZN2nn2os15FreeMemoryPagesEmm",0,0},
                {"_ZN2nn2os17FreeAddressRegionEm",0,0}};
            SpGuestProgram virtual_program={virtual_functions,1,virtual_symbols,5,sp_guest_fingerprint(image)};
            memset(&context,0,sizeof context);
            VERIFY(sp_guest_run(image,&virtual_program,0x7100000044ULL,&context)==SP_OK && context.x[0]==15);
            context.x[9]=1;
            VERIFY(sp_guest_run(image,&virtual_program,0x7100000044ULL,&context)==SP_ADDRESS);
            context.x[9]=2;
            VERIFY(sp_guest_run(image,&virtual_program,0x7100000044ULL,&context)==SP_ADDRESS);
        }
        VERIFY(sp_guest_close(&image)==SP_OK);
    }
    sp_log("RECOMP RESULT: %u checks, %u failures",checks,failures);
    return failures ? 1 : 0;
#undef VERIFY
}
int sp_game_bootstrap(const char *path) {
    last_error[0]=0;
    SpGuestImage *image=NULL;
    int rc=sp_guest_load(&image,path);
    if (rc) { sp_log("GAME BOOT: main.elf unavailable or invalid: %d",rc); return rc; }
    sp_log("GAME BOOT: AArch64 image mapped with translated addresses, %llu bytes, fingerprint %016llx",
           (unsigned long long)sp_guest_size(image),(unsigned long long)sp_guest_fingerprint(image));
#ifdef SP_GAME_LINKED
    extern const SpGuestProgram *sp_linked_game_program(void);
    extern uint64_t sp_linked_game_entry(void);
    Ctx context={0};
    const SpGuestProgram *program=sp_linked_game_program();
    Bootstrap bootstrap={0}; bootstrap.main=image; bootstrap.program=program;
#ifdef SP_SDK_LINKED
    extern const SpGuestProgram *sp_linked_sdk_program(void);
    bootstrap.sdk_program=sp_linked_sdk_program();
#endif
    char sdk_path[1024];
    size_t path_size=strlen(path);
    if (path_size<sizeof sdk_path-8) {
        memcpy(sdk_path,path,path_size+1);
        char *cut=NULL;
        for (char *p=sdk_path;*p;++p) if (*p=='/' || *p=='\\') cut=p;
        strcpy(cut ? cut+1 : sdk_path,"sdk.elf");
        int sdk_rc=sp_guest_load_at(&bootstrap.sdk,sdk_path,0x7200000000ULL);
        if (sdk_rc) sp_log("GAME SDK: optional image unavailable (%d)",sdk_rc);
        else sp_log("GAME SDK: mapped at 0x7200000000; %llu translated SDK functions linked",
                    (unsigned long long)(bootstrap.sdk_program ? bootstrap.sdk_program->function_count : 0));
    }
    size_t missing=0;
    if (bootstrap.sdk_program && (!bootstrap.sdk || bootstrap.sdk_program->image_fingerprint!=sp_guest_fingerprint(bootstrap.sdk))) rc=SP_INVALID;
    if (!rc && bootstrap.sdk) rc=sp_guest_relocate(bootstrap.sdk,bootstrap_resolve,&bootstrap,&missing);
    if (!rc) rc=sp_guest_relocate(image,bootstrap_resolve,&bootstrap,&missing);
    uint64_t entry=sp_linked_game_entry(); int sequence=0;
    /* NSO modules have no process ELF entry: nso2elf's e_entry=0 points at
     * MOD0 metadata. Match the original boot driver using the named export. */
    if (!entry) for (size_t i=0;i<program->symbol_count;++i)
        if (program->symbols[i].defined && !strcmp(program->symbols[i].name,"nninitStartup")) {
            entry=0x7100000000ULL+program->symbols[i].value; sequence=1; break;
        }
    if (!rc) {
        /* Translated native calls have their own bounded stack; the ARM SP
         * remains in the independent 1 MiB translated guest allocation. */
        BootWork work={image,program,entry ? entry : sp_guest_entry(image),&context,sequence,SP_SYSTEM,&bootstrap};
        SpFiber *fiber=NULL;
        rc=sp_fiber_create(&fiber,16*1024*1024,boot_on_fiber,&work);
        if (!rc) rc=sp_fiber_resume(fiber);
        if (!rc) rc=sp_fiber_finished(fiber) ? work.result : SP_INVALID;
        if (fiber) {
            int close_rc=sp_fiber_destroy(&fiber); if (!rc) rc=close_rc;
        }
        int shutdown_rc=sp_fiber_thread_shutdown(); if (!rc) rc=shutdown_rc;
    }
    sp_log("GAME BOOT: execution status=%d unresolved=%llu x0=%llu stop=%s",rc,(unsigned long long)missing,
           (unsigned long long)context.x[0],sp_guest_last_error());
    if (bootstrap.sdk) sp_guest_close(&bootstrap.sdk);
#else
    rc=SP_UNSUPPORTED;
    sp_log("GAME BOOT: native game function table not linked; refusing ARM64 execution");
#endif
    sp_guest_close(&image); return rc;
}
int sp_guest_inspect(const char *path) {
    SpGuestImage *image=NULL;
    int rc=sp_guest_load(&image,path);
    size_t missing=0;
    uint64_t initializers=0; size_t initializer_count=0;
    if (!rc) rc=sp_guest_initializers(image,&initializers,&initializer_count);
    if (!rc) rc=sp_guest_relocate(image,NULL,NULL,&missing);
    if (image) sp_guest_close(&image);
    printf("GUEST INSPECT: %d unresolved=%llu\n",rc,(unsigned long long)missing);
    return rc ? 1 : 0;
}
int sp_guest_batch(const char *path) {
    SpGuestImage *image=NULL;
    if (sp_guest_load(&image,path)) return 2;
    uint64_t *value=sp_guest_pointer(image,0x7100002000ULL,16,1);
    if (!value) { sp_guest_close(&image); return 2; }
    char line[80]; int result=0;
    while (fgets(line,sizeof line,stdin)) {
        char *end; uint64_t input=strtoull(line,&end,0);
        if (end==line) { result=2; break; }
        value[0]=input; value[1]=0;
        Ctx context={0}; context.x[0]=0x7100002000ULL;
        if (sp_guest_run(image,&sp_smoke_program,0x7100000020ULL,&context)) { result=2; break; }
        printf("%llu,%llu,%llu\n",(unsigned long long)input,(unsigned long long)context.x[0],(unsigned long long)value[1]);
    }
    sp_guest_close(&image); return result;
}
