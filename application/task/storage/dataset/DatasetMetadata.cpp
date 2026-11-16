#include "DatasetMetadata.h"
#include "../StoragePath.h"
#include "../../../utils/Utf8.h"
#include "../../../utils/Ellipsis.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <cerrno>

namespace geoputer {
namespace {
void trim(std::string_view& s) {
    const auto first=s.find_first_not_of(" \t"),last=s.find_last_not_of(" \t");
    s=first==std::string_view::npos?std::string_view{}:s.substr(first,last-first+1);
}
bool filename(std::string_view name,const char* extension) {
    const size_t n=std::strlen(extension);
    return name.size()>n && name.size()<config::DatasetFilenameBytes && name.find_first_of("/\\:")==std::string_view::npos && name.compare(name.size()-n,n,extension)==0;
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
void datasetCopyText(char* target,size_t capacity,std::string_view source) {
    if(!target || !capacity) return;
    const bool clipped=source.size()>=capacity && capacity>=4;
    size_t bytes=std::min(capacity-(clipped?4:1),source.size());
    if(bytes<source.size()) while(bytes && (static_cast<unsigned char>(source[bytes])&0xc0)==0x80) --bytes;
    if(bytes)std::memcpy(target,source.data(),bytes);
    if(clipped) {std::memcpy(target+bytes,EllipsisUtf8,EllipsisBytes);bytes+=EllipsisBytes;}
    target[bytes]='\0';
}
void readDatasetMetadataInto(std::string_view directory,UiLanguage ui,const char* requested,
    DatasetMetadata& result,StorageObserver* observer,DatasetMetadataText text,DatasetLanguages* languages) {
    result={};
    datasetCopyText(text.title,text.title_capacity,{});datasetCopyText(text.description,text.description_capacity,{});
    if(languages)*languages={};
    DatasetLanguages choices;
    char default_language[config::DatasetLanguageBytes]{};
    struct Close {StorageObserver* observer;void operator()(FILE* file) const {SdAccessScope access(observer);std::fclose(file);}};
    auto open=[&]()->FILE* {
        if(!TemporaryPath::fits({directory,"/dataset.ini"})) {errno=ENAMETOOLONG;return nullptr;}
        SdAccessScope access(observer);return std::fopen(TemporaryPath({directory,"/dataset.ini"}).c_str(),"rb");
    };
    std::unique_ptr<FILE,Close> file(open(),Close{observer});
    if(!file) {result.error=(errno==ENOENT || errno==ENOTDIR)?DatasetError::Missing:DatasetError::Data;return;}
    char buffer[config::DatasetLineBytes];
    char section_storage[config::DatasetLanguageBytes]{};
    std::string_view section; unsigned common=0,fields=0; bool common_seen=false,title_present=false;
    auto fail=[&](DatasetError e=DatasetError::Metadata) {
        result.error=e;
        datasetCopyText(text.title,text.title_capacity,{});datasetCopyText(text.description,text.description_capacity,{});
    };
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
        section={}; fields=0;
        std::string_view display_language;
        if(pass) {
            display_language=ui==UiLanguage::Japanese?"ja":"en";
            if(choices.find(display_language)==choices.count) display_language=default_language;
        }
        int status;
        while((status=line(file.get(),buffer,observer))>0) {
            std::string_view value(buffer);
            if(!validUtf8(value)) return fail();
            trim(value);if(value.empty() || value[0]=='#') continue;
            if(value[0]=='[') {
                if(value.back()!=']' || (!pass && !finish())) return fail();
                const auto next=value.substr(1,value.size()-2);
                if(next.size()>=sizeof(section_storage) || (next!="dataset" && !validDatasetLanguage(next))) return fail();
                std::memcpy(section_storage,next.data(),next.size());section_storage[next.size()]='\0';
                section={section_storage,next.size()};fields=0;
                if(section=="dataset") {if(!pass && common_seen) return fail();common_seen=true;}
                else if(!pass) {
                    if(choices.find(section)!=choices.count) return fail();
                    if(choices.count==choices.ids.size())return fail();
                    std::memcpy(choices.ids[choices.count++].data(),section_storage,sizeof(section_storage));
                }
                continue;
            }
            const auto equal=value.find('=');if(section.empty() || equal==std::string_view::npos) return fail();
            auto key=value.substr(0,equal);value.remove_prefix(equal+1);trim(key);trim(value);
            if(key.empty()) return fail();
            if(section=="dataset") {
                if(pass) continue;
                unsigned bit=0;
                if(key=="version") {bit=1;if(value!="1") return fail(DatasetError::Version);}
                else if(key=="map") {bit=2;if(!filename(value,".glc")) return fail();std::memcpy(result.map,value.data(),value.size());result.map[value.size()]=0;}
                else if(key=="default_language") {bit=4;if(!validDatasetLanguage(value)) return fail();std::memcpy(default_language,value.data(),value.size());default_language[value.size()]=0;}
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
                    if(section==display_language && key=="title") {title_present=!value.empty();datasetCopyText(text.title,text.title_capacity,value);}
                    if(section==display_language && key=="description")datasetCopyText(text.description,text.description_capacity,value);
                    if(section==result.language && key=="wordbook") {std::memcpy(result.wordbook,value.data(),value.size());result.wordbook[value.size()]=0;}
                    if(section==result.language && (key=="display_line1" || key=="display_line2")) {
                        if (!validPlaceFormat(value)) return fail();
                        result.display.specified = true;
                        if(!result.display.setLine(key=="display_line1"?0:1,value)) return fail();
                    }
                }
            }
        }
        if(status<0) return fail();
        if(!pass) {
            if(!finish() || common!=7 || !choices.count || choices.find(default_language)==choices.count)return fail();
            auto selected=choices.find(requested && *requested?requested:default_language);
            if(selected==choices.count)return fail(DatasetError::Missing);
            std::memcpy(result.language,choices.ids[selected].data(),sizeof(result.language));
            result.language_count=choices.count;result.language_index=selected;
        }
    }
    if(!title_present || !result.wordbook[0])return fail();
    if(languages)*languages=choices;
    return;
}

} // namespace geoputer
