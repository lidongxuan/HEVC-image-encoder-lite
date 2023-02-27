# -*- coding: utf-8 -*-
# Python3

# python standard libraries importation
import sys
import os
import platform
import subprocess as sp
from time import sleep
from random import randint

# python third-party libraries importation (additional installation is required, if you do not have)
import numpy as np
from PIL import Image                                             # if there are any error, such as "jpeg2000 is not supported", please use command "pip install --upgrade Pillow" to upgrade Pillow library
from skimage.metrics import structural_similarity as SSIM




# description : pad the image width and height to integral multiples of pad_size
def imagePad (img, pad_size = 32):
    ysz, xsz = img.shape
    
    ysz_new = ((ysz + pad_size - 1) // pad_size) * pad_size
    xsz_new = ((xsz + pad_size - 1) // pad_size) * pad_size
    
    img_new = np.zeros([ysz_new, xsz_new] , dtype=img.dtype)
    
    img_new[:ysz, :xsz] = img
    
    if xsz_new > xsz :
        for y in range(ysz) :
            img_new[y, xsz:xsz_new] = img[y, xsz-1]                # fill the right padded pixels with the nearest existing pixels
    
    if ysz_new > ysz :
        for x in range(xsz) :
            img_new[ysz:ysz_new, x] = img[ysz-1 ,x]                # fill the bottom padded pixels with the nearest existing pixels
    
    if xsz_new > xsz and ysz_new > ysz :
        img_new[ysz:ysz_new, xsz:xsz_new] = img[ysz-1, xsz-1]      # fill the right-bottom padded pixels with the nearest existing pixels
    
    return img_new




# description : load a image from a image file (.png, .jpg, etc.) , convert it to monochrome, and return as a 2-D numpy array
def readImageAsMonochrome(file_name) :
    img_obj = Image.open(file_name)
    img_mono = img_obj.convert('L')
    img_obj.close()
    return np.asarray(img_mono)




def callHEVCImageEncoder(img, out_fname, qpd6) :
    Image.fromarray(img).save(TMP_HEVC_INPUT_FNAME)                           # save as pgm file, for HEVC encoder's input
    sleep(2)
    
    EXE_FILE = 'HEVCe'                                                        # HEVCencoder executable file name
    
    if   platform.system().lower() != 'windows' :                             # linux or macOS (not windows)
        EXE_FILE = './' + EXE_FILE
    
    COMMANDS = [ EXE_FILE, TMP_HEVC_INPUT_FNAME, out_fname, TMP_HEVC_RCON_FNAME, str(qpd6) ]      # construct command line
    
    p = sp.Popen(COMMANDS, stdin=sp.PIPE, stdout=sp.PIPE, stderr=sp.PIPE)     # call the HEVCencoder executable file
    
    if p.wait() != 0 :
        print('run HEVC encoder failed')
        exit(-1)
    sleep(2)
    
    img_rcon = readImageAsMonochrome(TMP_HEVC_RCON_FNAME)
    sleep(2)
    
    os.remove(TMP_HEVC_INPUT_FNAME)
    os.remove(TMP_HEVC_RCON_FNAME)
    
    return img_rcon




def saveImageAsFormat(img, out_fname, quality) :
    if    out_fname.endswith('.jpg') :
        Image.fromarray(img).save(out_fname, optimize=True, quality=quality)                                # save as JPEG, enable size optimize
    elif  out_fname.endswith('.j2k') :
        Image.fromarray(img).save(out_fname, optimize=True, quality_mode='dB', quality_layers=[quality])    # save as JPEG2000, enable size optimize
    elif  out_fname.endswith('.webp') :
        Image.fromarray(img).save(out_fname, optimize=True, quality=quality)                                # save as WEBP, enable size optimize
    elif  out_fname.endswith('.png') :
        Image.fromarray(img).save(out_fname, optimize=True)                                                 # save as PNG, enable size optimize





comparison_list = [     # HEVC will compare to these image formats ############################################################
#   name         format (file suffix)  quality_lowest    quality_highest
  [ 'JPEG'     ,  '.jpg'             ,           1     ,         101              ] ,
  [ 'JPEG2000' ,  '.j2k'             ,           25    ,          75              ] ,
  [ 'WEBP'     ,  '.webp'            ,           1     ,         101              ]
]






USAGE_STRING = '''
    Usage:
        python  %s  <input-dir>  <output-dir>  [<qpd6>]
''' % sys.argv[0]



if __name__ == '__main__' : 
    
    # parse command line args #########################################################################################################################
    try :
        in_dirname, out_dirname = sys.argv[1:3]
        assert in_dirname != out_dirname
    except :
        print(USAGE_STRING)
        exit(-1)
    
    qpd6 = 3

    try :
        qpd6 = int(sys.argv[3])
    except :
        pass
    
    print()
    print('|-arguments --------------------------------------')
    print('|   input  dir     = %s' % in_dirname)
    print('|   output dir     = %s' % out_dirname)
    print('|   Qp%%6           = %d        (Qp = %d)' % (qpd6, qpd6*6+4) )
    print('|-------------------------------------------------')
    print()
    
    
    if not os.path.isdir(out_dirname) :
        print('mkdir %s' % out_dirname)
        print()
        os.mkdir(out_dirname)


    TMP_HEVC_INPUT_FNAME   = out_dirname + os.path.sep + 'tmp_hevc_input_file_%d.pgm' % randint(0, 9999999999999999)
    TMP_HEVC_RCON_FNAME    = out_dirname + os.path.sep + 'tmp_hevc_rcon_file_%d.pgm'  % randint(0, 9999999999999999)
    
    
    hevc_bpp_list = []
    comparison_bpp_lists = [ []  for i in range(len(comparison_list)) ]
    
    
    for fname in os.listdir(in_dirname) :
        in_fname  = in_dirname + os.path.sep + fname
        
        try :
            img = readImageAsMonochrome(in_fname)
        except :
            continue
        
        img = imagePad(img)
        
        ysz, xsz = img.shape
        
        print('\n%s    width=%d    height=%d' % (in_fname, xsz, ysz) )
        
        
        # output image file names
        out_fname_without_suffix, _ = os.path.splitext(fname)
        out_fname_without_suffix = out_dirname + os.path.sep + out_fname_without_suffix
        
        
        
        # compress to HEVC ##############################################################################
        
        out_fname_hevc = out_fname_without_suffix + '.h265'
        
        img_rcon = callHEVCImageEncoder(img, out_fname_hevc, qpd6)
        
        hevc_size = os.path.getsize(out_fname_hevc)
        hevc_bpp  = (8.0*hevc_size) / (xsz*ysz)
        hevc_ssim = SSIM(img, img_rcon, data_range=256.0)
        
        hevc_bpp_list.append(hevc_bpp)
        
        print('  HEVC     : ssim=%.5f    bpp=%.3f' % (hevc_ssim, hevc_bpp ) )
        
        
        
        # compare HEVC to comparison image formats ##############################################################################
        
        for i_format, (format_name, suffix, quality_lower, quality_upper)  in  enumerate(comparison_list) :
            out_fname = out_fname_without_suffix + suffix
            info_list = []
            
            while  quality_upper - quality_lower > 1 :                                         # dichotomy search to get the nearest SSIM to HEVC's SSIM
                quality = (quality_upper + quality_lower) // 2
                
                saveImageAsFormat(img, out_fname, quality)                                     # save to a file as the comparison image format
                sleep(2)
                
                out_img  = readImageAsMonochrome(out_fname)                                    # read the saved file
                out_ssim = SSIM(img, out_img, data_range=256.0)                                # calculate its SSIM
                out_size = os.path.getsize(out_fname)                                          # get its file size
                
                info_list.append( (abs(out_ssim-hevc_ssim), out_ssim, out_size, quality) )
                
                if out_ssim < hevc_ssim :
                    quality_lower = quality
                else :
                    quality_upper = quality 
        
            info_list.sort(key = lambda x:x[0])                                                # sort the info_list, using the abs of ssim delta as key
            
            _, out_ssim, out_size, quality = info_list[0]                                      # the first item in the sorted info_list (info_list[0]) has the nearest SSIM to HEVC's SSIM
            
            saveImageAsFormat(img, out_fname, quality)                                         # finally save to a file as the comparison image format

            out_bpp = (8.0*out_size) / (xsz*ysz)
            
            comparison_bpp_lists[i_format].append(out_bpp)
            
            print('  %-8s : ssim=%.5f    bpp=%.3f    qparam=%d    size/HEVCsize=%f' % (format_name, out_ssim, out_bpp, quality, out_size/hevc_size ) )
        
        
        print('bpp mean : HEVC:%.5f' % np.mean(hevc_bpp_list) , end='   ')
        
        for i_format, (format_name, _, _, _)  in  enumerate(comparison_list) :
            print('%s:%.5f' % ( format_name , np.mean(comparison_bpp_lists[i_format]) ) , end='   ')
        print()
    
    
    
    
    print('\n\nbpp lists ---------------------------------')
    for i_format, (format_name, _, _, _)  in  enumerate(comparison_list) :
        print(format_name)
        print(comparison_bpp_lists[i_format])

