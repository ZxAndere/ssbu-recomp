/* Builder fields were verified with accessors from the matching local SDK.
 * Unsupported wrap, reduction and border modes fail before native creation. */
typedef struct {
    uint64_t device;
    uint32_t min_filter,mag_filter,wrap[3];
    float min_lod,max_lod,bias;
    uint32_t compare,compare_function,border[4];
    float anisotropy; uint32_t reduction; float lod_snap;
} NativeSamplerParameters;
_Static_assert(offsetof(NativeSamplerParameters,border)==0x30,"matched guest sampler fields");
#if defined(SP_GUEST_VULKAN) && !defined(SSBU_PS4)
typedef struct { VkSampler sampler; } NativeSampler;
static int native_sampler_open(NativeGraphics *gpu,NativeSampler **out,const NativeSamplerParameters *p) {
    if (p->min_filter>5 || p->mag_filter>1 || p->compare>1 || p->compare_function<1 || p->compare_function>8 ||
        !(p->anisotropy>=1 && p->anisotropy<=1) || !(p->lod_snap==0) || p->reduction ||
        !(p->min_lod>=0 && p->max_lod>=p->min_lod) || !(p->bias>=-gpu->properties.limits.maxSamplerLodBias && p->bias<=gpu->properties.limits.maxSamplerLodBias))
        return SP_UNSUPPORTED;
    for (unsigned i=0;i<4;++i) if (p->border[i]) return SP_UNSUPPORTED;
    VkSamplerCreateInfo info={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .minFilter=(p->min_filter&1) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
        .magFilter=p->mag_filter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
        .mipmapMode=p->min_filter>=4 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .mipLodBias=p->bias,.compareEnable=p->compare,.compareOp=(VkCompareOp)(p->compare_function-1),
        .minLod=p->min_filter>=2 ? p->min_lod : 0,.maxLod=p->min_filter>=2 ? p->max_lod : 0,
        .borderColor=VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK};
    VkSamplerAddressMode modes[3];
    for (unsigned i=0;i<3;++i) switch (p->wrap[i]) {
        case 1: modes[i]=VK_SAMPLER_ADDRESS_MODE_REPEAT; break;
        case 5: modes[i]=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; break;
        case 6: modes[i]=VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT; break;
        case 7: modes[i]=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; break;
        default: return SP_UNSUPPORTED;
    }
    info.addressModeU=modes[0]; info.addressModeV=modes[1]; info.addressModeW=modes[2];
    NativeSampler *sampler=calloc(1,sizeof *sampler); if (!sampler) return SP_SYSTEM; *out=sampler;
    PFN_vkCreateSampler create=(PFN_vkCreateSampler)gpu->get(gpu->instance,"vkCreateSampler");
    return create && create(gpu->device,&info,NULL,&sampler->sampler)==VK_SUCCESS ? SP_OK : SP_SYSTEM;
}
static int native_sampler_close(NativeGraphics *gpu,NativeSampler **out) {
    NativeSampler *sampler=*out; if (!sampler) return SP_OK;
    int rc=native_graphics_wait(gpu); if (rc) return rc;
    PFN_vkDestroySampler destroy=(PFN_vkDestroySampler)gpu->get(gpu->instance,"vkDestroySampler");
    if (!destroy) return SP_SYSTEM;
    if (sampler->sampler) destroy(gpu->device,sampler->sampler,NULL); free(sampler); *out=NULL; return SP_OK;
}
#else
typedef struct NativeSampler NativeSampler;
static int native_sampler_open(NativeGraphics *gpu,NativeSampler **out,const NativeSamplerParameters *p) {
    (void)gpu; (void)out; (void)p; return SP_UNSUPPORTED;
}
static int native_sampler_close(NativeGraphics *gpu,NativeSampler **out) { (void)gpu; (void)out; return SP_UNSUPPORTED; }
#endif
