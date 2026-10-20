/* Offline shader cache is tied to code/control/constants, never just an
 * allocation address. All generated shader material remains private. */
static uint64_t nvn_shader_fingerprint(uint64_t hash,const void *data,size_t bytes) {
    const unsigned char *p=data; while (bytes--) { hash^=*p++; hash*=UINT64_C(1099511628211); } return hash;
}
static int nvn_shader_cache(const char *root,uint64_t key,unsigned index,uint64_t fingerprint,unsigned stage,NativeShader **out) {
    char path[2048]; int length=snprintf(path,sizeof path,"%s/%llx-%u.cache",root,(unsigned long long)key,index);
    if (length<0 || (size_t)length>=sizeof path) return SP_INVALID;
    FILE *file=fopen(path,"rb"); if (!file) return SP_UNSUPPORTED;
    unsigned char header[24]; uint64_t expected=0; uint32_t cached_stage=0,words=0;
    if (fread(header,1,sizeof header,file)!=sizeof header || memcmp(header,"SSBUSPV1",8)) { fclose(file); return SP_INVALID; }
    memcpy(&expected,header+8,8); memcpy(&cached_stage,header+16,4); memcpy(&words,header+20,4);
    if (expected!=fingerprint || cached_stage!=stage || words<5 || words>1024*1024) { fclose(file); return SP_INVALID; }
    uint32_t *code=malloc((size_t)words*4); if (!code) { fclose(file); return SP_SYSTEM; }
    int rc=SP_INVALID;
    if (fread(code,4,words,file)==words && fgetc(file)==EOF) rc=native_shader_open(active->graphics,out,code,(size_t)words*4,stage);
    free(code); fclose(file); return rc;
}
static void nvn_capture_part(const char *root,uint64_t program,unsigned stage,const char *part,const void *data,size_t bytes) {
    char path[2048]; int length=snprintf(path,sizeof path,"%s/%llx-%u.%s",root,(unsigned long long)program,stage,part);
    if (length<0 || (size_t)length>=sizeof path) fail(SP_INVALID,"private shader capture path too long",program);
    SpFile *file=NULL;
    if (sp_file_open(&file,path,1,1)) {
        if (sp_file_open(&file,path,0,0)) fail(SP_IO,"private shader capture cannot be opened",program);
        uint64_t size=0; int same=!sp_file_size(file,&size) && size==bytes;
        unsigned char block[8192]; size_t at=0;
        while (same && at<bytes) {
            size_t want=bytes-at<sizeof block ? bytes-at : sizeof block,done=0;
            same=!sp_file_read_at(file,at,block,want,&done) && done==want && !memcmp(block,(const unsigned char *)data+at,want); at+=want;
        }
        sp_file_close(&file); if (!same) fail(SP_IO,"existing private shader capture differs; use a new directory",program);
        return;
    }
    size_t done=0; int rc=sp_file_write_at(file,0,data,bytes,&done);
    if (!rc) rc=sp_file_flush(file); sp_file_close(&file);
    if (rc || done!=bytes) fail(SP_IO,"private shader capture write failed",program);
}
static int nvn_program_dispatch(Ctx *c,const char *name) {
    if (strncmp(name,"nvnProgram",10)) return 0;
    const char *op=name+10; unsigned index=128,empty=128;
    for (unsigned i=0;i<128;++i) {
        if (active->nvn_programs[i].key==c->x[0] && c->x[0]) index=i;
        if (!active->nvn_programs[i].key && empty==128) empty=i;
    }
    if (!strcmp(op,"Initialize")) {
        if (index!=128 || empty==128 || !active->graphics || !c->x[0] || c->x[1]!=active->nvn_device)
            fail(SP_INVALID,"NVN program lifecycle/device invalid",c->x[0]);
        sp_recomp_access(c->x[0],1,1); active->nvn_programs[empty].key=c->x[0]; active->nvn_programs[empty].device=c->x[1];
        c->x[0]=1; return 1;
    }
    if (index==128) fail(SP_INVALID,"unknown NVN program",c->x[0]);
    if (!strcmp(op,"Finalize")) {
        for (unsigned stage=0;stage<6;++stage) if (active->nvn_programs[index].stages[stage]) {
            int rc=native_shader_close(active->graphics,&active->nvn_programs[index].stages[stage]); if (rc) fail(rc,"native shader release failed",stage);
        }
        memset(&active->nvn_programs[index],0,sizeof active->nvn_programs[index]);
    }
    else if (!strcmp(op,"SetDebugLabel")) {
        char label[128]; if (c->x[1]) fs_guest_string(c->x[1],label,sizeof label); else label[0]=0;
        sp_log("GAME NVN: program label=%s",label);
    } else if (!strcmp(op,"SetShaders")) {
        if (!c->x[1] || c->x[1]>6) fail(SP_INVALID,"NVN shader stage count invalid",c->x[1]);
        const char *root=getenv("SSBU_SHADER_CAPTURE"),*cache=getenv("SSBU_SHADER_CACHE"); unsigned compiled=0;
        for (unsigned i=0;i<(unsigned)c->x[1];++i) {
            uint64_t pointers[2]; memcpy(pointers,sp_recomp_access(c->x[2]+i*16,16,0),16);
            const unsigned char *control=sp_recomp_access(pointers[1],0x760,0); uint32_t magic,bytes,stage,constant_bytes,constant_offset;
            memcpy(&magic,control,4); memcpy(&bytes,control+0x6f8,4); memcpy(&stage,control+0x714,4);
            memcpy(&constant_bytes,control+0x6fc,4); memcpy(&constant_offset,control+0x700,4);
            if (magic!=0x98761234 || !bytes || bytes>4*1024*1024 || constant_bytes>4*1024*1024)
                fail(SP_UNSUPPORTED,"unrecognized NVN shader control/code layout",pointers[1]);
            uint64_t code_address=pointers[0];
            if (stage!=5) {
                uint32_t header; memcpy(&header,sp_recomp_access(code_address,4,0),4);
                if (header!=0x12345678) fail(SP_UNSUPPORTED,"unrecognized NVN graphics shader data",code_address);
                code_address+=0x30;
                memcpy(&header,sp_recomp_access(code_address,4,0),4);
                static const int stages[]={-1,0,3,4,2,1}; unsigned type=(header>>10)&15;
                if (type>=sizeof stages/sizeof stages[0] || stages[type]<0) fail(SP_UNSUPPORTED,"NVN shader hardware stage unsupported",type);
                stage=(unsigned)stages[type];
            }
            const void *code=sp_recomp_access(code_address,bytes,0);
            const void *constants=constant_bytes ? sp_recomp_access(pointers[0]+constant_offset,constant_bytes,0) : NULL;
            sp_log("GAME NVN SHADER: program=%llx index=%u stage=%u code_bytes=%u constants=%u",(unsigned long long)c->x[0],i,stage,bytes,constant_bytes);
            if (root && *root) {
                nvn_capture_part(root,c->x[0],i,"control",control,0x760);
                nvn_capture_part(root,c->x[0],i,"maxwell",code,bytes);
                if (constant_bytes) nvn_capture_part(root,c->x[0],i,"constants",constants,constant_bytes);
                sp_log("GAME NVN SHADER: private capture saved; native shader compilation pending");
            }
            uint64_t fingerprint=nvn_shader_fingerprint(UINT64_C(14695981039346656037),control,0x760);
            fingerprint=nvn_shader_fingerprint(fingerprint,code,bytes); fingerprint=nvn_shader_fingerprint(fingerprint,constants,constant_bytes);
            if (cache && *cache) {
                NativeShader *shader=NULL; int rc=nvn_shader_cache(cache,c->x[0],i,fingerprint,stage,&shader);
                if (!rc) {
                    rc=native_shader_close(active->graphics,&active->nvn_programs[index].stages[stage]);
                    if (rc) { native_shader_close(active->graphics,&shader); fail(rc,"native shader replacement failed",stage); }
                    active->nvn_programs[index].stages[stage]=shader; ++compiled;
                    sp_log("GAME NVN SHADER: native module created stage=%u; drawing unimplemented",stage);
                } else { if (shader) native_shader_close(active->graphics,&shader); sp_log("GAME NVN SHADER: cache/module unavailable status=%d",rc); }
            }
        }
        if (compiled!=(unsigned)c->x[1]) fail(SP_UNSUPPORTED,"NVN Maxwell shader translation/native module creation pending",c->x[30]);
        c->x[0]=1;
    } else return 0;
    return 1;
}
