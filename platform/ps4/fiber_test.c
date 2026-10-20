#include "platform.h"
typedef struct { int step,error; } Work;
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
    VERIFY(sp_fiber_thread_shutdown()==SP_OK);
    sp_log("FIBER RESULT: %u checks, %u failures",checks,failures);
    return failures ? 1 : 0;
#undef VERIFY
}
