#include "MapIndexBenchmark.h"
#include <filesystem>
#include <cstdio>
#include <stdexcept>
int main(int argc,char** argv) {
    if(argc!=4)return 2;
    try {
        namespace fs=std::filesystem;const fs::path dir=argv[1];fs::create_directories(dir);
        geoputer::DatasetMetadata metadata;metadata.map=fs::path(argv[2]).filename().string();metadata.wordbook=fs::path(argv[3]).filename().string();metadata.language="ja";
        fs::copy_file(argv[2],dir/metadata.map,fs::copy_options::overwrite_existing);fs::copy_file(argv[3],dir/metadata.wordbook,fs::copy_options::overwrite_existing);
        geoputer::benchmark::runMapIndexBenchmark(dir.string(),metadata,0,[](const char* f,auto... args){if constexpr(sizeof...(args))std::printf(f,args...);else std::fputs(f,stdout);},[](){return 0u;});
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}
}
