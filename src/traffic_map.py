#!/usr/bin/env python3
"""Render configured traffic schematics; parse provider data without executing it."""
import argparse
import email.utils
import fcntl
import json
import math
import os
from pathlib import Path
import re
import shutil
import tempfile
import time
import urllib.request
from PIL import Image, ImageDraw, ImageFont

URL = 'https://traffic.houstontranstar.org/data/layers/speed_segments.js'
BINS = [(20, '#ff0000'), (30, '#ff8c00'), (40, '#ffff00'), (50, '#0000fe'), (math.inf, '#008000')]
GRAY = '#848484'
BG = '#071d48'
WHITE = '#ffffff'


def parse_transtar(body, viewport):
    matches = re.findall(r'new SpeedSegment\((.*?)\);', body)
    if not matches or len(matches) != body.count('new SpeedSegment('):
        raise ValueError('unrecognized speed segment rows')
    rows = []
    for raw in matches:
        r = json.loads('[' + raw + ']')
        if len(r) != 14 or any(isinstance(v, (dict, list, bool)) or v is None for v in r):
            raise ValueError('invalid speed segment schema')
        coords = [[float(v) for v in r[i].split()] for i in range(4)]
        if len(coords[0]) < 2 or len({len(c) for c in coords}) != 1:
            raise ValueError('coordinate arrays differ')
        if any(not math.isfinite(v) for c in coords for v in c):
            raise ValueError('nonfinite coordinate')
        if any(abs(v) > 90 for v in coords[0]) or any(abs(v) > 180 for v in coords[1]):
            raise ValueError('coordinate outside earth bounds')
        speed, miles = float(r[5]), float(r[7])
        if not math.isfinite(speed) or not math.isfinite(miles) or miles <= 0:
            raise ValueError('invalid speed or distance')
        if r[4] not in ('NB', 'SB', 'EB', 'WB'):
            raise ValueError('unknown direction')
        match = re.fullmatch(r'(\d+) minutes? (\d+) seconds?', r[6])
        seconds = int(match[1]) * 60 + int(match[2]) if match else None
        if not match and r[6] != 'Not Available':
            raise ValueError('unrecognized travel time')
        route = re.sub(r' (Northbound|Southbound|Eastbound|Westbound)$', '', r[11].split(' from ')[0])
        rows.append(dict(route=route, direction=r[4], speed=speed, miles=miles, seconds=seconds,
                         facility=r[8], origin=str(r[9]), destination=str(r[10]), description=r[11],
                         in_view=any(viewport[0] <= lon <= viewport[2] and viewport[1] <= lat <= viewport[3]
                                     for lat, lon in zip(coords[0], coords[1]))))
    return rows


def load_config(path):
    config = json.loads(path.read_text())
    if config.get('provider') != 'transtar':
        raise ValueError('only the transtar traffic provider is supported')
    if not isinstance(config.get('title'), str) or not config['title'].strip():
        raise ValueError('traffic title must be a nonempty string')
    viewport = config.get('viewport')
    if not isinstance(viewport, list) or len(viewport) != 4 or not all(
            isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v) for v in viewport):
        raise ValueError('viewport must contain west, south, east, north coordinates')
    west, south, east, north = viewport
    if not (-180 <= west < east <= 180 and -90 <= south < north <= 90):
        raise ValueError('invalid viewport bounds')
    roads = config.get('roads')
    if not isinstance(roads, list) or not roads:
        raise ValueError('traffic configuration must contain roads')
    keys = set()
    for road in roads:
        key = road.get('key')
        if not isinstance(key, str) or not key or key in keys:
            raise ValueError('road keys must be nonempty and unique')
        keys.add(key)
        routes = road.get('routes')
        if not isinstance(routes, list) or not routes or not all(isinstance(r, str) and r for r in routes):
            raise ValueError(f'{key}: routes must contain provider route names')
        directions = road.get('dirs')
        if directions not in (['NB', 'SB'], ['SB', 'NB'], ['EB', 'WB'], ['WB', 'EB']):
            raise ValueError(f'{key}: dirs must contain an opposing direction pair')
        if not isinstance(road.get('clip_to_viewport'), bool):
            raise ValueError(f'{key}: clip_to_viewport must be a boolean')
        points = road.get('points')
        if not isinstance(points, list) or len(points) < 2:
            raise ValueError(f'{key}: at least two map points are required')
        for p in points:
            validate_point(p)
        if any(a == b for a, b in zip(points, points[1:])):
            raise ValueError(f'{key}: adjacent map points must differ')
        for i in range(len(points)):
            if points[max(0, i-1)] == points[min(len(points)-1, i+1)]:
                raise ValueError(f'{key}: repeated points cannot determine line direction')
    labels = config.get('labels')
    if not isinstance(labels, list):
        raise ValueError('labels must be an array')
    for label in labels:
        validate_point(label['point'])
        if not isinstance(label.get('text'), str) or not label['text']:
            raise ValueError('label text must be a nonempty string')
    return config


