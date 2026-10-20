/* NV allocation callback registration. This does not initialize a GPU or
 * emulate NVN. Every unimplemented graphics entry still stops explicitly. */
static int graphics_import(Ctx *context,const char *name) {
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
