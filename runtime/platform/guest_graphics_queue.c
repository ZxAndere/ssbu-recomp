static GuestNvnQueue *nvn_queue(uint64_t key,int builder,int create) {
    GuestNvnQueue *empty=NULL;
    for (unsigned i=0;i<32;++i) {
        GuestNvnQueue *q=&active->nvn_queues[i];
        if (q->key==key && key) {
            if (q->builder!=(unsigned)builder) fail(SP_INVALID,"NVN queue object type mismatch",key);
            return q;
        }
        if (!q->key && !empty) empty=q;
    }
    if (!create || !key || !empty) fail(SP_INVALID,"unknown NVN queue object or full registry",key);
    sp_recomp_access(key,1,1); empty->key=key; empty->builder=(unsigned)builder; return empty;
}
static int nvn_queue_dispatch(Ctx *c,const char *name) {
    if (strncmp(name,"nvnQueue",8)) return 0;
    int builder=!strncmp(name+8,"Builder",7); const char *op=name+(builder ? 15 : 8);
    if (builder && !strcmp(op,"SetDefaults")) {
        GuestNvnQueue *q=nvn_queue(c->x[0],1,1); uint64_t key=q->key;
        memset(q,0,sizeof *q); q->key=key; q->builder=1; q->priority=100;
        q->command_bytes=q->compute_bytes=q->control_bytes=65536; return 1;
    }
    if (!builder && !strcmp(op,"Initialize")) {
        GuestNvnQueue *b=nvn_queue(c->x[1],1,0);
        if (!active->graphics || b->device!=active->nvn_device || !b->device) fail(SP_INVALID,"NVN queue has no device",b->device);
        /* In the matched local SDK (0x4242e0, 0x42437c), bits zero and one
         * suppress internal Tegra workspace allocations. Native backends own
         * workspace instead; this does not enable any shader feature. */
        if (b->flags&~3u) fail(SP_UNSUPPORTED,"NVN queue flags require native adaptation",b->flags);
        if (b->priority!=100) fail(SP_UNSUPPORTED,"NVN queue priority requires native adaptation",b->priority);
        GuestNvnQueue *q=nvn_queue(c->x[0],0,1); uint64_t key=q->key;
        *q=*b; q->key=key; q->builder=0;
        if (q->flags) sp_log("GAME NVN: queue flags %x suppress Tegra workspaces; native backend owns storage",q->flags);
        sp_log("GAME NVN: queue bound to real %s graphics queue",native_graphics_name()); c->x[0]=1; return 1;
    }
    GuestNvnQueue *q=nvn_queue(c->x[0],builder,0);
    if (!builder && !strcmp(op,"SubmitCommands")) {
        if (c->x[1]>128) fail(SP_INVALID,"NVN command list count invalid",c->x[1]);
        unsigned applied=0;
        for (uint64_t at=0;at<c->x[1];++at) {
            uint64_t handle; memcpy(&handle,sp_recomp_access(c->x[2]+at*8,8,0),8);
            unsigned list=128;
            for (unsigned i=0;i<128;++i) if (active->nvn_lists[i].handle==handle && handle) { list=i; break; }
            if (list==128) fail(SP_INVALID,"unknown NVN command list handle",handle);
            for (unsigned i=0;i<active->nvn_lists[list].count;++i) {
                const GuestNvnRecord *record=&active->nvn_lists[list].records[i];
                if (record->operation==1 || record->operation==2) {
                    nvn_storage(record->object,record->operation==1 ? NVN_TEXTURE_POOL : NVN_SAMPLER_POOL,0);
                    if (record->operation==1) q->texture_pool=record->object; else q->sampler_pool=record->object;
                } else if (record->operation>=3 && record->operation<=8) {
                    unsigned state=record->operation-3; memcpy(q->states[state],record->state,record->bytes); q->state_mask|=1u<<state;
                } else fail(SP_UNSUPPORTED,"NVN command has no native executor",record->operation);
                ++applied;
            }
        }
        sp_log("GAME NVN: applied %u state records; no draw/dispatch commands recorded",applied);
    } else if (builder && !strcmp(op,"SetDevice")) q->device=c->x[1];
    else if (builder && !strcmp(op,"SetFlags")) q->flags=(unsigned)c->x[1];
    else if (builder && !strcmp(op,"SetCommandMemorySize")) q->command_bytes=c->x[1];
    else if (builder && !strcmp(op,"SetComputeMemorySize")) q->compute_bytes=c->x[1];
    else if (builder && !strcmp(op,"SetControlMemorySize")) q->control_bytes=c->x[1];
    else if (builder && !strcmp(op,"SetCommandFlushThreshold")) q->threshold=c->x[1];
    else if (builder && !strcmp(op,"SetQueuePriority")) q->priority=(unsigned)c->x[1];
    else if (builder && !strcmp(op,"SetQueueMemory")) {
        if (c->x[2]) sp_recomp_access(c->x[1],(size_t)c->x[2],1);
        q->memory=c->x[1]; q->memory_bytes=c->x[2];
    } else if (!strcmp(op,"GetDevice")) c->x[0]=q->device;
    else if (!strcmp(op,"GetFlags")) c->x[0]=q->flags;
    else if (!strcmp(op,"GetCommandMemorySize")) c->x[0]=q->command_bytes;
    else if (!strcmp(op,"GetComputeMemorySize")) c->x[0]=q->compute_bytes;
    else if (!strcmp(op,"GetControlMemorySize")) c->x[0]=q->control_bytes;
    else if (!strcmp(op,"GetCommandFlushThreshold")) c->x[0]=q->threshold;
    else if (!strcmp(op,"GetQueuePriority")) c->x[0]=q->priority;
    else if (!strcmp(op,"GetMemory")) c->x[0]=q->memory;
    else if (!strcmp(op,"GetMemorySize")) c->x[0]=q->memory_bytes;
    else if (builder && !strcmp(op,"GetQueueMemorySize")) {
        if (q->command_bytes>512*1024*1024 || q->compute_bytes>512*1024*1024 || q->control_bytes>512*1024*1024)
            fail(SP_INVALID,"NVN queue workspace exceeds implementation limit",q->command_bytes);
        c->x[0]=(q->command_bytes+q->compute_bytes+q->control_bytes+4095)&~UINT64_C(4095);
    } else if (!builder && (!strcmp(op,"Flush") || !strcmp(op,"Finish") || !strcmp(op,"Finalize"))) {
        int rc=native_graphics_wait(active->graphics); if (rc) fail(rc,"native NVN queue wait failed",q->key);
        if (!strcmp(op,"Finalize")) memset(q,0,sizeof *q);
    } else if (!builder && !strcmp(op,"SetDebugLabel")) {
        char label[128]; if (c->x[1]) fs_guest_string(c->x[1],label,sizeof label); else label[0]=0;
        sp_log("GAME NVN: queue label=%s",label);
    } else return 0;
    return 1;
}
