/* Guest pointers must cross the memory boundary even for short C strings.
 * C-locale case folding is intentional; locale-dependent collation is not
 * implemented. A bounded failure replaces an unbounded malformed scan. */
static unsigned char string_byte(uint64_t address,size_t offset) {
    if (offset>=1024*1024) fail(SP_INVALID,"unterminated or oversized string",address);
    return *(unsigned char *)sp_recomp_access(address+offset,1,0);
}
static size_t string_length(uint64_t address,size_t limit) {
    size_t length=0;
    while (length<limit && string_byte(address,length)) ++length;
    return length;
}
static unsigned char string_lower(unsigned char byte) {
    return byte>='A' && byte<='Z' ? (unsigned char)(byte+'a'-'A') : byte;
}
static int string_digit(unsigned char byte) {
    if (byte>='0' && byte<='9') return byte-'0';
    byte=string_lower(byte);
    return byte>='a' && byte<='z' ? byte-'a'+10 : 36;
}
static void string_errno(uint32_t error) {
    uint64_t tls=0x7800000000ULL+(uint64_t)active->current_thread*0x10000;
    memcpy(sp_recomp_access(tls+0x100,4,1),&error,4);
}
static int integer_import(Ctx *context,const char *name) {
    int small=!strcmp(name,"atoi"),simple=small || !strcmp(name,"atol") || !strcmp(name,"atoll");
    int unsign=!strcmp(name,"strtoul") || !strcmp(name,"strtoull") || !strcmp(name,"strtoumax");
    if (!simple && !unsign && strcmp(name,"strtol") && strcmp(name,"strtoll") && strcmp(name,"strtoimax")) return 0;
    uint64_t source=context->x[0],end_pointer=simple ? 0 : context->x[1];
    unsigned base=simple ? 10 : (unsigned)context->x[2];
    if (base==1 || base>36) { string_errno(22); context->x[0]=0; return 1; }
    size_t at=0; unsigned char byte=string_byte(source,at);
    while (byte==' ' || (byte>='\t' && byte<='\r')) byte=string_byte(source,++at);
    int negative=byte=='-';
    if (negative || byte=='+') byte=string_byte(source,++at);
    if ((!base || base==16) && byte=='0') {
        unsigned char next=string_lower(string_byte(source,at+1));
        if (next=='x' && string_digit(string_byte(source,at+2))<16) { base=16; at+=2; }
    }
    if (!base) base=string_byte(source,at)=='0' ? 8 : 10;
    size_t digits=at; uint64_t value=0,maximum=unsign ? UINT64_MAX : negative ? UINT64_C(1)<<63 : INT64_MAX;
    int overflow=0;
    for (;;++at) {
        unsigned digit=(unsigned)string_digit(string_byte(source,at));
        if (digit>=base) break;
        if (value>maximum/base || (value==maximum/base && digit>maximum%base)) overflow=1;
        else if (!overflow) value=value*base+digit;
    }
    if (at==digits) { at=0; value=0; string_errno(22); }
    else if (overflow) { value=maximum; string_errno(34); }
    if (negative && (!unsign || !overflow)) value=0-value;
    if (end_pointer) { uint64_t end=source+at; memcpy(sp_recomp_access(end_pointer,8,1),&end,8); }
    context->x[0]=small ? (uint32_t)value : value;
    return 1;
}
static int string_import(Ctx *context,const char *name) {
    if (integer_import(context,name)) return 1;
    uint64_t first=context->x[0],second=context->x[1]; size_t limit=(size_t)context->x[2];
    if (!strcmp(name,"wmemcpy") || !strcmp(name,"wmemmove") || !strcmp(name,"wmemset") || !strcmp(name,"wmemcmp")) {
        /* AArch64 wchar_t is 32-bit; Windows wchar_t is 16-bit. */
        if (limit>SIZE_MAX/4) fail(SP_INVALID,"wide memory length overflow",limit);
        size_t bytes=limit*4;
        if (!strcmp(name,"wmemset")) {
            unsigned char *output=sp_recomp_access(first,bytes,1); uint32_t value=(uint32_t)second;
            for (size_t i=0;i<limit;++i) memcpy(output+i*4,&value,4);
        } else if (!strcmp(name,"wmemcmp")) {
            const unsigned char *a=sp_recomp_access(first,bytes,0),*b=sp_recomp_access(second,bytes,0);
            int result=0;
            for (size_t i=0;i<limit;++i) {
                int32_t x,y; memcpy(&x,a+i*4,4); memcpy(&y,b+i*4,4);
                if (x!=y) { result=x<y ? -1 : 1; break; }
            }
            context->x[0]=(uint32_t)result;
        } else memmove(sp_recomp_access(first,bytes,1),sp_recomp_access(second,bytes,0),bytes);
    } else if (!strcmp(name,"strlen") || !strcmp(name,"strnlen")) {
        context->x[0]=string_length(first,!strcmp(name,"strlen") ? SIZE_MAX : (size_t)second);
    } else if (!strcmp(name,"strcmp") || !strcmp(name,"strncmp") ||
               !strcmp(name,"strcasecmp") || !strcmp(name,"strncasecmp")) {
        int fold=!strcmp(name,"strcasecmp") || !strcmp(name,"strncasecmp");
        if (!strcmp(name,"strcmp") || !strcmp(name,"strcasecmp")) limit=SIZE_MAX;
        int difference=0;
        for (size_t i=0;i<limit;++i) {
            unsigned char a=string_byte(first,i),b=string_byte(second,i);
            difference=(int)(fold ? string_lower(a) : a)-(int)(fold ? string_lower(b) : b);
            if (difference || !a || !b) break;
        }
        context->x[0]=(uint32_t)difference;
    } else if (!strcmp(name,"strchr") || !strcmp(name,"strrchr")) {
        uint64_t found=0;
        for (size_t i=0;;++i) {
            unsigned char byte=string_byte(first,i);
            if (byte==(unsigned char)second) { found=first+i; if (!strcmp(name,"strchr")) break; }
            if (!byte) break;
        }
        context->x[0]=found;
    } else if (!strcmp(name,"memchr")) {
        const unsigned char *bytes=sp_recomp_access(first,limit,0);
        const unsigned char *found=memchr(bytes,(unsigned char)second,limit);
        context->x[0]=found ? first+(size_t)(found-bytes) : 0;
    } else if (!strcmp(name,"strcpy") || !strcmp(name,"strncpy") ||
               !strcmp(name,"strcat") || !strcmp(name,"strncat")) {
        int pad=!strcmp(name,"strncpy"),append=!strcmp(name,"strcat") || !strcmp(name,"strncat");
        size_t at=append ? string_length(first,SIZE_MAX) : 0;
        if (!strcmp(name,"strcpy") || !strcmp(name,"strcat")) limit=SIZE_MAX;
        size_t i=0;
        for (;i<limit;++i) {
            unsigned char byte=string_byte(second,i);
            *(unsigned char *)sp_recomp_access(first+at+i,1,1)=byte;
            if (!byte) break;
        }
        if (pad && i<limit) {
            size_t rest=limit-i;
            memset(sp_recomp_access(first+i,rest,1),0,rest);
        } else if (append && i==limit) *(unsigned char *)sp_recomp_access(first+at+i,1,1)=0;
    } else if (!strcmp(name,"strdup")) {
        size_t length=string_length(first,SIZE_MAX)+1;
        uint64_t copy=heap_allocate(length,16);
        if (copy) memcpy(sp_recomp_access(copy,length,1),sp_recomp_access(first,length,0),length);
        context->x[0]=copy;
    } else if (!strcmp(name,"strspn") || !strcmp(name,"strcspn") || !strcmp(name,"strpbrk")) {
        unsigned char set[256]={0};
        for (size_t i=0;;++i) { unsigned char byte=string_byte(second,i); if (!byte) break; set[byte]=1; }
        size_t length=0; unsigned char byte;
        int include=!strcmp(name,"strspn");
        while ((byte=string_byte(first,length))!=0 && (set[byte]!=0)==include) ++length;
        context->x[0]=!strcmp(name,"strpbrk") ? (byte ? first+length : 0) : length;
    } else if (!strcmp(name,"strstr")) {
        size_t needle=string_length(second,SIZE_MAX),haystack=string_length(first,SIZE_MAX);
        uint64_t found=0;
        if (needle<=haystack) for (size_t i=0;i<=haystack-needle;++i) {
            if (!memcmp(sp_recomp_access(first+i,needle,0),sp_recomp_access(second,needle,0),needle)) { found=first+i; break; }
        }
        context->x[0]=found;
    } else return 0;
    return 1;
}
