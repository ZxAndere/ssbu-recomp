#include "platform.h"
typedef struct { int step,error; } Work;
typedef struct { SpFiber *parent,*child; int step,error; } Nested;
static void nested_child(void *argument) {
    Nested *work=argument;
    if (sp_fiber_resume(work->parent)!=SP_INVALID) work->error=1;
    ++work->step;
    if (sp_fiber_yield()) work->error=1;
    ++work->step;
}
static void nested_parent(void *argument) {
    Nested *work=argument;
    if (sp_fiber_create(&work->child,128*1024,nested_child,work)) { work->error=1; return; }
    if (sp_fiber_resume(work->child) || sp_fiber_resume(work->child) || sp_fiber_destroy(&work->child)) work->error=1;
}
static void fiber_work(void *argument) {
    Work *work=argument;
    if (sp_fiber_get_local(0) || sp_fiber_set_local(0,work)) work->error=1;
    for (int i=0;i<2;++i) {
        ++work->step;
        if (sp_fiber_yield() || sp_fiber_get_local(0)!=work) work->error=1;
    }
    ++work->step;
}
int sp_fiber_test(void) {
    unsigned checks=0,failures=0;
#define VERIFY(expr) do { ++checks; if (!(expr)) { ++failures; sp_log("FIBER FAIL %d: %s",__LINE__,#expr); } } while (0)
    SpFiber *fiber=NULL; Work work={0};
    VERIFY(sp_fiber_create(&fiber,1,fiber_work,&work)==SP_INVALID);
    VERIFY(sp_fiber_set_local(0,&checks)==SP_OK);
    VERIFY(sp_fiber_create(&fiber,128*1024,fiber_work,&work)==SP_OK);
    if (fiber) {
        VERIFY(sp_fiber_thread_shutdown()==SP_INVALID);
        for (int i=1;i<=3;++i) {
            VERIFY(sp_fiber_resume(fiber)==SP_OK);
            VERIFY(work.step==i && !work.error && sp_fiber_get_local(0)==&checks);
            VERIFY(sp_fiber_finished(fiber)==(i==3));
        }
        VERIFY(sp_fiber_resume(fiber)==SP_INVALID);
        VERIFY(sp_fiber_destroy(&fiber)==SP_OK && !fiber);
    }
    VERIFY(sp_fiber_yield()==SP_INVALID);
    Nested nested={0};
    VERIFY(sp_fiber_create(&nested.parent,128*1024,nested_parent,&nested)==SP_OK);
    if (nested.parent) {
        VERIFY(sp_fiber_resume(nested.parent)==SP_OK);
        VERIFY(nested.step==2 && !nested.error && !nested.child);
        VERIFY(sp_fiber_destroy(&nested.parent)==SP_OK);
    }
    VERIFY(sp_fiber_thread_shutdown()==SP_OK);
    sp_log("FIBER RESULT: %u checks, %u failures",checks,failures);
    return failures ? 1 : 0;
#undef VERIFY
}
