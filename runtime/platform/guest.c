#include "guest.h"
#include <stdlib.h>
#include <string.h>
#define BASE 0x7100000000ULL
#define LIMIT (256ULL * 1024 * 1024)
typedef struct { uint64_t address, size, file_offset, file_size; uint32_t flags; } Segment;
struct SpGuestImage {
    SpMemory memory; Segment segments[16]; size_t count;
    uint64_t dynamic, dynamic_size, entry, fingerprint;
};
static uint16_t u16(const unsigned char *p) { uint16_t v; memcpy(&v,p,2); return v; }
static uint32_t u32(const unsigned char *p) { uint32_t v; memcpy(&v,p,4); return v; }
static uint64_t u64(const unsigned char *p) { uint64_t v; memcpy(&v,p,8); return v; }
static int within(uint64_t offset, uint64_t size, uint64_t total) { return offset <= total && size <= total-offset; }
static int read_exact(SpFile *file, uint64_t offset, void *data, size_t size) {
    size_t done = 0;
    return sp_file_read_at(file,offset,data,size,&done) || done != size ? SP_IO : SP_OK;
}
void *sp_guest_pointer(SpGuestImage *image, uint64_t address, size_t size, int write) {
    if (!image || address < BASE) return NULL;
    uint64_t offset = address - BASE;
    if (!within(offset,size,image->memory.size)) return NULL;
    for (size_t i=0;i<image->count;++i) {
        Segment *seg = &image->segments[i];
        if (offset >= seg->address && within(offset-seg->address,size,seg->size) &&
            (seg->flags & (write ? 2 : 4))) return (char *)image->memory.data + offset;
    }
    return NULL;
}
int sp_guest_load(SpGuestImage **out, const char *path) {
    if (!out || *out || !path) return SP_INVALID;
    SpFile *file = NULL; uint64_t file_size = 0; unsigned char header[64], ph[56];
    int rc = sp_file_open(&file,path,0,0);
    if (rc) return rc;
    SpGuestImage *image = calloc(1,sizeof *image);
    if (!image) { sp_file_close(&file); return SP_SYSTEM; }
    if (sp_file_size(file,&file_size) || file_size > LIMIT + 0x1000000 || read_exact(file,0,header,64)) { rc=SP_IO; goto fail; }
    if (memcmp(header,"\177ELF\2\1\1",7) || u16(header+16)!=3 || u16(header+18)!=183 ||
        u16(header+52)!=64 || u16(header+54)!=56) { rc=SP_INVALID; goto fail; }
    uint64_t phoff=u64(header+32), span=0; unsigned phnum=u16(header+56);
    if (!phnum || phnum>64 || !within(phoff,(uint64_t)phnum*56,file_size)) { rc=SP_INVALID; goto fail; }
    if (u64(header+24)>=LIMIT) { rc=SP_INVALID; goto fail; }
    image->entry=BASE+u64(header+24);
    for (unsigned i=0;i<phnum;++i) {
        if (read_exact(file,phoff+i*56,ph,56)) { rc=SP_IO; goto fail; }
        uint32_t type=u32(ph); uint64_t offset=u64(ph+8), address=u64(ph+16), filesz=u64(ph+32), memsz=u64(ph+40);
        if (type!=1 && type!=2) continue;
        if (filesz>memsz || !within(offset,filesz,file_size) || !within(address,memsz,LIMIT)) { rc=SP_INVALID; goto fail; }
        if (type==2) { image->dynamic=address; image->dynamic_size=filesz; continue; }
        if (!memsz || image->count==16 || !(u32(ph+4)&4)) { rc=SP_INVALID; goto fail; }
        for (size_t j=0;j<image->count;++j) {
            Segment *s=&image->segments[j];
            if (address<s->address+s->size && s->address<address+memsz) { rc=SP_INVALID; goto fail; }
        }
        image->segments[image->count++]=(Segment){address,memsz,offset,filesz,u32(ph+4)};
        if (span<address+memsz) span=address+memsz;
    }
    if (!image->count || !span || sp_memory_map(&image->memory,(size_t)span,NULL)) { rc=SP_SYSTEM; goto fail; }
    for (size_t i=0;i<image->count;++i) {
        Segment *seg=&image->segments[i];
        if (read_exact(file,seg->file_offset,(char *)image->memory.data+seg->address,(size_t)seg->file_size)) { rc=SP_IO; goto fail; }
    }
    /* FNV-1a fingerprint prevents mixing a locally generated table with another
     * dump. It is an identity guard, not a cryptographic authenticity check. */
    image->fingerprint=14695981039346656037ULL;
    unsigned char block[4096];
    for (uint64_t pos=0;pos<file_size;) {
        size_t n=file_size-pos>sizeof block ? sizeof block : (size_t)(file_size-pos);
        if (read_exact(file,pos,block,n)) { rc=SP_IO; goto fail; }
        for (size_t j=0;j<n;++j) { image->fingerprint^=block[j]; image->fingerprint*=1099511628211ULL; }
        pos+=n;
    }
    if (!sp_guest_pointer(image,image->entry,4,0)) { rc=SP_INVALID; goto fail; }
    sp_file_close(&file); *out=image; return SP_OK;
fail:
    sp_file_close(&file);
    if (image->memory.data) sp_memory_unmap(&image->memory);
    free(image); return rc;
}
int sp_guest_close(SpGuestImage **image) {
    if (!image || !*image) return SP_INVALID;
    int rc=sp_memory_unmap(&(*image)->memory);
    if (rc) return rc;
    free(*image); *image=NULL; return SP_OK;
}
uint64_t sp_guest_fingerprint(const SpGuestImage *image) { return image ? image->fingerprint : 0; }
uint64_t sp_guest_entry(const SpGuestImage *image) { return image ? image->entry : 0; }
size_t sp_guest_size(const SpGuestImage *image) { return image ? image->memory.size : 0; }
static int relocate_table(SpGuestImage *image,uint64_t address,uint64_t size,uint64_t symbols,uint64_t strings,
                          uint64_t string_size,SpGuestResolver resolve,void *user,size_t *unresolved,int apply) {
    if (size%24 || size>LIMIT) return SP_INVALID;
    for (uint64_t i=0;i<size;i+=24) {
        unsigned char *r=sp_guest_pointer(image,BASE+address+i,24,0);
        if (!r) return SP_INVALID;
        uint64_t offset=u64(r), info=u64(r+8), value=0, addend=u64(r+16);
        uint32_t type=(uint32_t)info;
        if (!type) continue;
        unsigned char *target=sp_guest_pointer(image,BASE+offset,8,1);
        if (!target) return SP_INVALID;
        if (type==0x403) {
            if (addend>=image->memory.size) return SP_INVALID;
            value=BASE+addend;
        } else if (type==0x101 || type==0x401 || type==0x402) {
            uint64_t index=info>>32;
            if (index>LIMIT/24 || symbols>LIMIT-index*24) return SP_INVALID;
            unsigned char *symbol=sp_guest_pointer(image,BASE+symbols+index*24,24,0);
            if (!symbol) return SP_INVALID;
            if (u16(symbol+6)) {
                if (u64(symbol+8)>=image->memory.size) return SP_INVALID;
                value=BASE+u64(symbol+8);
            } else {
                uint32_t name=u32(symbol);
                if (name>=string_size) return SP_INVALID;
                char *text=sp_guest_pointer(image,BASE+strings+name,(size_t)(string_size-name),0);
                if (!text || !memchr(text,0,(size_t)(string_size-name))) return SP_INVALID;
                if (!resolve || resolve(text,&value,user)) {
                    if ((symbol[4]>>4)!=2) { ++*unresolved; continue; }
                    value=0;
                }
            }
            if (value && addend>UINT64_MAX-value) return SP_INVALID;
            value+=addend;
        } else return SP_UNSUPPORTED;
        if (apply) memcpy(target,&value,8);
    }
    return SP_OK;
}
int sp_guest_relocate(SpGuestImage *image,SpGuestResolver resolve,void *user,size_t *unresolved) {
    if (!image || !unresolved) return SP_INVALID;
    *unresolved=0;
    uint64_t tags[32]={0}; int ended=0;
    if (!image->dynamic_size || image->dynamic_size>LIMIT || image->dynamic_size%16) return SP_INVALID;
    for (uint64_t i=0;i<image->dynamic_size;i+=16) {
        unsigned char *d=sp_guest_pointer(image,BASE+image->dynamic+i,16,0);
        if (!d) return SP_INVALID;
        uint64_t tag=u64(d),value=u64(d+8);
        if (!tag) { ended=1; break; }
        if (tag<32) tags[tag]=value;
    }
    if (!ended || (tags[9] && tags[9]!=24) || (tags[11] && tags[11]!=24) ||
        (tags[20] && tags[20]!=7) || tags[10]>LIMIT) return SP_INVALID;
    /* Validate everything first; unresolved imports never produce a partially
     * relocated image. The resolver must be deterministic between both passes. */
    for (int apply=0;apply<2;++apply) {
        size_t missing=0;
        int rc=relocate_table(image,tags[7],tags[8],tags[6],tags[5],tags[10],resolve,user,&missing,apply);
        if (!rc) rc=relocate_table(image,tags[23],tags[2],tags[6],tags[5],tags[10],resolve,user,&missing,apply);
        if (rc) return rc;
        if (missing) { *unresolved=missing; return SP_UNSUPPORTED; }
    }
    return SP_OK;
}
