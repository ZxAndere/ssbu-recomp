#ifdef SSBU_PS4
#include <sys/time.h>
#endif
static uint64_t guest_wall_us(void) {
#ifdef SSBU_PS4
    struct timeval now;
    if (gettimeofday(&now,NULL)) fail(SP_SYSTEM,"native realtime clock failed",0);
    return (uint64_t)now.tv_sec*1000000+(uint64_t)now.tv_usec;
#else
    FILETIME now; GetSystemTimeAsFileTime(&now);
    uint64_t ticks=((uint64_t)now.dwHighDateTime<<32)|now.dwLowDateTime;
    return (ticks-UINT64_C(116444736000000000))/10;
#endif
}
static int time_import(Ctx *context,const char *name) {
    if (!strcmp(name,"_ZN2nn4time13IsInitializedEv")) {
        context->x[0]=!!active->time_initialized;
    } else if (!strcmp(name,"_ZN2nn4time10InitializeEv")) {
        (void)guest_wall_us(); (void)sp_now_us(); active->time_initialized=1; context->x[0]=0;
    } else if (!strcmp(name,"_ZN2nn4time8FinalizeEv")) {
        active->time_initialized=0; context->x[0]=0;
    } else if (!strcmp(name,"_ZN2nn4time23StandardUserSystemClock14GetCurrentTimeEPNS0_9PosixTimeE")) {
        if (!active->time_initialized) context->x[0]=0x74;
        else { int64_t seconds=(int64_t)(guest_wall_us()/1000000); memcpy(sp_recomp_access(context->x[0],8,1),&seconds,8); context->x[0]=0; }
    } else if (!strcmp(name,"gettimeofday")) {
        uint64_t now=guest_wall_us();
        int64_t fields[2]={(int64_t)(now/1000000),(int64_t)(now%1000000)};
        if (context->x[0]) memcpy(sp_recomp_access(context->x[0],16,1),fields,16);
        if (context->x[1]) memset(sp_recomp_access(context->x[1],8,1),0,8);
        context->x[0]=0;
    } else if (!strcmp(name,"clock_gettime")) {
        if (context->x[0]>1) { string_errno(22); context->x[0]=UINT32_MAX; return 1; }
        uint64_t now=context->x[0]==0 ? guest_wall_us() : sp_now_us();
        int64_t fields[2]={(int64_t)(now/1000000),(int64_t)(now%1000000)*1000};
        memcpy(sp_recomp_access(context->x[1],16,1),fields,16); context->x[0]=0;
    } else if (!strcmp(name,"time")) {
        int64_t seconds=(int64_t)(guest_wall_us()/1000000);
        if (context->x[0]) memcpy(sp_recomp_access(context->x[0],8,1),&seconds,8);
        context->x[0]=(uint64_t)seconds;
    } else return 0;
    return 1;
}
