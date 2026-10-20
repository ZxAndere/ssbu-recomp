/* Original minimal SPIR-V fixtures. No game shaders or precompiled GCN. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <psbc_compile.h>
#include <pssl_types.h>

static const uint32_t vertex[]={
    0x07230203,0x00010000,0,12,0,
    0x00020011,1, 0x0003000e,0,1,
    0x0006000f,0,1,0x6e69616d,0,2,
    0x00040047,2,11,0,
    0x00020013,3, 0x00030021,4,3, 0x00030016,5,32,
    0x00040017,6,5,4, 0x00040020,7,3,6, 0x0004003b,7,2,3,
    0x0004002b,5,8,0, 0x0004002b,5,9,0x3f800000,
    0x0007002c,6,10,8,8,8,9,
    0x00050036,3,1,0,4, 0x000200f8,11,
    0x0003003e,2,10, 0x000100fd,0x00010038
};
static const uint32_t fragment[]={
    0x07230203,0x00010000,0,12,0,
    0x00020011,1, 0x0003000e,0,1,
    0x0006000f,4,1,0x6e69616d,0,2, 0x00030010,1,7,
    0x00040047,2,30,0,
    0x00020013,3, 0x00030021,4,3, 0x00030016,5,32,
    0x00040017,6,5,4, 0x00040020,7,3,6, 0x0004003b,7,2,3,
    0x0004002b,5,8,0, 0x0004002b,5,9,0x3f800000,
    0x0007002c,6,10,9,8,8,9,
    0x00050036,3,1,0,4, 0x000200f8,11,
    0x0003003e,2,10, 0x000100fd,0x00010038
};
/* Dispatch exactly one 1x1x1 group: no competing non-atomic SSBO writers.
 * layout(local_size_x=1) in; layout(set=0,binding=0,std430) buffer
 * Output { uint value; } output_data; void main(){ output_data.value=42; } */
