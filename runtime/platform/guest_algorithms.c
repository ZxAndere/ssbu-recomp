static int guest_unsigned_compare(const void *a,const void *b) {
    uint32_t x,y; memcpy(&x,a,4); memcpy(&y,b,4); return x<y ? -1 : x>y;
}
static int algorithm_import(Ctx *c,const char *name) {
    /* This concrete libc++ instantiation has uint32_t elements and the
     * stateless unsigned less comparator, not a guest callback. Equal values
     * are identical, so qsort preserves all observable sort semantics. */
    if (strcmp(name,"_ZNSt3__16__sortIRNS_6__lessIjjEEPjEEvT0_S5_T_")) return 0;
    uint64_t first=c->x[0],last=c->x[1];
    if ((first&3) || last<first || (last-first)%4) fail(SP_INVALID,"invalid unsigned sort range",first);
    size_t bytes=(size_t)(last-first);
    if (bytes) qsort(sp_recomp_access(first,bytes,1),bytes/4,4,guest_unsigned_compare);
    return 1;
}
