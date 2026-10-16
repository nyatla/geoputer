"""Legacy discrete-event model of the Draft 0.7 GPS/App queue protocol.

This models the superseded timeout-ends-request contract, not the current
persistent-result-wait contract. Simulation work has been stopped.

No hardware/RTOS/GIS dependencies. Run with Python 3 from any directory.
This checks message routing, bounded waits and queue capacity; it does not
simulate the GNSS hardware protocol or prove real-time performance.
"""
from collections import deque
from dataclasses import dataclass
import heapq
import json


@dataclass
class Request:
    id: int
    kind: str
    target: str | None = None
    delay: int = 0
    fail: bool = False
    lose_reply: bool = False
    deadline: int = 0


class Simulation:
    def __init__(self, app_capacity=8, gps_capacity=4, gps_pending_capacity=2,
                 app_dispatch_delay=0, reject_expired_returns=False):
        self.now = 0
        self.events = []
        self.order = 0
        self.app_queue = deque()
        self.return_queue = deque()
        self.gps_queue = deque()
        self.gps_pending = deque()
        self.app_capacity = app_capacity
        self.gps_capacity = gps_capacity
        self.gps_pending_capacity = gps_pending_capacity
        self.app_dispatch_delay = app_dispatch_delay
        self.reject_expired_returns = reject_expired_returns
        self.expired_returns = 0
        self.gps_busy = False
        self.current = None
        self.next_id = 1
        self.initialized = False
        self.state = 'PAUSE'
        self.trace = []
        self.results = []
        self.handled = []
        self.executed = []
        self.stale = []
        self.reply_drops = 0
        self.sample_drops = 0
        self.peak_app = 0
        self.peak_return = 0

    def at(self, time, callback, priority=10):
        self.order += 1
        heapq.heappush(self.events, (time, priority, self.order, callback))

    def log(self, event, **fields):
        self.trace.append({'ms': self.now, 'event': event, **fields})

    def run(self):
        while self.events:
            self.now, _, _, callback = heapq.heappop(self.events)
            callback()

    def start(self, kind, timeout, *, target=None, delay=0, fail=False,
              lose_reply=False):
        assert self.current is None, 'Only one synchronous call is allowed'
        while self.return_queue:
            self.discard_stale(self.return_queue.popleft())
        request = Request(self.next_id, kind, target, delay, fail, lose_reply, self.now + timeout)
        self.next_id += 1
        self.current = {'request': request, 'deadline': self.now + timeout,
                        'enqueued': False}
        self.log('sync_start', id=request.id, kind=kind)
        self.try_enqueue()
        self.at(self.now + timeout, lambda: self.expire(request.id), priority=20)
        return request.id

    def try_enqueue(self):
        if self.current is None or self.current['enqueued']:
            return
        if len(self.gps_queue) < self.gps_capacity:
            request = self.current['request']
            self.gps_queue.append(request)
            self.current['enqueued'] = True
            self.log('request_enqueued', id=request.id)
            self.at(self.now, self.gps_step, priority=0)
        elif self.now < self.current['deadline']:
            self.at(self.now + 1, self.try_enqueue)

    def gps_step(self):
        if self.gps_busy or not self.gps_queue:
            return
        request = self.gps_queue.popleft()
        self.gps_busy = True
        self.log('gps_begin', id=request.id, kind=request.kind)
        self.at(self.now + request.delay,
                lambda: self.gps_finish(request), priority=0)

    def gps_finish(self, request):
        self.executed.append(request)
        success = not request.fail
        if success and request.kind == 'InitializeGps':
            self.initialized = True
            self.state = 'PAUSE'
        elif success and request.kind == 'SetGnssState':
            self.state = request.target
        elif request.kind not in ('InitializeGps', 'SetGnssState', 'GetGpsStatus'):
            success = False
        reply = {'source': 'GPS', 'id': request.id, 'kind': request.kind,
                 'success': success, 'state': self.state,
                 'initialized': self.initialized, 'observed_at': self.now}
        self.log('gps_finish', **reply)
        if self.reject_expired_returns and self.now > request.deadline:
            self.expired_returns += 1
            self.log('expired_return_suppressed', id=request.id)
        elif request.lose_reply:
            self.reply_drops += 1
            self.log('reply_lost', id=request.id)
        else:
            self.send_reply(reply)
        self.gps_busy = False
        self.at(self.now, self.gps_step, priority=0)

    def send_reply(self, reply):
        if self.return_queue:
            self.reply_drops += 1
            self.log('return_queue_full', id=reply['id'])
            return False
        self.return_queue.append(reply)
        self.peak_return = max(self.peak_return, len(self.return_queue))
        self.log('reply_enqueued', id=reply['id'])
        self.at(self.now + self.app_dispatch_delay, self.receive_reply)
        return True

    def discard_stale(self, reply):
        self.stale.append(reply)
        self.log('stale_discarded', id=reply['id'])

    def receive_reply(self):
        if self.current is None:
            return
        request = self.current['request']
        while self.return_queue:
            reply = self.return_queue.popleft()
            if (reply['source'], reply['id'], reply['kind']) != (
                    'GPS', request.id, request.kind):
                self.discard_stale(reply)
                continue
            assert self.now <= self.current['deadline']
            self.complete('Returned', reply)
            return

    def expire(self, request_id):
        if self.current is None or self.current['request'].id != request_id:
            return
        result = 'ReturnTimeout' if self.current['enqueued'] else 'QueueTimeout'
        self.complete(result)

    def complete(self, outcome, reply=None):
        request = self.current['request']
        self.results.append({'id': request.id, 'kind': request.kind,
                             'outcome': outcome, 'reply': reply})
        self.log('sync_end', id=request.id, outcome=outcome)
        self.current = None
        self.drain_notifications()

    def notify(self, kind, sequence):
        message = {'kind': kind, 'seq': sequence, 'produced_at': self.now}
        if len(self.app_queue) < self.app_capacity:
            self.app_queue.append(message)
            self.peak_app = max(self.peak_app, len(self.app_queue))
            self.log('notification_enqueued', kind=kind, seq=sequence)
            self.drain_notifications()
        elif kind == 'GpsSampleReady' and len(self.gps_pending) < self.gps_pending_capacity:
            self.gps_pending.append(message)
            self.log('sample_held', seq=sequence)
        elif kind == 'GpsSampleReady':
            self.sample_drops += 1
            self.log('sample_dropped', seq=sequence)
        else:
            raise AssertionError('Non-sample notification overflow not modeled')

    def drain_notifications(self):
        if self.current is not None:
            return
        while self.app_queue or self.gps_pending:
            while self.app_queue:
                message = self.app_queue.popleft()
                self.handled.append(message)
                self.log('notification_handled', kind=message['kind'], seq=message['seq'])
            while self.gps_pending and len(self.app_queue) < self.app_capacity:
                self.app_queue.append(self.gps_pending.popleft())

    def summary(self, name):
        assert self.peak_return <= 1
        assert self.current is None
        for result in self.results:
            if result['outcome'] == 'Returned':
                assert result['id'] == result['reply']['id']
                assert result['kind'] == result['reply']['kind']
        return {'case': name, 'results': self.results,
                'notifications_handled': len(self.handled),
                'stale_replies': len(self.stale), 'reply_drops': self.reply_drops,
                'expired_returns': self.expired_returns,
                'sample_drops': self.sample_drops,
                'peak_app_queue': self.peak_app, 'peak_return_queue': self.peak_return}


