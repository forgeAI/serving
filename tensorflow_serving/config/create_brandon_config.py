#!/usr/bin/env python3

import re
import os
import sys

sys.path.append(os.getenv('CLASSIFY'))
import lib
from lib.preprocessing import read

config_name = 'nuclear_test'
base_dir = lib.PATHS('models', 'servables')
servable_dirs = [d for d in read.get_dirpaths(base_dir, nested=False)if config_name in d]

def get_config_string(base_path):
    config_string = 'config: {' \
        + '\n\t\tname: "{n}",\n\t\tbase_path: "{b}",'.format(n=os.path.basename(base_path), b=base_path) \
        + '\n\t\tmodel_platform: "tensorflow"' \
        + '\n\t}'
    return config_string

brandon_config_string = "model_config_list: {\n\t"
brandon_config_string += ',\n\t'.join([get_config_string(bp) for bp in servable_dirs])
brandon_config_string += "\n}"

print(brandon_config_string)

with open('brandon_config.pbtxt', 'w+') as f:
    f.write(brandon_config_string)
