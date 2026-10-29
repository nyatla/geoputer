#pragma once
#include "support/StorageFixture.h"
// Physical-I/O tracing belongs to the host fake FatFs, never the app Reader.
namespace traced_sd {
struct Trace {std::vector<size_t> opens,reads;Trace* peer=nullptr;unsigned live=0,peak=0;size_t fail_at=SIZE_MAX;};
class File {
    const std::vector<uint8_t>& bytes_;Trace local_;Trace& trace_;
    test_storage::File file_;
    static inline std::vector<File*> registry_;
    static void before(const char* path){for(auto* f:registry_)if(f->file_.nativePath()==fatfs_test::native(path))f->file_.replace(f->bytes_);}
    static void opened(FIL* p,const char* path){for(auto* f:registry_)if(f->file_.nativePath()==fatfs_test::native(path)){
        if(f->trace_.peer && f->trace_.peer->live)throw std::runtime_error("index and map overlap");
        p->context=f;p->first_seek=true;f->trace_.peak=std::max(f->trace_.peak,++f->trace_.live);}}
    static void closed(FIL* p){if(p->context)--static_cast<File*>(p->context)->trace_.live;}
    static void sought(FIL* p,size_t offset){if(p->context&&p->first_seek){static_cast<File*>(p->context)->trace_.opens.push_back(offset);p->first_seek=false;}}
    static bool reading(FIL* p,unsigned count){if(!p->context)return true;auto& t=static_cast<File*>(p->context)->trace_;
        for(unsigned i=0;i<count;++i){if(p->fptr+i==t.fail_at)return false;t.reads.push_back(p->fptr+i);}return true;}
    void bind(){registry_.push_back(this);fatfs_test::before_open=&before;fatfs_test::on_open=&opened;fatfs_test::on_close=&closed;fatfs_test::on_seek=&sought;fatfs_test::before_read=&reading;}
public:
    explicit File(const std::vector<uint8_t>& b):bytes_(b),trace_(local_),file_(b,{1,0}){bind();}
    File(const std::vector<uint8_t>& b,Trace& t):bytes_(b),trace_(t),file_(b,{1,0}){bind();}
    ~File(){registry_.erase(std::find(registry_.begin(),registry_.end(),this));}
    operator geoputer::ScopedReaderSource()const{return geoputer::ScopedReaderSource(file_.source());}
};
}
