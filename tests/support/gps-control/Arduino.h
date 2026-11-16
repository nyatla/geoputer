#pragma once
struct HostESP { void restart(){} };
inline HostESP ESP;
inline unsigned host_cpu_mhz=240;
inline unsigned getCpuFrequencyMhz(){return host_cpu_mhz;}
inline bool setCpuFrequencyMhz(unsigned mhz){host_cpu_mhz=mhz;return true;}
