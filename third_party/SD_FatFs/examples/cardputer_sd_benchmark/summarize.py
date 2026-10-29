#!/usr/bin/env python3
"""Validate a Cardputer SD benchmark log and emit per-file median comparisons."""
from pathlib import Path
import argparse, csv, json, re, statistics, sys

def summarize(path):
    text=path.read_text(encoding='utf-8')
    if '[SD-AB-DONE] OK' not in text or '[SD-AB-ERROR]' in text:
        raise ValueError('Benchmark did not complete successfully')
    files={int(i):(int(n),p) for i,n,p in re.findall(r'\[SD-AB-FILE\] id=(\d+) bytes=(\d+) path=([^\r\n]+)',text)}
    records=[];groups={}
    for line in text.splitlines():
        if not line.startswith('[SD-AB] '):continue
        record=dict(re.findall(r'(\w+)=([^ ]+)',line))
        for key in ['file','round','io_us','total_us','physical_us','bytes','reads','sectors','writes']:
            record[key]=int(record[key])
        if record['writes']:raise ValueError('Write attempt')
        records.append(record)
        groups.setdefault((record['file'],record['pattern']),[]).append(record)
    expected_files,expected_cases=map(int,re.search(r'\[SD-AB-DONE\] OK files=(\d+) cases=(\d+) writes=0',text).groups())
    if len(files)!=expected_files or len(records)!=expected_cases or len(groups)!=len(files)*6:
        raise ValueError('Missing benchmark records')
    summary=[]
    for (number,pattern),group in sorted(groups.items()):
        if {(r['mode'],r['round']) for r in group}!={(m,n) for m in ['sdk4096','sd512'] for n in range(5)} or len(group)!=10:
            raise ValueError('Missing repeats')
        for key in ['crc','bytes','reads','sectors']:
            if len({r[key] for r in group})!=1:raise ValueError('Output/physical-I/O mismatch: '+key)
        by_mode={mode:[r for r in group if r['mode']==mode and r['round']>0] for mode in ['sdk4096','sd512']}
        result={'file':number,'path':files[number][1],'file_bytes':files[number][0],'pattern':pattern}
        for mode,label in [('sdk4096','sdk'),('sd512','sd')]:
            result[label+'_io_ms']=statistics.median(r['io_us'] for r in by_mode[mode])/1000
            result[label+'_physical_ms']=statistics.median(r['physical_us'] for r in by_mode[mode])/1000
        result['change_percent']=(result['sd_io_ms']/result['sdk_io_ms']-1)*100
        for key in ['bytes','crc','reads','sectors']:result[key]=group[0][key]
        summary.append(result)
    return records,summary

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('log',type=Path)
    parser.add_argument('--summary-csv',type=Path);parser.add_argument('--raw-csv',type=Path);args=parser.parse_args()
    raw,summary=summarize(args.log)
    for path,rows in [(args.summary_csv,summary),(args.raw_csv,raw)]:
        if path:
            with path.open('w',encoding='utf-8',newline='') as out:
                writer=csv.DictWriter(out,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    print(json.dumps(summary,indent=2,ensure_ascii=False))
if __name__=='__main__':main()
