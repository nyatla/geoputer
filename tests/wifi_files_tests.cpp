#include "wifi/WifiFiles.h"
#include <SD_FatFs.h>
#include <vector>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <string>
using namespace geoputer::wifi;
namespace {
std::vector<uint8_t> disk(64*1024*1024);
bool fail_sync=false;
SD_FATFS_Device device{nullptr,
 [](void*)->SD_FATFS_DSTATUS{return 0;},[](void*)->SD_FATFS_DSTATUS{return 0;},
 [](void*,uint8_t* out,uint32_t sector,unsigned n)->SD_FATFS_DRESULT{
  if(uint64_t(sector)*512+uint64_t(n)*512>disk.size())return SD_FATFS_RES_PARERR;
  std::memcpy(out,disk.data()+sector*512,n*512);return SD_FATFS_RES_OK;},
 [](void*,const uint8_t* in,uint32_t sector,unsigned n)->SD_FATFS_DRESULT{
  if(uint64_t(sector)*512+uint64_t(n)*512>disk.size())return SD_FATFS_RES_PARERR;
  std::memcpy(disk.data()+sector*512,in,n*512);return SD_FATFS_RES_OK;},
 [](void*,uint8_t command,void* out)->SD_FATFS_DRESULT {
  switch(command){case SD_FATFS_CTRL_SYNC:return fail_sync?SD_FATFS_RES_ERROR:SD_FATFS_RES_OK;
  case SD_FATFS_GET_SECTOR_COUNT:*static_cast<uint32_t*>(out)=disk.size()/512;return SD_FATFS_RES_OK;
  case SD_FATFS_GET_SECTOR_SIZE:*static_cast<uint16_t*>(out)=512;return SD_FATFS_RES_OK;
  case SD_FATFS_GET_BLOCK_SIZE:*static_cast<uint32_t*>(out)=1;return SD_FATFS_RES_OK;default:return SD_FATFS_RES_PARERR;}},
 [](void*,uint32_t){return 1;},[](void*){}};
void save(Files& files,const char* path,const std::string& value,bool replace=false,const char* version=nullptr,bool text=false){
 assert(files.beginWrite(path,value.size(),replace,version,text));assert(files.write(value.data(),value.size()));assert(files.finish());}
std::string load(Files& files,const char* path){assert(files.openRead(path));std::string data;unsigned n=0;do{assert(files.read(files.buffer(),4096,n));data.append(reinterpret_cast<char*>(files.buffer()),n);}while(n);assert(files.close());return data;}
}
int main(){
 assert(validPath("/")&&validPath("/geoputer/dataset/01/日本語.ini"));
 for(const char* path:{"","//","/../x","/a/../x","/a/./x","/x/","/x.","/x ","/x\\y","/.RGP-wifi/upload","/a/.rgp-x","/\xC0\xAF"})assert(!validPath(path));
 char path[256];assert(decodePath("%2Fdataset%2F01%2F%E6%97%A5.ini",path,sizeof(path)));assert(!decodePath("/%00x",path,sizeof(path)));assert(!decodePath("/%2e%2e/x",path,sizeof(path)));assert(!decodePath("/%zz",path,sizeof(path)));assert(decodePath("/%252e%252e",path,sizeof(path)));assert(std::strcmp(path,"/%2e%2e")==0);
 Utf8Text utf8;assert(utf8.accept("\xE6",1));assert(!utf8.complete());assert(utf8.accept("\x97\xA5",2)&&utf8.complete());Utf8Text broken;assert(!broken.accept("a\0b",3));Utf8Text surrogate;assert(!surrogate.accept("\xED\xA0\x80",3));
 assert(sd_fatfs_bind_device(0,&device)==SD_FATFS_FR_OK);uint8_t work[4096];
 assert(sd_fatfs_f_mkfs("0:",SD_FATFS_FM_FAT32|SD_FATFS_FM_SFD,512,work,sizeof(work))==SD_FATFS_FR_OK);
 SD_FATFS_FATFS fs{};assert(sd_fatfs_f_mount(&fs,"0:",1)==SD_FATFS_FR_OK);
 Files files;files.attach(0);assert(files.recover());
 SD_FATFS_FILINFO hidden{};assert(sd_fatfs_f_stat("0:/geoputer/.rgp-wifi",&hidden)==SD_FATFS_FR_OK);
 if(hidden.altname[0]){const auto alias=std::string("/geoputer/")+hidden.altname;char out[260];assert(!files.native(alias.c_str(),out,sizeof(out)));}
 assert(files.mkdir("/geoputer/dataset"));assert(files.mkdir("/geoputer/dataset/01"));
 const char* name="/geoputer/dataset/01/日本語.ini";save(files,name,"日本語\nline 2\r\n");assert(load(files,name)=="日本語\nline 2\r\n");
 char version[17];assert(files.inspectText(name,version));assert(!files.beginWrite(name,3,false));assert(!files.beginWrite(name,3,true,"0000000000000000",true));
 assert(files.beginWrite(name,10,true));assert(files.write("short",5));assert(!files.finish());files.abort();assert(load(files,name)=="日本語\nline 2\r\n");
 save(files,name,"changed",true,version,true);assert(load(files,name)=="changed");
 assert(files.beginWrite(name,3,true,nullptr,true));assert(!files.write("a\0b",3));files.abort();assert(load(files,name)=="changed");
 std::string payload(100003,'x');for(size_t i=0;i<payload.size();++i)payload[i]=char(i*137);
 assert(files.beginWrite("/large.glc",payload.size(),false));for(size_t i=0;i<payload.size();i+=3333)assert(files.write(payload.data()+i,std::min<size_t>(3333,payload.size()-i)));assert(files.finish());assert(load(files,"/large.glc")==payload);assert(!files.inspectText("/large.glc",version));
 assert(files.beginWrite(name,3,true));assert(files.write("new",3));fail_sync=true;assert(!files.finish());fail_sync=false;files.abort();assert(load(files,name)=="changed");
 // Inject a power loss after journaling and after moving the old target aside.
 assert(files.beginWrite(name,3,true));assert(files.write("new",3));assert(f_sync(&files.file_)==FR_OK);assert(files.close());files.writing_=false;assert(files.journal());
 assert(sd_fatfs_f_rename("0:/geoputer/dataset/01/日本語.ini","0:/geoputer/.rgp-wifi/backup")==SD_FATFS_FR_OK);
 {Files rebooted;rebooted.attach(0);assert(rebooted.recover());assert(load(rebooted,name)=="changed");}
 // If the new name was published before reset, recovery retains the new file.
 assert(files.beginWrite(name,3,true));assert(files.write("new",3));assert(f_sync(&files.file_)==FR_OK);assert(files.close());files.writing_=false;assert(files.journal());
 assert(sd_fatfs_f_rename("0:/geoputer/dataset/01/日本語.ini","0:/geoputer/.rgp-wifi/backup")==SD_FATFS_FR_OK);
 assert(sd_fatfs_f_rename("0:/geoputer/.rgp-wifi/upload","0:/geoputer/dataset/01/日本語.ini")==SD_FATFS_FR_OK);
 assert(files.recover());assert(load(files,name)=="new");
 assert(files.mkdir("/geoputer/dataset/01/child"));save(files,"/geoputer/dataset/01/child/test.txt","ok");
 assert(!files.remove("/"));assert(!files.remove("/geoputer"));assert(!files.remove("/GEOPUTER"));
 assert(!files.remove("/geoputer/dataset/01",[](void*){return true;}));
 assert(files.remove("/geoputer/dataset"));SD_FATFS_FILINFO info{};assert(sd_fatfs_f_stat("0:/geoputer/dataset",&info)==SD_FATFS_FR_NO_FILE);
 std::puts("PASS Wi-Fi files: real FAT32/UTF-8, traversal, chunked binary writes, stale edit rejection, abort, sync failure, overwrite recovery, recursive deletion/cancel");
}
