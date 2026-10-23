#!/usr/bin/env python3
"""Bounded Ephem calculation / Pillow crop; no network or display imports."""
import json
import sys
from datetime import datetime, timezone

mode, lat, lon, output = sys.argv[1:5]
if mode == 'moon':
    import ephem
    observer = ephem.Observer()
    observer.lat, observer.lon = lat, lon
    observer.date = datetime.now(timezone.utc)
    moon = ephem.Moon(observer)
    def local(value):
        return value.datetime().replace(tzinfo=timezone.utc).astimezone().strftime('%b %d %I:%M %p')
    result = dict(date=datetime.now().date().isoformat(), illumination=float(moon.phase),
                  rise=local(observer.next_rising(moon)), set=local(observer.next_setting(moon)),
                  full=local(ephem.next_full_moon(observer.date)), new=local(ephem.next_new_moon(observer.date)))
    with open(output, 'w') as stream:
        json.dump(result, stream)
elif mode == 'radar':
    from PIL import Image, ImageFilter
    with Image.open(sys.argv[5]) as image:
        image = image.convert('RGB')
        width, height = image.size
        cx = int(width * max(0, min(1, (float(lon) + 125) / 60)))
        cy = int(height * max(0, min(1, (50 - float(lat)) / 26)))
        bw, bh = width // 5, height // 5
        left = min(width - bw, max(0, cx - bw // 2))
        top = min(height - bh, max(0, cy - bh // 2))
        image.crop((left, top, left + bw, top + bh)).resize((500, 300), Image.Resampling.LANCZOS).filter(ImageFilter.SMOOTH).save(output, 'PNG')
else:
    raise ValueError('unknown helper mode')
