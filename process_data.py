

import os
import json
from PIL import Image
from tqdm import tqdm  # progress bar
import json

# Directory path

# Prepare lists
files = []
categories = [
    {"id": 0, "name": "car", "supercategory": "vehicle"},
    {"id": 1, "name": "motorbike", "supercategory": "vehicle"}
]

with open('./dataset/training/_annotations.coco.json') as f:
    annotations = json.load(f)

    i = 0

    # Process each txt file with tqdm
    for entry in annotations['images']:

        obj = {
            'path': entry['file_name'],
            'category': 'training',
            'label': {
                'type': 'label',
                'label': 'vehicle'
            },
            'boundingBoxes': []
        }

        while i < len(annotations['annotations']) and annotations['annotations'][i]['image_id'] == entry['id']:
            obj['boundingBoxes'].append(
                {
                    "label": categories[ int(annotations['annotations'][i]['category_id']) ]['name'],
                    "x": int(annotations['annotations'][i]['bbox'][0]),
                    "y": int(annotations['annotations'][i]['bbox'][1]),
                    "width": int(annotations['annotations'][i]['bbox'][2]),
                    "height": int(annotations['annotations'][i]['bbox'][3])
                }
            )
            i += 1
        files.append(obj)

    # Combine everything
    final_output = {
        "version": 1,
        "files": files
    }

    # Write to JSON
    with open("bounding_boxes.labels", "w") as outfile:
        json.dump(final_output, outfile, indent=4)
