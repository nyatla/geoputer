"""Validate and summarize the app-only decimate-before-edge hardware comparison."""
from pathlib import Path
import argparse,json,statistics
from summarize_raw_device import records

def summarize(log):
    assert '[DECIM-DONE] cases=48 sampled_images_equal=1 center_equal=1' in log
    assert '[DECIM-DISPLAY]' in log
    assert not any(t in log for t in ('[DECIM-ERROR]','Guru Meditation','CORRUPT HEAP','stack canary','abort()'))
    cases=records(log,'[DECIM-CASE]');io=records(log,'[DECIM-IO]');plans=records(log,'[DECIM-PLAN]')
    keys={(scale,mode,n) for scale in (1,2,4) for mode in (('single','strips16') if scale==1 else ('single','strips16','old_edges16')) for n in range(1,7)}
    assert len(cases)==48 and len(io)==96 and len(plans)==3
    assert {(x['scale'],x['mode'],x['round']) for x in cases}==keys
    assert len({x['current'] for x in cases})==1
    summary=[]
    for scale in (1,2,4):
        sampled=[x for x in cases if x['scale']==scale and x['mode']!='old_edges16']
        assert len({x['hash'] for x in sampled})==1
        for mode in (('single','strips16') if scale==1 else ('single','strips16','old_edges16')):
            selected=[x for x in cases if x['scale']==scale and x['mode']==mode]
            assert len({x['hash'] for x in selected})==1
            assert all(x['read_us']==x['file_us']+x['decode_us']+x['index_other_us'] for x in selected)
            counts={}
            for file in ('map','index'):
                rows=[x for x in io if x['scale']==scale and x['mode']==mode and x['file']==file]
                assert len(rows)==6 and all(x['opens']==x['closes'] for x in rows)
                assert len({tuple(x[k] for k in ('bytes','reads','seeks','opens','closes')) for x in rows})==1
                counts[file]={k:rows[0][k] for k in ('bytes','reads','seeks','opens','closes')}
            med={k:statistics.median(x[k] for x in selected) for k in ('total_us','read_us','file_us','decode_us','index_other_us','extract_us','yield_us')}
            summary.append(dict(scale=scale,mode=mode,median=med,min_us=min(x['total_us'] for x in selected),max_us=max(x['total_us'] for x in selected),
                                work_bytes=selected[0]['work'],heap_min=min(x['heap_min'] for x in selected),calls=selected[0]['calls'],
                                hash=selected[0]['hash'],io=counts))
    return dict(cases=cases,io=io,plans=plans,summary=summary,clock=records(log,'[SD-CLOCK]'),display=records(log,'[DECIM-DISPLAY]'),dataset=records(log,'[DECIM-BEGIN]'))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();result=summarize(a.log.read_text(encoding='utf-8',errors='replace'))
    a.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    for row in result['summary']:print(json.dumps(row,ensure_ascii=False))
