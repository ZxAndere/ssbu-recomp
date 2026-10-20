static GuestNvnStorage *nvn_storage(uint64_t key,unsigned kind,int create) {
    GuestNvnStorage *empty=NULL;
    for (unsigned i=0;i<512;++i) {
        GuestNvnStorage *object=&active->nvn_storage[i];
        if (object->key==key && key) {
            if (object->kind!=kind) {
                /* Stack builders have no finalize call and their storage may
                 * be reused for another builder type after its scope ends. */
                if (create && (kind==NVN_POOL_BUILDER || kind==NVN_BUFFER_BUILDER) &&
                    (object->kind==NVN_POOL_BUILDER || object->kind==NVN_BUFFER_BUILDER)) {
                    memset(object,0,sizeof *object); object->key=key; object->kind=kind;
                } else fail(SP_INVALID,"NVN object type mismatch",key);
            }
            return object;
        }
        if (!object->key && !empty) empty=object;
    }
    if (!create || !key) fail(SP_INVALID,"unknown NVN storage object",key);
    if (!empty) fail(SP_SYSTEM,"NVN storage registry full",key);
    sp_recomp_access(key,1,1); empty->key=key; empty->kind=kind; return empty;
}
static int nvn_storage_dispatch(Ctx *c,const char *name) {
    unsigned kind; const char *op;
    if (!strncmp(name,"nvnMemoryPoolBuilder",20)) { kind=NVN_POOL_BUILDER; op=name+20; }
    else if (!strncmp(name,"nvnMemoryPool",13)) { kind=NVN_POOL; op=name+13; }
    else if (!strncmp(name,"nvnBufferBuilder",16)) { kind=NVN_BUFFER_BUILDER; op=name+16; }
    else if (!strncmp(name,"nvnBuffer",9)) { kind=NVN_BUFFER; op=name+9; }
    else if (!strncmp(name,"nvnTexturePool",14)) { kind=NVN_TEXTURE_POOL; op=name+14; }
    else if (!strncmp(name,"nvnSamplerPool",14)) { kind=NVN_SAMPLER_POOL; op=name+14; }
    else return 0;
    int builder=kind==NVN_POOL_BUILDER || kind==NVN_BUFFER_BUILDER;
    if ((kind==NVN_TEXTURE_POOL || kind==NVN_SAMPLER_POOL) && !strcmp(op,"Initialize")) {
        GuestNvnStorage *pool=nvn_storage(c->x[1],NVN_POOL,0);
        uint64_t maximum=kind==NVN_TEXTURE_POOL ? 1u<<20 : 1u<<12;
        if (!c->x[3] || c->x[3]>maximum || c->x[2]%sizeof(NativeDescriptor) || c->x[2]>pool->size ||
            c->x[3]>(pool->size-c->x[2])/sizeof(NativeDescriptor)) fail(SP_INVALID,"NVN descriptor pool outside storage",c->x[2]);
        GuestNvnStorage *o=nvn_storage(c->x[0],kind,1);
        if (o->pool) fail(SP_INVALID,"NVN descriptor pool initialized twice",o->key);
        o->pool=pool->key; o->offset=c->x[2]; o->size=c->x[3]*sizeof(NativeDescriptor);
        o->memory=pool->memory+o->offset; o->device=pool->device;
        sp_log("GAME NVN: descriptor table entries=%llu offset=%llu",(unsigned long long)c->x[3],(unsigned long long)o->offset);
        c->x[0]=1; return 1;
    }
    if (builder && !strcmp(op,"SetDefaults")) {
        GuestNvnStorage *o=nvn_storage(c->x[0],kind,1); uint64_t key=o->key;
        memset(o,0,sizeof *o); o->key=key; o->kind=kind; return 1;
    }
    if (!builder && !strcmp(op,"Initialize")) {
        GuestNvnStorage *b=nvn_storage(c->x[1],kind-1,0);
        if (!active->graphics || !active->nvn_device || b->device!=active->nvn_device)
            fail(SP_INVALID,"NVN storage builder has no device",b->device);
        GuestNvnStorage *o=nvn_storage(c->x[0],kind,1);
        if (o->native || o->size) fail(SP_INVALID,"NVN storage initialized twice",o->key);
        if (!b->size) { c->x[0]=0; return 1; }
        if (kind==NVN_POOL) {
            if ((b->flags&0x200) || !b->memory) fail(SP_UNSUPPORTED,"NVN virtual memory pools require page mappings",b->flags);
            void *memory=sp_recomp_access(b->memory,(size_t)b->size,0);
            int rc=native_buffer_open(active->graphics,&o->native,(size_t)b->size);
            if (rc) fail(rc,"native NVN memory-pool allocation failed",b->size);
            if (native_buffer_copy(o->native,0,memory,(size_t)b->size,0)) fail(SP_SYSTEM,"native pool upload failed",b->memory);
            o->memory=b->memory; o->size=b->size; o->flags=b->flags; o->device=b->device;
            sp_log("GAME NVN: native pool bytes=%llu flags=%x",(unsigned long long)o->size,o->flags);
        } else {
            GuestNvnStorage *pool=nvn_storage(b->pool,NVN_POOL,0);
            if (!pool->native || b->offset>pool->size || b->size>pool->size-b->offset) fail(SP_INVALID,"NVN buffer outside pool",b->offset);
            o->memory=pool->memory+b->offset; o->size=b->size; o->pool=b->pool; o->offset=b->offset; o->device=b->device;
        }
        c->x[0]=1; return 1;
    }
    GuestNvnStorage *o=nvn_storage(c->x[0],kind,0);
    if (builder && !strcmp(op,"SetDevice")) {
        if (c->x[1]!=active->nvn_device || !c->x[1]) fail(SP_INVALID,"NVN builder device mismatch",c->x[1]);
        o->device=c->x[1];
    } else if (builder && !strcmp(op,"SetStorage")) {
        if (kind==NVN_POOL_BUILDER) { o->memory=c->x[1]; o->size=c->x[2]; }
        else { o->pool=c->x[1]; o->offset=c->x[2]; o->size=c->x[3]; }
    } else if (kind==NVN_POOL_BUILDER && !strcmp(op,"SetFlags")) o->flags=(unsigned)c->x[1];
    else if (!strcmp(op,"GetDevice")) c->x[0]=o->device;
    else if (!strcmp(op,"GetSize")) c->x[0]=o->kind>=NVN_TEXTURE_POOL ? o->size/sizeof(NativeDescriptor) : o->size;
    else if (!strcmp(op,"GetFlags")) c->x[0]=o->flags;
    else if (!strcmp(op,"GetMemory") || !strcmp(op,"Map")) c->x[0]=o->memory;
    else if (!strcmp(op,"GetBufferAddress") || !strcmp(op,"GetAddress")) c->x[0]=o->memory;
    else if (!strcmp(op,"GetMemoryPool")) c->x[0]=o->pool;
    else if (!strcmp(op,"GetMemoryOffset")) c->x[0]=o->offset;
    else if (!strcmp(op,"GetMemoryPoolOffset")) c->x[0]=o->offset;
    else if (!builder && !strcmp(op,"SetDebugLabel")) {
        char label[128]; if (c->x[1]) fs_guest_string(c->x[1],label,sizeof label);
        else label[0]=0; sp_log("GAME NVN: storage %llx label=%s",(unsigned long long)o->key,label);
    } else if (!builder && !strcmp(op,"GetDebugID")) c->x[0]=o->key;
    else if (!builder && (!strcmp(op,"FlushMappedRange") || !strcmp(op,"InvalidateMappedRange"))) {
        if (c->x[1]>o->size || c->x[2]>o->size-c->x[1]) fail(SP_INVALID,"NVN flush outside storage",c->x[1]);
        GuestNvnStorage *pool=kind==NVN_POOL ? o : nvn_storage(o->pool,NVN_POOL,0);
        void *memory=sp_recomp_access(o->memory+c->x[1],(size_t)c->x[2],!strcmp(op,"InvalidateMappedRange"));
        int rc=native_buffer_copy(pool->native,(size_t)(o->offset+c->x[1]),memory,(size_t)c->x[2],!strcmp(op,"InvalidateMappedRange"));
        if (rc) fail(rc,"native NVN storage transfer failed",o->key);
    } else if (!builder && !strcmp(op,"Finalize")) {
        if (kind==NVN_SAMPLER_POOL) for (unsigned i=0;i<512;++i) if (active->nvn_samplers[i].pool==o->key) {
            int rc=native_sampler_close(active->graphics,&active->nvn_samplers[i].native);
            if (rc) fail(rc,"native sampler table retirement failed",o->key);
            memset(&active->nvn_samplers[i],0,sizeof active->nvn_samplers[i]);
        }
        if (kind==NVN_POOL) {
            for (unsigned i=0;i<512;++i) if (active->nvn_storage[i].pool==o->key && active->nvn_storage[i].key)
                fail(SP_INVALID,"NVN pool still owns resources",o->key);
            int rc=native_buffer_close(active->graphics,&o->native); if (rc) fail(rc,"native pool retirement failed",o->key);
        }
        memset(o,0,sizeof *o);
    } else return 0;
    return 1;
}