static const uint32_t compute[]={
    0x07230203,0x00010000,0,13,0,
    0x00020011,1, 0x0003000e,0,1,
    0x0005000f,5,10,0x6e69616d,0,
    0x00060010,10,17,1,1,1,
    0x00030047,4,3, 0x00050048,4,0,35,0,
    0x00040047,6,34,0, 0x00040047,6,33,0,
    0x00020013,1, 0x00030021,2,1, 0x00040015,3,32,0,
    0x0003001e,4,3, 0x00040020,5,2,4, 0x0004003b,5,6,2,
    0x00040020,7,2,3, 0x0004002b,3,8,0, 0x0004002b,3,9,42,
    0x00050036,1,10,0,2, 0x000200f8,11,
    0x00050041,7,12,6,8, 0x0003003e,12,9, 0x000100fd,0x00010038
};
static const unsigned char *shader_code(const PsbcShaderOutput *output,size_t *bytes,unsigned stage) {
    if (!output->data || output->size<sizeof(PsslBinaryHeader)+sizeof(GnmShaderFileHeader)) return NULL;
    const PsslBinaryHeader *header=output->data;
    if (header->codesize>output->size-sizeof *header || header->codesize<sizeof(GnmShaderBinaryInfo)) return NULL;
    const GnmShaderFileHeader *gnm=psslSbGnmShader(header);
    const GnmShaderBinaryInfo *info=psslSbGnmShaderBinaryInfo(header);
    size_t offset=sizeof *header+sizeof *gnm+gnm->headersizedwords*4;
    if (gnm->magic!=GNM_SHADER_FILE_HEADER_ID || gnm->type!=stage ||
        gnm->targetgpumodes!=GNM_TARGETGPUMODE_BASE || memcmp(info->signature,"OrbShdr",7) || !info->length ||
        offset>output->size || info->length>output->size-offset ||
        (const unsigned char *)output->data+offset+info->length!=(const unsigned char *)info) return NULL;
    *bytes=info->length; return (const unsigned char *)output->data+offset;
}
int main(void) {
    FILE *log=NULL;
#ifdef __FreeBSD__
    log=fopen("/data/ssbu-shader-probe.log","ab");
#endif
    if (log) { fprintf(log,"GCN COMPILER PROBE: begin, original fixtures only\n"); fflush(log); }
    psbc_init(); unsigned failures=0;
    const struct { const char *name; const uint32_t *words; size_t bytes; PsbcStage stage; unsigned native; } fixtures[]={
        {"vertex",vertex,sizeof vertex,PSBC_STAGE_VERTEX,GNM_SHADER_VERTEX},
        {"fragment",fragment,sizeof fragment,PSBC_STAGE_FRAGMENT,GNM_SHADER_PIXEL},
        {"compute-storage",compute,sizeof compute,PSBC_STAGE_COMPUTE,GNM_SHADER_COMPUTE}};
    for (unsigned i=0;i<sizeof fixtures/sizeof fixtures[0];++i) {
        if (log) { fprintf(log,"GCN COMPILER PROBE: compiling %s\n",fixtures[i].name); fflush(log); }
        PsbcCompileOptions options={.target=PSBC_TARGET_PS4_BASE,.stage=fixtures[i].stage,.entrypoint="main",.optimise=true};
        PsbcShaderOutput output={0};
        PsbcResult result=psbc_compile_shader(fixtures[i].words,fixtures[i].bytes,&options,&output);
        size_t code_bytes=0; const unsigned char *code=shader_code(&output,&code_bytes,fixtures[i].native);
        int valid=result==PSBC_RESULT_OK && code;
        unsigned resource_slots=0;
        if (valid && i==2) {
            const GnmShaderFileHeader *gnm=psslSbGnmShader(output.data);
            const GnmCsShader *cs=(const GnmCsShader *)(gnm+1);
            const GnmInputUsageSlot *slots=(const GnmInputUsageSlot *)(cs+1);
            for (unsigned slot=0;slot<cs->common.numinputusageslots;++slot)
                resource_slots+=slots[slot].usagetype==GNM_SHINPUTUSAGE_PTR_INDIRECTRESOURCETABLE;
            valid=resource_slots==1 && cs->registers.computenumthreadx==1 &&
                cs->registers.computenumthready==1 && cs->registers.computenumthreadz==1;
        }
        int deterministic=0,changed=0;
        PsbcShaderOutput repeat={0},variation={0};
        if (valid && psbc_compile_shader(fixtures[i].words,fixtures[i].bytes,&options,&repeat)==PSBC_RESULT_OK) {
            size_t bytes=0; const unsigned char *other=shader_code(&repeat,&bytes,fixtures[i].native);
            deterministic=other && bytes==code_bytes && !memcmp(code,other,bytes);
        }
        uint32_t modified[sizeof compute/4]; size_t source_bytes=fixtures[i].bytes;
        memcpy(modified,fixtures[i].words,source_bytes);
        for (unsigned at=5;at<source_bytes/4;) {
            unsigned words=modified[at]>>16; if (!words) break;
            if ((modified[at]&0xffff)==44) { modified[at+3]=i ? 8 : 9; if (i) modified[at+4]=9; break; }
            if (i==2 && (modified[at]&0xffff)==43 && words==4 && modified[at+3]==42) { modified[at+3]=43; break; }
            at+=words;
        }
        if (valid && psbc_compile_shader(modified,source_bytes,&options,&variation)==PSBC_RESULT_OK) {
            size_t bytes=0; const unsigned char *other=shader_code(&variation,&bytes,fixtures[i].native);
            changed=other && (bytes!=code_bytes || memcmp(code,other,bytes));
        }
        printf("GCN COMPILER PROBE: stage=%s result=%d bytes=%zu code_bytes=%zu valid_container=%d deterministic=%d changed_code=%d resource_slots=%u (GPU execution untested)\n",fixtures[i].name,result,output.size,code_bytes,valid,deterministic,changed,resource_slots); fflush(stdout);
        if (log) { fprintf(log,"GCN COMPILER PROBE: stage=%u result=%d bytes=%zu valid_container=%d\n",i,result,output.size,valid); fflush(log); }
        failures+=!valid || !deterministic || !changed;
        psbc_free_output(&repeat); psbc_free_output(&variation); psbc_free_output(&output);
    }
    psbc_shutdown(); if (log) { fprintf(log,"GCN COMPILER PROBE: end failures=%u\n",failures); fclose(log); } return failures ? 1 : 0;
}
