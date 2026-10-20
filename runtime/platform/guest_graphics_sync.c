static unsigned nvn_sync(uint64_t key,int create) {
    unsigned empty=128;
    for (unsigned i=0;i<128;++i) {
        if (active->nvn_syncs[i].key==key && key) return i;
        if (!active->nvn_syncs[i].key && empty==128) empty=i;
    }
    if (!create || !key || empty==128) fail(SP_INVALID,"invalid NVN sync or registry full",key);
    sp_recomp_access(key,1,1); active->nvn_syncs[empty].key=key; return empty;
}
static int nvn_sync_dispatch(Ctx *c,const char *name) {
    if (!strcmp(name,"nvnSyncInitialize")) {
        if (!active->graphics || c->x[1]!=active->nvn_device) fail(SP_INVALID,"NVN sync device invalid",c->x[1]);
        unsigned i=nvn_sync(c->x[0],1);
        if (active->nvn_syncs[i].native) fail(SP_INVALID,"NVN sync initialized twice",c->x[0]);
        int rc=native_fence_open(active->graphics,&active->nvn_syncs[i].native);
        if (rc) fail(rc,"native NVN fence allocation failed",c->x[0]);
        c->x[0]=1;
    } else if (!strcmp(name,"nvnSyncFinalize")) {
        unsigned i=nvn_sync(c->x[0],0); int rc=native_fence_close(active->graphics,&active->nvn_syncs[i].native);
        if (rc) fail(rc,"native NVN fence release failed",c->x[0]); active->nvn_syncs[i].key=0;
    } else if (!strcmp(name,"nvnSyncSetDebugLabel")) {
        nvn_sync(c->x[0],0); char label[128]; if (c->x[1]) fs_guest_string(c->x[1],label,sizeof label); else label[0]=0;
        sp_log("GAME NVN: sync label=%s",label);
    } else if (!strcmp(name,"nvnSyncWait")) {
        unsigned i=nvn_sync(c->x[0],0); c->x[0]=native_fence_wait(active->graphics,active->nvn_syncs[i].native,c->x[1]);
    } else if (!strcmp(name,"nvnQueueFenceSync")) {
        nvn_queue(c->x[0],0,0); unsigned i=nvn_sync(c->x[1],0);
        if (c->x[2]>1 || c->x[3]>1) fail(SP_UNSUPPORTED,"NVN fence mode unsupported",c->x[2]);
        int rc=native_fence_signal(active->graphics,active->nvn_syncs[i].native);
        if (rc) fail(rc,"native NVN fence submission failed",c->x[1]);
    } else if (!strcmp(name,"nvnQueueWaitSync")) {
        nvn_queue(c->x[0],0,0); unsigned i=nvn_sync(c->x[1],0);
        unsigned result=native_fence_wait(active->graphics,active->nvn_syncs[i].native,UINT64_MAX); c->x[0]=result<2;
    } else return 0;
    return 1;
}