def cases():
    runs = []
    s = Simulation()
    s.at(0, lambda: s.start('InitializeGps', 100, delay=20))
    s.at(30, lambda: s.start('SetGnssState', 100, target='ACTIVE', delay=10))
    s.at(50, lambda: s.notify('GpsSampleReady', 1), priority=0)
    s.run()
    assert [r['outcome'] for r in s.results] == ['Returned', 'Returned']
    assert s.state == 'ACTIVE' and len(s.handled) == 1
    runs.append(('normal_init_active_fix', s))

    s = Simulation()
    s.initialized = True
    s.state = 'ACTIVE'
    s.at(0, lambda: s.start('GetGpsStatus', 100, delay=80))
    for i, kind in enumerate(('GpsSampleReady', 'UiActionRequested', 'StorageStateChanged'), 1):
        s.at(i*10, lambda i=i, kind=kind: s.notify(kind, i), priority=0)
    s.at(70, lambda: (len(s.app_queue) == 3 and not s.handled) or
         (_ for _ in ()).throw(AssertionError('Notifications must remain queued')))
    s.run()
    assert len(s.handled) == 3 and s.results[0]['outcome'] == 'Returned'
    runs.append(('normal_notifications_during_sync', s))

    s = Simulation()
    s.initialized = True
    s.at(0, lambda: s.start('SetGnssState', 30, target='ACTIVE', delay=10, lose_reply=True))
    s.at(50, lambda: s.start('GetGpsStatus', 30))
    s.run()
    assert s.results[0]['outcome'] == 'ReturnTimeout'
    assert s.results[1]['reply']['state'] == 'ACTIVE'
    assert sum(r.kind == 'SetGnssState' for r in s.executed) == 1
    runs.append(('lost_reply_status_recovers_actual_state', s))

    s = Simulation()
    s.initialized = True
    s.at(0, lambda: s.start('SetGnssState', 30, target='ACTIVE', delay=10, fail=True, lose_reply=True))
    for i in range(10):
        s.at(50+i*40, lambda: s.start('GetGpsStatus', 30))
    s.run()
    assert all(r['reply']['state'] == 'PAUSE' for r in s.results[1:])
    assert sum(r.kind == 'SetGnssState' for r in s.executed) == 1
    runs.append(('failed_change_status_queries_do_not_apply_target', s))

    # GPS runs before App at the same timestamp. The timed-out command finishes,
    # then a fast status request replies before App has consumed the old reply.
    s = Simulation()
    s.at(0, lambda: s.start('InitializeGps', 100, delay=120))
    s.at(101, lambda: s.start('GetGpsStatus', 100))
    s.at(250, lambda: s.start('GetGpsStatus', 100))
    s.run()
    assert [r['outcome'] for r in s.results] == ['ReturnTimeout', 'ReturnTimeout', 'Returned']
    assert s.stale and s.reply_drops == 1
    runs.append(('late_reply_fills_single_return_slot', s))

    # Comparison only: request deadlines at the return sender are NOT currently
    # required by the design. This does not prove a preemption-safe implementation.
    s = Simulation(reject_expired_returns=True)
    s.at(0, lambda: s.start('InitializeGps', 100, delay=120))
    s.at(101, lambda: s.start('GetGpsStatus', 100))
    s.at(250, lambda: s.start('GetGpsStatus', 100))
    s.run()
    assert [r['outcome'] for r in s.results] == ['ReturnTimeout', 'Returned', 'Returned']
    assert s.reply_drops == 0 and s.expired_returns == 1
    runs.append(('comparison_sender_rejects_expired_return', s))

    s = Simulation(app_capacity=4, gps_pending_capacity=2)
    s.initialized = True
    s.state = 'ACTIVE'
    s.at(0, lambda: s.start('GetGpsStatus', 10000, delay=9000))
    for i in range(1,10):
        s.at(i*1000, lambda i=i: s.notify('GpsSampleReady', i), priority=0)
    s.run()
    assert len(s.handled) == 6 and s.sample_drops == 3
    runs.append(('long_sync_overflows_notification_storage', s))

    s = Simulation(gps_capacity=1)
    s.gps_busy = True
    s.gps_queue.append(Request(999, 'GetGpsStatus'))
    s.at(0, lambda: s.start('InitializeGps', 20))
    s.run()
    assert s.results[0]['outcome'] == 'QueueTimeout' and not s.executed
    runs.append(('request_queue_full_no_execution', s))

    s = Simulation()
    s.at(0, lambda: s.start('GetGpsStatus', 50, delay=30))
    s.at(10, lambda: s.send_reply({'source': 'GPS', 'id': 999,
                                  'kind': 'InitializeGps', 'success': True,
                                  'state': 'ACTIVE'}), priority=0)
    s.run()
    assert len(s.stale) == 1 and s.results[0]['reply']['state'] == 'PAUSE'
    runs.append(('wrong_reply_cannot_complete_current_request', s))
    return runs


