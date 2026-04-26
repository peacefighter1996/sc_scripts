import os
import argparse
import json
from flask import Flask, send_from_directory, jsonify, request, render_template_string
from PIL import Image

app = Flask(__name__)

HTML = '''
<!doctype html>
<html>
  <head>
    <meta charset="utf-8" />
    <title>YOLO Signatures Annotator</title>
    <style>
      body { font-family: Arial, sans-serif; }
      #canvas { border:1px solid #333; cursor: crosshair }
      #sidebar { position:fixed; right:10px; top:10px; width:260px; }
      label { display:block; margin-top:8px }
    </style>
  </head>
  <body>
    <h3>YOLO Signatures Annotator</h3>
    <div>
      <button id="prev">Prev</button>
      <button id="next">Next</button>
      <span id="fname"></span>
    </div>
    <canvas id="canvas"></canvas>
    <div id="sidebar">
      <label>Class
        <select id="cls"><option>unknown</option><option>mineral</option></select>
      </label>
      <label>Subtype<input id="subtype" /></label>
      <button id="add">Add Box (click two points)</button>
      <button id="save">Save</button>
      <div id="info"></div>
    </div>

    <script>
    let images = [];
    let idx = 0;
    let boxes = {}; // image -> [{xmin,ymin,xmax,ymax,class,subtype}]
    let mode = 'view';
    let tmp = [];

    async function loadList(){
      const r = await fetch('/api/images');
      images = await r.json();
      if(images.length===0){ alert('No images in directory'); }
      loadImage(0);
    }

    function drawImage(img){
      const c = document.getElementById('canvas');
      const ctx = c.getContext('2d');
      c.width = img.width; c.height = img.height;
      ctx.drawImage(img,0,0);
      const name = images[idx];
      const arr = boxes[name]||[];
      ctx.lineWidth = 2;
      for(const b of arr){
        ctx.strokeStyle = 'lime'; ctx.strokeRect(b.xmin,b.ymin,b.xmax-b.xmin,b.ymax-b.ymin);
        ctx.fillStyle = 'rgba(0,0,0,0.5)'; ctx.fillRect(b.xmin,b.ymin-18,120,18);
        ctx.fillStyle = 'white'; ctx.fillText((b.class||'') + ' ' + (b.subtype||''), b.xmin+4, b.ymin-4);
      }
      if(tmp.length===1){ ctx.fillStyle='red'; ctx.fillRect(tmp[0].x-3,tmp[0].y-3,6,6); }
    }

    async function loadImage(i){
      if(i<0) i = images.length-1; if(i>=images.length) i=0; idx=i;
      document.getElementById('fname').textContent = images[idx];
      const img = new Image(); img.src = '/images/' + encodeURIComponent(images[idx]);
      img.onload = ()=> drawImage(img);
      const r = await fetch('/api/annotations/' + encodeURIComponent(images[idx]));
      const data = await r.json();
      boxes[images[idx]] = data.boxes || [];
    }

    document.getElementById('next').onclick = ()=> loadImage(idx+1);
    document.getElementById('prev').onclick = ()=> loadImage(idx-1);
    document.getElementById('add').onclick = ()=> { mode='add'; tmp=[]; document.getElementById('info').textContent='Click two points on image'; };
    document.getElementById('save').onclick = async ()=>{
      const name = images[idx];
      const payload = { image: name, boxes: boxes[name] || [] };
      const res = await fetch('/api/save_image_annotations',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});
      const j = await res.json(); alert(j.message||'saved');
    };

    document.getElementById('canvas').addEventListener('click', (ev)=>{
      const rect = ev.target.getBoundingClientRect();
      const x = ev.clientX - rect.left; const y = ev.clientY - rect.top;
      if(mode==='add'){
        tmp.push({x:Math.round(x), y:Math.round(y)});
        if(tmp.length===2){
          const a=tmp[0], b=tmp[1];
          const xmin=Math.min(a.x,b.x), ymin=Math.min(a.y,b.y), xmax=Math.max(a.x,b.x), ymax=Math.max(a.y,b.y);
          const cls = document.getElementById('cls').value; const subtype = document.getElementById('subtype').value;
          const name = images[idx]; boxes[name] = boxes[name]||[]; boxes[name].push({xmin,ymin,xmax,ymax,class:cls,subtype:subtype});
          tmp=[]; mode='view';
          // redraw image locally so the new box remains in memory (don't re-fetch annotations)
          const img = new Image(); img.src = '/images/' + encodeURIComponent(images[idx]); img.onload = ()=> drawImage(img);
        } else {
          // show temporary point without reloading annotations
          const img = new Image(); img.src = '/images/' + encodeURIComponent(images[idx]); img.onload = ()=> drawImage(img);
        }
      }
    });

    loadList();
    </script>
  </body>
</html>
'''


def find_images(root):
    exts = {'.png', '.jpg', '.jpeg'}
    names = [f for f in os.listdir(root) if os.path.splitext(f)[
        1].lower() in exts]
    names.sort()
    return names


