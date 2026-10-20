/* Offline bridge to the existing portable Maxwell translator. Inputs and
 * output are supplied explicitly by the private probe driver. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mx_translate.h"
static unsigned char *read_part(const char *base,const char *suffix,size_t *bytes) {
    char path[2048]; if (snprintf(path,sizeof path,"%s.%s",base,suffix)>=(int)sizeof path) return NULL;
    FILE *file=fopen(path,"rb"); if (!file) return NULL;
    if (fseek(file,0,SEEK_END)) { fclose(file); return NULL; }
    long size=ftell(file); if (size<0 || size>4*1024*1024 || fseek(file,0,SEEK_SET)) { fclose(file); return NULL; }
    unsigned char *data=malloc((size_t)size+1); if (!data) { fclose(file); return NULL; }
    if (fread(data,1,(size_t)size,file)!=(size_t)size) { free(data); fclose(file); return NULL; }
    fclose(file); *bytes=(size_t)size; return data;
}
static unsigned word(const void *data,size_t offset) { unsigned value; memcpy(&value,(const unsigned char *)data+offset,4); return value; }
int main(int argc,char **argv) {
    if (argc!=3) return 2;
    size_t control_bytes=0,code_bytes=0,constant_bytes=0;
    unsigned char *control=read_part(argv[1],"control",&control_bytes),*code=read_part(argv[1],"maxwell",&code_bytes);
    unsigned char *constants=read_part(argv[1],"constants",&constant_bytes); int status=1;
    if (!control || control_bytes<0x760 || !code || code_bytes!=word(control,0x6f8) || constant_bytes!=word(control,0x6fc)) goto done;
    MxIn input={0}; MxOut output={0};
    input.stage=MX_COMPUTE;
    if (word(control,0x714)!=5) {
        if (code_bytes<0x50) goto done;
        static const int stages[]={-1,MX_VERTEX,MX_TESS_CONTROL,MX_TESS_EVAL,MX_GEOMETRY,MX_FRAGMENT};
        unsigned hardware=(word(code,0)>>10)&15;
        if (hardware>=sizeof stages/sizeof stages[0] || stages[hardware]<0) goto done;
        input.stage=stages[hardware];
    }
    input.code=code; input.code_len=(int)code_bytes; input.cdata=constants; input.cdata_size=(unsigned)constant_bytes;
    input.binding_base=(unsigned)input.stage*32; input.ssbo_base=input.binding_base+16;
    input.tex_base=(unsigned)input.stage*64; input.image_base=(unsigned)input.stage*8; input.target=1;
    if (input.stage==MX_COMPUTE) {
        for (unsigned i=0;i<3;++i) input.local_size[i]=word(control,0x748+i*4);
        input.smem_bytes=word(control,0x754); input.lmem_bytes=word(control,0x758);
    }
    if (!mx_translate(&input,&output)) { fprintf(stderr,"MAXWELL PROBE: failed stage=%d opcode=%s reason=%s\n",input.stage,output.op,output.why); goto done; }
    FILE *file=fopen(argv[2],"wb");
    if (file) {
        size_t size=strlen(output.glsl); int good=fwrite(output.glsl,1,size,file)==size;
        if (fclose(file)) good=0;
        if (good) { printf("MAXWELL PROBE: stage=%d instructions=%d blocks=%d glsl_bytes=%zu\n",input.stage,output.insns,output.blocks,size); status=0; }
    }
    free(output.glsl);
done:
    free(control); free(code); free(constants); return status;
}
