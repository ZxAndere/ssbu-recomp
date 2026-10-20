/* Primary guest ThreadType ABI: size independently published by nnsdk-rs;
 * offsets verified against the private SDK's accessor/setup instructions.
 * Guest workers run as real translated C on cooperative native fibers. */
#define PRIMARY_THREAD UINT64_C(0x7500000000)
static uint64_t current_thread_object(void) {
    return active->current_thread ? active->threads[active->current_thread].object : PRIMARY_THREAD;
}
static unsigned thread_index(uint64_t address) {
    if (address==PRIMARY_THREAD) return 0;
    for (unsigned i=1;i<33;++i) if (active->threads[i].object==address) return i;
    fail(SP_INVALID,"unknown guest thread",address); return 0;
}
static void setup_thread_object(uint64_t address,uint64_t stack,uint64_t size,int priority,uint64_t id,unsigned state) {
    unsigned char *object=sp_recomp_access(address,0x1c0,1);
    memset(object,0,0x1c0); uint64_t list=address+0x10,name=address+0x180;
    memcpy(object,&address,8); memcpy(object+8,&address,8); memcpy(object+0x10,&list,8); memcpy(object+0x18,&list,8);
    int16_t value=(int16_t)priority; memcpy(object+0x44,&value,2); object[0x46]=1; object[0x40]=(unsigned char)state;
    memcpy(object+0x48,&stack,8); memcpy(object+0x50,&stack,8); memcpy(object+0x58,&size,8);
    memcpy(object+0x1a0,&name,8); memcpy(object+0x1b8,&id,8);
}
static void initialize_primary_thread(void) {
    unsigned char *object=active->primary_thread;
    uint64_t address=PRIMARY_THREAD,id=1,stack=0x7700000000ULL,size=active->stack.size,name=address+0x180;
    memcpy(object,&address,8); memcpy(object+8,&address,8);
    uint64_t list=address+0x10; memcpy(object+0x10,&list,8); memcpy(object+0x18,&list,8);
    object[0x40]=3; object[0x44]=16; object[0x46]=1;
    memcpy(object+0x48,&stack,8); memcpy(object+0x50,&stack,8); memcpy(object+0x58,&size,8);
    memcpy(object+0x180,"MainThread",11); memcpy(object+0x1a0,&name,8); memcpy(object+0x1b8,&id,8);
    memcpy(sp_recomp_access(0x78000001f8ULL,8,1),&address,8);
    active->threads[0].object=address; active->threads[0].state=TASK_RUNNING;
    active->threads[0].core_mask=1;
}
static void validate_primary_thread(uint64_t address) {
    (void)thread_index(address);
}
static void thread_finish(GuestThread *thread) {
    if (!active->error) *(unsigned char *)sp_recomp_access(thread->object+0x40,1,1)=4;
    thread->armed=0; thread->state=TASK_DONE;
}
static void guest_thread_entry(void *argument) {
    GuestThread *thread=argument; thread->armed=1;
    int reason=setjmp(thread->escape);
    if (!reason) invoke(&thread->context,thread->entry);
    if (reason!=1) {
        for (unsigned pass=0;pass<4;++pass) for (unsigned slot=0;slot<64;++slot)
            if (active->tls_slots[slot] && active->tls_destructors[slot] && thread->tls_values[slot]) {
                uint64_t value=thread->tls_values[slot]; thread->tls_values[slot]=0;
                thread->context.x[0]=value; invoke(&thread->context,active->tls_destructors[slot]);
            }
    }
    thread_finish(thread);
}
static int thread_wait_ready(GuestThread *thread) {
    if ((thread->state==TASK_EVENT || thread->state==TASK_SLEEP) && thread->deadline && sp_now_us()>=thread->deadline) return 1;
    if (thread->state==TASK_JOIN) return active->threads[thread_index(thread->wait_object)].state==TASK_DONE;
    if (thread->state==TASK_EVENT) return *(unsigned char *)sp_recomp_access(thread->wait_object+0x10,1,0)!=0;
    if (thread->state==TASK_GUARD) return *(unsigned char *)sp_recomp_access(thread->wait_object+1,1,0)==0;
    if (thread->state==TASK_MUTEX) { GuestMutex *mutex=find_mutex(thread->wait_object); return mutex && !mutex->depth; }
    return 0;
}
static void guest_thread_cooperate(void) {
    if (!active) return;
    if (active->current_thread) {
        GuestThread *self=&active->threads[active->current_thread];
        if (self->state==TASK_RUNNING) self->state=TASK_READY;
        if (sp_fiber_yield()) fail(SP_SYSTEM,"worker fiber yield failed",self->object);
        return;
    }
    for (unsigned i=1;i<33;++i) if (active->threads[i].state==TASK_EVENT && active->threads[i].wait_object==0x7500000200ULL) {
        application_refresh_focus(); break;
    }
    for (unsigned step=0;step<32;++step) {
        unsigned index=1+(active->next_thread+step)%32; GuestThread *thread=&active->threads[index];
        if ((thread->state==TASK_JOIN || thread->state==TASK_EVENT || thread->state==TASK_MUTEX || thread->state==TASK_GUARD || thread->state==TASK_SLEEP) && thread_wait_ready(thread)) thread->state=TASK_READY;
        if (thread->state!=TASK_READY) continue;
        active->next_thread=index%32; active->current_thread=index; thread->state=TASK_RUNNING;
        int rc=sp_fiber_resume(thread->fiber); active->current_thread=0;
        if (active->error) longjmp(active->escape,1);
        if (rc) fail(rc,"worker fiber resume failed",thread->object);
        return;
    }
}
static int thread_import(Ctx *context,const char *name) {
    if (!strcmp(name,"_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mi") ||
        !strcmp(name,"_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mii")) {
        unsigned index=1; while (index<33 && active->threads[index].object) ++index;
        if (index==33) fail(SP_SYSTEM,"guest worker slots exhausted",context->x[0]);
        for (unsigned i=0;i<33;++i) if (active->threads[i].object==context->x[0]) fail(SP_INVALID,"thread already exists",context->x[0]);
        uint64_t stack=context->x[3],size=context->x[4]; int priority=(int32_t)context->x[5];
        if (!size || (stack&15) || size>SIZE_MAX-stack || priority < -28 || priority>35) fail(SP_INVALID,"invalid guest thread parameters",stack);
        sp_recomp_access(stack,(size_t)size,1);
        GuestThread *thread=&active->threads[index]; thread->object=context->x[0]; thread->entry=context->x[1]; thread->argument=context->x[2];
        thread->stack=stack; thread->stack_size=size; thread->state=TASK_CREATED; thread->id=active->next_thread_id++;
        thread->core=strstr(name,"mii") ? (int32_t)context->x[6] : 0;
        if (thread->core<0) thread->core=0;
        if (thread->core>2) fail(SP_INVALID,"invalid guest logical core",thread->core);
        thread->core_mask=UINT64_C(1)<<thread->core;
        if (sp_memory_map(&thread->tls,64*1024,NULL)) fail(SP_SYSTEM,"worker TLS allocation failed",thread->object);
        thread->context.x[31]=stack+size; thread->context.x[0]=thread->argument;
        thread->context.tpidr=0x7800000000ULL+(uint64_t)index*0x10000;
        memcpy((unsigned char *)thread->tls.data+0x1f8,&thread->object,8);
        setup_thread_object(thread->object,stack,size,priority,thread->id,1);
        sp_log("GAME THREAD: created id=%llu entry=%llx stack=%llx/%llu",(unsigned long long)thread->id,
               (unsigned long long)thread->entry,(unsigned long long)stack,(unsigned long long)size);
        context->x[0]=0;
    } else if (!strcmp(name,"_ZN2nn2os17SetThreadCoreMaskEPNS0_10ThreadTypeEim")) {
        GuestThread *thread=&active->threads[thread_index(context->x[0])];
        int core=(int32_t)context->x[1]; uint64_t mask=context->x[2];
        if (core==-2) { core=0; mask=1; }
        if (core==-3) core=thread->core;
        if (!mask || (mask&~UINT64_C(7)) || core < -1 || core>2) fail(SP_INVALID,"invalid guest thread core mask",mask);
        if (core==-1) core=(int)__builtin_ctzll(mask);
        if (!(mask&(UINT64_C(1)<<core))) fail(SP_INVALID,"ideal core excluded from mask",mask);
        thread->core=core; thread->core_mask=mask;
    } else if (!strcmp(name,"_ZN2nn2os17GetThreadCoreMaskEPiPmPKNS0_10ThreadTypeE")) {
        GuestThread *thread=&active->threads[thread_index(context->x[2])]; int32_t core=thread->core;
        if (context->x[0]) memcpy(sp_recomp_access(context->x[0],4,1),&core,4);
        if (context->x[1]) memcpy(sp_recomp_access(context->x[1],8,1),&thread->core_mask,8);
    } else if (!strcmp(name,"_ZN2nn2os11StartThreadEPNS0_10ThreadTypeE")) {
        unsigned index=thread_index(context->x[0]); GuestThread *thread=&active->threads[index];
        if (!index || thread->state!=TASK_CREATED) fail(SP_INVALID,"thread cannot start twice",context->x[0]);
        if (sp_fiber_create(&thread->fiber,16*1024*1024,guest_thread_entry,thread)) fail(SP_SYSTEM,"worker fiber creation failed",thread->object);
        thread->state=TASK_READY; *(unsigned char *)sp_recomp_access(thread->object+0x40,1,1)=3;
        if (!active->scheduler_started) {
            active->scheduler_started=1; active->remaining_budget=sp_recomp_budget;
            active->quantum=active->remaining_budget<10000 ? active->remaining_budget : 10000; sp_recomp_budget=active->quantum;
        }
        guest_thread_cooperate();
    } else if (!strcmp(name,"_ZN2nn2os11YieldThreadEv")) {
        guest_thread_cooperate();
    } else if (!strcmp(name,"_ZN2nn2os11SleepThreadENS_8TimeSpanE")) {
        int64_t ns=(int64_t)context->x[0];
        if (ns<=0) guest_thread_cooperate();
        else {
            uint64_t deadline=sp_now_us()+(uint64_t)ns/1000+!!(ns%1000);
            while (sp_now_us()<deadline) {
                if (active->current_thread) {
                    GuestThread *self=&active->threads[active->current_thread]; self->state=TASK_SLEEP; self->deadline=deadline;
                }
                if (!sp_recomp_budget--) sp_recomp_checkpoint(context->x[30]);
                guest_thread_cooperate(); if (!active->current_thread) sp_sleep_ms(1);
            }
        }
    } else if (!strcmp(name,"_ZN2nn2os10ExitThreadEv")) {
        GuestThread *self=&active->threads[active->current_thread];
        if (!active->current_thread || !self->armed) fail(SP_UNSUPPORTED,"primary thread exit requested",context->x[30]);
        longjmp(self->escape,2);
    } else if (!strcmp(name,"_ZN2nn2os10WaitThreadEPNS0_10ThreadTypeE") || !strcmp(name,"_ZN2nn2os13TryWaitThreadEPNS0_10ThreadTypeE")) {
        unsigned index=thread_index(context->x[0]);
        if (index==active->current_thread || active->threads[index].state==TASK_CREATED) fail(SP_INVALID,"invalid thread join",context->x[0]);
        if (strstr(name,"TryWait")) context->x[0]=active->threads[index].state==TASK_DONE;
        else while (active->threads[index].state!=TASK_DONE) {
            if (active->current_thread) { GuestThread *self=&active->threads[active->current_thread]; self->state=TASK_JOIN; self->wait_object=context->x[0]; }
            guest_thread_cooperate();
        }
    } else if (!strcmp(name,"_ZN2nn2os13DestroyThreadEPNS0_10ThreadTypeE")) {
        unsigned index=thread_index(context->x[0]); GuestThread *thread=&active->threads[index];
        if (!index || (thread->state!=TASK_CREATED && thread->state!=TASK_DONE)) fail(SP_INVALID,"destroying live guest thread",context->x[0]);
        if (thread->fiber && sp_fiber_destroy(&thread->fiber)) fail(SP_SYSTEM,"worker fiber release failed",context->x[0]);
        if (thread->tls.data && sp_memory_unmap(&thread->tls)) fail(SP_SYSTEM,"worker TLS release failed",context->x[0]);
        *(unsigned char *)sp_recomp_access(thread->object+0x40,1,1)=0; memset(thread,0,sizeof *thread);
    } else if (!strcmp(name,"_ZN2nn2os16GetCurrentThreadEv")) {
        context->x[0]=current_thread_object();
    } else if (!strcmp(name,"_ZN2nn2os25GetCurrentProcessorNumberEv")) {
        context->x[0]=(uint64_t)active->threads[active->current_thread].core;
    } else if (!strcmp(name,"_ZN2nn2os11GetThreadIdEPKNS0_10ThreadTypeE")) {
        validate_primary_thread(context->x[0]); memcpy(&context->x[0],sp_recomp_access(context->x[0]+0x1b8,8,0),8);
    } else if (!strcmp(name,"_ZN2nn2os17GetThreadPriorityEPKNS0_10ThreadTypeE")) {
        validate_primary_thread(context->x[0]); int16_t priority; memcpy(&priority,sp_recomp_access(context->x[0]+0x44,2,0),2);
        context->x[0]=(uint32_t)(int32_t)priority;
    } else if (!strcmp(name,"_ZN2nn2os13SetThreadNameEPNS0_10ThreadTypeEPKc")) {
        validate_primary_thread(context->x[0]);
        size_t length=context->x[1] ? string_length(context->x[1],31) : 0;
        unsigned char *object=sp_recomp_access(context->x[0],0x1c0,1);
        memset(object+0x180,0,32);
        if (length) memcpy(object+0x180,sp_recomp_access(context->x[1],length,0),length);
        uint64_t pointer=context->x[0]+0x180; memcpy(object+0x1a0,&pointer,8);
    } else if (!strcmp(name,"_ZN2nn2os20SetThreadNamePointerEPNS0_10ThreadTypeEPKc")) {
        validate_primary_thread(context->x[0]);
        if (context->x[1]) string_length(context->x[1],SIZE_MAX);
        uint64_t pointer=context->x[1] ? context->x[1] : context->x[0]+0x180;
        memcpy(sp_recomp_access(context->x[0]+0x1a0,8,1),&pointer,8);
    } else if (!strcmp(name,"_ZN2nn2os18GetThreadStackInfoEPmS1_PKNS0_10ThreadTypeE")) {
        validate_primary_thread(context->x[2]); uint64_t address,size;
        memcpy(&address,sp_recomp_access(context->x[2]+0x50,8,0),8); memcpy(&size,sp_recomp_access(context->x[2]+0x58,8,0),8);
        if (context->x[0]) memcpy(sp_recomp_access(context->x[0],8,1),&address,8);
        if (context->x[1]) memcpy(sp_recomp_access(context->x[1],8,1),&size,8);
    } else return 0;
    return 1;
}
