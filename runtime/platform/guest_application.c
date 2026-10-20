/* Own application lifecycle resources replace Horizon applet-service setup.
 * Notification delivery/focus/power policy still require a native adapter;
 * unknown APIs fail instead of returning invented platform capabilities. */
static int application_import(Ctx *context,const char *name) {
    if (!strcmp(name,"_ZN2nn2oe10InitializeEv")) {
        if (!active->application_event) {
            if (sp_event_create(&active->application_event,1,0)) fail(SP_SYSTEM,"application event creation failed",0);
            active->application_started=sp_now_us();
            sp_log("GAME APPLICATION: lifecycle initialized");
        }
    } else if (!strcmp(name,"_ZN2nn2oe8FinalizeEv")) {
        if (!active->application_event) fail(SP_INVALID,"application not initialized",0);
        if (sp_event_destroy(&active->application_event)) fail(SP_SYSTEM,"application event release failed",0);
        active->application_started=0;
    } else if (!strcmp(name,"_ZN2nn2oe27SetPerformanceConfigurationENS0_15PerformanceModeEi")) {
        if (!active->application_event || context->x[0]>1) fail(SP_INVALID,"invalid application performance request",context->x[0]);
        /* Nintendo power profiles are hints on a host with fixed clocks.
         * Preserve requests for native scheduling/renderer policy; never claim
         * to apply Tegra frequencies to a PS4 or to have measured performance. */
        active->performance_hints[context->x[0]]=(uint32_t)context->x[1];
        sp_log("GAME APPLICATION: mode=%u profile=%x recorded; native clocks unchanged",
               (unsigned)context->x[0],(unsigned)context->x[1]);
    } else return 0;
    (void)context; return 1;
}
