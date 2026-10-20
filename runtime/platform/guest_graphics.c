/* NV allocation registration and optional real desktop Vulkan initialization.
 * Unimplemented NVN entry points stop explicitly before advertising support. */
#include "guest_graphics_storage.c"
#include "guest_graphics_commands.c"
#include "guest_graphics_queue.c"
#include "guest_graphics_sync.c"
#include "guest_graphics_sampler.c"
#include "guest_graphics_program.c"
static uint64_t nvn_entry(const char *name) {
    for (unsigned i=0;i<active->nvn_entry_count;++i)
        if (!strcmp(name,active->nvn_entries[i].name)) return 0x7900000000ULL+(uint64_t)i*256;
    if (active->nvn_entry_count==2048) fail(SP_SYSTEM,"NVN linkage registry full",0);
    unsigned index=active->nvn_entry_count++; strcpy(active->nvn_entries[index].name,name);
    return 0x7900000000ULL+(uint64_t)index*256;
}
static uint64_t nvn_cpu_state_target(const char *name) {
    static const char *const prefixes[]={"nvnBlendState","nvnChannelMaskState","nvnColorState","nvnDepthStencilState",
        "nvnMultisampleState","nvnPolygonState","nvnVertexAttribState","nvnVertexStreamState","nvnTextureView","nvnSamplerBuilder"};
    int allowed=0;
    for (unsigned i=0;i<sizeof prefixes/sizeof prefixes[0];++i)
        if (!strncmp(name,prefixes[i],strlen(prefixes[i]))) allowed=1;
    if (!allowed || !active->bootstrap || !active->bootstrap->sdk_program) return 0;
    SpGuestImage *sdk=active->bootstrap->sdk;
    const void *raw=sp_guest_pointer(sdk,0x7200c3b348ULL,8,0); if (!raw) return 0;
    uint64_t table; memcpy(&table,raw,8);
    for (unsigned i=0;i<516;++i) {
        uint32_t offset; raw=sp_guest_pointer(sdk,0x7200a968e4ULL+i*4,4,0); if (!raw) return 0;
        memcpy(&offset,raw,4); raw=sp_guest_pointer(sdk,0x7200a93230ULL+offset,128,0); if (!raw || !memchr(raw,0,128)) return 0;
        if (strcmp((const char *)raw,name+3)) continue;
        raw=sp_guest_pointer(sdk,table+i*8,8,0); if (!raw) return 0;
        uint64_t address; memcpy(&address,raw,8);
        const SpGuestProgram *program=active->bootstrap->sdk_program;
        size_t low=0,high=program->function_count;
        while (low<high) { size_t middle=low+(high-low)/2; if (program->functions[middle].addr<address) low=middle+1; else high=middle; }
        return low<program->function_count && program->functions[low].addr==address ? address : 0;
    }
    return 0;
}
/* In the locally matched 13.0.5 SDK these queries return literal scalars
 * without reading the device. Extract only that proven constant-return path;
 * never substitute the Switch's texture limits or GPU feature table.
 * The values appear to describe shader ABI versions (inference). */
