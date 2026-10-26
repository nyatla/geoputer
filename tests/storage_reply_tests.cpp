#include "task/storage/StorageReplies.h"
#include "utils/SyncClient.h"
#include <cstring>
#include <stdexcept>

using namespace geoputer;
namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
}
void testStorageReplies() {
    static_assert(sizeof(StorageReplies)<sizeof(ReturnMessage)+sizeof(AppNotification), "one shared reply area");
    Queue<ReturnMessage,1> results;Queue<AppNotification,8> notifications;
    results.initialize();notifications.initialize();StorageReplies replies;
    ReturnMessage queued{};queued.request_id=7;results.send(queued);
    StorageRequest request{};request.kind=MessageKind::GetStorageStatus;request.request_id=101;
    auto& result=replies.beginReturn();prepareSyncReturn(result,TaskId::Storage,request,1,OperationStatus::Ok);
    result.payload.result.storage.error=-123;
    const void* area=&result;
    replies.sendReturn(results);replies.notifyCompletion(notifications);
    check(replies.returnPending(),"full return queue must retain the same result");
    results.receive(queued);check(queued.request_id==7,"return retry cannot overwrite the queued value");
    replies.sendReturn(results);check(replies.idle(),"accepted copy releases shared area");
    AppNotification sentinel{};sentinel.sequence=9;
    for(unsigned i=0;i<8;++i)check(notifications.send(sentinel),"fill notification queue");
    auto& completion=replies.beginCompletion();check(area==&completion,"return and completion share storage");
    completion.kind=MessageKind::ReadCompleted;completion.request_id=202;
    completion.payload.storage_completion.read_kind=StorageReadKind::LogBrowse;
    auto& read=completion.payload.storage_completion.logs;read.count=10;
    std::memset(read.data.records,0x5a,sizeof(read.data.records));
    replies.notifyCompletion(notifications);replies.sendReturn(results);
    check(replies.completionPending(),"full notification queue must retain the complete payload");
    check(results.receive(queued) && queued.request_id==101 && queued.payload.result.storage.error==-123,
          "reusing producer area cannot change a return already queued");
    AppNotification event{};notifications.receive(event);replies.notifyCompletion(notifications);
    check(replies.idle(),"notification retry releases the area exactly once");
    for(unsigned i=0;i<7;++i)check(notifications.receive(event) && event.sequence==9,"existing notifications remain ordered");
    check(notifications.receive(event) && event.request_id==202 && event.payload.storage_completion.logs.count==10 &&
          event.payload.storage_completion.logs.data.records[9][31]==0x5a,"large completion survives backpressure");
    for(unsigned i=0;i<100;++i) {
        request.kind=MessageKind::GetStorageStatus;request.request_id=1000+i;
        auto& next=replies.beginReturn();prepareSyncReturn(next,TaskId::Storage,request,i,OperationStatus::Ok);
        next.payload.result.storage.log.saved=i;replies.sendReturn(results);
        auto& done=replies.beginCompletion();done.request_id=2000+i;replies.notifyCompletion(notifications);
        check(results.receive(queued) && queued.request_id==1000+i && queued.payload.result.storage.log.saved==i,
              "Storage snapshot return survives subsequent completion construction");
        check(notifications.receive(event) && event.request_id==2000+i && replies.idle(),"repeated member transitions release state");
    }
}
