#include "WifiFiles.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cctype>
namespace geoputer::wifi {
namespace {
constexpr char DataRoot[]="/geoputer";
constexpr char Private[]="/geoputer/.rgp-wifi";
constexpr uint32_t MaxUpload=config::WifiUploadMaxBytes; // Arduino WebServer Content-Length is signed 32 bit.
bool dataRoot(std::string_view path) {
    return path.size()==sizeof(DataRoot)-1 &&
        std::equal(path.begin(),path.end(),DataRoot,[](char a,char b){
            return std::tolower(static_cast<unsigned char>(a))==std::tolower(static_cast<unsigned char>(b));
        });
}
bool reserved(std::string_view name) {
    constexpr char prefix[]=".rgp-";
    if(name.size()<sizeof(prefix)-1)return false;
    for(size_t i=0;i<sizeof(prefix)-1;++i)if(std::tolower(static_cast<unsigned char>(name[i]))!=prefix[i])return false;
    return true;
}
uint64_t hash(uint64_t h,const void* data,size_t n) {
    const auto* p=static_cast<const uint8_t*>(data);
    while(n--)h=(h^*p++)*1099511628211ull;
    return h;
}
constexpr uint64_t HashStart=14695981039346656037ull;
struct Journal {char magic[8];char path[256];uint64_t checksum;};
}
bool Utf8Text::accept(const void* data,size_t bytes) {
    const auto* p=static_cast<const uint8_t*>(data);
    while(bytes-- && good_) {
        const auto c=*p++;
        if(remaining_) {
            if((c&0xc0)!=0x80){good_=false;break;}
            code_=(code_<<6)|(c&63);
            if(!--remaining_ && (code_<min_ || code_>0x10ffff || (code_>=0xd800 && code_<=0xdfff)))good_=false;
        } else if(c<128) {
            if((c<32 && c!='\t' && c!='\n' && c!='\r') || c==127)good_=false;
        } else if(c>=0xc2 && c<=0xdf){remaining_=1;code_=c&31;min_=0x80;}
        else if(c>=0xe0 && c<=0xef){remaining_=2;code_=c&15;min_=0x800;}
        else if(c>=0xf0 && c<=0xf4){remaining_=3;code_=c&7;min_=0x10000;}
        else good_=false;
    }
    return good_;
}
bool validPath(std::string_view path,bool root) {
    if(path.empty() || path.size()>=256 || path[0]!='/' || (path.size()==1 && !root))return false;
    Utf8Text utf8;if(!utf8.accept(path.data(),path.size()) || !utf8.complete())return false;
    if(path=="/")return true;
    size_t start=1;
    for(size_t i=1;i<=path.size();++i) {
        if(i<path.size()) {
            const auto c=static_cast<unsigned char>(path[i]);
            if(c<32 || c==127 || std::strchr("\\:*?\"<>|",c))return false;
        }
        if(i==path.size() || path[i]=='/') {
            const auto part=path.substr(start,i-start);
            if(part.empty() || part=="." || part==".." || part.back()=='.' || part.back()==' ' || reserved(part))return false;
            start=i+1;
        }
    }
    return true;
}
bool decodePath(std::string_view in,char* out,size_t bytes) {
    auto hex=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
    size_t n=0;
    for(size_t i=0;i<in.size();++i) {
        unsigned char c=in[i];
        if(c=='%') {if(i+2>=in.size())return false;const int a=hex(in[i+1]),b=hex(in[i+2]);if(a<0||b<0)return false;c=a*16+b;i+=2;}
        if(!c || n+1>=bytes)return false;
        out[n++]=c;
    }
    if(!bytes)return false;
    out[n]=0;return validPath({out,n});
}
bool textExtension(const char* path) {
    const auto* ext=std::strrchr(path,'.');if(!ext)return false;
    for(auto allowed:{".ini",".txt",".csv",".md",".log",".json"}) {
        size_t i=0;while(ext[i]&&allowed[i]&&std::tolower(static_cast<unsigned char>(ext[i]))==allowed[i])++i;
        if(!ext[i]&&!allowed[i])return true;
    }
    return false;
}
bool Files::visible(const char* name){return name[0] && std::strcmp(name,".") && std::strcmp(name,"..") && !reserved(name);}
bool Files::native(const char* logical,char* out,size_t bytes)const {
    if(drive_==0xff || !validPath(logical))return false;
    const int n=std::snprintf(out,bytes,"%u:%s",drive_,logical);
    if(n<=0 || static_cast<size_t>(n)>=bytes)return false;
    // Query the private entry by its long name: FatFs f_stat on an alias
    // may return the alias itself in fname rather than the canonical long name.
    if(logical[1]) {
        char private_dir[64];std::snprintf(private_dir,sizeof(private_dir),"%u:%s",drive_,Private);
        FILINFO info{};
        if(f_stat(private_dir,&info)==FR_OK && info.altname[0]) {
            const auto* part=logical+1;
            constexpr size_t root_length=sizeof(DataRoot)-1;
            if(std::strlen(logical)>=root_length &&
               std::equal(DataRoot,DataRoot+root_length,logical,[](char a,char b){
                   return std::tolower(static_cast<unsigned char>(a))==std::tolower(static_cast<unsigned char>(b));
               }) && logical[root_length]=='/')part=logical+root_length+1;
            else part=nullptr;
            if(!part)return true;
            const auto* slash=std::strchr(part,'/');
            const size_t count=slash?static_cast<size_t>(slash-part):std::strlen(part);
            const auto* alias=info.altname;
            if(std::strlen(alias)==count) {
                size_t i=0;
                while(i<count && std::tolower(static_cast<unsigned char>(part[i]))==std::tolower(static_cast<unsigned char>(alias[i])))++i;
                if(i==count)return false;
            }
        }
    }
    return true;
}
bool Files::privatePath(const char* name,char* out,size_t bytes)const {
    const int n=std::snprintf(out,bytes,"%u:%s/%s",drive_,Private,name);
    return drive_!=0xff && n>0 && static_cast<size_t>(n)<bytes;
}
bool Files::existsNative(const char* path){FILINFO info{};return f_stat(path,&info)==FR_OK;}
bool Files::erasePrivate(const char* name){char p[64];privatePath(name,p,sizeof(p));const auto r=f_unlink(p);return r==FR_OK || r==FR_NO_FILE || r==FR_NO_PATH;}
FRESULT Files::stat(const char* path,FILINFO& info){char p[260];if(!native(path,p,sizeof(p)))return FR_INVALID_NAME;return f_stat(p,&info);}
bool Files::mkdir(const char* path) {
    char p[260];if(!validPath(path,false)||!native(path,p,sizeof(p)))return fail("Invalid directory path");
    return f_mkdir(p)==FR_OK || fail("Cannot create directory (exists, full, or SD error)");
}
bool Files::close() {
    if(!open_)return true;
    const auto r=f_close(&file_);open_=false;
    return r==FR_OK || fail("SD close failed");
}
bool Files::openRead(const char* path) {
    if(open_)return fail("File operation busy");
    char p[260];if(!native(path,p,sizeof(p)))return fail("Invalid file path");
    if(f_open(&file_,p,FA_READ)!=FR_OK)return fail("Cannot open file");
    open_=true;return true;
}
bool Files::read(void* out,unsigned bytes,unsigned& got) {
    got=0;return (open_ && f_read(&file_,out,bytes,&got)==FR_OK) || fail("SD read failed");
}
bool Files::inspectText(const char* path,char version[17]) {
    if(!textExtension(path))return fail("Only text files can be viewed or edited");
    if(!openRead(path))return false;
    if(size()>config::WifiTextMaxBytes){close();return fail("Text exceeds 64 KiB limit");}
    Utf8Text utf8;uint64_t h=HashStart;bool ok=true;
    for(;;) {
        unsigned n=0;if(!read(buffer_,sizeof(buffer_),n)){ok=false;break;}
        if(!n)break;
        h=hash(h,buffer_,n);if(!utf8.accept(buffer_,n)){ok=false;break;}
    }
    ok=ok && utf8.complete();const bool closed=close();
    if(!ok)return fail("File is not valid UTF-8 text");
    if(!closed)return false;
    std::snprintf(version,17,"%016llx",static_cast<unsigned long long>(h));return true;
}
bool Files::recover() {
    if(open_)return fail("File operation busy");
    char root[64];std::snprintf(root,sizeof(root),"%u:%s",drive_,DataRoot);
    const auto root_made=f_mkdir(root);if(root_made!=FR_OK && root_made!=FR_EXIST)return fail("Cannot prepare Geoputer directory");
    char dir[64];std::snprintf(dir,sizeof(dir),"%u:%s",drive_,Private);
    const auto made=f_mkdir(dir);if(made!=FR_OK && made!=FR_EXIST)return fail("Cannot prepare recovery directory");
    char record[64];privatePath("transaction",record,sizeof(record));FILINFO info{};
    const auto sr=f_stat(record,&info);
    if(sr==FR_OK) {
        Journal j{};unsigned n=0;
        if(f_open(&file_,record,FA_READ)!=FR_OK)return fail("Cannot open recovery journal");
        open_=true;const auto rr=f_read(&file_,&j,sizeof(j),&n);const bool closed=close();
        if(rr!=FR_OK || !closed || n!=sizeof(j) || std::memcmp(j.magic,"RGPWIFI1",8) || !std::memchr(j.path,0,sizeof(j.path)) ||
           !validPath(j.path,false) || j.checksum!=hash(HashStart,j.path,sizeof(j.path)))return fail("Recovery journal damaged; inspect SD card");
        char target[260],backup[64];native(j.path,target,sizeof(target));privatePath("backup",backup,sizeof(backup));
        const auto target_result=f_stat(target,&info);
        if(target_result!=FR_OK && target_result!=FR_NO_FILE && target_result!=FR_NO_PATH)return fail("Cannot inspect recovery target");
        if(target_result!=FR_OK && existsNative(backup) && f_rename(backup,target)!=FR_OK)return fail("Cannot restore previous file");
        if(!erasePrivate("backup") || !erasePrivate("upload") || !erasePrivate("transaction"))return fail("Recovery cleanup failed");
    } else if(sr!=FR_NO_FILE && sr!=FR_NO_PATH)return fail("Cannot read recovery state");
    // No journal means a staged body was never committed. Preserve unexpected
    // backups rather than silently deleting the only surviving copy.
    char backup[64];privatePath("backup",backup,sizeof(backup));
    if(existsNative(backup))return fail("Unmatched backup; inspect SD card");
    return erasePrivate("upload") || fail("Cannot remove incomplete upload");
}
bool Files::beginWrite(const char* path,uint32_t size,bool replace,const char* version,bool text) {
    error_="";
    if(open_ || writing_)return fail("File operation busy");
    if(!validPath(path,false) || size>MaxUpload)return fail("Invalid path or file exceeds 2 GiB HTTP limit");
    if(text && (size>config::WifiTextMaxBytes || !textExtension(path)))return fail("Unsupported text file or size");
    if(!recover())return false;
    FILINFO info{};const auto sr=stat(path,info);
    if(sr==FR_OK && (info.fattrib&AM_DIR))return fail("Destination is a directory");
    if(sr==FR_OK && !replace)return fail("File exists; confirm replacement");
    if(sr!=FR_OK && sr!=FR_NO_FILE && sr!=FR_NO_PATH)return fail("Cannot inspect destination");
    if(version) {char current[17];if(!inspectText(path,current))return false;if(std::strcmp(version,current))return fail("File changed; reload before saving");}
    char volume[8];std::snprintf(volume,sizeof(volume),"%u:",drive_);DWORD clusters=0;FATFS* fs=nullptr;
    if(f_getfree(volume,&clusters,&fs)!=FR_OK || !fs || uint64_t(clusters)*fs->csize*512<size)return fail("Insufficient SD free space");
    char temporary[64];privatePath("upload",temporary,sizeof(temporary));
    if(f_open(&file_,temporary,FA_WRITE|FA_CREATE_NEW)!=FR_OK)return fail("Cannot create upload file");
    std::strcpy(target_,path);expected_=size;written_=0;utf8_={};text_=text;open_=writing_=true;return true;
}
bool Files::write(const void* data,unsigned bytes) {
    if(!writing_ || !open_ || written_>expected_ || bytes>expected_-written_)return fail("Unexpected upload size");
    if(text_ && !utf8_.accept(data,bytes))return fail("Invalid UTF-8 text");
    unsigned n=0;if(f_write(&file_,data,bytes,&n)!=FR_OK || n!=bytes)return fail("SD write failed or card full");
    written_+=n;return true;
}
bool Files::journal() {
    Journal j{};std::memcpy(j.magic,"RGPWIFI1",8);std::strcpy(j.path,target_);j.checksum=hash(HashStart,j.path,sizeof(j.path));
    char p[64];privatePath("transaction",p,sizeof(p));
    if(f_open(&file_,p,FA_WRITE|FA_CREATE_NEW)!=FR_OK)return fail("Cannot create replacement journal");
    open_=true;unsigned n=0;
    const bool wrote=f_write(&file_,&j,sizeof(j),&n)==FR_OK && n==sizeof(j);
    const bool synced=wrote && f_sync(&file_)==FR_OK;const bool closed=close();
    return (synced && closed) || fail("Cannot persist replacement journal");
}
bool Files::finish() {
    if(!writing_ || !open_ || written_!=expected_ || (text_ && !utf8_.complete()))return fail("Incomplete upload or UTF-8 text");
    const bool synced=f_sync(&file_)==FR_OK;const bool closed=close();writing_=false;
    if(!synced || !closed)return fail("Cannot finish SD write");
    if(!journal())return false;
    char target[260],temporary[64],backup[64];native(target_,target,sizeof(target));
    privatePath("upload",temporary,sizeof(temporary));privatePath("backup",backup,sizeof(backup));
    FILINFO info{};const auto sr=f_stat(target,&info);
    if(sr==FR_OK && f_rename(target,backup)!=FR_OK)return fail("Cannot preserve previous file");
    if(sr!=FR_OK && sr!=FR_NO_FILE && sr!=FR_NO_PATH)return fail("Cannot inspect replacement target");
    if(f_rename(temporary,target)!=FR_OK)return fail("Cannot publish uploaded file; recovery required");
    if(!erasePrivate("backup") || !erasePrivate("transaction"))return fail("File saved; recovery cleanup required");
    return true;
}
void Files::abort(){close();writing_=false;erasePrivate("upload");}
bool Files::remove(const char* logical,bool(*cancelled)(void*),void* context) {
    if(!validPath(logical,false) || dataRoot(logical))return fail("Cannot delete Geoputer root or invalid path");
    // Iterative depth-first removal: one directory object, no recursive stack or
    // full subtree list. A failure reports a possibly partially removed subtree.
    char path[256];std::strcpy(path,logical);const size_t base=std::strlen(path);unsigned depth=0;
    for(;;) {
        if(cancelled && cancelled(context))return fail("Delete cancelled (partial deletion possible)");
        FILINFO info{};const auto sr=stat(path,info);if(sr!=FR_OK)return fail("Cannot inspect delete target");
        if(info.fattrib&AM_DIR) {
            char native_path[260];native(path,native_path,sizeof(native_path));FF_DIR dir{};
            if(f_opendir(&dir,native_path)!=FR_OK)return fail("Cannot open delete directory");
            bool child=false,ok=true;
            for(;;) {
                if(f_readdir(&dir,&info)!=FR_OK){ok=false;break;}
                if(!info.fname[0])break;
                if(!std::strcmp(info.fname,".")||!std::strcmp(info.fname,".."))continue;
                if(!visible(info.fname)){ok=false;break;}
                const auto len=std::strlen(path),n=std::strlen(info.fname);
                if(len+n+2>sizeof(path) || depth>=config::WifiDeleteMaxDepth){ok=false;break;}
                path[len]='/';std::memcpy(path+len+1,info.fname,n+1);++depth;child=true;break;
            }
            const bool closed=f_closedir(&dir)==FR_OK;
            if(!ok || !closed)return fail("Delete stopped: path/depth limit or SD error (partial deletion possible)");
            if(child)continue;
        }
        char native_path[260];if(!native(path,native_path,sizeof(native_path)) || f_unlink(native_path)!=FR_OK)return fail("Delete failed (partial deletion possible)");
        if(std::strlen(path)==base)return true;
        *std::strrchr(path,'/')=0;--depth;
    }
}
}