def schedule_sweep():
    tested = 0
    collisions = 0
    for old_delay in range(90, 151, 5):
        for query_delay in (0, 1, 5, 20):
            for dispatch_delay in (0, 1, 5):
                s = Simulation(app_dispatch_delay=dispatch_delay)
                s.at(0, lambda s=s, d=old_delay: s.start('InitializeGps', 100, delay=d))
                s.at(110, lambda s=s, d=query_delay: s.start('GetGpsStatus', 100, delay=d))
                s.at(260, lambda s=s: s.start('GetGpsStatus', 100))
                s.run()
                s.summary('sweep')
                assert s.results[-1]['outcome'] == 'Returned'
                tested += 1
                collisions += bool(s.reply_drops)
    return {'schedules': tested, 'schedules_with_reply_collision': collisions,
            'eventual_status_recovery': tested}


if __name__ == '__main__':
    from pathlib import Path
    output = Path(__file__).with_name('gps_app_simulation_results.json')
    runs = cases()
    result = {'scope': 'Discrete-event queue model; assumed timings/capacities, not hardware measurements',
              'cases': [s.summary(name) for name, s in runs],
              'schedule_sweep': schedule_sweep(),
              'traces': {name: s.trace for name, s in runs}}
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n')
    for case in result['cases']:
        print(json.dumps({k:v for k,v in case.items() if k != 'results'}, ensure_ascii=False))
    print(json.dumps(result['schedule_sweep'], ensure_ascii=False))
    print(f'Full traces: {output}')
