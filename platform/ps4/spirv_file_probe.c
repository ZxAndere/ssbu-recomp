/* Explicit offline SPIR-V input. Never embeds or publishes game shader data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psbc_compile.h>
#include <pssl_types.h>
int main(int argc,char **argv) {
    if (argc!=4) return 2;
    unsigned stage=(unsigned)strtoul(argv[2],NULL,10);
    PsbcCompileOptions options={.target=PSBC_TARGET_PS4_BASE,.entrypoint="main",.optimise=true};
    unsigned type;
    if (!stage) { options.stage=PSBC_STAGE_VERTEX; type=GNM_SHADER_VERTEX; }
    else if (stage==1) { options.stage=PSBC_STAGE_FRAGMENT; type=GNM_SHADER_PIXEL; }
    else if (stage==5) { options.stage=PSBC_STAGE_COMPUTE; type=GNM_SHADER_COMPUTE; }
    else return 2;
    FILE *file=fopen(argv[1],"rb"); if (!file) return 2;
    if (fseek(file,0,SEEK_END)) { fclose(file); return 2; }
    long bytes=ftell(file);
    if (bytes<20 || bytes>4*1024*1024 || bytes%4 || fseek(file,0,SEEK_SET)) { fclose(file); return 2; }
    uint32_t *words=malloc((size_t)bytes); if (!words) { fclose(file); return 2; }
    if (fread(words,1,(size_t)bytes,file)!=(size_t)bytes) { free(words); fclose(file); return 2; } fclose(file);
    PsbcShaderOutput output={0}; psbc_init();
    PsbcResult result=psbc_compile_shader(words,(size_t)bytes,&options,&output); free(words);
    int valid=0; unsigned code_bytes=0;
    if (!result && output.data && output.size>=sizeof(PsslBinaryHeader)+sizeof(GnmShaderFileHeader)) {
        const PsslBinaryHeader *header=output.data;
        if (header->codesize<=output.size-sizeof *header && header->codesize>=sizeof(GnmShaderBinaryInfo)) {
            const GnmShaderFileHeader *gnm=psslSbGnmShader(header);
            const GnmShaderBinaryInfo *info=psslSbGnmShaderBinaryInfo(header);
            size_t offset=sizeof *header+sizeof *gnm+gnm->headersizedwords*4;
            valid=gnm->magic==GNM_SHADER_FILE_HEADER_ID && gnm->type==type && gnm->targetgpumodes==GNM_TARGETGPUMODE_BASE &&
                !memcmp(info->signature,"OrbShdr",7) && info->length && offset<=output.size && info->length<=output.size-offset &&
                (const unsigned char *)output.data+offset+info->length==(const unsigned char *)info;
            code_bytes=info->length;
        }
    }
    printf("SPIRV FILE PROBE: stage=%u result=%d valid_container=%d binary_bytes=%zu code_bytes=%u (GPU execution untested)\n",stage,result,valid,output.size,code_bytes);
    int status=1;
    if (valid) {
        file=fopen(argv[3],"wb");
        if (file) { int good=fwrite(output.data,1,output.size,file)==output.size; if (fclose(file)) good=0; status=!good; }
    }
    psbc_free_output(&output); psbc_shutdown(); return status;
}