def read_csv_annotations(csv_path):
    boxes = {}
    if not csv_path or not os.path.exists(csv_path):
        return boxes
    with open(csv_path, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split(',')
            # Support two CSV formats:
            # 1) id,image,xmin,ymin,xmax,ymax,class,subtype
            # 2) image,xmin,ymin,xmax,ymax,class,subtype
            if len(parts) < 6:
                continue
            if len(parts) >= 8 and parts[0].isdigit():
                mid = int(parts[0])
                image = parts[1]
                xmin = int(parts[2])
                ymin = int(parts[3])
                xmax = int(parts[4])
                ymax = int(parts[5])
                cls = parts[6]
                subtype = parts[7] if len(parts) > 7 else ''
            else:
                mid = None
                image = parts[0]
                xmin = int(parts[1])
                ymin = int(parts[2])
                xmax = int(parts[3])
                ymax = int(parts[4])
                cls = parts[5]
                subtype = parts[6] if len(parts) > 6 else ''
            entry = {'xmin': xmin, 'ymin': ymin, 'xmax': xmax,
                     'ymax': ymax, 'class': cls, 'subtype': subtype}
            if mid is not None:
                entry['id'] = mid
            boxes.setdefault(image, []).append(entry)
    return boxes


@app.route('/')
def index():
    return render_template_string(HTML)


@app.route('/api/images')
def api_images():
    return jsonify(app.config['IMG_LIST'])


@app.route('/images/<path:fname>')
def image_serve(fname):
    return send_from_directory(app.config['IMG_DIR'], fname)


@app.route('/api/annotations/<path:imagename>')
def api_annotations(imagename):
    # return boxes for this image
    d = app.config.get('CSV_ANN', {})
    boxes = d.get(imagename, [])
    return jsonify({'boxes': boxes})


@app.route('/api/save_image_annotations', methods=['POST'])
def api_save_image_annotations():
    payload = request.get_json()
    image = payload.get('image')
    boxes = payload.get('boxes', [])
    # load existing CSV annotations, update, and write COCO canonical
    csv_path = app.config.get('CSV_PATH')
    d = app.config.get('CSV_ANN', {})
    # preserve existing IDs and assign new management ids to boxes missing 'id'
    # compute current max id
    cur_max = 0
    for bl in d.values():
        for b in bl:
            if isinstance(b.get('id'), int) and b['id'] > cur_max:
                cur_max = b['id']
    # also check incoming boxes for existing ids
    for b in boxes:
        if isinstance(b.get('id'), int) and b['id'] > cur_max:
            cur_max = b['id']
    # assign ids where missing
    next_id = cur_max + 1
    for b in boxes:
        if 'id' not in b or not isinstance(b.get('id'), int):
            b['id'] = next_id
            next_id += 1
    d[image] = boxes
    # write backup CSV
    if csv_path:
        bak = csv_path + '.bak'
        if os.path.exists(csv_path):
            os.replace(csv_path, bak)
        with open(csv_path, 'w', encoding='utf-8') as f:
            for img, bl in d.items():
                for b in bl:
                    # write management id as first field
                    mid = b.get('id', '')
                    f.write(
                        f"{mid},{img},{b['xmin']},{b['ymin']},{b['xmax']},{b['ymax']},{b.get('class','')},{b.get('subtype','')}\n")
    # also write a simple COCO JSON
    coco = {'images': [], 'annotations': [], 'categories': [
        {'id': 0, 'name': 'unknown'}, {'id': 1, 'name': 'mineral'}]}
    img_id = 1
    ann_id = 1
    for img in app.config['IMG_LIST']:
        path = os.path.join(app.config['IMG_DIR'], img)
        try:
            with Image.open(path) as im:
                w, h = im.size
        except Exception:
            w = h = 0
        coco['images'].append(
            {'id': img_id, 'file_name': img, 'width': w, 'height': h})
        for b in d.get(img, []):
            bbox = [b['xmin'], b['ymin'], b['xmax'] -
                    b['xmin'], b['ymax']-b['ymin']]
            cat = 0 if (b.get('class', '') == 'unknown') else 1
            # include management id in attributes for traceability
            attributes = {'subtype': b.get('subtype', '')}
            if 'id' in b:
                attributes['management_id'] = b['id']
            coco['annotations'].append({'id': ann_id, 'image_id': img_id, 'bbox': bbox,
                                       'area': bbox[2]*bbox[3], 'category_id': cat, 'attributes': attributes})
            ann_id += 1
        img_id += 1
    out = os.path.join('dataset', 'coco_annotations.json')
    os.makedirs('dataset', exist_ok=True)
    with open(out, 'w', encoding='utf-8') as f:
        json.dump(coco, f, indent=2)
    app.config['CSV_ANN'] = d
    return jsonify({'message': 'saved', 'coco': out})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--dir', default='images/test/bb_unknown', help='images directory')
    parser.add_argument('--annotations', default='annotations.csv',
                        help='CSV annotations path (optional)')
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', default=5000, type=int)
    args = parser.parse_args()

    img_dir = os.path.abspath(args.dir)
    if not os.path.isdir(img_dir):
        print('Image directory not found:', img_dir)
        return
    imgs = find_images(img_dir)
    csv_path = args.annotations if os.path.exists(args.annotations) else None
    csv_ann = read_csv_annotations(csv_path) if csv_path else {}

    app.config['IMG_DIR'] = img_dir
    app.config['IMG_LIST'] = imgs
    app.config['CSV_PATH'] = csv_path
    app.config['CSV_ANN'] = csv_ann

    print(f'Serving {len(imgs)} images from', img_dir)
    app.run(host=args.host, port=args.port, debug=False)


if __name__ == '__main__':
    main()
