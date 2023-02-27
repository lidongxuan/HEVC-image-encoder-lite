# -*- coding: utf-8 -*-
# Python3

# python standard libraries importation
import sys
import os

# python third-party libraries importation (additional installation is required, if you do not have)
import numpy as np
from PIL import Image




# description : load a image from a image file (.png, .jpg, etc.) , convert it to monochrome, and return as a 2-D numpy array
def readImageAsMonochrome(file_name) :
    img_obj = Image.open(file_name)
    img_mono = img_obj.convert('L')
    img_obj.close()
    return np.asarray(img_mono)





USAGE_STRING = '''
    Usage :
        python  %s  <input_file(.jpg|.png|.tiff|...)>  <output_file(.pgm)>
    or :
        python  %s  <input-dir>  <output-dir>
''' % (sys.argv[0], sys.argv[0])



if __name__ == '__main__' : 
    
    try :
        name1, name2 = sys.argv[1:3]                      # parse command line args
        assert name1 != name2
    except :
        print(USAGE_STRING)
        exit(-1)
    
    
    if not os.path.isdir(name1) :                         # user specific a single file
        try :
            img = readImageAsMonochrome(name1)
        except :
            print('could not open %s' % name1)
            exit (-1)
        
        _, ext_name = os.path.splitext(name2)
        if ext_name != '.pgm' :
            out_fname = name2 + '.pgm'
        else :
            out_fname = name2
        
        try :
            Image.fromarray(img).save(out_fname)
        except :
            print('failed to write %s' % out_fname)
        

    else :                                                # user specific a dir
        if not os.path.exists(name2) :
            print('mkdir %s\n' % name2)
            os.mkdir(name2)
    
        for fname in os.listdir(name1) :
            in_fname  = name1  + os.path.sep + fname
            out_fname, _ = os.path.splitext(fname)
            out_fname = name2 + os.path.sep + out_fname + '.pgm'
            
            try :
                img = readImageAsMonochrome(in_fname)
            except :
                print('skip %s' % in_fname)
                continue

            try :
                Image.fromarray(img).save(out_fname)
            except :
                print('failed to write %s' % out_fname)


