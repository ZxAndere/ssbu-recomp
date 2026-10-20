/* Private implementation included by guest_exec.c. Guest pages are 4 KiB;
 * Orbis host pages are 16 KiB. Every translated access checks guest permissions.
 * There is no executable mapping and no arbitrary-address fixed mapping. */
#ifdef SSBU_PS4
#include <orbis/libkernel.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
typedef struct {
    SpMemory reservation; size_t requested,page_count;
    unsigned char *permissions,*host_present; size_t committed_bytes;
} GuestVm;
#define GUEST_PAGE 4096
static size_t vm_host_page(void) {
#ifdef SP_VM_NATIVE_PAGE
    return SP_VM_NATIVE_PAGE; /* PC model test of Orbis page granularity only. */
#else
    return sp_page_size();
#endif
}
static int vm_range(GuestVm *vm,size_t offset,size_t size) {
    return vm && offset<=vm->requested && size<=vm->requested-offset;
}
static int vm_pages(GuestVm *vm,size_t offset,size_t size) {
    return size && !(offset%GUEST_PAGE) && !(size%GUEST_PAGE) && vm_range(vm,offset,size);
}
static int vm_reserve(GuestVm **out,size_t size) {
    if (!out || *out || !size || size%GUEST_PAGE || size>UINT64_C(64)*1024*1024*1024) return SP_INVALID;
    GuestVm *vm=calloc(1,sizeof *vm);
    if (!vm) return SP_SYSTEM;
    size_t native_page=vm_host_page(),rounded=(size+native_page-1)/native_page*native_page;
    vm->permissions=calloc(size/GUEST_PAGE,1);
    vm->host_present=calloc(rounded/native_page,1);
    if (!vm->permissions || !vm->host_present) { free(vm->host_present); free(vm->permissions); free(vm); return SP_SYSTEM; }
    void *pointer=NULL;
#ifdef SSBU_PS4
    int rc=sceKernelMmap(NULL,rounded,0,0x1002,-1,0,&pointer);
    if (rc) pointer=NULL;
#else
    pointer=VirtualAlloc(NULL,rounded,MEM_RESERVE,PAGE_NOACCESS);
#endif
    if (!pointer) { free(vm->host_present); free(vm->permissions); free(vm); return SP_SYSTEM; }
    vm->reservation=(SpMemory){pointer,rounded}; vm->requested=size; vm->page_count=size/GUEST_PAGE;
    *out=vm; return SP_OK;
}
static int vm_host_commit(GuestVm *vm,size_t offset) {
    size_t page=vm_host_page(); void *pointer=(char *)vm->reservation.data+offset;
#ifdef SSBU_PS4
    return sceKernelMprotect(pointer,page,3) ? SP_SYSTEM : SP_OK;
#else
    return VirtualAlloc(pointer,page,MEM_COMMIT,PAGE_READWRITE)==pointer ? SP_OK : SP_SYSTEM;
#endif
}
static int vm_host_discard(GuestVm *vm,size_t offset) {
    size_t page=vm_host_page();
    if (offset%page || offset>vm->reservation.size || page>vm->reservation.size-offset) return SP_INVALID;
    void *pointer=(char *)vm->reservation.data+offset;
#ifdef SSBU_PS4
    /* Replace exactly one page owned by THIS reservation. MAP_FIXED is never
     * used with a caller-provided pointer or outside the validated allocation.
     * Anonymous replacement discards backing while preserving the reservation. */
    void *replacement=NULL;
    return sceKernelMmap(pointer,page,0,0x1002|0x10,-1,0,&replacement) || replacement!=pointer ? SP_SYSTEM : SP_OK;
#else
    return VirtualFree(pointer,page,MEM_DECOMMIT) ? SP_OK : SP_SYSTEM;
#endif
}
static int vm_host_in_use(GuestVm *vm,size_t host_offset) {
    size_t first=host_offset/GUEST_PAGE,count=vm_host_page()/GUEST_PAGE;
    for (size_t i=first;i<first+count && i<vm->page_count;++i) if (vm->permissions[i]) return 1;
    return 0;
}
static int vm_commit(GuestVm *vm,size_t offset,size_t size) {
    if (!vm_pages(vm,offset,size)) return SP_INVALID;
    size_t page=vm_host_page(),begin=offset/page*page,end=(offset+size+page-1)/page*page;
    for (size_t at=begin;at<end;at+=page) if (!vm->host_present[at/page]) {
        if (vm_host_commit(vm,at)) {
            for (size_t undo=begin;undo<at;undo+=page) if (!vm_host_in_use(vm,undo) && !vm_host_discard(vm,undo)) {
                vm->host_present[undo/page]=0; vm->committed_bytes-=page;
            }
            return SP_SYSTEM;
        }
        vm->host_present[at/page]=1; vm->committed_bytes+=page;
    }
    for (size_t at=offset;at<offset+size;at+=GUEST_PAGE) {
        size_t index=at/GUEST_PAGE;
        if (!vm->permissions[index]) {
            memset((char *)vm->reservation.data+at,0,GUEST_PAGE); vm->permissions[index]=3;
        }
    }
    return SP_OK;
}
static int vm_decommit(GuestVm *vm,size_t offset,size_t size) {
    if (!vm_pages(vm,offset,size)) return SP_INVALID;
    for (size_t at=offset;at<offset+size;at+=GUEST_PAGE) {
        if (vm->permissions[at/GUEST_PAGE]) memset((char *)vm->reservation.data+at,0,GUEST_PAGE);
        vm->permissions[at/GUEST_PAGE]=0;
    }
    size_t page=vm_host_page(),begin=offset/page*page,end=(offset+size+page-1)/page*page;
    int rc=SP_OK;
    for (size_t at=begin;at<end;at+=page) if (vm->host_present[at/page] && !vm_host_in_use(vm,at)) {
        if (vm_host_discard(vm,at)) rc=SP_SYSTEM;
        else { vm->host_present[at/page]=0; vm->committed_bytes-=page; }
    }
    return rc;
}
static int vm_permission(GuestVm *vm,size_t offset,size_t size,unsigned access) {
    if (!vm_pages(vm,offset,size) || (access!=0 && access!=1 && access!=3)) return SP_INVALID;
    for (size_t at=offset;at<offset+size;at+=GUEST_PAGE) if (!vm->permissions[at/GUEST_PAGE]) return SP_INVALID;
    /* Bit 2 retains committed state even for guest NOACCESS. The native page
     * remains RW if any of its four guest pages needs it; software checks gate
     * every CPU access, including host imports. */
    for (size_t at=offset;at<offset+size;at+=GUEST_PAGE) vm->permissions[at/GUEST_PAGE]=(unsigned char)(4|access);
    return SP_OK;
}
static void *vm_pointer(GuestVm *vm,size_t offset,size_t size,int write) {
    if (!vm_range(vm,offset,size)) return NULL;
    if (size) {
        size_t last=(offset+size-1)/GUEST_PAGE;
        for (size_t i=offset/GUEST_PAGE;i<=last;++i)
            if (!(vm->permissions[i] & (write ? 2 : 1))) return NULL;
    }
    return (char *)vm->reservation.data+offset;
}
static int vm_close(GuestVm **vm) {
    if (!vm || !*vm) return SP_INVALID;
    int rc=sp_memory_unmap(&(*vm)->reservation); if (rc) return rc;
    free((*vm)->host_present); free((*vm)->permissions); free(*vm); *vm=NULL; return SP_OK;
}
