# SPDX-License-Identifier: MIT
"""Our scaled-video frame identity, independent of JS callback counters."""
WIDTH,HEIGHT,RATE,FRAMES=1920,1080,60,900
PREFIX=(1,1,0,1);SUFFIX=(0,0,1,0)
LAYOUT=dict(columns=18,bits=10,left=96,top=80,pitch=96,cell=80,row_pitch=112,
            sample_left=88,sample_width=1728,row_centers=[120,232],black=32,white=224)


def bits(frame):
    if not isinstance(frame,int) or not 0<=frame<FRAMES:raise ValueError('Frame outside the finite fixture')
    gray=frame^(frame>>1)
    return list(PREFIX)+[(gray>>b)&1 for b in range(10)]+list(SUFFIX)


def decode_levels(rows):
    if len(rows)!=2 or any(len(row)!=18 for row in rows):raise ValueError('Two complete marker rows required')
    binary=[]
    for row in rows:
        values=[]
        for level in row:
            if not 0<=level<=255 or 80<=level<=180:raise ValueError('Ambiguous or invalid marker level')
            values.append(int(level>180))
        binary.append(values)
    if any(a==b for a,b in zip(*binary)):raise ValueError('Complement mismatch: unqualified/mixed rows')
    row=binary[0]
    if tuple(row[:4])!=PREFIX or tuple(row[-4:])!=SUFFIX:raise ValueError('Marker sync guards absent')
    gray=sum(value<<b for b,value in enumerate(row[4:14]));frame=0
    while gray:frame^=gray;gray>>=1
    if frame>=FRAMES:raise ValueError('Encoded frame is outside the finite fixture')
    return frame


def decode_gray_frame(data,width,height):
    if len(data)!=width*height or width<160 or height<90:raise ValueError('Complete suitably scaled gray video required')
    rows=[]
    for cy in LAYOUT['row_centers']:
        y=round(cy*height/HEIGHT)
        centers=[round((LAYOUT['left']+column*LAYOUT['pitch']+LAYOUT['cell']/2)*width/WIDTH) for column in range(18)]
        rows.append([sum(data[y*width+x+offset] for offset in [-1,0,1])/3 for x in centers])
    return decode_levels(rows)


def filters():
    result=['drawbox=x=88:y=72:w=1744:h=208:color=0x202020:t=fill']
    for row in range(2):
        for column in range(18):
            x=LAYOUT['left']+column*LAYOUT['pitch'];y=LAYOUT['top']+row*LAYOUT['row_pitch']
            if 4<=column<14:
                b=column-4;expr=f'mod(floor(n/{1<<b})+floor(n/{2<<b}),2)'
                enable=f":enable='eq({expr},{1-row})'"
            else:
                value=(PREFIX+SUFFIX)[column if column<4 else column-10]
                if value==row:continue
                enable=''
            result.append(f'drawbox=x={x}:y={y}:w=80:h=80:color=0xE0E0E0:t=fill'+enable)
    return ','.join(result)
