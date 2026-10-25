#include "task/gps/GpsTask.h"
#include "utils/Console.h"
#include <stdexcept>
#include <cstdio>
using namespace geoputer;
namespace geoputer {USBCDC Console;}
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){
    Queue<AppNotification,8> notifications;Queue<ReturnMessage,1> results;
    notifications.initialize();results.initialize();GpsTask gps(notifications,results);
    GpsRequest request{};request.source=TaskId::Application;request.request_id=1;
    request.kind=MessageKind::StartGpsReception;gps.handle(request);gps.sendReturn();
    ReturnMessage reply{};check(results.receive(reply) && reply.payload.result.status==OperationStatus::Ok,"start reply");
    const auto initial=reply.payload.result.gps;
    const char* sentence="$GNRMC,010001.000,A,3540.98460,N,14001.63164,E,2.0,140.0,251026,,,A*76\r\n";
    // Repeated Start is idempotent: it cannot reset parser state or send standby commands.
    for(const char* p=sentence;*p;++p)gps.receiver_.feed(*p,1000);
    gps.receiver_.poll(1100);
    const auto received=gps.receiver_.snapshot();
    check(received.received && received.fix.valid && received.sample_seq>initial.sample_seq,"valid receiving fixture");
    ++request.request_id;gps.handle(request);gps.sendReturn();
    check(results.receive(reply) && reply.payload.result.gps.revision==received.revision && reply.payload.result.gps.fix.valid,"repeated start preserves current fix");
    check(gps.serial_.sent.empty(),"GPS control must not transmit standby or configuration commands");
    // A full return queue retains the completed query while reception can continue.
    results.send(reply);request.kind=MessageKind::GetGpsStatus;++request.request_id;gps.handle(request);
    gps.sendReturn();check(gps.has_return_,"full return queue retains result");
    gps.receiver_.poll(5000);check(!gps.receiver_.snapshot().fix.valid,"receiver remains serviced while result waits");
    results.receive(reply);gps.sendReturn();check(!gps.has_return_ && results.receive(reply) && reply.payload.result.gps.fix.valid,"late return is an immutable snapshot consumed once");
    check(gps.serial_.sent.empty(),"queries do not transmit GNSS commands");
    std::puts("PASS: GPS continuous reception, idempotent start, zero standby TX and late returns");
}
