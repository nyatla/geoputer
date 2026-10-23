// Diagnostic only; requires the internal helpers in DatasetIndex.cpp.
namespace geoputer::benchmark {
IndexGenerationMeasurement measureIndexGeneration(const std::string& directory,
    const DatasetMetadata& metadata,uint8_t drive,StorageObserver* observer,void (*stage)(const char*)) {
    const auto scratch=directory+"/_index_timing.tmp";
    // Never overwrite or delete a file that predates this measurement.
    require(!exists(drive,scratch));
    bool created=false;
    try {
        IndexGenerationMeasurement result;
        const auto map=directory+"/"+metadata.map,book=directory+"/"+metadata.wordbook;
        auto start=monotonicMicros();
        const auto map_stamp=fingerprint(drive,map,"GLCH",observer),book_stamp=fingerprint(drive,book,"GW00",observer);
        result.prepare_us=monotonicMicros()-start;
        result.map_bytes=map_stamp.bytes;result.book_bytes=book_stamp.bytes;
        if(stage)stage("generate");
        start=monotonicMicros();created=true;
        generate(drive,scratch,directory,metadata,map_stamp,book_stamp,observer);
        result.generate_us=monotonicMicros()-start;
        result.index_bytes=statFile(drive,scratch).bytes;
        DatasetIndex index(drive,observer);
        if(stage)stage("verify");
        for(unsigned i=0;i<3;++i) {
            start=monotonicMicros();const bool ok=index.validate(scratch);
            result.verify_us[i]=monotonicMicros()-start;require(ok);
        }
        start=monotonicMicros();
        require(map_stamp==fingerprint(drive,map,"GLCH",observer) && book_stamp==fingerprint(drive,book,"GW00",observer));
        result.source_check_us=monotonicMicros()-start;
        {SdAccessScope access(observer);require(removeFile(drive,scratch));}created=false;
        return result;
    } catch(...) {
        if(created) {SdAccessScope access(observer);(void)removeFile(drive,scratch);}
        throw;
    }
}
}
