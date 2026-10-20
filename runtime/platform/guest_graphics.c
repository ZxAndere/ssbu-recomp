/* NV allocation registration and optional real desktop Vulkan initialization.
 * Unimplemented NVN entry points stop explicitly before advertising support. */
static uint64_t nvn_entry(const char *name) {
    for (unsigned i=0;i<active->nvn_entry_count;++i)
        if (!strcmp(name,active->nvn_entries[i].name)) return 0x7900000000ULL+(uint64_t)i*256;
    if (active->nvn_entry_count==2048) fail(SP_SYSTEM,"NVN linkage registry full",0);
    unsigned index=active->nvn_entry_count++; strcpy(active->nvn_entries[index].name,name);
    return 0x7900000000ULL+(uint64_t)index*256;
}
static void nvn_dispatch(Ctx *context,unsigned index) {
    if (index>=active->nvn_entry_count) fail(SP_INVALID,"invalid NVN linkage index",index);
    const char *name=active->nvn_entries[index].name;
    if (!strcmp(name,"nvnSetExternalDebuggerEnabledNVX")) {
        if (context->x[0]) fail(SP_UNSUPPORTED,"NVN external debugger backend unavailable",context->x[30]);
        active->nvn_debugger_disabled=1; sp_log("GAME NVN: external debugger disabled (no debugger backend)");
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
        sp_log("GAME NVN: device bound to real desktop Vulkan context; rendering unimplemented");
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
