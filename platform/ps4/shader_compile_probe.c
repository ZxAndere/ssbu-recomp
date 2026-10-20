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
static const unsigned char *shader_code(const PsbcShaderOutput *output,size_t *bytes,unsigned stage) {
    if (!output->data || output->size<sizeof(PsslBinaryHeader)+sizeof(GnmShaderFileHeader)) return NULL;
    const PsslBinaryHeader *header=output->data;
    if (header->codesize>output->size-sizeof *header || header->codesize<sizeof(GnmShaderBinaryInfo)) return NULL;
    const GnmShaderFileHeader *gnm=psslSbGnmShader(header);
    const GnmShaderBinaryInfo *info=psslSbGnmShaderBinaryInfo(header);
    size_t offset=sizeof *header+sizeof *gnm+gnm->headersizedwords*4;
    if (gnm->magic!=GNM_SHADER_FILE_HEADER_ID || gnm->type!=(stage ? GNM_SHADER_PIXEL : GNM_SHADER_VERTEX) ||
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
    for (unsigned i=0;i<2;++i) {
        if (log) { fprintf(log,"GCN COMPILER PROBE: compiling %s\n",i ? "fragment" : "vertex"); fflush(log); }
        PsbcCompileOptions options={.target=PSBC_TARGET_PS4_BASE,.stage=i ? PSBC_STAGE_FRAGMENT : PSBC_STAGE_VERTEX,.entrypoint="main",.optimise=true};
        PsbcShaderOutput output={0};
        PsbcResult result=psbc_compile_shader(i ? fragment : vertex,i ? sizeof fragment : sizeof vertex,&options,&output);
        size_t code_bytes=0; const unsigned char *code=shader_code(&output,&code_bytes,i);
        int valid=result==PSBC_RESULT_OK && code;
        int deterministic=0,changed=0;
        PsbcShaderOutput repeat={0},variation={0};
        if (valid && psbc_compile_shader(i ? fragment : vertex,i ? sizeof fragment : sizeof vertex,&options,&repeat)==PSBC_RESULT_OK) {
            size_t bytes=0; const unsigned char *other=shader_code(&repeat,&bytes,i);
            deterministic=other && bytes==code_bytes && !memcmp(code,other,bytes);
        }
        uint32_t modified[sizeof fragment/4]; size_t source_bytes=i ? sizeof fragment : sizeof vertex;
        memcpy(modified,i ? fragment : vertex,source_bytes);
        for (unsigned at=5;at<source_bytes/4;) {
            unsigned words=modified[at]>>16; if (!words) break;
            if ((modified[at]&0xffff)==44) { modified[at+3]=i ? 8 : 9; if (i) modified[at+4]=9; break; }
            at+=words;
        }
        if (valid && psbc_compile_shader(modified,source_bytes,&options,&variation)==PSBC_RESULT_OK) {
            size_t bytes=0; const unsigned char *other=shader_code(&variation,&bytes,i);
            changed=other && (bytes!=code_bytes || memcmp(code,other,bytes));
        }
        printf("GCN COMPILER PROBE: stage=%s result=%d bytes=%zu code_bytes=%zu valid_container=%d deterministic=%d changed_code=%d (GPU execution untested)\n",i ? "fragment" : "vertex",result,output.size,code_bytes,valid,deterministic,changed);
        if (log) { fprintf(log,"GCN COMPILER PROBE: stage=%u result=%d bytes=%zu valid_container=%d\n",i,result,output.size,valid); fflush(log); }
        failures+=!valid || !deterministic || !changed;
        psbc_free_output(&repeat); psbc_free_output(&variation); psbc_free_output(&output);
    }
    psbc_shutdown(); if (log) { fprintf(log,"GCN COMPILER PROBE: end failures=%u\n",failures); fclose(log); } return failures ? 1 : 0;
}
