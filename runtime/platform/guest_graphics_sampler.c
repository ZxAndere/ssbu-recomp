static unsigned nvn_sampler_index(uint64_t key,int create) {
    unsigned empty=512;
    for (unsigned i=0;i<512;++i) {
        if (active->nvn_samplers[i].key==key && key) return i;
        if (!active->nvn_samplers[i].key && empty==512) empty=i;
    }
    if (!create || empty==512 || !key) fail(SP_INVALID,"unknown NVN sampler or registry full",key);
    active->nvn_samplers[empty].key=key; return empty;
}
static int nvn_sampler_dispatch(Ctx *c,const char *name) {
    if (!strcmp(name,"nvnSamplerInitialize")) {
        NativeSamplerParameters parameters; memcpy(&parameters,sp_recomp_access(c->x[1],sizeof parameters,0),sizeof parameters);
        if (parameters.device!=active->nvn_device || !active->graphics) fail(SP_INVALID,"NVN sampler builder has no device",parameters.device);
        sp_recomp_access(c->x[0],1,1); unsigned i=nvn_sampler_index(c->x[0],1);
        if (active->nvn_samplers[i].native) fail(SP_INVALID,"NVN sampler initialized twice",c->x[0]);
        int rc=native_sampler_open(active->graphics,&active->nvn_samplers[i].native,&parameters);
        if (rc) fail(rc,"native NVN sampler creation failed",parameters.min_filter);
        active->nvn_samplers[i].parameters=parameters; c->x[0]=1;
    } else if (!strcmp(name,"nvnSamplerFinalize")) {
        unsigned i=nvn_sampler_index(c->x[0],0);
        int rc=native_sampler_close(active->graphics,&active->nvn_samplers[i].native);
        if (rc) fail(rc,"native NVN sampler release failed",c->x[0]); memset(&active->nvn_samplers[i],0,sizeof active->nvn_samplers[i]);
    } else if (!strcmp(name,"nvnSamplerSetDebugLabel")) {
        nvn_sampler_index(c->x[0],0); char label[128]; if (c->x[1]) fs_guest_string(c->x[1],label,sizeof label); else label[0]=0;
        sp_log("GAME NVN: sampler label=%s",label);
    } else if (!strcmp(name,"nvnSamplerGetDebugID")) { nvn_sampler_index(c->x[0],0); }
    else if (!strcmp(name,"nvnSamplerPoolRegisterSampler") || !strcmp(name,"nvnSamplerPoolRegisterSamplerBuilder")) {
        GuestNvnStorage *table=nvn_storage(c->x[0],NVN_SAMPLER_POOL,0);
        if (c->x[1]>=table->size/sizeof(NativeDescriptor)) fail(SP_INVALID,"NVN sampler descriptor index outside pool",c->x[1]);
        NativeSamplerParameters parameters;
        if (strstr(name,"Builder")) memcpy(&parameters,sp_recomp_access(c->x[2],sizeof parameters,0),sizeof parameters);
        else parameters=active->nvn_samplers[nvn_sampler_index(c->x[2],0)].parameters;
        unsigned slot=512;
        for (unsigned i=0;i<512;++i) if (active->nvn_samplers[i].pool==table->key && active->nvn_samplers[i].index==c->x[1]) { slot=i; break; }
        if (slot==512) {
            for (unsigned i=0;i<512;++i) if (!active->nvn_samplers[i].key) { slot=i; break; }
            if (slot==512) fail(SP_SYSTEM,"NVN sampler registry full",0);
            active->nvn_samplers[slot].key=UINT64_C(0x7b00000000)+slot*256;
        }
        int rc=native_sampler_close(active->graphics,&active->nvn_samplers[slot].native);
        if (!rc) rc=native_sampler_open(active->graphics,&active->nvn_samplers[slot].native,&parameters);
        if (rc) fail(rc,"native sampler descriptor creation failed",c->x[1]);
        active->nvn_samplers[slot].parameters=parameters; active->nvn_samplers[slot].pool=table->key; active->nvn_samplers[slot].index=c->x[1];
        NativeDescriptor descriptor={.object=active->nvn_samplers[slot].key,.generation=1};
        uint64_t offset=c->x[1]*sizeof descriptor;
        memcpy(sp_recomp_access(table->memory+offset,sizeof descriptor,1),&descriptor,sizeof descriptor);
        GuestNvnStorage *pool=nvn_storage(table->pool,NVN_POOL,0);
        if (native_buffer_copy(pool->native,(size_t)(table->offset+offset),&descriptor,sizeof descriptor,0)) fail(SP_SYSTEM,"native sampler descriptor upload failed",c->x[1]);
        sp_log("GAME NVN: native sampler registered index=%llu",(unsigned long long)c->x[1]);
    } else return 0;
    return 1;
}