def validate_point(point):
    if not isinstance(point, list) or len(point) != 2 or not all(
            isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v) for v in point):
        raise ValueError('map points must be finite [x, y] pairs')
    if not (0 <= point[0] <= 512 and 0 <= point[1] <= 240):
        raise ValueError('map points must be inside the 512 by 240 road area')


def aggregate(rows, spec, direction):
    selected = [r for r in rows if r['route'] in spec['routes'] and r['direction']==direction
                and r['facility']=='ML' and (not spec['clip_to_viewport'] or r['in_view'])]
    valid = [r for r in selected if r['speed'] >= 0 and r['seconds'] is not None and r['seconds'] > 0]
    distance = sum(r['miles'] for r in selected)
    measured_distance = sum(r['miles'] for r in valid)
    return dict(key=spec['key'], direction=direction, routes=spec['routes'], segments=len(selected),
                measured_segments=len(valid), miles=distance, measured_miles=measured_distance,
                complete=bool(selected) and len(valid)==len(selected),
                flow_mph=3600*measured_distance/sum(r['seconds'] for r in valid) if valid else None,
                bottleneck_mph=min(r['speed'] for r in valid) if valid else None,
                records=[{k:r[k] for k in ('origin','destination','description','miles','seconds','speed')} for r in selected])


def color(group, mode, stale):
    value = group[mode+'_mph']
    if stale or not group['complete'] or value is None:
        return GRAY
    return next(c for limit,c in BINS if value < limit)


def offset(points, amount):
    result=[]
    for i,(x,y) in enumerate(points):
        a,b=points[max(0,i-1)],points[min(len(points)-1,i+1)]
        dx,dy=b[0]-a[0],b[1]-a[1]
        length=math.hypot(dx,dy)
        result.append((x-dy/length*amount,y+dx/length*amount))
    return result


