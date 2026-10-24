"""Verify and summarize the paired RAW/SD diagnostic's serial log."""
from pathlib import Path
import argparse,json,re,statistics

def records(log,tag):
    rows=[]
    for line in log.splitlines():
        if tag not in line:continue
        row={}
        for k,v in re.findall(r'(\w+)=([^\s]+)',line.split(tag,1)[1]):
            row[k]=int(v) if v.isdigit() else v
        rows.append(row)
    return rows

def summarize_raw(rows):
    assert len(rows)==240
    assert {(r['kind'],r['profile'],r['round'],r['backend']) for r in rows}=={
        (kind,profile,round_number,backend) for kind in range(5) for profile in range(4)
        for round_number in range(1,7) for backend in ('before','rows')}
    summary=[]
    for kind in range(5):
        for profile in range(4):
            cases=[r for r in rows if r['kind']==kind and r['profile']==profile]
            assert len({r['hash'] for r in cases})==1
            assert len({r['bytes'] for r in cases})==1
            assert all(r['repeats']==8 and r['cells']==8 and r['total_us']>0 for r in cases)
            times={backend:statistics.median(r['total_us']/r['repeats'] for r in cases if r['backend']==backend)
                   for backend in ('before','rows')}
            summary.append(dict(kind=kind,profile=profile,before_us=times['before'],rows_us=times['rows'],
                change_pct=100*(times['rows']/times['before']-1),hash=cases[0]['hash']))
    return summary

def summarize_pipeline(cases,ios,census):
    assert len(cases)==36 and len(ios)==72 and len(census)==36
    assert {(r['backend'],r['scale'],r['round']) for r in cases}=={
        (b,s,n) for b in ('before','rows') for s in (1,2,4) for n in range(1,7)}
    summary=[]
    for scale in (1,2,4):
        selected=[r for r in cases if r['scale']==scale]
        assert len({r['hash'] for r in selected})==1
        counts={tuple(r[k] for k in ('cells','pixels','target')) for r in census if r['scale']==scale}
        assert len(counts)==1
        for file in ('map','index'):
            io=[r for r in ios if r['scale']==scale and r['file']==file]
            assert len(io)==12 and len({tuple(r[k] for k in ('bytes','reads','seeks','opens','closes')) for r in io})==1
            assert all(r['opens']==r['closes'] for r in io)
        assert all(r['file_us']+r['decode_us']+r['index_other_us']==r['readrect_us'] for r in selected)
        med={b:{key:statistics.median(r[key] for r in selected if r['backend']==b)
                for key in ('total_us','readrect_us','file_us','decode_us','index_other_us','extract_us','heap_min')}
             for b in ('before','rows')}
        summary.append(dict(scale=scale,median=med,change_pct=100*(med['rows']['total_us']/med['before']['total_us']-1),
                            raw_census=dict(zip(('cells','pixels','target'),next(iter(counts)))),hash=selected[0]['hash']))
    return summary

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();log=a.log.read_text(encoding='utf-8',errors='replace')
    assert '[RAW-CELL-DONE] cases=240' in log and '[PIPE-DONE] cases=36' in log
    assert '[PIPE-DISPLAY] mode=background' in log
    assert not any(error in log for error in ('[PIPE-ERROR]','Guru Meditation','CORRUPT HEAP','stack canary','abort()'))
    raw=records(log,'[RAW-CELL-CASE]');cases=records(log,'[PIPE-CASE]');io=records(log,'[PIPE-IO]');census=records(log,'[RAW-PIPE]')
    result=dict(raw_cases=raw,raw_summary=summarize_raw(raw),pipeline_cases=cases,pipeline_io=io,
                pipeline_census=census,pipeline_summary=summarize_pipeline(cases,io,census),
                display=records(log,'[PIPE-DISPLAY]'),clock=records(log,'[SD-CLOCK]'),dataset=records(log,'[PIPE-BEGIN]'))
    a.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    for case in result['raw_summary']:
        if case['profile']==0:print('RAW full',json.dumps(case))
    for case in result['pipeline_summary']:print('MAP',json.dumps(case))
