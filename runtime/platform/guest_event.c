static GuestEvent *find_event(uint64_t address) {
    for (unsigned i=0;i<128;++i) if (active->events[i].native && active->events[i].address==address) return &active->events[i];
    fail(SP_INVALID,"unknown guest event",address); return NULL;
}
static GuestEvent *register_event(uint64_t address,SpEvent *native,unsigned system,unsigned auto_clear,unsigned owned) {
    GuestEvent *slot=NULL;
    for (unsigned i=0;i<128;++i) {
        GuestEvent *event=&active->events[i];
        if (event->native && event->address==address) {
            if (owned) sp_event_destroy(&native);
            fail(SP_INVALID,"event already initialized",address);
        }
        if (!event->native && !slot) slot=event;
    }
    if (!slot) { if (owned) sp_event_destroy(&native); fail(SP_SYSTEM,"guest event registry full",address); }
    *slot=(GuestEvent){native,address,system,auto_clear,owned};
    unsigned char *object=sp_recomp_access(address,system ? 0x29 : 0x28,1);
    memset(object,0,system ? 0x29 : 0x28);
    memcpy(object,&address,8); memcpy(object+8,&address,8);
    object[0x12]=(unsigned char)auto_clear; object[0x13]=1;
    if (system) object[0x28]=1; /* In-process EventType in the SystemEvent union. */
    return slot;
}
static void event_signal(GuestEvent *event,int signaled) {
    if ((signaled ? sp_event_set(event->native) : sp_event_reset(event->native)))
        fail(SP_SYSTEM,"native guest event transition failed",event->address);
    *(unsigned char *)sp_recomp_access(event->address+0x10,1,1)=(unsigned char)!!signaled;
}
static int event_wait(GuestEvent *event,uint64_t timeout) {
    uint64_t started=sp_now_us();
    for (;;) {
        if (event->address==0x7500000200ULL) application_refresh_focus();
        int rc=sp_event_wait(event->native,0);
        if (!rc) {
            if (event->auto_clear) *(unsigned char *)sp_recomp_access(event->address+0x10,1,1)=0;
            return 1;
        }
        if (rc!=SP_TIMEOUT) fail(SP_SYSTEM,"native guest event wait failed",event->address);
        if (!timeout || (timeout!=UINT64_MAX && sp_now_us()-started>=timeout)) return 0;
        if (active->current_thread) {
            GuestThread *thread=&active->threads[active->current_thread];
            thread->state=TASK_EVENT; thread->wait_object=event->address;
            thread->deadline=timeout==UINT64_MAX ? 0 : started+timeout;
        }
        if (!sp_recomp_budget--) sp_recomp_checkpoint(active->current_thread ? active->threads[active->current_thread].context.x[30] : 0);
        guest_thread_cooperate();
        if (!active->current_thread) sp_sleep_ms(1);
    }
}
static int event_import(Ctx *context,const char *name) {
    if (!strcmp(name,"_ZN2nn2os15InitializeEventEPNS0_9EventTypeEbNS0_14EventClearModeE") ||
        !strcmp(name,"_ZN2nn2os17CreateSystemEventEPNS0_15SystemEventTypeENS0_14EventClearModeEb")) {
        unsigned system=!strcmp(name,"_ZN2nn2os17CreateSystemEventEPNS0_15SystemEventTypeENS0_14EventClearModeEb");
        unsigned clear=(unsigned)context->x[system ? 1 : 2];
        if (clear>1) fail(SP_INVALID,"invalid event clear mode",clear);
        if (system && context->x[2]) fail(SP_UNSUPPORTED,"cross-process guest event requested",context->x[0]);
        SpEvent *native=NULL; unsigned signaled=system ? 0 : !!context->x[1];
        if (sp_event_create(&native,!clear,signaled)) fail(SP_SYSTEM,"native guest event creation failed",context->x[0]);
        GuestEvent *event=register_event(context->x[0],native,system,clear,1);
        if (signaled) event_signal(event,1);
        if (system) context->x[0]=0;
    } else if (!strcmp(name,"_ZN2nn2os11SignalEventEPNS0_9EventTypeE")) event_signal(find_event(context->x[0]),1);
    else if (!strcmp(name,"_ZN2nn2os10ClearEventEPNS0_9EventTypeE") || !strcmp(name,"_ZN2nn2os16ClearSystemEventEPNS0_15SystemEventTypeE"))
        event_signal(find_event(context->x[0]),0);
    else if (!strcmp(name,"_ZN2nn2os12TryWaitEventEPNS0_9EventTypeE") || !strcmp(name,"_ZN2nn2os18TryWaitSystemEventEPNS0_15SystemEventTypeE"))
        context->x[0]=(uint64_t)event_wait(find_event(context->x[0]),0);
    else if (!strcmp(name,"_ZN2nn2os14TimedWaitEventEPNS0_9EventTypeENS_8TimeSpanE") || !strcmp(name,"_ZN2nn2os20TimedWaitSystemEventEPNS0_15SystemEventTypeENS_8TimeSpanE")) {
        int64_t ns=(int64_t)context->x[1];
        context->x[0]=(uint64_t)event_wait(find_event(context->x[0]),ns<=0 ? 0 : (uint64_t)ns/1000+!!(ns%1000));
    } else if (!strcmp(name,"_ZN2nn2os9WaitEventEPNS0_9EventTypeE") || !strcmp(name,"_ZN2nn2os15WaitSystemEventEPNS0_15SystemEventTypeE"))
        event_wait(find_event(context->x[0]),UINT64_MAX);
    else if (!strcmp(name,"_ZN2nn2os13FinalizeEventEPNS0_9EventTypeE") || !strcmp(name,"_ZN2nn2os18DestroySystemEventEPNS0_15SystemEventTypeE")) {
        GuestEvent *event=find_event(context->x[0]);
        if (!event->owned) fail(SP_INVALID,"cannot destroy borrowed application event",context->x[0]);
        if (sp_event_destroy(&event->native)) fail(SP_SYSTEM,"guest event release failed",context->x[0]);
        memset(sp_recomp_access(event->address,event->system ? 0x29 : 0x28,1),0,event->system ? 0x29 : 0x28);
    } else return 0;
    return 1;
}