def draw_page(path, mode, groups, config, metadata):
    scale=3
    im=Image.new('RGB',(512*scale,290*scale),BG)
    draw=ImageDraw.Draw(im)
    font=ImageFont.load_default(size=10*scale)
    small=ImageFont.load_default(size=9*scale)
    def line(points,fill,width): draw.line([(int(x*scale),int(y*scale)) for x,y in points],fill=fill,width=int(width*scale),joint='curve')
    def text(x,y,value,fill=WHITE,tiny=False): draw.text((x*scale,y*scale),value,font=small if tiny else font,fill=fill,stroke_width=1,stroke_fill=BG)
    index=0
    for spec in config['roads']:
        for n,direction in enumerate(spec['dirs']):
            g=groups[index]; index+=1
            pts=offset(spec['points'],2 if n==0 else -2)
            line(pts,'#020b1b',5.2)
            line(pts,color(g,mode,metadata['stale']),2.1)
            # One small arrow identifies direction without assigning any new measurement.
            a,b=pts[len(pts)//2-1],pts[len(pts)//2]
            dx,dy=b[0]-a[0],b[1]-a[1]
            if (direction=='NB' and dy>0) or (direction=='SB' and dy<0) or (direction=='EB' and dx<0) or (direction=='WB' and dx>0): dx,dy=-dx,-dy
            length=math.hypot(dx,dy); ux,uy=dx/length,dy/length
            mx,my=(a[0]+b[0])/2,(a[1]+b[1])/2
            tri=[(mx+ux*3,my+uy*3),(mx-ux*2-uy*1.8,my-uy*2+ux*1.8),(mx-ux*2+uy*1.8,my-uy*2-ux*1.8)]
            draw.polygon([(int(x*scale),int(y*scale)) for x,y in tri],fill=WHITE)
    for label in config['labels']:
        text(*label['point'],label['text'],tiny=True)
    x=30
    for label,c,ink,width in [('<20','#ff0000','#000000',64),('20-29','#ff8c00','#000000',64),('30-39','#ffff00','#000000',64),('40-49','#0000fe',WHITE,64),('50+','#008000',WHITE,64),('Partial/N/A',GRAY,'#000000',112)]:
        draw.rectangle((x*scale,246*scale,(x+width)*scale,266*scale),fill=c)
        draw.text(((x+width/2)*scale,256*scale),label,font=font,fill=ink,anchor='mm')
        x+=width+4
    source=metadata['source_updated_at']
    stamp=time.strftime('%b %d %I:%M %p %Z',time.localtime(source)) if source else 'time unknown'
    footer='MPH | Schematic | '+metadata['source']+' '+stamp+(' | STALE' if metadata['stale'] else '')
    draw.text((256*scale,280*scale),footer,font=small,fill=WHITE,anchor='mm')
    im.resize((512,290),Image.Resampling.LANCZOS).save(path)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--config',required=True,type=Path)
    parser.add_argument('--output-dir',required=True,type=Path)
    args=parser.parse_args(); out=args.output_dir
    config=load_config(args.config)
    out.mkdir(parents=True,exist_ok=True)
    with (out/'collector.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        request=urllib.request.Request(URL,headers={'User-Agent':'WeatherStar-4000-native-traffic/1.0'})
        with urllib.request.urlopen(request,timeout=15) as response:
            body=response.read(2_000_001)
            if len(body)>2_000_000: raise ValueError('speed feed exceeds size bound')
            modified=response.headers.get('Last-Modified')
        now=int(time.time()); source=None
        if modified:
            try: source=int(email.utils.parsedate_to_datetime(modified).timestamp())
            except (ValueError,TypeError,OverflowError):
                source=None  # Unknown provider time intentionally makes all measurements stale.
        rows=parse_transtar(body.decode('utf-8-sig'),config['viewport']); specs=config['roads']
        groups=[aggregate(rows,spec,direction) for spec in specs for direction in spec['dirs']]
        if not any(g['segments'] for g in groups): raise ValueError('no provider segments match the configured roads')
        metadata=dict(title=config['title'],source='Houston TranStar',feed_url=URL,source_updated_at=source,fetched_at=now,
                      stale=source is None or now-source>900 or source>now+300,
                      stale_after_seconds=900,viewport=config['viewport'],provider_rows=len(rows),groups=groups,
                      scope='Schematic named corridors; viewport filtering selects whole provider segments without interpolation')
        generation=Path(tempfile.mkdtemp(prefix='generation-',dir=out))
        for mode,name in [('flow','flow.png'),('bottleneck','bottlenecks.png')]:
            draw_page(generation/name,mode,groups,config,metadata)
        metadata['flow_image']=str((generation/'flow.png').resolve())
        metadata['bottlenecks_image']=str((generation/'bottlenecks.png').resolve())
        (generation/'metadata.json').write_text(json.dumps(metadata,indent=2)+'\n')
        # One atomic pointer switches all three public filenames to a completed generation.
        pending=out/'current.next'
        try: pending.unlink()
        except FileNotFoundError: pass
        pending.symlink_to(generation.name,target_is_directory=True)
        os.replace(pending,out/'current')
        for name in ['flow.png','bottlenecks.png','metadata.json']:
            link=out/name
            if not link.is_symlink(): link.symlink_to('current/'+name)
        print(json.dumps({'metadata':str(out/'metadata.json'),'groups':len(groups),'source_updated_at':source}))
        # Keep three completed generations for readers holding a previous metadata snapshot.
        old=sorted(out.glob('generation-*'),key=lambda p:p.stat().st_mtime,reverse=True)
        for path in old[3:]:
            if path.is_dir() and not path.is_symlink(): shutil.rmtree(path)

if __name__=='__main__':
    main()
