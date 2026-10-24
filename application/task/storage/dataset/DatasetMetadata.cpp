#include "DatasetMetadata.h"
#include "../../../utils/Utf8.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <cerrno>

namespace geoputer {
namespace {
void trim(std::string& s) {
    const auto first=s.find_first_not_of(" \t"),last=s.find_last_not_of(" \t");
    s=first==std::string::npos?std::string{}:s.substr(first,last-first+1);
}
bool filename(const std::string& name,const char* extension) {
    const size_t n=std::strlen(extension);
    return name.size()>n && name.find_first_of("/\\:")==std::string::npos && name.compare(name.size()-n,n,extension)==0;
}
// 1 = line, 0 = EOF, -1 = invalid/I/O. CRLF and BOM do not consume payload bytes.
int line(FILE* file,char (&buffer)[config::DatasetLineBytes],StorageObserver* observer) {
    SdAccessScope access(observer);
    size_t n=0; int ch;
    while((ch=std::fgetc(file))!=EOF && ch!='\n') {
        if(ch=='\r') { if(std::fgetc(file)!='\n') return -1; break; }
        if(!ch || n+1>=sizeof(buffer)) return -1;
        buffer[n++]=static_cast<char>(ch);
    }
    if(std::ferror(file)) return -1;
    buffer[n]='\0';
    return ch==EOF && n==0?0:1;
}
}
void datasetCopyText(char* target,size_t capacity,const std::string& source) {
    if(!capacity) return;
    const bool clipped=source.size()>=capacity && capacity>=4;
    size_t bytes=std::min(capacity-(clipped?4:1),source.size());
    if(bytes<source.size()) while(bytes && (static_cast<unsigned char>(source[bytes])&0xc0)==0x80) --bytes;
    std::memcpy(target,source.data(),bytes);
    if(clipped) {std::memcpy(target+bytes,"...",3);bytes+=3;}
    target[bytes]='\0';
}
DatasetMetadata readDatasetMetadata(const std::string& directory,UiLanguage ui,const char* requested,StorageObserver* observer) {
    DatasetMetadata result;
    struct Close {StorageObserver* observer;void operator()(FILE* file) const {SdAccessScope access(observer);std::fclose(file);}};
    auto open=[&](){SdAccessScope access(observer);return std::fopen((directory+"/dataset.ini").c_str(),"rb");};
    std::unique_ptr<FILE,Close> file(open(),Close{observer});
    if(!file) {result.error=(errno==ENOENT || errno==ENOTDIR)?DatasetError::Missing:DatasetError::Data;return result;}
    char buffer[config::DatasetLineBytes];
    std::string section; unsigned common=0,fields=0; bool common_seen=false;
    auto fail=[&](DatasetError e=DatasetError::Metadata) {result.error=e;return result;};
    auto finish=[&]() {return section.empty() || section=="dataset" || (fields&5)==5;};
    // Pass one validates every section but retains only common values and language IDs.
    for(unsigned pass=0;pass<2;++pass) {
        {
        SdAccessScope access(observer);
        if(std::fseek(file.get(),0,SEEK_SET)) return fail(DatasetError::Data);
        const int first=std::fgetc(file.get());
        if(first==0xef) { if(std::fgetc(file.get())!=0xbb || std::fgetc(file.get())!=0xbf) return fail(); }
        else if(first!=EOF) std::ungetc(first,file.get());
        }
        section.clear(); fields=0;
        std::string display_language;
        if(pass) {
            display_language=ui==UiLanguage::Japanese?"ja":"en";
            if(std::find(result.languages.begin(),result.languages.end(),display_language)==result.languages.end()) display_language=result.default_language;
        }
        int status;
        while((status=line(file.get(),buffer,observer))>0) {
            std::string value(buffer);
            if(!validUtf8(value)) return fail();
            trim(value);if(value.empty() || value[0]=='#') continue;
            if(value[0]=='[') {
                if(value.back()!=']' || (!pass && !finish())) return fail();
                section=value.substr(1,value.size()-2);fields=0;
                if(section=="dataset") {if(!pass && common_seen) return fail();common_seen=true;}
                else {
                    if(!validDatasetLanguage(section.c_str())) return fail();
                    if(!pass) {if(std::find(result.languages.begin(),result.languages.end(),section)!=result.languages.end()) return fail();result.languages.push_back(section);}
                }
                continue;
            }
            const auto equal=value.find('=');if(section.empty() || equal==std::string::npos) return fail();
            auto key=value.substr(0,equal);value.erase(0,equal+1);trim(key);trim(value);
            if(key.empty()) return fail();
            if(section=="dataset") {
                if(pass) continue;
                unsigned bit=0;
                if(key=="version") {bit=1;if(value!="1") return fail(DatasetError::Version);}
                else if(key=="map") {bit=2;if(!filename(value,".glc")) return fail();result.map=value;}
                else if(key=="default_language") {bit=4;if(!validDatasetLanguage(value.c_str())) return fail();result.default_language=value;}
                if(bit && (common&bit)) return fail();
                common|=bit;
            } else {
                unsigned bit=key=="title"?1:key=="description"?2:key=="wordbook"?4:
                    key=="display_line1"?8:key=="display_line2"?16:0;
                if(!pass) {
                    if(bit && (fields&bit)) return fail();
                    fields|=bit;
                    if((bit==1 && value.empty()) || (bit==4 && !filename(value,".giswordbook"))) return fail();
                    if ((bit == 8 || bit == 16) && !validPlaceFormat(value)) return fail();
                } else {
                    if(key=="wordbook" && !filename(value,".giswordbook")) return fail();
                    if(section==display_language && key=="title") result.title=value;
                    if(section==display_language && key=="description") result.description=value;
                    if(section==result.language && key=="wordbook") result.wordbook=value;
                    if(section==result.language && (key=="display_line1" || key=="display_line2")) {
                        if (!validPlaceFormat(value)) return fail();
                        result.display.specified = true;
                        result.display.lines[key=="display_line1"?0:1] = value;
                    }
                }
            }
        }
        if(status<0) return fail();
        if(!pass) {
            if(!finish() || common!=7 || result.languages.empty() || std::find(result.languages.begin(),result.languages.end(),result.default_language)==result.languages.end()) return fail();
            result.language=requested && *requested?requested:result.default_language;
            if(std::find(result.languages.begin(),result.languages.end(),result.language)==result.languages.end()) return fail(DatasetError::Missing);
        }
    }
    if(result.title.empty() || result.wordbook.empty()) return fail();
    return result;
}

} // namespace geoputer
