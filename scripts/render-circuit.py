from pathlib import Path
from html import escape
from PIL import Image, ImageDraw, ImageFont

OUT=Path(__file__).resolve().parents[1] / 'docs' / 'diagrams'
OUT.mkdir(parents=True,exist_ok=True)
W,H,S=1600,1100,2
im=Image.new('RGB',(W*S,H*S),'white')
draw=ImageDraw.Draw(im)
svg=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">','<rect width="100%" height="100%" fill="white"/>']
INK='#19313D'; MUTED='#5C6D76'; PWR='#C13636'; DATA='#087D75'; LED='#BD6519'; BTN='#7750A1'
def text(x,y,t,size=25,col=INK,bold=False,anchor='start'):
    font=ImageFont.truetype(r'C:\Windows\Fonts\arialbd.ttf' if bold else r'C:\Windows\Fonts\arial.ttf',size*S)
    draw.text((x*S,y*S),t,font=font,fill=col,anchor={'start':'ls','middle':'ms','end':'rs'}[anchor])
    svg.append(f'<text x="{x}" y="{y}" font-family="Arial, sans-serif" font-size="{size}" fill="{col}" font-weight="{"bold" if bold else "normal"}" text-anchor="{anchor}">{escape(t)}</text>')
def line(points,col=INK,width=3):
    draw.line([(x*S,y*S) for x,y in points],fill=col,width=width*S)
    svg.append(f'<polyline points="{" ".join(f"{x},{y}" for x,y in points)}" fill="none" stroke="{col}" stroke-width="{width}" stroke-linejoin="round"/>')
def rect(x,y,w,h,fill='white',stroke=INK):
    draw.rectangle((x*S,y*S,(x+w)*S,(y+h)*S),fill=fill,outline=stroke,width=3*S)
    svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}" stroke="{stroke}" stroke-width="3"/>')
def circle(x,y,r,col=INK):
    draw.ellipse(((x-r)*S,(y-r)*S,(x+r)*S,(y+r)*S),fill='white',outline=col,width=3*S)
    svg.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="white" stroke="{col}" stroke-width="3"/>')
def ground(x,y):
    line([(x,y),(x,y+15)])
    for dy,a in [(15,22),(25,14),(35,6)]:line([(x-a,y+dy),(x+a,y+dy)])
    text(x,y+66,'GND',22,MUTED,anchor='middle')
def arrow(x,y,xe,ye,col):
    line([(x,y),(xe,ye)],col)
    line([(xe-12,ye+2),(xe,ye),(xe-2,ye+12)],col)

text(85,70,'Simple elevator mockup circuit',39,bold=True)
text(85,112,'ESP32-WROOM-32 + I2C LCD + one status LED + verification button',26,MUTED)
line([(85,145),(1515,145)],'#D2DCE2',2)

rect(200,215,380,650,'#F3F7F9')
text(390,272,'ESP32',37,bold=True,anchor='middle')
text(390,309,'WROOM-32 development board',23,MUTED,anchor='middle')
rect(1070,215,350,350,'#F3F7F9')
text(1245,272,'LCD',37,bold=True,anchor='middle')
text(1245,309,'HD44780 / 16x2 / 0x27',23,MUTED,anchor='middle')
for y,left,right,col in [(365,'3V3','VCC',PWR),(415,'GND','GND',INK),(465,'GPIO21','SDA',DATA),(515,'GPIO22','SCL',DATA)]:
    text(558,y+8,left,27,col,anchor='end')
    line([(580,y),(1070,y)],col,4)
    text(1092,y+8,right,27,col)
text(825,345,'VCC = 3V3 as currently wired',22,PWR,anchor='middle')

text(558,683,'GPIO25',27,LED,anchor='end')
line([(580,675),(725,675)],LED,4)
rect(725,663,140,24)
text(795,634,'R1  330 ohms',24,bold=True,anchor='middle')
line([(865,675),(1090,675)],LED,4)
# LED diode symbol: anode on the left, cathode bar on the right.
line([(1090,650),(1090,700),(1140,675),(1090,650)],LED,3)
line([(1140,648),(1140,702)],LED,4)
line([(1140,675),(1395,675)],INK,3)
ground(1395,675)
arrow(1110,634,1140,604,LED)
arrow(1135,638,1165,608,LED)
text(1220,597,'D1  STATUS LED',26,LED,bold=True,anchor='middle')
text(1080,737,'Anode (+)',22,LED,anchor='middle')
text(1240,737,'Cathode (-)',22,INK,anchor='middle')
text(1100,772,'Long leg = anode; flat side / short leg = cathode',21,MUTED,anchor='middle')

text(558,838,'GPIO27',27,BTN,anchor='end')
line([(580,830),(1030,830)],BTN,4)
circle(1030,830,6,BTN)
line([(1170,830),(1395,830)],INK,3)
circle(1170,830,6,BTN)
line([(1020,810),(1180,810)],BTN,4)
line([(1100,790),(1100,810)],BTN,3)
ground(1395,830)
text(1100,956,'SW1  VERIFY BUTTON',26,BTN,bold=True,anchor='middle')
text(1100,995,'Normally open. Press connects GPIO27 to GND.',22,MUTED,anchor='middle')
text(730,883,'INPUT_PULLUP',23,BTN,bold=True,anchor='middle')
text(730,916,'Released = HIGH',22,MUTED,anchor='middle')
text(730,949,'Pressed = LOW',22,MUTED,anchor='middle')

rect(250,825,156,40,'#DFE8EE')
text(328,853,'USB',24,bold=True,anchor='middle')
line([(328,865),(328,925)],INK,4)
text(328,964,'USB power + programming',24,bold=True,anchor='middle')
text(328,1000,'Wi-Fi / MQTT to Windows',21,MUTED,anchor='middle')

line([(85,1030),(1515,1030)],'#D2DCE2',2)
text(85,1070,'GPIO numbers, not header positions. Check actual LCD labels before wiring.',21,MUTED)
text(1515,1070,'Mockup / no motors connected',20,MUTED,anchor='end')
svg.append('</svg>')
(OUT/'03-simple-mockup-circuit.svg').write_text('\n'.join(svg),encoding='utf-8')
im.save(OUT/'03-simple-mockup-circuit.png',optimize=True)
print(OUT/'03-simple-mockup-circuit.png')
