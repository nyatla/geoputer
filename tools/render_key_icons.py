"""Export native ROM key bitmaps and a nearest-neighbor comparison preview."""
from pathlib import Path
import re
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
folder=ROOT/'doc/画面・操作'
preview=Image.new('RGB',(48,16),'black')
for name,width,x in [('Escape',24,0),('Enter',12,32)]:
    source=(ROOT/f'application/task/ui/{name}Icon.h').read_text()
    values=re.findall(r'0b[01]+|0x[0-9a-fA-F]+',source.split('Rows[Height]=',1)[1])
    assert len(values)==12
    icon=Image.new('RGB',(width,12),'black')
    for y,value in enumerate(values):
        bits=int(value,0)
        for col in range(width):
            if bits & (1<<(width-1-col)):icon.putpixel((col,y),(255,255,255))
    icon.save(folder/f'{name.lower()}-icon.png')
    icon.resize((width*12,144),getattr(Image,'Resampling',Image).NEAREST).save(folder/f'{name.lower()}-icon-preview.png')
    preview.paste(icon,(x,2))
preview.resize((576,192),getattr(Image,'Resampling',Image).NEAREST).save(folder/'key-icons-preview.png')
print('PASS: native ESC 24x12, Enter 12x12 and nearest-neighbor preview from ROM rows')
