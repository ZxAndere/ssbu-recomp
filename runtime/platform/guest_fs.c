#ifdef SSBU_PS4
#include <sys/stat.h>
#endif
#define FS_NOT_FOUND UINT32_C(0x202)
#define FS_MOUNT_EXISTS UINT32_C(0x7802)
#define FS_NOT_MOUNTED UINT32_C(0x35f202)
#define FS_PERMISSION UINT32_C(0x320002)
#define FS_OUT_OF_RANGE UINT32_C(0x177a02)
#define FS_FAILURE UINT32_C(0x3e802)
#define FS_CACHE_BYTES 8192
static int fs_is_directory(const char *path) {
#ifdef SSBU_PS4
    struct stat info;
    return !stat(path,&info) && S_ISDIR(info.st_mode);
#else
    DWORD attributes=GetFileAttributesA(path);
    return attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_DIRECTORY) && !(attributes&FILE_ATTRIBUTE_REPARSE_POINT);
#endif
}
static void fs_guest_string(uint64_t address,char *output,size_t capacity) {
    size_t length=string_length(address,capacity);
    if (length==capacity) fail(SP_INVALID,"filesystem name/path too long",address);
    memcpy(output,sp_recomp_access(address,length+1,0),length+1);
}
static int fs_mount_index(const char *name) {
    for (unsigned i=0;i<8;++i) if (active->mounts[i].used && !strcmp(active->mounts[i].name,name)) return (int)i;
    return -1;
}
static uint32_t fs_resolve(uint64_t address,char *output,size_t capacity,unsigned *mount) {
    char path[1024]; fs_guest_string(address,path,sizeof path);
    char *colon=strchr(path,':');
    if (!colon || colon==path || colon-path>15) return FS_NOT_MOUNTED;
    *colon=0; int index=fs_mount_index(path);
    if (index<0) return FS_NOT_MOUNTED;
    char *relative=colon+1;
    if (*relative!='/') return FS_NOT_FOUND;
    ++relative;
    for (char *at=relative;*at;++at) if (*at=='\\' || *at==':' || (unsigned char)*at<32) return FS_PERMISSION;
    for (char *at=relative;*at;) {
        char *end=strchr(at,'/'); size_t length=end ? (size_t)(end-at) : strlen(at);
        if (!length || (length==1 && at[0]=='.') || (length==2 && at[0]=='.' && at[1]=='.')) return FS_PERMISSION;
        if (!end) break;
        at=end+1;
    }
    GuestMount *entry=&active->mounts[index];
    int size=snprintf(output,capacity,"%s/%s",entry->root,relative);
    if (size<0 || (size_t)size>=capacity) return FS_NOT_FOUND;
    *mount=(unsigned)index; return 0;
}
static GuestFile *fs_file(uint64_t token) {
    for (unsigned i=0;i<128;++i) if (active->files[i].native && active->files[i].token==token) return &active->files[i];
    fail(SP_INVALID,"invalid or closed filesystem handle",token); return NULL;
}
static int fs_import(Ctx *context,const char *name) {
    if (!strcmp(name,"_ZN2nn2fs22QueryMountRomCacheSizeEPm")) {
        uint64_t bytes=FS_CACHE_BYTES; memcpy(sp_recomp_access(context->x[0],8,1),&bytes,8); context->x[0]=0;
    } else if (!strcmp(name,"_ZN2nn2fs8MountRomEPKcPvm")) {
        char mount_name[16]; fs_guest_string(context->x[0],mount_name,sizeof mount_name);
        if (!mount_name[0] || strspn(mount_name,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=strlen(mount_name))
            fail(SP_INVALID,"invalid mount name",context->x[0]);
        if (fs_mount_index(mount_name)>=0) { context->x[0]=FS_MOUNT_EXISTS; return 1; }
        unsigned slot=0; while (slot<8 && active->mounts[slot].used) ++slot;
        if (slot==8) fail(SP_SYSTEM,"filesystem mount registry full",0);
        const char *root=active->romfs_override[0] ? active->romfs_override : getenv("SSBU_ROMFS");
        if (!root || !*root) root="/data/ssbu/romfs";
        if (strlen(root)>=sizeof active->mounts[slot].root) fail(SP_INVALID,"native RomFS root too long",0);
        if (!fs_is_directory(root)) { context->x[0]=FS_NOT_FOUND; return 1; }
        if (context->x[2]<FS_CACHE_BYTES) fail(SP_INVALID,"RomFS cache buffer too small",context->x[2]);
        memset(sp_recomp_access(context->x[1],FS_CACHE_BYTES,1),0,FS_CACHE_BYTES);
        GuestMount *mount=&active->mounts[slot];
        strcpy(mount->name,mount_name); strcpy(mount->root,root); mount->cache=context->x[1]; mount->used=1;
        sp_log("GAME ROMFS: mounted %s from local directory",mount->name); context->x[0]=0;
    } else if (!strcmp(name,"_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci")) {
        char path[2048]; unsigned mount=0; uint64_t destination=context->x[0];
        uint32_t rc=fs_resolve(context->x[1],path,sizeof path,&mount);
        if (rc) { context->x[0]=rc; return 1; }
        if (context->x[2]!=1) { context->x[0]=FS_PERMISSION; return 1; }
        if (fs_is_directory(path)) { context->x[0]=FS_NOT_FOUND; return 1; }
        unsigned slot=0; while (slot<128 && active->files[slot].native) ++slot;
        if (slot==128) fail(SP_SYSTEM,"filesystem handle registry full",0);
        GuestFile *file=&active->files[slot];
        if (sp_file_open(&file->native,path,0,0)) { context->x[0]=FS_NOT_FOUND; return 1; }
        if (sp_file_size(file->native,&file->size)) { sp_file_close(&file->native); context->x[0]=FS_FAILURE; return 1; }
        file->token=UINT64_C(0x4e46530000000000)|(++active->next_file<<8)|slot; file->mount=mount; file->first_read=1;
        memcpy(sp_recomp_access(destination,8,1),&file->token,8);
        ++active->resource_opens; sp_log("GAME RESOURCE OPEN: %s (%llu bytes)",path,(unsigned long long)file->size);
        context->x[0]=0;
    } else if (!strcmp(name,"_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE")) {
        GuestFile *file=fs_file(context->x[1]); memcpy(sp_recomp_access(context->x[0],8,1),&file->size,8); context->x[0]=0;
    } else if (!strcmp(name,"_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm") || !strcmp(name,"_ZN2nn2fs8ReadFileENS0_10FileHandleElPvm")) {
        int out_count=!strcmp(name,"_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm");
        uint64_t count_pointer=out_count ? context->x[0] : 0,offset=context->x[out_count ? 2 : 1];
        GuestFile *file=fs_file(context->x[out_count ? 1 : 0]);
        size_t requested=(size_t)context->x[out_count ? 4 : 3],done=0;
        uint64_t destination=context->x[out_count ? 3 : 2];
        if ((int64_t)offset<0 || offset>file->size || (!out_count && requested>file->size-offset)) { context->x[0]=FS_OUT_OF_RANGE; return 1; }
        size_t wanted=file->size-offset<requested ? (size_t)(file->size-offset) : requested;
        void *output=sp_recomp_access(destination,wanted,1); GuestMount *mount=&active->mounts[file->mount];
        int rc=0;
        if (wanted && wanted<=4096 && offset/4096==(offset+wanted-1)/4096) {
            unsigned char *cache=sp_recomp_access(mount->cache,FS_CACHE_BYTES,1);
            uint64_t stored_token=0,stored_offset=0,stored_size=0;
            memcpy(&stored_token,cache,8); memcpy(&stored_offset,cache+8,8); memcpy(&stored_size,cache+16,8);
            uint64_t block=offset/4096*4096;
            if (stored_token!=file->token || stored_offset!=block) {
                size_t received=0; rc=sp_file_read_at(file->native,block,cache+64,4096,&received);
                stored_token=rc ? 0 : file->token; stored_offset=block; stored_size=received;
                memcpy(cache,&stored_token,8); memcpy(cache+8,&stored_offset,8); memcpy(cache+16,&stored_size,8);
            }
            if (!rc && offset-block+wanted<=stored_size) { memcpy(output,cache+64+(size_t)(offset-block),wanted); done=wanted; }
            else rc=SP_IO;
        } else rc=sp_file_read_at(file->native,offset,output,wanted,&done);
        if (rc) { context->x[0]=FS_FAILURE; return 1; }
        if (count_pointer) { uint64_t count=done; memcpy(sp_recomp_access(count_pointer,8,1),&count,8); }
        ++active->resource_reads; active->resource_bytes+=done;
        if (file->first_read) { sp_log("GAME RESOURCE READ: offset=%llu requested=%llu received=%llu",(unsigned long long)offset,
            (unsigned long long)requested,(unsigned long long)done); file->first_read=0; }
        context->x[0]=0;
    } else if (!strcmp(name,"_ZN2nn2fs9CloseFileENS0_10FileHandleE")) {
        GuestFile *file=fs_file(context->x[0]);
        if (sp_file_close(&file->native)) fail(SP_IO,"native resource file close failed",context->x[0]);
    } else if (!strcmp(name,"_ZN2nn2fs7UnmountEPKc")) {
        char mount_name[16]; fs_guest_string(context->x[0],mount_name,sizeof mount_name); int index=fs_mount_index(mount_name);
        if (index<0) fail(SP_INVALID,"unmount of unknown filesystem",context->x[0]);
        for (unsigned i=0;i<128;++i) if (active->files[i].native && active->files[i].mount==(unsigned)index)
            fail(SP_INVALID,"unmount with resources still open",context->x[0]);
        memset(&active->mounts[index],0,sizeof active->mounts[index]);
    } else return 0;
    return 1;
}
