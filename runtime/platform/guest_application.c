/* Native application focus -> Horizon focus states/notification numbers.
 * Mode semantics: libnx applet.h; SystemService status prefix: shadPS4's
 * public systemservice.h. The PS4 adapter remains hardware-unverified. */
static const char *language_codes[]={"ja","en-US","fr","de","it","es","zh-CN","ko","nl","pt","ru","zh-TW","en-GB","fr-CA","es-419","zh-Hans","zh-Hant","pt-BR"};
static uint64_t language_code(unsigned language) {
    if (language>=sizeof language_codes/sizeof *language_codes) fail(SP_INVALID,"invalid language enum",language);
    uint64_t value=0; memcpy(&value,language_codes[language],strlen(language_codes[language])); return value;
}
static const char *application_language(void) {
    const char **codes=language_codes;
    const char *configured=getenv("SSBU_LANGUAGE");
    if (configured && *configured) {
        for (unsigned i=0;i<sizeof language_codes/sizeof *language_codes;++i) if (!strcmp(configured,codes[i])) return codes[i];
        fail(SP_INVALID,"unsupported configured game language",0);
    }
#ifdef SSBU_PS4
    if (!active->application_module && sp_module_open(&active->application_module,"/system/common/lib/libSceSystemService.sprx"))
        fail(SP_SYSTEM,"cannot open SystemService for language",0);
    void *symbol=NULL; int (*query)(int,int *); int language=-1;
    if (sp_module_symbol(active->application_module,"sceSystemServiceParamGetInt",&symbol)) fail(SP_UNSUPPORTED,"native language export absent",0);
    memcpy(&query,&symbol,sizeof query);
    if (query(1,&language)) fail(SP_SYSTEM,"native system language query failed",0);
    static const unsigned map[]={0,1,2,5,3,4,8,9,10,7,11,6,1,1,1,1,1,17,12,1,14,1,13};
    return codes[language>=0 && (unsigned)language<sizeof map/sizeof *map ? map[language] : 1];
#else
    LANGID language=GetUserDefaultUILanguage();
    switch (PRIMARYLANGID(language)) {
        case LANG_JAPANESE:return codes[0];
        case LANG_ENGLISH:return codes[SUBLANGID(language)==SUBLANG_ENGLISH_UK ? 12 : 1];
        case LANG_FRENCH:return codes[SUBLANGID(language)==SUBLANG_FRENCH_CANADIAN ? 13 : 2];
        case LANG_GERMAN:return codes[3]; case LANG_ITALIAN:return codes[4];
        case LANG_SPANISH:return codes[SUBLANGID(language)==SUBLANG_SPANISH || SUBLANGID(language)==SUBLANG_SPANISH_MODERN ? 5 : 14];
        case LANG_CHINESE:return codes[SUBLANGID(language)==SUBLANG_CHINESE_TRADITIONAL || SUBLANGID(language)==SUBLANG_CHINESE_HONGKONG ? 16 : 15];
        case LANG_KOREAN:return codes[7]; case LANG_DUTCH:return codes[8];
        case LANG_PORTUGUESE:return codes[SUBLANGID(language)==SUBLANG_PORTUGUESE_BRAZILIAN ? 17 : 9];
        case LANG_RUSSIAN:return codes[10]; default:return codes[1];
    }
#endif
}
static unsigned application_native_focus(void) {
#ifdef SSBU_PS4
    if (!active->application_module && sp_module_open(&active->application_module,"/system/common/lib/libSceSystemService.sprx"))
        fail(SP_SYSTEM,"cannot open SystemService for focus",0);
    if (!active->application_status.data && sp_memory_map(&active->application_status,sp_page_size(),NULL))
        fail(SP_SYSTEM,"cannot allocate native focus status",0);
    void *symbol=NULL;
    if (sp_module_symbol(active->application_module,"sceSystemServiceGetStatus",&symbol))
        fail(SP_UNSUPPORTED,"native focus status export absent",0);
    int (*query)(void *); memcpy(&query,&symbol,sizeof query);
    memset(active->application_status.data,0,active->application_status.size);
    int rc=query(active->application_status.data);
    const unsigned char *status=active->application_status.data;
    if (rc || status[4]>1 || status[5]>1) fail(SP_SYSTEM,"invalid native focus status",(uint32_t)rc);
    return status[5] ? 3 : status[4] ? 2 : 1;
#else
    HWND foreground=GetForegroundWindow(); DWORD process=0;
    if (foreground) GetWindowThreadProcessId(foreground,&process);
    return foreground && process==GetCurrentProcessId() ? 1 : 3;
#endif
}
static void application_notify(unsigned message) {
    if (active->notification_count==32) fail(SP_SYSTEM,"application notification queue full",message);
    active->notifications[(active->notification_head+active->notification_count++)%32]=message;
    if (sp_event_set(active->application_event)) fail(SP_SYSTEM,"application event signal failed",message);
    active->application_event_object[0x10]=1;
}
static int application_suspends(unsigned mode,unsigned focus) {
    return mode==3 ? focus!=1 : mode!=1 && focus==3;
}
static void application_refresh_focus(void) {
    unsigned focus=application_native_focus();
    if (active->focus_known && focus!=active->focus_state) {
        application_notify(15);
        if (active->focus_state==3 && focus==1) application_notify(16);
    }
    active->focus_state=focus; active->focus_known=1;
    if (active->notification_count) { sp_event_set(active->application_event); active->application_event_object[0x10]=1; }
}
static int application_import(Ctx *context,const char *name) {
    if (!strcmp(name,"_ZN2nn2oe10InitializeEv")) {
        if (!active->application_event) {
            if (sp_event_create(&active->application_event,1,0)) fail(SP_SYSTEM,"application event creation failed",0);
            active->application_started=sp_now_us();
            register_event(0x7500000200ULL,active->application_event,1,0,0);
            sp_log("GAME APPLICATION: lifecycle initialized");
        }
    } else if (!strcmp(name,"_ZN2nn2oe8FinalizeEv")) {
        if (!active->application_event) fail(SP_INVALID,"application not initialized",0);
        find_event(0x7500000200ULL)->native=NULL;
        if (sp_event_destroy(&active->application_event)) fail(SP_SYSTEM,"application event release failed",0);
        active->application_started=0;
        active->notification_head=active->notification_count=active->focus_known=0;
    } else if (!strcmp(name,"_ZN2nn2oe20SetFocusHandlingModeENS0_17FocusHandlingModeE") ||
               !strcmp(name,"_ZN2nn2oe20SetFocusHandlingModeEi")) {
        if (!active->application_event || context->x[0]>3) fail(SP_INVALID,"invalid focus handling mode",context->x[0]);
        active->focus_mode=(unsigned)context->x[0];
        sp_log("GAME APPLICATION: focus mode=%u background-suspends=%d",active->focus_mode,application_suspends(active->focus_mode,3));
    } else if (!strcmp(name,"_ZN2nn2oe20GetCurrentFocusStateEv")) {
        if (!active->application_event) fail(SP_INVALID,"application not initialized",0);
        application_refresh_focus(); context->x[0]=active->focus_state;
    } else if (!strcmp(name,"_ZN2nn2oe27GetNotificationMessageEventEv")) {
        if (!active->application_event) fail(SP_INVALID,"application not initialized",0);
        context->x[0]=0x7500000200ULL;
    } else if (!strcmp(name,"_ZN2nn2oe25TryPopNotificationMessageEPj")) {
        if (!active->application_event) fail(SP_INVALID,"application not initialized",0);
        application_refresh_focus();
        if (!active->notification_count) context->x[0]=0;
        else {
            uint32_t message=active->notifications[active->notification_head];
            memcpy(sp_recomp_access(context->x[0],4,1),&message,4);
            active->notification_head=(active->notification_head+1)%32; --active->notification_count;
            if (!active->notification_count && sp_event_reset(active->application_event)) fail(SP_SYSTEM,"application event reset failed",0);
            if (!active->notification_count) active->application_event_object[0x10]=0;
            context->x[0]=1;
        }
    } else if (!strcmp(name,"_ZN2nn2oe16DisableRecordingEv")) {
        if (!active->application_event) fail(SP_INVALID,"application not initialized",0);
        /* This runtime has no Nintendo gameplay recorder. Disabling that
         * optional recorder is supported; enabling it remains unsupported.
         * This does not claim to control the PS4 shell's SHARE recorder. */
        active->recording_disabled=1;
        sp_log("GAME APPLICATION: internal gameplay recording disabled (no recorder backend)");
    } else if (!strcmp(name,"_ZN2nn2oe18GetDesiredLanguageEv")) {
        if (!active->application_event) fail(SP_INVALID,"application not initialized",0);
        const char *language=application_language(); context->x[0]=0;
        memcpy(&context->x[0],language,strlen(language));
        sp_log("GAME APPLICATION: language=%s",language);
    } else if (!strcmp(name,"_ZN2nn8settings12LanguageCode4MakeENS0_8LanguageE")) {
        context->x[0]=language_code((unsigned)context->x[0]);
    } else if (!strcmp(name,"_ZN2nn8settingseqERKNS0_12LanguageCodeERKNS0_8LanguageE") ||
               !strcmp(name,"_ZN2nn8settingsneERKNS0_12LanguageCodeERKNS0_8LanguageE")) {
        uint64_t code=0; uint32_t language=0;
        memcpy(&code,sp_recomp_access(context->x[0],8,0),8); memcpy(&language,sp_recomp_access(context->x[1],4,0),4);
        int equal=code==language_code(language); context->x[0]=strstr(name,"settingsne") ? !equal : equal;
    } else if (!strcmp(name,"_ZN2nn2oe16GetOperationModeEv")) {
        if (!active->application_event) fail(SP_INVALID,"application not initialized",0);
        context->x[0]=1; /* TV/desktop mode, with no portable LCD on this port. */
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
