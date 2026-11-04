from PIL import Image, ImageDraw, ImageFont
S=2380/2000
W,H=2380,1020
BG=(16,18,22);TXT=(245,240,235);MUT=(170,170,175);GRID=(48,50,56);RED=(245,90,85);DIM=(62,30,32);CREAM=(245,215,180);DIMC=(60,56,52)
B='/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf';R='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
f=lambda s,b=False:ImageFont.truetype(B if b else R,int(s*S))
im=Image.new('RGB',(W,H),BG);d=ImageDraw.Draw(im)
def T(x,y,t,s=20,c=TXT,b=False,a='la'): d.text((x*S,y*S),t,font=f(s,b),fill=c,anchor=a)
def Rc(x0,y0,x1,y1,c,r=0): d.rounded_rectangle((x0*S,y0*S,x1*S,y1*S),radius=int(r*S),fill=c)
T(120,40,'UFC3MONS RECOMP / DEVELOPMENT PROGRESS',34,TXT,True)
T(120,102,'R8 to BETA 1.2 · fight measurements',22,MUT)
def axis(x0,x1,y0,y1,mx,ticks,label,fmt=str,ref=None):
    sc=lambda v:x0+(x1-x0)*v/mx
    for t in ticks:
        d.line((sc(t)*S,y0*S,sc(t)*S,y1*S),fill=GRID,width=1); T(sc(t),y1+14,fmt(t),18,MUT,a='ma')
    d.line((x0*S,y1*S,x1*S,y1*S),fill=(80,80,86),width=2)
    T((x0+x1)/2,y1+50,label,19,MUT,a='ma')
    return sc
# Panel 1: FPS
x0,x1,y0,y1=210,760,230,620
T(x0,165,'ROG Ally Z1 Extreme, Linux: fight FPS',21,(225,225,225))
sc=axis(x0,x1,y0,y1,70,[0,15,30,45,60],'Game frames per second')
rows=[('R8',30,30,RED,''),('R8.5',57,60,RED,''),('P14.4 · 20 W',46,50,RED,''),('P14.5 · 20 W',60,60,CREAM,'')]
for i,(n,a,b,c,_) in enumerate(rows):
    y=270+i*92
    T(x0-20,y,n,20,TXT,a='rm')
    Rc(x0,y-13,sc(a),y+13,DIMC if c==CREAM else DIM)
    if a==b: d.ellipse(((sc(a)-7)*S,(y-7)*S,(sc(a)+7)*S,(y+7)*S),fill=c)
    else: Rc(sc(a)-6,y-9,sc(b)+6,y+9,c,7)
    T(sc(b)+22,y,(str(a) if a==b else f'{a}–{b}'),20,c,True,'lm')
# Panel 2: speed
x0,x1=985,1330
T(x0-85,165,'Game speed below 60 FPS (slow PC simulated)',21,(225,225,225))
sc=axis(x0,x1,y0,y1,110,[0,50,100],'% of normal speed',lambda v:f'{v}%')
for i,(n,v,c,l) in enumerate([('P14.5',80,(120,95,102),'80% · 47.9 FPS'),('BETA 1.0+',100,RED,'100% · 44 FPS')]):
    y=320+i*170
    T(x0-18,y,n,20,TXT,a='rm'); Rc(x0,y-32,sc(v),y+32,c); T(x0+14,y+50,l,19,TXT,True)
# Panel 3: GPU
x0,x1=1525,1880
T(x0-85,165,'GPU time per fight frame, 2× scale (RTX 5060 Ti)',21,(225,225,225))
sc=axis(x0,x1,y0,y1,7,[0,2,4,6],'Milliseconds (lower is better)')
for i,(n,v,c,l) in enumerate([('BETA 1.1',5.68,(120,95,102),'5.68 ms'),('BETA 1.2',4.06,RED,'4.06 ms')]):
    y=320+i*170
    T(x0-18,y,n,20,TXT,a='rm'); Rc(x0,y-32,sc(v),y+32,c); T(sc(v)-12,y,l,19,TXT,True,'rm')
T(120,725,'BETA 1.2: −28% GPU time per fight frame vs BETA 1.1 (same session, runs alternated, no FPS limit).',20,(215,215,215))
T(120,762,'Different sessions and hardware; not a controlled benchmark. R8 and R8.5 had no 20 W limit recorded in this history.',18,MUT)
T(120,796,'BETA 1.0 to 1.2 were not measured on the ROG Ally. Results depend on the scene, driver, settings and shader cache.',18,MUT)
im.save('/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/github-UFC3MONS/media/performance-r9.png',optimize=True); print(im.size)
