/* State-only IR, with snapshots taken when recorded. Pipelines consume this
 * state at draw time. Draw/dispatch/copy/clear are not silently recorded: they
 * still stop until a native executor exists for the particular operation. */
static void nvn_record(GuestNvnCommand *object,unsigned operation,uint64_t source,unsigned bytes) {
    if (!object->recording) fail(SP_INVALID,"NVN command outside recording",object->key);
    if (bytes>24 || object->record_count==128 || object->command_used>object->command_bytes ||
        sizeof(GuestNvnRecord)>object->command_bytes-object->command_used)
        fail(SP_UNSUPPORTED,"NVN recorder needs more command memory",object->key);
    GuestNvnRecord *record=&object->records[object->record_count++]; memset(record,0,sizeof *record);
    record->operation=operation; record->object=source; record->bytes=bytes;
    if (bytes) memcpy(record->state,sp_recomp_access(source,bytes,0),bytes);
    memcpy(sp_recomp_access(object->command_memory+object->command_used,sizeof *record,1),record,sizeof *record);
    object->command_used+=sizeof *record;
}
static int nvn_commands_dispatch(Ctx *c,const char *name) {
    if (strncmp(name,"nvnCommandBuffer",16)) return 0;
    const char *op=name+16; GuestNvnCommand *object=NULL,*empty=NULL;
    for (unsigned i=0;i<64;++i) {
        if (active->nvn_commands[i].key==c->x[0] && c->x[0]) object=&active->nvn_commands[i];
        if (!active->nvn_commands[i].key && !empty) empty=&active->nvn_commands[i];
    }
    if (!strcmp(op,"Initialize")) {
        if (object || !empty || !c->x[0] || c->x[1]!=active->nvn_device || !active->graphics)
            fail(SP_INVALID,"NVN command recorder lifecycle invalid",c->x[0]);
        sp_recomp_access(c->x[0],1,1); empty->key=c->x[0]; empty->device=c->x[1]; empty->callback_enabled=1;
        sp_log("GAME NVN: command recorder initialized; GPU command translation pending"); c->x[0]=1; return 1;
    }
    if (!object) fail(SP_INVALID,"unknown NVN command recorder",c->x[0]);
    if (!strcmp(op,"SetMemoryCallback")) object->callback=c->x[1];
    else if (!strcmp(op,"SetMemoryCallbackData")) object->callback_data=c->x[1];
    else if (!strcmp(op,"GetMemoryCallback")) c->x[0]=object->callback;
    else if (!strcmp(op,"GetMemoryCallbackData")) c->x[0]=object->callback_data;
    else if (!strcmp(op,"SetCommandMemoryCallbackEnabled")) object->callback_enabled=(unsigned)(c->x[1]&1);
    else if (!strcmp(op,"AddCommandMemory")) {
        GuestNvnStorage *pool=nvn_storage(c->x[1],NVN_POOL,0);
        if (!c->x[3] || (c->x[2]&3) || (c->x[3]&3) || c->x[2]>pool->size || c->x[3]>pool->size-c->x[2])
            fail(SP_INVALID,"NVN command memory outside pool",c->x[2]);
        if (object->command_memory) fail(SP_UNSUPPORTED,"NVN multiple command memory regions pending",c->x[0]);
        object->command_memory=pool->memory+c->x[2]; object->command_bytes=c->x[3];
    } else if (!strcmp(op,"AddControlMemory")) {
        if (!c->x[2] || (c->x[1]&7)) fail(SP_INVALID,"invalid NVN control memory",c->x[1]);
        sp_recomp_access(c->x[1],(size_t)c->x[2],1);
        if (object->control_memory) fail(SP_UNSUPPORTED,"NVN multiple control memory regions pending",c->x[0]);
        object->control_memory=c->x[1]; object->control_bytes=c->x[2];
    } else if (!strcmp(op,"GetCommandMemorySize")) c->x[0]=object->command_bytes;
    else if (!strcmp(op,"GetControlMemorySize")) c->x[0]=object->control_bytes;
    else if (!strcmp(op,"GetCommandMemoryFree")) c->x[0]=object->command_bytes-object->command_used;
    else if (!strcmp(op,"GetControlMemoryFree")) c->x[0]=object->control_bytes-object->control_used;
    else if (!strcmp(op,"GetCommandMemoryUsed")) c->x[0]=object->command_used;
    else if (!strcmp(op,"GetControlMemoryUsed")) c->x[0]=object->control_used;
    else if (!strcmp(op,"IsRecording")) c->x[0]=object->recording;
    else if (!strcmp(op,"BeginRecording")) {
        if (object->recording) fail(SP_INVALID,"NVN command recorder already recording",c->x[0]);
        object->recording=1; object->record_count=0;
    } else if (!strcmp(op,"SetTexturePool") || !strcmp(op,"SetSamplerPool")) {
        unsigned texture=!strcmp(op,"SetTexturePool");
        nvn_storage(c->x[1],texture ? NVN_TEXTURE_POOL : NVN_SAMPLER_POOL,0);
        nvn_record(object,texture ? 1 : 2,c->x[1],0);
    } else if (!strcmp(op,"EndRecording")) {
        if (!object->recording || object->control_used>object->control_bytes || 32>object->control_bytes-object->control_used)
            fail(SP_INVALID,"NVN end-recording lifecycle/control memory invalid",object->key);
        unsigned slot=128;
        for (unsigned i=0;i<128;++i) if (!active->nvn_lists[i].handle) { slot=i; break; }
        if (slot==128) fail(SP_SYSTEM,"NVN command list registry full",0);
        size_t bytes=object->record_count*sizeof(GuestNvnRecord);
        GuestNvnRecord *records=malloc(bytes ? bytes : 1); if (!records) fail(SP_SYSTEM,"NVN command list allocation failed",bytes);
        memcpy(records,object->records,bytes);
        uint64_t handle=UINT64_C(0x7a00000000)+(++active->next_nvn_list)*256;
        active->nvn_lists[slot].handle=handle; active->nvn_lists[slot].records=records; active->nvn_lists[slot].count=object->record_count;
        unsigned char control[32]={0}; memcpy(control,&handle,8);
        memcpy(sp_recomp_access(object->control_memory+object->control_used,32,1),control,32);
        object->control_used+=32; object->recording=0; c->x[0]=handle;
    } else if (!strcmp(op,"SetDebugLabel")) {
        char label[128]; if (c->x[1]) fs_guest_string(c->x[1],label,sizeof label); else label[0]=0;
        sp_log("GAME NVN: command recorder label=%s",label);
    } else if (!strcmp(op,"Finalize")) {
        if (object->recording) fail(SP_INVALID,"NVN recorder finalized during recording",c->x[0]);
        memset(object,0,sizeof *object);
    } else {
        static const struct { const char *name; unsigned bytes; } bindings[]={
            {"BindBlendState",8},{"BindChannelMaskState",4},{"BindColorState",4},{"BindDepthStencilState",8},
            {"BindMultisampleState",24},{"BindPolygonState",4}};
        for (unsigned i=0;i<sizeof bindings/sizeof bindings[0];++i) if (!strcmp(op,bindings[i].name)) {
            nvn_record(object,i+3,c->x[1],bindings[i].bytes); return 1;
        }
        return 0;
    }
    return 1;
}
