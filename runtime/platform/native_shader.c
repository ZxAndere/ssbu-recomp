#if defined(SP_GUEST_VULKAN) && !defined(SSBU_PS4)
typedef struct { VkShaderModule module; unsigned stage; } NativeShader;
static int native_shader_open(NativeGraphics *gpu,NativeShader **out,const uint32_t *words,size_t bytes,unsigned stage) {
    if (!bytes || bytes%4 || bytes<20 || words[0]!=0x07230203 || stage>5) return SP_INVALID;
    for (size_t at=5;at<bytes/4;) {
        unsigned length=words[at]>>16,operation=words[at]&0xffff;
        if (!length || length>bytes/4-at) return SP_INVALID;
        if (operation==17 && length==2) switch (words[at+1]) {
            case 2: if (!gpu->enabled_features.geometryShader) return SP_UNSUPPORTED; break;
            case 3: if (!gpu->enabled_features.tessellationShader) return SP_UNSUPPORTED; break;
            case 10: if (!gpu->enabled_features.shaderFloat64) return SP_UNSUPPORTED; break;
            case 11: if (!gpu->enabled_features.shaderInt64) return SP_UNSUPPORTED; break;
            case 12: return SP_UNSUPPORTED; /* 64-bit atomic feature chain not enabled. */
            case 22: if (!gpu->enabled_features.shaderInt16) return SP_UNSUPPORTED; break;
            case 4427: if (!gpu->draw_parameters) return SP_UNSUPPORTED; break;
            case 5379: if (!gpu->shader_demote) return SP_UNSUPPORTED; break;
            default: break;
        }
        at+=length;
    }
    NativeShader *shader=calloc(1,sizeof *shader); if (!shader) return SP_SYSTEM; *out=shader; shader->stage=stage;
    VkShaderModuleCreateInfo info={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=bytes,.pCode=words};
    PFN_vkCreateShaderModule create=(PFN_vkCreateShaderModule)gpu->get(gpu->instance,"vkCreateShaderModule");
    return create && create(gpu->device,&info,NULL,&shader->module)==VK_SUCCESS ? SP_OK : SP_SYSTEM;
}
static int native_shader_close(NativeGraphics *gpu,NativeShader **out) {
    NativeShader *shader=*out; if (!shader) return SP_OK;
    int rc=native_graphics_wait(gpu); if (rc) return rc;
    PFN_vkDestroyShaderModule destroy=(PFN_vkDestroyShaderModule)gpu->get(gpu->instance,"vkDestroyShaderModule");
    if (!destroy) return SP_SYSTEM;
    if (shader->module) destroy(gpu->device,shader->module,NULL); free(shader); *out=NULL; return SP_OK;
}
#else
typedef struct NativeShader NativeShader;
static int native_shader_open(NativeGraphics *gpu,NativeShader **out,const uint32_t *words,size_t bytes,unsigned stage) {
    (void)gpu; (void)out; (void)words; (void)bytes; (void)stage; return SP_UNSUPPORTED;
}
static int native_shader_close(NativeGraphics *gpu,NativeShader **out) { (void)gpu; (void)out; return SP_UNSUPPORTED; }
#endif
