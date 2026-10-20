#include "guest.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    SpGuestImage *image; const SpGuestProgram *program;
    SpMemory stack; jmp_buf escape; int error;
} Execution;
static Execution *active;
uint64_t sp_recomp_budget;
static char last_error[256];
const char *sp_guest_last_error(void) { return last_error; }
static void fail(int error,const char *what,uint64_t address) {
    snprintf(last_error,sizeof last_error,"%s at 0x%llx",what,(unsigned long long)address);
    sp_log("GUEST STOP: %s",last_error);
    if (active) { active->error=error; longjmp(active->escape,1); }
    abort();
}
void sp_recomp_checkpoint(uint64_t pc) { fail(SP_TIMEOUT,"bootstrap instruction budget exhausted",pc); }
void *sp_recomp_access(uint64_t address,size_t size,int write) {
    if (!active) fail(SP_INVALID,"no active guest context",address);
    static unsigned char empty;
    if (!size) return &empty;
    void *pointer=sp_guest_pointer(active->image,address,size,write);
    const uint64_t stack_base=0x7700000000ULL;
    if (!pointer && address>=stack_base && address-stack_base<=active->stack.size &&
        size<=active->stack.size-(address-stack_base)) pointer=(char *)active->stack.data+(address-stack_base);
    if (!pointer) fail(SP_ADDRESS,write ? "invalid guest write" : "invalid guest read",address);
    return pointer;
}
void guest_trap(const char *what,uint64_t address) { fail(SP_UNSUPPORTED,what,address); }
void guest_unimpl(uint64_t pc,uint32_t instruction,const char *what) {
    (void)instruction; fail(SP_UNSUPPORTED,what,pc);
}
void guest_svc(Ctx *context,uint32_t number) { (void)context; fail(SP_UNSUPPORTED,"unimplemented guest SVC",number); }
void stx_diag(Ctx *context,uint64_t address,uint64_t expected,uint64_t seen,uint64_t value,int bits) {
    (void)context; (void)address; (void)expected; (void)seen; (void)value; (void)bits;
}
static void invoke(Ctx *context,uint64_t address) {
    if (address>=0x7300000000ULL && !( (address-0x7300000000ULL) % 256 ) &&
        (address-0x7300000000ULL)/256<active->program->symbol_count) {
        host_import(context,(int)((address-0x7300000000ULL)/256)); return;
    }
    size_t low=0,high=active->program->function_count;
    while (low<high) {
        size_t middle=low+(high-low)/2;
        if (active->program->functions[middle].addr<address) low=middle+1; else high=middle;
    }
    if (low==active->program->function_count || active->program->functions[low].addr!=address)
        fail(SP_UNSUPPORTED,"function not statically recompiled",address);
    active->program->functions[low].fn(context);
}
void call_indirect(Ctx *context,uint64_t address) { if (!active) fail(SP_INVALID,"no guest",address); invoke(context,address); }
void host_import(Ctx *context,int index) {
    if (!active || index<0 || (size_t)index>=active->program->symbol_count) fail(SP_INVALID,"invalid import index",(uint64_t)index);
    const char *name=active->program->symbols[index].name;
    if (!name) fail(SP_INVALID,"null import name",(uint64_t)index);
    size_t size=(size_t)context->x[2];
    if (!strcmp(name,"memcpy") || !strcmp(name,"memmove")) {
        void *destination=sp_recomp_access(context->x[0],size,1);
        void *source=sp_recomp_access(context->x[1],size,0);
        memmove(destination,source,size);
    } else if (!strcmp(name,"memset")) {
        memset(sp_recomp_access(context->x[0],size,1),(int)context->x[1],size);
    } else if (!strcmp(name,"memcmp")) {
        context->x[0]=(uint64_t)(int64_t)memcmp(sp_recomp_access(context->x[0],size,0),sp_recomp_access(context->x[1],size,0),size);
    } else if (!strcmp(name,"strlen")) {
        size_t length=0;
        while (*(unsigned char *)sp_recomp_access(context->x[0]+length,1,0)) {
            if (++length>1024*1024) fail(SP_INVALID,"unterminated string",context->x[0]);
        }
        context->x[0]=length;
    } else fail(SP_UNSUPPORTED,name,context->x[30]);
}
int sp_guest_run(SpGuestImage *image,const SpGuestProgram *program,uint64_t entry,Ctx *context) {
    if (!image || !program || !context || active || !program->functions || !program->function_count ||
        program->image_fingerprint!=sp_guest_fingerprint(image)) return SP_INVALID;
    for (size_t i=0;i<program->function_count;++i)
        if (!program->functions[i].fn || (i && program->functions[i-1].addr>=program->functions[i].addr)) return SP_INVALID;
    Execution *execution=calloc(1,sizeof *execution);
    if (!execution) return SP_SYSTEM;
    execution->image=image; execution->program=program;
    if (sp_memory_map(&execution->stack,256*1024,NULL)) { free(execution); return SP_SYSTEM; }
    if (!context->x[31]) context->x[31]=0x7700000000ULL+execution->stack.size;
    last_error[0]=0; active=execution; sp_recomp_budget=1000000;
    if (!setjmp(execution->escape)) invoke(context,entry);
    active=NULL; int rc=execution->error;
    sp_memory_unmap(&execution->stack); free(execution);
    return rc;
}
extern const SpGuestProgram sp_smoke_program;
#ifdef SP_GAME_LINKED
static int bootstrap_resolve(const char *name,uint64_t *address,void *argument) {
    const SpGuestProgram *program=argument;
    if (strcmp(name,"memcpy") && strcmp(name,"memmove") && strcmp(name,"memset") &&
        strcmp(name,"memcmp") && strcmp(name,"strlen")) {
        sp_log("GAME IMPORT MISSING: %s",name); return SP_UNSUPPORTED;
    }
    for (size_t i=0;i<program->symbol_count;++i) if (!strcmp(name,program->symbols[i].name)) {
        *address=0x7300000000ULL+i*256; return SP_OK;
    }
    return SP_UNSUPPORTED;
}
#endif
int sp_recomp_test(const char *path) {
    unsigned checks=0,failures=0;
#define VERIFY(expr) do { ++checks; if (!(expr)) { ++failures; sp_log("RECOMP FAIL %d: %s",__LINE__,#expr); } } while (0)
    SpGuestImage *image=NULL;
    VERIFY(sp_guest_load(&image,path)==SP_OK);
    if (image) {
        size_t missing=99;
        VERIFY(sp_guest_relocate(image,NULL,NULL,&missing)==SP_OK && !missing);
        uint64_t *value=sp_guest_pointer(image,0x7100002000ULL,24,1);
        VERIFY(value && value[0]==7 && value[2]==0x7100000020ULL);
        VERIFY(!sp_guest_pointer(image,0x7100000020ULL,8,1));
        VERIFY(!sp_guest_pointer(image,UINT64_MAX,16,0));
        VERIFY(!sp_guest_pointer(image,0x710000005aULL,8,0));
        if (value) {
            for (unsigned i=0;i<100;++i) {
                value[0]=(uint64_t)i*7919; value[1]=0;
                Ctx context={0}; context.x[0]=0x7100002000ULL;
                VERIFY(sp_guest_run(image,&sp_smoke_program,0x7100000020ULL,&context)==SP_OK);
                VERIFY(context.x[0]==value[0]+10 && value[1]==value[0]+5);
            }
            Ctx context={0};
            VERIFY(sp_guest_run(image,&sp_smoke_program,0x7100000044ULL,&context)==SP_OK && context.x[0]==1234);
            VERIFY(sp_guest_run(image,&sp_smoke_program,0x710000004cULL,&context)==SP_UNSUPPORTED);
            context.x[0]=0xdeadbeef;
            VERIFY(sp_guest_run(image,&sp_smoke_program,0x7100000054ULL,&context)==SP_ADDRESS);
            VERIFY(sp_guest_run(image,&sp_smoke_program,0x7100000080ULL,&context)==SP_UNSUPPORTED);
            VERIFY(sp_guest_run(image,&sp_smoke_program,0x710000005cULL,&context)==SP_TIMEOUT);
            SpGuestProgram wrong=sp_smoke_program; wrong.image_fingerprint^=1;
            VERIFY(sp_guest_run(image,&wrong,0x7100000020ULL,&context)==SP_INVALID);
        }
        VERIFY(sp_guest_close(&image)==SP_OK);
    }
    sp_log("RECOMP RESULT: %u checks, %u failures",checks,failures);
    return failures ? 1 : 0;
#undef VERIFY
}
int sp_game_bootstrap(const char *path) {
    SpGuestImage *image=NULL;
    int rc=sp_guest_load(&image,path);
    if (rc) { sp_log("GAME BOOT: main.elf unavailable or invalid: %d",rc); return rc; }
    sp_log("GAME BOOT: AArch64 image mapped with translated addresses, %llu bytes, fingerprint %016llx",
           (unsigned long long)sp_guest_size(image),(unsigned long long)sp_guest_fingerprint(image));
#ifdef SP_GAME_LINKED
    extern const SpGuestProgram *sp_linked_game_program(void);
    extern uint64_t sp_linked_game_entry(void);
    Ctx context={0};
    const SpGuestProgram *program=sp_linked_game_program();
    size_t missing=0;
    rc=sp_guest_relocate(image,bootstrap_resolve,(void *)program,&missing);
    uint64_t entry=sp_linked_game_entry();
    if (!rc) rc=sp_guest_run(image,program,entry ? entry : sp_guest_entry(image),&context);
    sp_log("GAME BOOT: execution status=%d unresolved=%llu x0=%llu stop=%s",rc,(unsigned long long)missing,
           (unsigned long long)context.x[0],sp_guest_last_error());
#else
    rc=SP_UNSUPPORTED;
    sp_log("GAME BOOT: native game function table not linked; refusing ARM64 execution");
#endif
    sp_guest_close(&image); return rc;
}
int sp_guest_inspect(const char *path) {
    SpGuestImage *image=NULL;
    int rc=sp_guest_load(&image,path);
    size_t missing=0;
    if (!rc) rc=sp_guest_relocate(image,NULL,NULL,&missing);
    if (image) sp_guest_close(&image);
    printf("GUEST INSPECT: %d unresolved=%llu\n",rc,(unsigned long long)missing);
    return rc ? 1 : 0;
}
int sp_guest_batch(const char *path) {
    SpGuestImage *image=NULL;
    if (sp_guest_load(&image,path)) return 2;
    uint64_t *value=sp_guest_pointer(image,0x7100002000ULL,16,1);
    if (!value) { sp_guest_close(&image); return 2; }
    char line[80]; int result=0;
    while (fgets(line,sizeof line,stdin)) {
        char *end; uint64_t input=strtoull(line,&end,0);
        if (end==line) { result=2; break; }
        value[0]=input; value[1]=0;
        Ctx context={0}; context.x[0]=0x7100002000ULL;
        if (sp_guest_run(image,&sp_smoke_program,0x7100000020ULL,&context)) { result=2; break; }
        printf("%llu,%llu,%llu\n",(unsigned long long)input,(unsigned long long)context.x[0],(unsigned long long)value[1]);
    }
    sp_guest_close(&image); return result;
}
