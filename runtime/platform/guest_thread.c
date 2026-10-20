/* Primary guest ThreadType ABI: size independently published by nnsdk-rs;
 * offsets verified against the private SDK's accessor/setup instructions.
 * This does not claim support for additional guest threads or scheduling. */
#define PRIMARY_THREAD UINT64_C(0x7500000000)
static void initialize_primary_thread(void) {
    unsigned char *object=active->primary_thread;
    uint64_t address=PRIMARY_THREAD,id=1,stack=0x7700000000ULL,size=active->stack.size,name=address+0x180;
    memcpy(object,&address,8); memcpy(object+8,&address,8);
    uint64_t list=address+0x10; memcpy(object+0x10,&list,8); memcpy(object+0x18,&list,8);
    object[0x40]=3; object[0x44]=16; object[0x46]=1;
    memcpy(object+0x48,&stack,8); memcpy(object+0x50,&stack,8); memcpy(object+0x58,&size,8);
    memcpy(object+0x180,"MainThread",11); memcpy(object+0x1a0,&name,8); memcpy(object+0x1b8,&id,8);
    memcpy(sp_recomp_access(0x78000001f8ULL,8,1),&address,8);
}
static void validate_primary_thread(uint64_t address) {
    if (address!=PRIMARY_THREAD) fail(SP_UNSUPPORTED,"guest thread is not the primary thread",address);
}
static int thread_import(Ctx *context,const char *name) {
    if (!strcmp(name,"_ZN2nn2os16GetCurrentThreadEv")) {
        memcpy(&context->x[0],sp_recomp_access(0x78000001f8ULL,8,0),8);
    } else if (!strcmp(name,"_ZN2nn2os25GetCurrentProcessorNumberEv")) {
        context->x[0]=0; /* primary guest thread is assigned logical core zero */
    } else if (!strcmp(name,"_ZN2nn2os11GetThreadIdEPKNS0_10ThreadTypeE")) {
        validate_primary_thread(context->x[0]); memcpy(&context->x[0],active->primary_thread+0x1b8,8);
    } else if (!strcmp(name,"_ZN2nn2os17GetThreadPriorityEPKNS0_10ThreadTypeE")) {
        validate_primary_thread(context->x[0]); int16_t priority; memcpy(&priority,active->primary_thread+0x44,2);
        context->x[0]=(uint32_t)(int32_t)priority;
    } else if (!strcmp(name,"_ZN2nn2os13SetThreadNameEPNS0_10ThreadTypeEPKc")) {
        validate_primary_thread(context->x[0]);
        size_t length=context->x[1] ? string_length(context->x[1],31) : 0;
        memset(active->primary_thread+0x180,0,32);
        if (length) memcpy(active->primary_thread+0x180,sp_recomp_access(context->x[1],length,0),length);
        uint64_t pointer=PRIMARY_THREAD+0x180; memcpy(active->primary_thread+0x1a0,&pointer,8);
    } else if (!strcmp(name,"_ZN2nn2os20SetThreadNamePointerEPNS0_10ThreadTypeEPKc")) {
        validate_primary_thread(context->x[0]);
        if (context->x[1]) string_length(context->x[1],SIZE_MAX);
        uint64_t pointer=context->x[1] ? context->x[1] : PRIMARY_THREAD+0x180;
        memcpy(active->primary_thread+0x1a0,&pointer,8);
    } else if (!strcmp(name,"_ZN2nn2os18GetThreadStackInfoEPmS1_PKNS0_10ThreadTypeE")) {
        validate_primary_thread(context->x[2]); uint64_t address=0x7700000000ULL,size=active->stack.size;
        if (context->x[0]) memcpy(sp_recomp_access(context->x[0],8,1),&address,8);
        if (context->x[1]) memcpy(sp_recomp_access(context->x[1],8,1),&size,8);
    } else return 0;
    return 1;
}
