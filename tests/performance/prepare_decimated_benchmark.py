# Prepare an isolated app-only decimate-then-extract hardware comparison.
from pathlib import Path
from benchmark_support import (prepare_workspace, inject_storage_probe, instrument_decode_reader,
                               install_background_latch, diagnostic_config, copy_device_helpers)
root=Path(__file__).resolve().parents[2]
work=prepare_workspace(root,'decimated-map',['DecimatedMapBenchmark.h', 'QuarterScaleBenchmark.h', 'ScaledBoundaryPrototype.h', 'DecodeProfile.h'])
instrument_decode_reader(work)
install_background_latch(work,'decimation_background_ms')
p=work/'application/task/storage/StorageTask.cpp';s=p.read_text()
s=inject_storage_probe(s,'DecimatedMapBenchmark.h','''
        static bool decimation_ran=false;
        const auto background=benchmark::decimation_background_ms.load(std::memory_order_relaxed);
        if(!decimation_ran && !usb_mode_ && initialized_ && !hasJob() && !has_completion_ && !has_return_ &&
           state_.card_state==CardState::Mounted && place_lookup_.cacheStats().valid && place_request_.fix.valid &&
           background && taskTimeMs()-background>=2000) {
            decimation_ran=true;
            Console.printf("[DECIM-DISPLAY] background_ms=%lu now_ms=%lu timeout_s=%u\\n",static_cast<unsigned long>(background),
                           static_cast<unsigned long>(taskTimeMs()),ScreenTimeouts[state_.settings.screen_timeout]);
            Console.printf("[SD-CLOCK] requested=%lu actual=%lu\\n",static_cast<unsigned long>(config::SdSpiHz),
                           static_cast<unsigned long>(spiClockDivToFrequency(spi_.getClockDivider())));
            try {
                benchmark::runDecimatedMapBenchmark(DatasetCatalog{}.directory(state_.settings.dataset),state_.settings.wordbook_language,drive_,this,
                    [](const char* format,auto... values){Console.printf(format,values...);});
            }catch(const std::exception& error){Console.printf("[DECIM-ERROR] %s\\n",error.what());}
        }
''');p.write_text(s)
s=diagnostic_config(root,work)
(root/'.pio/decimated-map.ini').write_text(s)
copy_device_helpers(root,work)
print('Prepared',work)
