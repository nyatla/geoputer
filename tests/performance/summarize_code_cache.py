"""Summarize real-device code-cache size timings, in milliseconds."""
from pathlib import Path
import argparse,csv,json,re,statistics
p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();lines=a.log.read_text(encoding='utf-8',errors='replace').splitlines()
rows=[];follow=[];datasets={}
for line in lines:
    values=dict(re.findall(r'(\w+)=([^\s]+)',line))
    if '[CODE-CACHE-DATASET]' in line:datasets[values['dir']]=values
    elif '[CODE-CACHE-CASE]' in line:rows.append(values)
    elif '[CODE-CACHE-FOLLOW]' in line:follow.append(values)
    elif '[CODE-CACHE-ERROR]' in line:raise RuntimeError(line)
assert any('[CODE-CACHE-DONE]' in line for line in lines),'capture incomplete'
summary=[]
for directory in sorted(datasets):
    for side in (128,64,32):
        group=[r for r in rows if r['dir']==directory and int(r['side'])==side]
        hits=[r for r in follow if r['dir']==directory and int(r['side'])==side]
        if not group:continue
        ms=lambda key: [int(r[key])/1000 for r in group]
        summary.append(dict(dataset=directory,map=datasets[directory]['map'],side=side,bytes=side*side*4,samples=len(group),
            mean_ms=round(statistics.mean(ms('total_us')),3),min_ms=round(min(ms('total_us')),3),max_ms=round(max(ms('total_us')),3),
            rect_mean_ms=round(statistics.mean(ms('rect_us')),3),
            crossing_mean_ms=round(statistics.mean(int(r['crossing_us'])/1000 for r in hits),3),
            hit_mean_us=round(statistics.mean(int(r['hit100_us'])/100 for r in hits),3),
            map_bytes_mean=round(statistics.mean(int(r['map_bytes']) for r in group),1),
            map_reads_mean=round(statistics.mean(int(r['map_reads']) for r in group),2)))
a.output.mkdir(parents=True,exist_ok=True)
(a.output/'summary.json').write_text(json.dumps(dict(datasets=datasets,summary=summary,cases=rows,follow=follow),indent=2,ensure_ascii=False)+'\n')
with (a.output/'summary.csv').open('w',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=list(summary[0]));writer.writeheader();writer.writerows(summary)
print('| dataset | side | bytes | mean ms | min–max ms | crossing ms | hit µs | map bytes |')
print('|---|---:|---:|---:|---:|---:|---:|---:|')
for r in summary:
    print(f"| {r['dataset']} | {r['side']} | {r['bytes']} | {r['mean_ms']} | {r['min_ms']}–{r['max_ms']} | {r['crossing_mean_ms']} | {r['hit_mean_us']} | {r['map_bytes_mean']} |")