static int nvn_software_info(unsigned query,int32_t *value) {
    if ((query>1 && (query<0x2e || query>0x31)) || !active->bootstrap || !active->bootstrap->sdk) return 0;
    SpGuestImage *sdk=active->bootstrap->sdk; uint64_t table=0x7200a930a8ULL;
    const void *pointer=sp_guest_pointer(sdk,table+query*4,4,0); if (!pointer) return 0;
    int32_t relative; memcpy(&relative,pointer,4); uint64_t pc=table+(int64_t)relative;
    pointer=sp_guest_pointer(sdk,pc,4,0); if (!pointer) return 0;
    uint32_t instruction; memcpy(&instruction,pointer,4);
    int32_t result;
    if ((instruction&0xffe0001f)==0x52800000) result=(int32_t)((instruction>>5)&0xffff); /* MOVZ W0, literal. */
    else if ((instruction&0xffff03ff)==0x320003e0 && ((instruction>>10)&63)<31)
        result=(int32_t)((UINT32_C(1)<<(((instruction>>10)&63)+1))-1); /* ORR W0,WZR, low-bit mask. */
    else {
        if (query!=0) return 0;
        pointer=sp_guest_pointer(sdk,0x720041d1b0ULL,4,0); if (!pointer) return 0;
        uint32_t initial; memcpy(&initial,pointer,4);
        if ((initial&0xffe0001f)!=0x52800000) return 0;
        result=(int32_t)((initial>>5)&0xffff); pc-=4; /* MOV W0 default before the switch. */
    }
    pc+=4;
    for (unsigned step=0;step<8;++step,pc+=4) {
        pointer=sp_guest_pointer(sdk,pc,4,0); if (!pointer) return 0; memcpy(&instruction,pointer,4);
        if (instruction==0xd65f03c0) { *value=result; return 1; }
        if ((instruction&0xfc000000)==0x14000000) {
            int64_t delta=(int32_t)(instruction<<6)>>4; pc+=delta-4; continue;
        }
        /* Restore pairs from SP, excluding X0; ADD SP,SP,immediate. */
        if ((instruction&0xffc003e0)==0xa94003e0 && (instruction&31)!=0 && ((instruction>>10)&31)!=0) continue;
        if ((instruction&0xffc003ff)==0x910003ff) continue;
        return 0;
    }
    return 0;
}
static void nvn_dispatch(Ctx *context,unsigned index) {
    if (index>=active->nvn_entry_count) fail(SP_INVALID,"invalid NVN linkage index",index);
    const char *name=active->nvn_entries[index].name;
    if (!active->nvn_entries[index].sdk_target) active->nvn_entries[index].sdk_target=nvn_cpu_state_target(name);
    if (active->nvn_entries[index].sdk_target) { invoke(context,active->nvn_entries[index].sdk_target); return; }
    if (nvn_sampler_dispatch(context,name)) return;
    if (nvn_program_dispatch(context,name)) return;
    if (nvn_storage_dispatch(context,name)) return;
    if (nvn_commands_dispatch(context,name)) return;
    if (nvn_sync_dispatch(context,name)) return;
    if (nvn_queue_dispatch(context,name)) return;
    if (!strcmp(name,"nvnSetExternalDebuggerEnabledNVX")) {
        if (context->x[0]) fail(SP_UNSUPPORTED,"NVN external debugger backend unavailable",context->x[30]);
        active->nvn_debugger_disabled=1; sp_log("GAME NVN: external debugger disabled (no debugger backend)");
    } else if (!strcmp(name,"nvnDeviceFinalizeCommandHandle")) {
        if (context->x[0]!=active->nvn_device || !active->nvn_device) fail(SP_INVALID,"NVN device invalid",context->x[0]);
        unsigned slot=128;
        for (unsigned i=0;i<128;++i) if (active->nvn_lists[i].handle==context->x[1] && context->x[1]) { slot=i; break; }
        if (slot==128) fail(SP_INVALID,"NVN command list handle invalid",context->x[1]);
        free(active->nvn_lists[slot].records); memset(&active->nvn_lists[slot],0,sizeof active->nvn_lists[slot]);
    } else if (!strcmp(name,"nvnDeviceGetInteger")) {
        if (!active->nvn_device || context->x[0]!=active->nvn_device) fail(SP_INVALID,"NVN device is not initialized",context->x[0]);
        int32_t value=0;
        int software=nvn_software_info((unsigned)context->x[1],&value);
        int policy=!software && native_graphics_policy((unsigned)context->x[1],&value);
        if (!software && !policy && !native_graphics_integer(active->graphics,(unsigned)context->x[1],&value))
            fail(SP_UNSUPPORTED,"unimplemented NVN device capability",context->x[1]);
        memcpy(sp_recomp_access(context->x[2],4,1),&value,4);
        sp_log("GAME NVN: %s %x=%d",software ? "device-independent SDK scalar" : policy ? "translator policy" : "native backend limit",(unsigned)context->x[1],value);
    } else if (!strcmp(name,"nvnDeviceSetWindowOriginMode") || !strcmp(name,"nvnDeviceSetDepthMode")) {
        if (context->x[0]!=active->nvn_device || !active->nvn_device || context->x[1]>1)
            fail(SP_INVALID,"invalid NVN coordinate mode",context->x[1]);
        /* These are persistent device state, not immediate GPU commands. The
         * command translator must consume them when drawing is implemented. */
        if (strstr(name,"Origin")) active->nvn_origin_mode=(uint32_t)context->x[1];
        else active->nvn_depth_mode=(uint32_t)context->x[1];
    } else if (!strcmp(name,"nvnDeviceGetWindowOriginMode") || !strcmp(name,"nvnDeviceGetDepthMode")) {
        if (context->x[0]!=active->nvn_device || !active->nvn_device) fail(SP_INVALID,"NVN device not initialized",context->x[0]);
        context->x[0]=strstr(name,"Origin") ? active->nvn_origin_mode : active->nvn_depth_mode;
    } else if (!strcmp(name,"nvnDeviceRegisterFastClearDepth") || !strcmp(name,"nvnDeviceRegisterFastClearColor") ||
               !strcmp(name,"nvnDeviceRegisterFastClearColori") || !strcmp(name,"nvnDeviceRegisterFastClearColorui")) {
        if (!active->nvn_device || context->x[0]!=active->nvn_device) fail(SP_INVALID,"NVN device not initialized",context->x[0]);
        /* Optional hardware compression registration is unavailable. Report
         * failure so callers can use ordinary clears; never fake registration. */
        sp_log("GAME NVN: %s unavailable; registration rejected",name); context->x[0]=0;
    } else if (!strcmp(name,"nvnDeviceGetProcAddress")) {
        char requested[128]; fs_guest_string(context->x[1],requested,sizeof requested);
        /* Like unresolved module imports, a missing entry binds an explicit
         * stop thunk. Resolving a name is never counted as implementation. */
        context->x[0]=nvn_entry(requested);
    } else if (!strcmp(name,"nvnDeviceBuilderSetDefaults")) {
        sp_recomp_access(context->x[0],1,1); active->nvn_builder=context->x[0]; active->nvn_builder_flags=0;
    } else if (!strcmp(name,"nvnDeviceBuilderSetFlags")) {
        if (context->x[0]!=active->nvn_builder) fail(SP_INVALID,"NVN builder not initialized",context->x[0]);
        active->nvn_builder_flags=(uint32_t)context->x[1];
    } else if (!strcmp(name,"nvnDeviceBuilderGetFlags")) {
        if (context->x[0]!=active->nvn_builder) fail(SP_INVALID,"NVN builder not initialized",context->x[0]);
        context->x[0]=active->nvn_builder_flags;
    } else if (!strcmp(name,"nvnDeviceInitialize")) {
        if (!active->graphics || active->nvn_device || context->x[1]!=active->nvn_builder)
            fail(SP_INVALID,"NVN device/backend lifecycle invalid",context->x[0]);
        if (active->nvn_builder_flags) fail(SP_UNSUPPORTED,"NVN device flags require backend implementation",active->nvn_builder_flags);
        sp_recomp_access(context->x[0],1,1); active->nvn_device=context->x[0]; context->x[0]=1;
        sp_log("GAME NVN: device bound to real %s context; rendering unimplemented",native_graphics_name());
    } else {
        sp_log("GAME NVN UNIMPLEMENTED: %s x0=%llx x1=%llx",name,(unsigned long long)context->x[0],(unsigned long long)context->x[1]);
        fail(SP_UNSUPPORTED,name,context->x[30]);
    }
}
static int graphics_import(Ctx *context,const char *name) {
    if (!strcmp(name,"nvnBootstrapLoader")) {
        char entry[128]; fs_guest_string(context->x[0],entry,sizeof entry);
        sp_log("GAME NVN: requested %s",entry);
        context->x[0]=nvn_entry(entry); return 1;
    }
    if (!strcmp(name,"_ZN2nv18InitializeGraphicsEPvm")) {
        if (active->graphics) fail(SP_INVALID,"graphics already initialized",context->x[30]);
        if (!active->graphics_allocators[0][0] || !context->x[1]) fail(SP_INVALID,"graphics allocator/workspace absent",context->x[0]);
        sp_recomp_access(context->x[0],(size_t)context->x[1],1);
        int rc=native_graphics_open(&active->graphics);
        if (rc) fail(rc,"native graphics backend unavailable",context->x[30]);
        active->graphics_workspace=context->x[0]; active->graphics_workspace_bytes=context->x[1]; return 1;
    }
    if (strcmp(name,"_ZN2nv20SetGraphicsAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_") &&
        strcmp(name,"_ZN2nv28SetGraphicsDevtoolsAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_")) return 0;
    unsigned slot=strstr(name,"Devtools")!=NULL;
    for (unsigned i=0;i<3;++i) {
        if (!context->x[i] || !sp_guest_pointer(active->image,context->x[i],4,0))
            fail(SP_INVALID,"invalid NV allocation callback",context->x[i]);
    }
    memcpy(active->graphics_allocators[slot],context->x,4*sizeof(uint64_t));
    sp_log("GAME GRAPHICS: %s allocation callbacks registered; GPU initialization pending",slot ? "devtools" : "runtime");
    return 1;
}
