#include <errno.h>
static int math_import(Ctx *context,const char *name) {
    static const struct { const char *name; float (*function)(float); } unary_f[]={
#define UNARY_F(function) {#function,function}
        UNARY_F(sinf),UNARY_F(cosf),UNARY_F(tanf),UNARY_F(asinf),UNARY_F(acosf),UNARY_F(atanf),
        UNARY_F(sinhf),UNARY_F(coshf),UNARY_F(tanhf),UNARY_F(sqrtf),UNARY_F(cbrtf),UNARY_F(expf),UNARY_F(exp2f),
        UNARY_F(logf),UNARY_F(log2f),UNARY_F(log10f),UNARY_F(floorf),UNARY_F(ceilf),UNARY_F(roundf),UNARY_F(truncf),UNARY_F(fabsf),UNARY_F(erfcf)
#undef UNARY_F
    };
    static const struct { const char *name; double (*function)(double); } unary_d[]={
#define UNARY_D(function) {#function,function}
        UNARY_D(sin),UNARY_D(cos),UNARY_D(tan),UNARY_D(asin),UNARY_D(acos),UNARY_D(atan),
        UNARY_D(sinh),UNARY_D(cosh),UNARY_D(tanh),UNARY_D(sqrt),UNARY_D(cbrt),UNARY_D(exp),UNARY_D(exp2),
        UNARY_D(log),UNARY_D(log2),UNARY_D(log10),UNARY_D(floor),UNARY_D(ceil),UNARY_D(round),UNARY_D(trunc),UNARY_D(fabs),UNARY_D(erfc)
#undef UNARY_D
    };
    static const struct { const char *name; float (*function)(float,float); } binary_f[]={
#define BINARY_F(function) {#function,function}
        BINARY_F(powf),BINARY_F(atan2f),BINARY_F(fmodf),BINARY_F(remainderf),BINARY_F(hypotf),BINARY_F(copysignf),
        BINARY_F(fminf),BINARY_F(fmaxf),BINARY_F(nextafterf)
#undef BINARY_F
    };
    static const struct { const char *name; double (*function)(double,double); } binary_d[]={
#define BINARY_D(function) {#function,function}
        BINARY_D(pow),BINARY_D(atan2),BINARY_D(fmod),BINARY_D(remainder),BINARY_D(hypot),BINARY_D(copysign),
        BINARY_D(fmin),BINARY_D(fmax),BINARY_D(nextafter)
#undef BINARY_D
    };
    if (!strcmp(name,"frexpf") || !strcmp(name,"frexp")) {
        int exponent=0;
        if (!strcmp(name,"frexpf")) { float value=frexpf(context->v[0].f[0],&exponent); memset(&context->v[0],0,sizeof context->v[0]); context->v[0].f[0]=value; }
        else { double value=frexp(context->v[0].df[0],&exponent); memset(&context->v[0],0,sizeof context->v[0]); context->v[0].df[0]=value; }
        int32_t output=exponent; memcpy(sp_recomp_access(context->x[0],4,1),&output,4); return 1;
    }
    if (!strcmp(name,"modff") || !strcmp(name,"modf")) {
        if (!strcmp(name,"modff")) {
            float integer=0,value=modff(context->v[0].f[0],&integer); memcpy(sp_recomp_access(context->x[0],4,1),&integer,4);
            memset(&context->v[0],0,sizeof context->v[0]); context->v[0].f[0]=value;
        } else {
            double integer=0,value=modf(context->v[0].df[0],&integer); memcpy(sp_recomp_access(context->x[0],8,1),&integer,8);
            memset(&context->v[0],0,sizeof context->v[0]); context->v[0].df[0]=value;
        }
        return 1;
    }
    for (unsigned i=0;i<sizeof unary_f/sizeof unary_f[0];++i) if (!strcmp(name,unary_f[i].name)) {
        errno=0; float value=unary_f[i].function(context->v[0].f[0]);
        memset(&context->v[0],0,sizeof context->v[0]); context->v[0].f[0]=value;
        if (errno) string_errno((uint32_t)errno); return 1;
    }
    for (unsigned i=0;i<sizeof unary_d/sizeof unary_d[0];++i) if (!strcmp(name,unary_d[i].name)) {
        errno=0; double value=unary_d[i].function(context->v[0].df[0]);
        memset(&context->v[0],0,sizeof context->v[0]); context->v[0].df[0]=value;
        if (errno) string_errno((uint32_t)errno); return 1;
    }
    for (unsigned i=0;i<sizeof binary_f/sizeof binary_f[0];++i) if (!strcmp(name,binary_f[i].name)) {
        errno=0; float value=binary_f[i].function(context->v[0].f[0],context->v[1].f[0]);
        memset(&context->v[0],0,sizeof context->v[0]); context->v[0].f[0]=value;
        if (errno) string_errno((uint32_t)errno); return 1;
    }
    for (unsigned i=0;i<sizeof binary_d/sizeof binary_d[0];++i) if (!strcmp(name,binary_d[i].name)) {
        errno=0; double value=binary_d[i].function(context->v[0].df[0],context->v[1].df[0]);
        memset(&context->v[0],0,sizeof context->v[0]); context->v[0].df[0]=value;
        if (errno) string_errno((uint32_t)errno); return 1;
    }
    return 0;
}
