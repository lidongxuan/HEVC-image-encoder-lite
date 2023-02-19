// Copyright : github.com/WangXuan95
//
// A light-weight H.265/HEVC intra frame encoder for monochrome (grayscale) still image compression.
// This code may be the most understandable HEVC implementation in the world.




///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// configurations
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//#define       ENABLE_DEBUG_PRINT

#ifdef        ENABLE_DEBUG_PRINT
    #include  <stdio.h>
    #define   DEBUG_PRINT           printf
#else
    #define   DEBUG_PRINT           
#endif





///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// define of abbr. :
//
//   Y   : luma channel
//   U   : chroma-blue channel (Cb)
//   V   : chroma-red  channel (Cr)
//   ch  : channel, Y(0), U(1), or V(2)
//
//   pix : pixel
//  coef : coefficient
//
//   hor : horizontal
//   ver : vertical
//    sz : size
//   pos : position
// cudep : CU tree depth
//
//  orig : original (pixels)
//  pred : predicted (pixels)
//  resi : residual (pixels)
//  tran : transformed (pixels)
//  quat : quantized (pixels)
//  deqt : de-quantized (pixels)
//  resr : reconstructed residual (pixels), which are inv-transformed from de-quantized pixels
//  rcon : reconstructed (pixels)
//
//   img : image
//   CTU : code tree unit (32x32)
//   CU  : code unit (8x8, 16x16, or 32x32)
//   CG  : coefficient group (4x4)
//   pix : pixel
//
//   bla  : border on left-above  (which contains 1 pixel)
//   bllb : border on left  and left-below  (which contains 2*CU-size pixels)
//   baar : border on above and above-right (which contains 2*CU-size pixels)
//          Note: If the prefix "u" is added to the above abbr., it means "unfiltered", and the prefix "f" means "filtered". For example: ubllb, fblb
//
//  pmode : prediction mode (0~34)
//  rdcost: RD-cost (Rate-Distortion Cost)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////





///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// data type define
// this HEVC encoder will only use these data type
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef  unsigned char  BOOL;          // unsigned integer, 8-bit, used as boolean: 0=false 1=true
typedef  unsigned char  UI8;           // unsigned integer, 8-bit
typedef            int  I32;           // signed integer, must be at least 32 bits 





///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// common define for HEVC
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef    NULL
#define    NULL                 0
#endif

#define    F                    0                                  // this value doesn't make sense, just for filling the arrays

#define    MAX_YSZ              8192                               // max image height
#define    MAX_XSZ              8192                               // max image width

#define    I32_MAX_VALUE        ((I32)(0x7fffffff))

#define    PIX_MIN_VALUE        ((UI8)(  0))
#define    PIX_MAX_VALUE        ((UI8)(255))
#define    PIX_MIDDLE_VALUE     ((UI8)(128))

#define    COEF_MIN_VALUE       ((I32)(-32768))
#define    COEF_MAX_VALUE       ((I32)( 32767))

#define    LOG2_CTU_SZ          5
#define    CTU_SZ               (1<<LOG2_CTU_SZ)    // 32

#define    CUDEP_MAX            2

#define    LOG2_MIN_CU_SZ       (LOG2_CTU_SZ-CUDEP_MAX)

#define    CH_Y                 0
#define    CH_U                 1
#define    CH_V                 2

#define    GET_CU_SZ(cudep, ch)           ( CTU_SZ     >> (cudep) >> ((ch)!=CH_Y) )
#define    GET_LOG2_CU_SZ(cudep, ch)      ( LOG2_CTU_SZ - (cudep)  - ((ch)!=CH_Y) )

#define    PMODE_PLANAR         0                   // planar prediction mode
#define    PMODE_DC             1                   // DC prediction mode
#define    PMODE_DEG45          2                   // angular prediction mode : up right   (45°)
#define    PMODE_HOR            10                  // angular prediction mode : right      (90°)
#define    PMODE_DEG135         18                  // angular prediction mode : down right (135°)
#define    PMODE_VER            26                  // angular prediction mode : down       (180°)
#define    PMODE_DEG225         34                  // angular prediction mode : down left  (225°)
#define    PMODE_COUNT          35                  // there are total 35 prediction modes (0~34)

#define    CG_SZ                4
#define    CG_SZxSZ             (CG_SZ*CG_SZ)

#define    GET_I_CG(i)          ((i) / CG_SZxSZ)    // input a pixel index, output its corresponding CG index
#define    GET_I_IN_CG(i)       ((i) % CG_SZxSZ)    // input a pixel index, output its index in the CG

#define    SCAN_TYPE_DIAG       0
#define    SCAN_TYPE_HOR        1
#define    SCAN_TYPE_VER        2

const I32 RDCOST_WEIGHT_DIST [] = {11, 11, 11,  5,  1};
const I32 RDCOST_WEIGHT_BITS [] = { 1,  4, 16, 29, 23};

I32 calcRDcost (I32 qpd6, I32 dist, I32 bits) {                 // calculate RD-cost, avoid overflow from 32-bit int
    const I32  weight_dist = RDCOST_WEIGHT_DIST[qpd6];
    const I32  weight_bits = RDCOST_WEIGHT_BITS[qpd6];
    I32 cost1, cost2;
    if ( I32_MAX_VALUE/weight_dist <= dist )
        cost1 = I32_MAX_VALUE;
    else
        cost1 = weight_dist * dist;
    if ( I32_MAX_VALUE/weight_bits <= bits )
        cost2 = I32_MAX_VALUE;
    else
        cost2 = weight_bits * bits;
    if ( I32_MAX_VALUE-cost1 <= cost2 )
        return I32_MAX_VALUE;
    else
        return cost1 + cost2;
}





///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// useful macros and functions 
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define ABS(x)             ( ((x) < 0) ? (-(x)) : (x) )
#define MAX(x, y)          ( ((x)<(y)) ? (y) : (x) )
#define MIN(x, y)          ( ((x)<(y)) ? (x) : (y) )
#define CLIP(x, min, max)  ( MIN(MAX((x), (min)), (max)) )
#define PIX_CLIP(x)        ( (UI8)CLIP((x),  PIX_MIN_VALUE,  PIX_MAX_VALUE) )                           // description : clip a value between 0~255 , and convert it to UI8 type (pixel type)
#define COEF_CLIP(x)       ( (I32)CLIP((x), COEF_MIN_VALUE, COEF_MAX_VALUE) )                           // description : clip a value between -32768~32767 (HEVC-specified coefficient range)

#define GET2D(ptr, ysz, xsz, y, x) ( *( (ptr) + (xsz)*CLIP((y),0,(ysz)-1) + CLIP((x),0,(xsz)-1) ) )     // regard a 1-D array (ptr) as a 2-D array, and get value from position (y,x)


// description: bubble sort. In addition, if indexs!=NULL, you can get the sorted index from indexs[]
void arraySort (I32 *array, I32 len, BOOL descend_flag, I32 *indexs) {
    I32 i, j, tmp;
    if (indexs != NULL)
        for (i=0; i<len; i++)
            indexs[i] = i;                                                              // initialize indexs
    for (j=len-1; j>0; j--) {
        for (i=0; i<j; i++) {
            BOOL swap = descend_flag ? (array[i] < array[i+1]) : (array[i] > array[i+1]);
            if (swap) {
                tmp = array[i];
                array[i] = array[i+1];
                array[i+1] = tmp;
            }
            if (swap && indexs != NULL) {
                tmp = indexs[i];
                indexs[i] = indexs[i+1];
                indexs[i+1] = tmp;
            }
        }
    }
}





///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// definitions and functions for prediction 
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// description : get border pixels from the reconstructed image for prediction.
void getBorder (
    const UI8 *img,                                   // 1-D array as a 2-D array
    const I32  ysz,
    const I32  xsz,
    const I32  y,
    const I32  x,
    const I32  cudep,
    const I32  ch,
          UI8  ubla  [1],
          UI8  ubllb [CTU_SZ*2],
          UI8  ubaar [CTU_SZ*2]
) {
    static const BOOL CU_BLB_EXIST_TABLE [][4][4] = {
      { {0, F, F, F}, {F, F, F, F}, {F, F, F, F}, {F, F, F, F} },    // cudep = 0
      { {1, 0, F, F}, {0, 0, F, F}, {F, F, F, F}, {F, F, F, F} },    // cudep = 1
      { {1, 0, 1, 0}, {1, 0, 0, 0}, {1, 0, 1, 0}, {0, 0, 0, 0} }     // cudep = 2
    };

    static const BOOL CU_BAR_EXIST_TABLE [][4][4] = {
      { {1, F, F, F}, {F, F, F, F}, {F, F, F, F}, {F, F, F, F} },    // cudep = 0
      { {1, 1, F, F}, {1, 0, F, F}, {F, F, F, F}, {F, F, F, F} },    // cudep = 1
      { {1, 1, 1, 1}, {1, 0, 1, 0}, {1, 1, 1, 0}, {1, 0, 1, 0} }     // cudep = 2
    };

    const I32  cu_sz       = GET_CU_SZ(cudep, ch);
    const I32  y_cu_in_ctu = (y%CTU_SZ) / cu_sz;
    const I32  x_cu_in_ctu = (x%CTU_SZ) / cu_sz;
    
    const BOOL bll_exist = (x > 0);
    const BOOL blb_exist = bll_exist && (y+cu_sz < ysz) && CU_BLB_EXIST_TABLE[cudep][y_cu_in_ctu][x_cu_in_ctu];
    const BOOL baa_exist = (y > 0);
    const BOOL bar_exist = baa_exist && (x+cu_sz < xsz) && CU_BAR_EXIST_TABLE[cudep][y_cu_in_ctu][x_cu_in_ctu];
    
    I32 i;
    
    if      (bll_exist && baa_exist)                         // 1st, construct the bla (border on left-above pixel)
        ubla[0] = GET2D(img, ysz, xsz, y-1, x-1);
    else if (bll_exist)
        ubla[0] = GET2D(img, ysz, xsz, y  , x-1);
    else if (baa_exist)
        ubla[0] = GET2D(img, ysz, xsz, y-1, x  );
    else
        ubla[0] = PIX_MIDDLE_VALUE;
    
    for (i=0; i<cu_sz; i++)                                  // 2nd, construct the bll (border on left pixels)
        if (bll_exist)
            ubllb[i] = GET2D(img, ysz, xsz, y+i, x-1);
        else
            ubllb[i] = ubla[0];
    
    for (i=cu_sz; i<cu_sz*2; i++)                            // 3rd, construct the blb (border on left-below pixels)
        if (blb_exist)
            ubllb[i] = GET2D(img, ysz, xsz, y+i, x-1);
        else
            ubllb[i] = ubllb[cu_sz-1];
    
    for (i=0; i<cu_sz; i++)                                  // 4th, construct the baa (border on above pixels)
        if (baa_exist)
            ubaar[i] = GET2D(img, ysz, xsz, y-1, x+i);
        else
            ubaar[i] = ubla[0];
    
    for (i=cu_sz; i<cu_sz*2; i++)                            // 5th, construct the bar (border on above-right pixels)
        if (bar_exist)
            ubaar[i] = GET2D(img, ysz, xsz, y-1, x+i);
        else
            ubaar[i] = ubaar[cu_sz-1];
}



// description : filter the border pixels.
void filterBorder (
    const I32  cudep,
    const I32  ch,
    const UI8  ubla,
    const UI8  ubllb [CTU_SZ*2],
    const UI8  ubaar [CTU_SZ*2],
          UI8  fbla  [1],
          UI8  fbllb [CTU_SZ*2],
          UI8  fbaar [CTU_SZ*2]
) {
    const I32  cu_sz = GET_CU_SZ(cudep, ch);
    I32 i;
    
    fbla[0]  = ( 2 + ubllb[0] + ubaar[0] + 2*ubla ) >> 2;
    fbllb[0] = ( 2 + 2*ubllb[0] + ubllb[1] + ubla ) >> 2;
    fbaar[0] = ( 2 + 2*ubaar[0] + ubaar[1] + ubla ) >> 2;
    
    for (i=1; i<cu_sz*2-1; i++) {
        fbllb[i] = ( 2 + 2*ubllb[i] + ubllb[i-1] + ubllb[i+1] ) >> 2;
        fbaar[i] = ( 2 + 2*ubaar[i] + ubaar[i-1] + ubaar[i+1] ) >> 2;
    }
    
    fbllb[cu_sz*2-1] = ubllb[cu_sz*2-1];
    fbaar[cu_sz*2-1] = ubaar[cu_sz*2-1];
}



// description : do prediction, getting the predicted CU
void predict (
    const I32  cudep,
    const I32  ch,
    const I32  pmode,
    const UI8  ubla,
    const UI8  ubllb [CTU_SZ*2],
    const UI8  ubaar [CTU_SZ*2],
    const UI8  fbla,
    const UI8  fbllb [CTU_SZ*2],
    const UI8  fbaar [CTU_SZ*2],
          UI8  cu_pred [CTU_SZ][CTU_SZ]
) {
    static const BOOL WHETHER_FILTER_BORDER_FOR_Y_TABLE [][35] = {
      { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },      // size = 4x4   , pmode = 0~34
      { 1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 },      // size = 8x8   , pmode = 0~34
      { 1,0,1,1,1,1,1,1,1,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,1,1,1,1,1,1,1 },      // size = 16x16 , pmode = 0~34
      { 1,0,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1 }       // size = 32x32 , pmode = 0~34
    };

    static const I32         ANGLE_TABLE [] = {0, 0,  32,  26,  21,  17,  13,   9,    5, 2   , 0,   -2,   -5,  -9, -13, -17, -21, -26, -32, -26, -21, -17, -13,  -9,   -5,   -2, 0,    2,    5,   9,  13,  17,  21,  26,  32 };
    static const I32 ABS_INV_ANGLE_TABLE [] = {0, 0, 256, 315, 390, 482, 630, 910, 1638, 4096, 0, 4096, 1638, 910, 630, 482, 390, 315, 256, 315, 390, 482, 630, 910, 1638, 4096, 0, 4096, 1638, 910, 630, 482, 390, 315, 256 };
    
    const I32  cu_sz      = GET_CU_SZ     (cudep, ch);
    const I32  log2_cu_sz = GET_LOG2_CU_SZ(cudep, ch);
    const BOOL whether_filter_edge   = (ch==CH_Y) && (cu_sz <= 16);
    const BOOL whether_filter_border = (ch==CH_Y) && WHETHER_FILTER_BORDER_FOR_Y_TABLE[log2_cu_sz-2][pmode];
    const UI8  bla  = whether_filter_border ? fbla  : ubla;
    const UI8 *bllb = whether_filter_border ? fbllb : ubllb;
    const UI8 *baar = whether_filter_border ? fbaar : ubaar;
    
    I32 i, j;
    
    if        ( pmode == PMODE_PLANAR ) {
        for (i=0; i<cu_sz; i++) {
            for (j=0; j<cu_sz; j++) {
                const I32 hor_pred = (cu_sz-j-1) * bllb[i] + (j+1) * baar[cu_sz];
                const I32 ver_pred = (cu_sz-i-1) * baar[j] + (i+1) * bllb[cu_sz];
                cu_pred[i][j] = (UI8)( (cu_sz + hor_pred + ver_pred) >> (log2_cu_sz+1) );
            }
        }
        
    } else if ( pmode == PMODE_DC     ) {
        I32 dc_pix = cu_sz;
        for (i=0; i<cu_sz; i++)
            dc_pix += bllb[i] + baar[i];
        dc_pix >>= (log2_cu_sz+1);                                               // calculate the DC value (mean value of border pixels)
        
        for (i=0; i<cu_sz; i++)
            for (j=0; j<cu_sz; j++)
                cu_pred[i][j] = (UI8)dc_pix;                                     // fill all predict pixel with dc_pix
        
        if (whether_filter_edge) {                                               // apply the edge filter for DC mode
            cu_pred[0][0] = (UI8)( (2 + 2*dc_pix + bllb[0] + baar[0]) >> 2 );    // filter the top-left pixel        of the predicted CU
            for (i=1; i<cu_sz; i++) {
                cu_pred[0][i] = (UI8)( (2 + 3*dc_pix + baar[i] ) >> 2 );         // filter the pixels in top row     of the predicted CU (except the top-left pixel)
                cu_pred[i][0] = (UI8)( (2 + 3*dc_pix + bllb[i] ) >> 2 );         // filter the pixels in left column of the predicted CU (except the top-left pixel)
            }
        }
        
    } else if ( pmode == PMODE_HOR    ) {
        for (i=0; i<cu_sz; i++)
            for (j=0; j<cu_sz; j++)
                cu_pred[i][j] = bllb[i];
        
        if (whether_filter_edge)
            for (j=0; j<cu_sz; j++) {
                const I32 bias = (baar[j] - bla) >> 1;
                cu_pred[0][j] = PIX_CLIP( bias + cu_pred[0][j] );
            }
        
    } else if ( pmode == PMODE_VER    ) {
        for (i=0; i<cu_sz; i++)
            for (j=0; j<cu_sz; j++)
                cu_pred[i][j] = baar[j];
        
        if (whether_filter_edge)
            for (i=0; i<cu_sz; i++) {
                const I32 bias = (bllb[i] - bla) >> 1;
                cu_pred[i][0] = PIX_CLIP( bias + cu_pred[i][0] );
            }
        
    } else {   // pmode = 2~9, 11~25, 27~34  (Angular mode without pure horizontal and pure vertical)
        const BOOL is_horizontal = (pmode < PMODE_DEG135);
        const I32  angle         = ANGLE_TABLE        [pmode];
        const I32  abs_inv_angle = ABS_INV_ANGLE_TABLE[pmode];
        
        const UI8 *bmain = is_horizontal ? bllb : baar;
        const UI8 *bside = is_horizontal ? baar : bllb;
        
        UI8  ref_buff0 [CTU_SZ*4+1] ;
        UI8 *ref_buff = ref_buff0 + CTU_SZ*2;
        
        ref_buff[0] = bla;
        
        for (i=0; i<cu_sz*2; i++)
            ref_buff[1+i] = bside[i];
        
        for (i=-1; i>((cu_sz*angle)>>5); i--) {
            j = (128 - abs_inv_angle * i) >> 8;
            ref_buff[i] = ref_buff[j];
        }
        
        for (i=0; i<cu_sz*2; i++)
            ref_buff[1+i] = bmain[i];
        
        for (i=0; i<cu_sz; i++) {
            const I32 off   = angle * (i+1);
            const I32 off_i = off >> 5;
            const I32 off_f = off & 0x1f;
            for (j=0; j<cu_sz; j++) {
                const UI8 pix1 = ref_buff[off_i+j+1];
                const UI8 pix2 = ref_buff[off_i+j+2];
                const UI8 pix  = (UI8)( ( (32-off_f)*pix1 + off_f*pix2 + 16 ) >> 5 );
                if (is_horizontal)
                    cu_pred[j][i] = pix;
                else
                    cu_pred[i][j] = pix;
            }
        }
    }
}





///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// definitions and functions for transform and inverse transform
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const I32 DCT8_MAT  [][CTU_SZ] = {       // DCT matrix for CU 8x8
  { 64,  64,  64,  64,  64,  64,  64,  64, },
  { 89,  75,  50,  18, -18, -50, -75, -89, },
  { 83,  36, -36, -83, -83, -36,  36,  83, },
  { 75, -18, -89, -50,  50,  89,  18, -75, },
  { 64, -64, -64,  64,  64, -64, -64,  64, },
  { 50, -89,  18,  75, -75, -18,  89, -50, },
  { 36, -83,  83, -36, -36,  83, -83,  36, },
  { 18, -50,  75, -89,  89, -75,  50, -18, }
};


const I32 DCT16_MAT [][CTU_SZ] = {       // DCT matrix for CU 16x16
  { 64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64, },
  { 90,  87,  80,  70,  57,  43,  25,   9,  -9, -25, -43, -57, -70, -80, -87, -90, },
  { 89,  75,  50,  18, -18, -50, -75, -89, -89, -75, -50, -18,  18,  50,  75,  89, },
  { 87,  57,   9, -43, -80, -90, -70, -25,  25,  70,  90,  80,  43,  -9, -57, -87, },
  { 83,  36, -36, -83, -83, -36,  36,  83,  83,  36, -36, -83, -83, -36,  36,  83, },
  { 80,   9, -70, -87, -25,  57,  90,  43, -43, -90, -57,  25,  87,  70,  -9, -80, },
  { 75, -18, -89, -50,  50,  89,  18, -75, -75,  18,  89,  50, -50, -89, -18,  75, },
  { 70, -43, -87,   9,  90,  25, -80, -57,  57,  80, -25, -90,  -9,  87,  43, -70, },
  { 64, -64, -64,  64,  64, -64, -64,  64,  64, -64, -64,  64,  64, -64, -64,  64, },
  { 57, -80, -25,  90,  -9, -87,  43,  70, -70, -43,  87,   9, -90,  25,  80, -57, },
  { 50, -89,  18,  75, -75, -18,  89, -50, -50,  89, -18, -75,  75,  18, -89,  50, },
  { 43, -90,  57,  25, -87,  70,   9, -80,  80,  -9, -70,  87, -25, -57,  90, -43, },
  { 36, -83,  83, -36, -36,  83, -83,  36,  36, -83,  83, -36, -36,  83, -83,  36, },
  { 25, -70,  90, -80,  43,   9, -57,  87, -87,  57,  -9, -43,  80, -90,  70, -25, },
  { 18, -50,  75, -89,  89, -75,  50, -18, -18,  50, -75,  89, -89,  75, -50,  18, },
  {  9, -25,  43, -57,  70, -80,  87, -90,  90, -87,  80, -70,  57, -43,  25,  -9, }
};


const I32 DCT32_MAT [][CTU_SZ] = {       // DCT matrix for CU 32x32
  { 64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64, },
  { 90,  90,  88,  85,  82,  78,  73,  67,  61,  54,  46,  38,  31,  22,  13,   4,  -4, -13, -22, -31, -38, -46, -54, -61, -67, -73, -78, -82, -85, -88, -90, -90, },
  { 90,  87,  80,  70,  57,  43,  25,   9,  -9, -25, -43, -57, -70, -80, -87, -90, -90, -87, -80, -70, -57, -43, -25,  -9,   9,  25,  43,  57,  70,  80,  87,  90, },
  { 90,  82,  67,  46,  22,  -4, -31, -54, -73, -85, -90, -88, -78, -61, -38, -13,  13,  38,  61,  78,  88,  90,  85,  73,  54,  31,   4, -22, -46, -67, -82, -90, },
  { 89,  75,  50,  18, -18, -50, -75, -89, -89, -75, -50, -18,  18,  50,  75,  89,  89,  75,  50,  18, -18, -50, -75, -89, -89, -75, -50, -18,  18,  50,  75,  89, },
  { 88,  67,  31, -13, -54, -82, -90, -78, -46,  -4,  38,  73,  90,  85,  61,  22, -22, -61, -85, -90, -73, -38,   4,  46,  78,  90,  82,  54,  13, -31, -67, -88, },
  { 87,  57,   9, -43, -80, -90, -70, -25,  25,  70,  90,  80,  43,  -9, -57, -87, -87, -57,  -9,  43,  80,  90,  70,  25, -25, -70, -90, -80, -43,   9,  57,  87, },
  { 85,  46, -13, -67, -90, -73, -22,  38,  82,  88,  54,  -4, -61, -90, -78, -31,  31,  78,  90,  61,   4, -54, -88, -82, -38,  22,  73,  90,  67,  13, -46, -85, },
  { 83,  36, -36, -83, -83, -36,  36,  83,  83,  36, -36, -83, -83, -36,  36,  83,  83,  36, -36, -83, -83, -36,  36,  83,  83,  36, -36, -83, -83, -36,  36,  83, },
  { 82,  22, -54, -90, -61,  13,  78,  85,  31, -46, -90, -67,   4,  73,  88,  38, -38, -88, -73,  -4,  67,  90,  46, -31, -85, -78, -13,  61,  90,  54, -22, -82, },
  { 80,   9, -70, -87, -25,  57,  90,  43, -43, -90, -57,  25,  87,  70,  -9, -80, -80,  -9,  70,  87,  25, -57, -90, -43,  43,  90,  57, -25, -87, -70,   9,  80, },
  { 78,  -4, -82, -73,  13,  85,  67, -22, -88, -61,  31,  90,  54, -38, -90, -46,  46,  90,  38, -54, -90, -31,  61,  88,  22, -67, -85, -13,  73,  82,   4, -78, },
  { 75, -18, -89, -50,  50,  89,  18, -75, -75,  18,  89,  50, -50, -89, -18,  75,  75, -18, -89, -50,  50,  89,  18, -75, -75,  18,  89,  50, -50, -89, -18,  75, },
  { 73, -31, -90, -22,  78,  67, -38, -90, -13,  82,  61, -46, -88,  -4,  85,  54, -54, -85,   4,  88,  46, -61, -82,  13,  90,  38, -67, -78,  22,  90,  31, -73, },
  { 70, -43, -87,   9,  90,  25, -80, -57,  57,  80, -25, -90,  -9,  87,  43, -70, -70,  43,  87,  -9, -90, -25,  80,  57, -57, -80,  25,  90,   9, -87, -43,  70, },
  { 67, -54, -78,  38,  85, -22, -90,   4,  90,  13, -88, -31,  82,  46, -73, -61,  61,  73, -46, -82,  31,  88, -13, -90,  -4,  90,  22, -85, -38,  78,  54, -67, },
  { 64, -64, -64,  64,  64, -64, -64,  64,  64, -64, -64,  64,  64, -64, -64,  64,  64, -64, -64,  64,  64, -64, -64,  64,  64, -64, -64,  64,  64, -64, -64,  64, },
  { 61, -73, -46,  82,  31, -88, -13,  90,  -4, -90,  22,  85, -38, -78,  54,  67, -67, -54,  78,  38, -85, -22,  90,   4, -90,  13,  88, -31, -82,  46,  73, -61, },
  { 57, -80, -25,  90,  -9, -87,  43,  70, -70, -43,  87,   9, -90,  25,  80, -57, -57,  80,  25, -90,   9,  87, -43, -70,  70,  43, -87,  -9,  90, -25, -80,  57, },
  { 54, -85,  -4,  88, -46, -61,  82,  13, -90,  38,  67, -78, -22,  90, -31, -73,  73,  31, -90,  22,  78, -67, -38,  90, -13, -82,  61,  46, -88,   4,  85, -54, },
  { 50, -89,  18,  75, -75, -18,  89, -50, -50,  89, -18, -75,  75,  18, -89,  50,  50, -89,  18,  75, -75, -18,  89, -50, -50,  89, -18, -75,  75,  18, -89,  50, },
  { 46, -90,  38,  54, -90,  31,  61, -88,  22,  67, -85,  13,  73, -82,   4,  78, -78,  -4,  82, -73, -13,  85, -67, -22,  88, -61, -31,  90, -54, -38,  90, -46, },
  { 43, -90,  57,  25, -87,  70,   9, -80,  80,  -9, -70,  87, -25, -57,  90, -43, -43,  90, -57, -25,  87, -70,  -9,  80, -80,   9,  70, -87,  25,  57, -90,  43, },
  { 38, -88,  73,  -4, -67,  90, -46, -31,  85, -78,  13,  61, -90,  54,  22, -82,  82, -22, -54,  90, -61, -13,  78, -85,  31,  46, -90,  67,   4, -73,  88, -38, },
  { 36, -83,  83, -36, -36,  83, -83,  36,  36, -83,  83, -36, -36,  83, -83,  36,  36, -83,  83, -36, -36,  83, -83,  36,  36, -83,  83, -36, -36,  83, -83,  36, },
  { 31, -78,  90, -61,   4,  54, -88,  82, -38, -22,  73, -90,  67, -13, -46,  85, -85,  46,  13, -67,  90, -73,  22,  38, -82,  88, -54,  -4,  61, -90,  78, -31, },
  { 25, -70,  90, -80,  43,   9, -57,  87, -87,  57,  -9, -43,  80, -90,  70, -25, -25,  70, -90,  80, -43,  -9,  57, -87,  87, -57,   9,  43, -80,  90, -70,  25, },
  { 22, -61,  85, -90,  73, -38,  -4,  46, -78,  90, -82,  54, -13, -31,  67, -88,  88, -67,  31,  13, -54,  82, -90,  78, -46,   4,  38, -73,  90, -85,  61, -22, },
  { 18, -50,  75, -89,  89, -75,  50, -18, -18,  50, -75,  89, -89,  75, -50,  18,  18, -50,  75, -89,  89, -75,  50, -18, -18,  50, -75,  89, -89,  75, -50,  18, },
  { 13, -38,  61, -78,  88, -90,  85, -73,  54, -31,   4,  22, -46,  67, -82,  90, -90,  82, -67,  46, -22,  -4,  31, -54,  73, -85,  90, -88,  78, -61,  38, -13, },
  {  9, -25,  43, -57,  70, -80,  87, -90,  90, -87,  80, -70,  57, -43,  25,  -9,  -9,  25, -43,  57, -70,  80, -87,  90, -90,  87, -80,  70, -57,  43, -25,   9, },
  {  4, -13,  22, -31,  38, -46,  54, -61,  67, -73,  78, -82,  85, -88,  90, -90,  90, -90,  88, -85,  82, -78,  73, -67,  61, -54,  46, -38,  31, -22,  13,  -4, }
};



// description : do matrix multiply, which is only called by function transform (transformation, DCT)
void matMul (
    const I32  sz,
    const I32  sft,
    const BOOL src2_transpose,
    const I32  src1  [CTU_SZ][CTU_SZ],
    const I32  src2  [CTU_SZ][CTU_SZ],
          I32  dst   [CTU_SZ][CTU_SZ]
) {
    I32 i, j, k, add=(1<<sft>>1);

    for (i=0; i<sz; i++) {
        for (j=0; j<sz; j++) {
            I32 sum = add;
            for (k=0; k<sz; k++)
                sum += src1[i][k] * (src2_transpose ? src2[j][k] : src2[k][j]);
            dst[i][j] = sum >> sft;
        }
    }
}



// description : do transformation (DCT), getting the transformed CU
void transform (
    const I32  cudep,
    const I32  ch,
    const I32  src   [CTU_SZ][CTU_SZ],
          I32  dst   [CTU_SZ][CTU_SZ]
) {
    const I32  cu_sz = GET_CU_SZ(cudep, ch);

    const I32 (*dctm) [CTU_SZ] = (cu_sz == 8) ? DCT8_MAT : (cu_sz == 16) ? DCT16_MAT : (cu_sz == 32) ? DCT32_MAT : NULL;

    const I32 sft1 = (cu_sz == 8) ? 2 : (cu_sz == 16) ? 3 : (cu_sz == 32) ? 4 : 0;
    const I32 sft2 = sft1 + 7;

    I32 tmp [CTU_SZ][CTU_SZ];
    
    matMul(cu_sz, sft1, 0, dctm,  src, tmp);
    matMul(cu_sz, sft2, 1,  tmp, dctm, dst);
}



// description : do inverse matrix multiply, which is only called by function invTransform (inverse transformation, inv-DCT)
void invMatMul (
    const I32  sz,
    const I32  m   [CTU_SZ][CTU_SZ],
    const I32  src [CTU_SZ][CTU_SZ],
          I32  dst [CTU_SZ][CTU_SZ]
) {
    I32 O[16], E[16], EO[8], EE[8], EEO[4], EEE[4], EEEO[2], EEEE[2];
    
    I32 j, k;
    
    if      (sz == 8 ) {
        for (j=0; j<8; j++) {
            for (k=0; k<4; k++)
                O[k] = m[1][k] * src[1][j] + m[3][k] * src[3][j] + m[5][k] * src[5][j] + m[7][k] * src[7][j];
            EO[0] = m[2][0] * src[2][j] + m[6][0] * src[6][j];
            EO[1] = m[2][1] * src[2][j] + m[6][1] * src[6][j];
            EE[0] = m[0][0] * src[0][j] + m[4][0] * src[4][j];
            EE[1] = m[0][1] * src[0][j] + m[4][1] * src[4][j];
            E[0] = EE[0] + EO[0];
            E[1] = EE[1] + EO[1];
            E[2] = EE[1] - EO[1];
            E[3] = EE[0] - EO[0];
            for (k=0; k<4; k++) {
                dst[j][k]   = E[k]   + O[k];
                dst[j][k+4] = E[3-k] - O[3-k];
            }
        }
        
    } else if (sz == 16) {
        for (j=0; j<16; j++) {
            for (k=0; k<8; k++)
                O[k] = m[ 1][k] * src[ 1][j] + m[ 3][k] * src[ 3][j] + m[ 5][k] * src[ 5][j] + m[ 7][k] * src[ 7][j] + m[ 9][k] * src[ 9][j] + m[11][k] * src[11][j] + m[13][k] * src[13][j] + m[15][k] * src[15][j];
            for (k=0; k<4; k++)
                EO[k]= m[ 2][k] * src[ 2][j] + m[ 6][k] * src[ 6][j] + m[10][k] * src[10][j] + m[14][k] * src[14][j];
            EEO[0] = m[4][0] * src[4][j] + m[12][0] * src[12][j];
            EEO[1] = m[4][1] * src[4][j] + m[12][1] * src[12][j];
            EEE[0] = m[0][0] * src[0][j] + m[ 8][0] * src[ 8][j];
            EEE[1] = m[0][1] * src[0][j] + m[ 8][1] * src[ 8][j];
            EE[0] = EEE[0] + EEO[0];
            EE[1] = EEE[1] + EEO[1];
            EE[2] = EEE[1] - EEO[1];
            EE[3] = EEE[0] - EEO[0];
            for (k=0; k<4; k++) {
                E[k]   = EE[k]   + EO[k];
                E[k+4] = EE[3-k] - EO[3-k];
            }
            for (k=0; k<8; k++) {
                dst[j][k]   = E[k]   + O[k];
                dst[j][k+8] = E[7-k] - O[7-k];
            }
        }
        
    } else if (sz == 32) {
        for (j=0; j<32; j++) {
            for (k=0; k<16; k++)
                O[k]  = m[ 1][k] * src[ 1][j] + m[ 3][k] * src[ 3][j] + m[ 5][k] * src[ 5][j] + m[ 7][k] * src[ 7][j] + m[ 9][k] * src[ 9][j] + m[11][k] * src[11][j] + m[13][k] * src[13][j] + m[15][k] * src[15][j] + m[17][k] * src[17][j] + m[19][k] * src[19][j] + m[21][k] * src[21][j] + m[23][k] * src[23][j] + m[25][k] * src[25][j] + m[27][k] * src[27][j] + m[29][k] * src[29][j] + m[31][k] * src[31][j];
            for (k=0; k<8; k++)
                EO[k] = m[ 2][k] * src[ 2][j] + m[ 6][k] * src[ 6][j] + m[10][k] * src[10][j] + m[14][k] * src[14][j] + m[18][k] * src[18][j] + m[22][k] * src[22][j] + m[26][k] * src[26][j] + m[30][k] * src[30][j];
            for (k=0; k<4; k++)
                EEO[k]= m[ 4][k] * src[ 4][j] + m[12][k] * src[12][j] + m[20][k] * src[20][j] + m[28][k] * src[28][j];
            EEEO[0] = m[8][0] * src[8][j] + m[24][0] * src[24][j];
            EEEO[1] = m[8][1] * src[8][j] + m[24][1] * src[24][j];
            EEEE[0] = m[0][0] * src[0][j] + m[16][0] * src[16][j];
            EEEE[1] = m[0][1] * src[0][j] + m[16][1] * src[16][j];
            EEE[0] = EEEE[0] + EEEO[0];
            EEE[1] = EEEE[1] + EEEO[1];
            EEE[2] = EEEE[1] - EEEO[1];
            EEE[3] = EEEE[0] - EEEO[0];
            for (k=0; k<4; k++) {
                EE[k]   = EEE[k]   + EEO[k];
                EE[k+4] = EEE[3-k] - EEO[3-k];
            }
            for (k=0; k<8; k++) {
                E[k]   = EE[k]   + EO[k];
                E[k+8] = EE[7-k] - EO[7-k];
            }
            for (k=0; k<16; k++) {
                dst[j][k]    = E[k]    + O[k];
                dst[j][k+16] = E[15-k] - O[15-k];
            }
        }
    }
}



// description : do inverse transformation (inv-DCT), getting the reconstructed residual CU
void invTransform (
    const I32  cudep,
    const I32  ch,
    const I32  src   [CTU_SZ][CTU_SZ],
          I32  dst   [CTU_SZ][CTU_SZ]
) {
    const I32  cu_sz = GET_CU_SZ(cudep, ch);

    const I32 (*dctm) [CTU_SZ] = (cu_sz == 8) ? DCT8_MAT : (cu_sz == 16) ? DCT16_MAT : (cu_sz == 32) ? DCT32_MAT : NULL;

    const I32 sft1 = 7;
    const I32 sft2 = 12;
    const I32 add1 = (1<<sft1>>1);
    const I32 add2 = (1<<sft2>>1);
    
    I32 tmp [CTU_SZ][CTU_SZ];
    
    I32 i, j;
    
    invMatMul(cu_sz, dctm, src, tmp);
    
    for (i=0; i<cu_sz; i++)
        for (j=0; j<cu_sz; j++)
            tmp[i][j] = COEF_CLIP( (tmp[i][j] + add1) >> sft1 );
    
    invMatMul(cu_sz, dctm, tmp, dst);
    
    for (i=0; i<cu_sz; i++)
        for (j=0; j<cu_sz; j++)
            dst[i][j] = COEF_CLIP( (dst[i][j] + add2) >> sft2 );
}





///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// definitions and functions for quantize and de-quantize
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



// description : get the scan order in CU for quantize and encode
I32 getScanOrder (                 // return: scan_type
    const I32  cudep,
    const I32  ch,
    const I32  pmode,
    const UI8 (**p_scan) [2]       // output: array of scan order
) {
    static const UI8 SCAN_HOR_8x8    [][2] = {{0,0},{0,1},{0,2},{0,3},{1,0},{1,1},{1,2},{1,3},{2,0},{2,1},{2,2},{2,3},{3,0},{3,1},{3,2},{3,3},{0,4},{0,5},{0,6},{0,7},{1,4},{1,5},{1,6},{1,7},{2,4},{2,5},{2,6},{2,7},{3,4},{3,5},{3,6},{3,7},{4,0},{4,1},{4,2},{4,3},{5,0},{5,1},{5,2},{5,3},{6,0},{6,1},{6,2},{6,3},{7,0},{7,1},{7,2},{7,3},{4,4},{4,5},{4,6},{4,7},{5,4},{5,5},{5,6},{5,7},{6,4},{6,5},{6,6},{6,7},{7,4},{7,5},{7,6},{7,7}};
    static const UI8 SCAN_HOR_16x16  [][2] = {{0,0},{0,1},{0,2},{0,3},{1,0},{1,1},{1,2},{1,3},{2,0},{2,1},{2,2},{2,3},{3,0},{3,1},{3,2},{3,3},{0,4},{0,5},{0,6},{0,7},{1,4},{1,5},{1,6},{1,7},{2,4},{2,5},{2,6},{2,7},{3,4},{3,5},{3,6},{3,7},{0,8},{0,9},{0,10},{0,11},{1,8},{1,9},{1,10},{1,11},{2,8},{2,9},{2,10},{2,11},{3,8},{3,9},{3,10},{3,11},{0,12},{0,13},{0,14},{0,15},{1,12},{1,13},{1,14},{1,15},{2,12},{2,13},{2,14},{2,15},{3,12},{3,13},{3,14},{3,15},{4,0},{4,1},{4,2},{4,3},{5,0},{5,1},{5,2},{5,3},{6,0},{6,1},{6,2},{6,3},{7,0},{7,1},{7,2},{7,3},{4,4},{4,5},{4,6},{4,7},{5,4},{5,5},{5,6},{5,7},{6,4},{6,5},{6,6},{6,7},{7,4},{7,5},{7,6},{7,7},{4,8},{4,9},{4,10},{4,11},{5,8},{5,9},{5,10},{5,11},{6,8},{6,9},{6,10},{6,11},{7,8},{7,9},{7,10},{7,11},{4,12},{4,13},{4,14},{4,15},{5,12},{5,13},{5,14},{5,15},{6,12},{6,13},{6,14},{6,15},{7,12},{7,13},{7,14},{7,15},{8,0},{8,1},{8,2},{8,3},{9,0},{9,1},{9,2},{9,3},{10,0},{10,1},{10,2},{10,3},{11,0},{11,1},{11,2},{11,3},{8,4},{8,5},{8,6},{8,7},{9,4},{9,5},{9,6},{9,7},{10,4},{10,5},{10,6},{10,7},{11,4},{11,5},{11,6},{11,7},{8,8},{8,9},{8,10},{8,11},{9,8},{9,9},{9,10},{9,11},{10,8},{10,9},{10,10},{10,11},{11,8},{11,9},{11,10},{11,11},{8,12},{8,13},{8,14},{8,15},{9,12},{9,13},{9,14},{9,15},{10,12},{10,13},{10,14},{10,15},{11,12},{11,13},{11,14},{11,15},{12,0},{12,1},{12,2},{12,3},{13,0},{13,1},{13,2},{13,3},{14,0},{14,1},{14,2},{14,3},{15,0},{15,1},{15,2},{15,3},{12,4},{12,5},{12,6},{12,7},{13,4},{13,5},{13,6},{13,7},{14,4},{14,5},{14,6},{14,7},{15,4},{15,5},{15,6},{15,7},{12,8},{12,9},{12,10},{12,11},{13,8},{13,9},{13,10},{13,11},{14,8},{14,9},{14,10},{14,11},{15,8},{15,9},{15,10},{15,11},{12,12},{12,13},{12,14},{12,15},{13,12},{13,13},{13,14},{13,15},{14,12},{14,13},{14,14},{14,15},{15,12},{15,13},{15,14},{15,15}};
    static const UI8 SCAN_VER_8x8    [][2] = {{0,0},{1,0},{2,0},{3,0},{0,1},{1,1},{2,1},{3,1},{0,2},{1,2},{2,2},{3,2},{0,3},{1,3},{2,3},{3,3},{4,0},{5,0},{6,0},{7,0},{4,1},{5,1},{6,1},{7,1},{4,2},{5,2},{6,2},{7,2},{4,3},{5,3},{6,3},{7,3},{0,4},{1,4},{2,4},{3,4},{0,5},{1,5},{2,5},{3,5},{0,6},{1,6},{2,6},{3,6},{0,7},{1,7},{2,7},{3,7},{4,4},{5,4},{6,4},{7,4},{4,5},{5,5},{6,5},{7,5},{4,6},{5,6},{6,6},{7,6},{4,7},{5,7},{6,7},{7,7}};
    static const UI8 SCAN_VER_16x16  [][2] = {{0,0},{1,0},{2,0},{3,0},{0,1},{1,1},{2,1},{3,1},{0,2},{1,2},{2,2},{3,2},{0,3},{1,3},{2,3},{3,3},{4,0},{5,0},{6,0},{7,0},{4,1},{5,1},{6,1},{7,1},{4,2},{5,2},{6,2},{7,2},{4,3},{5,3},{6,3},{7,3},{8,0},{9,0},{10,0},{11,0},{8,1},{9,1},{10,1},{11,1},{8,2},{9,2},{10,2},{11,2},{8,3},{9,3},{10,3},{11,3},{12,0},{13,0},{14,0},{15,0},{12,1},{13,1},{14,1},{15,1},{12,2},{13,2},{14,2},{15,2},{12,3},{13,3},{14,3},{15,3},{0,4},{1,4},{2,4},{3,4},{0,5},{1,5},{2,5},{3,5},{0,6},{1,6},{2,6},{3,6},{0,7},{1,7},{2,7},{3,7},{4,4},{5,4},{6,4},{7,4},{4,5},{5,5},{6,5},{7,5},{4,6},{5,6},{6,6},{7,6},{4,7},{5,7},{6,7},{7,7},{8,4},{9,4},{10,4},{11,4},{8,5},{9,5},{10,5},{11,5},{8,6},{9,6},{10,6},{11,6},{8,7},{9,7},{10,7},{11,7},{12,4},{13,4},{14,4},{15,4},{12,5},{13,5},{14,5},{15,5},{12,6},{13,6},{14,6},{15,6},{12,7},{13,7},{14,7},{15,7},{0,8},{1,8},{2,8},{3,8},{0,9},{1,9},{2,9},{3,9},{0,10},{1,10},{2,10},{3,10},{0,11},{1,11},{2,11},{3,11},{4,8},{5,8},{6,8},{7,8},{4,9},{5,9},{6,9},{7,9},{4,10},{5,10},{6,10},{7,10},{4,11},{5,11},{6,11},{7,11},{8,8},{9,8},{10,8},{11,8},{8,9},{9,9},{10,9},{11,9},{8,10},{9,10},{10,10},{11,10},{8,11},{9,11},{10,11},{11,11},{12,8},{13,8},{14,8},{15,8},{12,9},{13,9},{14,9},{15,9},{12,10},{13,10},{14,10},{15,10},{12,11},{13,11},{14,11},{15,11},{0,12},{1,12},{2,12},{3,12},{0,13},{1,13},{2,13},{3,13},{0,14},{1,14},{2,14},{3,14},{0,15},{1,15},{2,15},{3,15},{4,12},{5,12},{6,12},{7,12},{4,13},{5,13},{6,13},{7,13},{4,14},{5,14},{6,14},{7,14},{4,15},{5,15},{6,15},{7,15},{8,12},{9,12},{10,12},{11,12},{8,13},{9,13},{10,13},{11,13},{8,14},{9,14},{10,14},{11,14},{8,15},{9,15},{10,15},{11,15},{12,12},{13,12},{14,12},{15,12},{12,13},{13,13},{14,13},{15,13},{12,14},{13,14},{14,14},{15,14},{12,15},{13,15},{14,15},{15,15}};
    static const UI8 SCAN_DIAG_8x8   [][2] = {{0,0},{1,0},{0,1},{2,0},{1,1},{0,2},{3,0},{2,1},{1,2},{0,3},{3,1},{2,2},{1,3},{3,2},{2,3},{3,3},{4,0},{5,0},{4,1},{6,0},{5,1},{4,2},{7,0},{6,1},{5,2},{4,3},{7,1},{6,2},{5,3},{7,2},{6,3},{7,3},{0,4},{1,4},{0,5},{2,4},{1,5},{0,6},{3,4},{2,5},{1,6},{0,7},{3,5},{2,6},{1,7},{3,6},{2,7},{3,7},{4,4},{5,4},{4,5},{6,4},{5,5},{4,6},{7,4},{6,5},{5,6},{4,7},{7,5},{6,6},{5,7},{7,6},{6,7},{7,7}};
    static const UI8 SCAN_DIAG_16x16 [][2] = {{0,0},{1,0},{0,1},{2,0},{1,1},{0,2},{3,0},{2,1},{1,2},{0,3},{3,1},{2,2},{1,3},{3,2},{2,3},{3,3},{4,0},{5,0},{4,1},{6,0},{5,1},{4,2},{7,0},{6,1},{5,2},{4,3},{7,1},{6,2},{5,3},{7,2},{6,3},{7,3},{0,4},{1,4},{0,5},{2,4},{1,5},{0,6},{3,4},{2,5},{1,6},{0,7},{3,5},{2,6},{1,7},{3,6},{2,7},{3,7},{8,0},{9,0},{8,1},{10,0},{9,1},{8,2},{11,0},{10,1},{9,2},{8,3},{11,1},{10,2},{9,3},{11,2},{10,3},{11,3},{4,4},{5,4},{4,5},{6,4},{5,5},{4,6},{7,4},{6,5},{5,6},{4,7},{7,5},{6,6},{5,7},{7,6},{6,7},{7,7},{0,8},{1,8},{0,9},{2,8},{1,9},{0,10},{3,8},{2,9},{1,10},{0,11},{3,9},{2,10},{1,11},{3,10},{2,11},{3,11},{12,0},{13,0},{12,1},{14,0},{13,1},{12,2},{15,0},{14,1},{13,2},{12,3},{15,1},{14,2},{13,3},{15,2},{14,3},{15,3},{8,4},{9,4},{8,5},{10,4},{9,5},{8,6},{11,4},{10,5},{9,6},{8,7},{11,5},{10,6},{9,7},{11,6},{10,7},{11,7},{4,8},{5,8},{4,9},{6,8},{5,9},{4,10},{7,8},{6,9},{5,10},{4,11},{7,9},{6,10},{5,11},{7,10},{6,11},{7,11},{0,12},{1,12},{0,13},{2,12},{1,13},{0,14},{3,12},{2,13},{1,14},{0,15},{3,13},{2,14},{1,15},{3,14},{2,15},{3,15},{12,4},{13,4},{12,5},{14,4},{13,5},{12,6},{15,4},{14,5},{13,6},{12,7},{15,5},{14,6},{13,7},{15,6},{14,7},{15,7},{8,8},{9,8},{8,9},{10,8},{9,9},{8,10},{11,8},{10,9},{9,10},{8,11},{11,9},{10,10},{9,11},{11,10},{10,11},{11,11},{4,12},{5,12},{4,13},{6,12},{5,13},{4,14},{7,12},{6,13},{5,14},{4,15},{7,13},{6,14},{5,15},{7,14},{6,15},{7,15},{12,8},{13,8},{12,9},{14,8},{13,9},{12,10},{15,8},{14,9},{13,10},{12,11},{15,9},{14,10},{13,11},{15,10},{14,11},{15,11},{8,12},{9,12},{8,13},{10,12},{9,13},{8,14},{11,12},{10,13},{9,14},{8,15},{11,13},{10,14},{9,15},{11,14},{10,15},{11,15},{12,12},{13,12},{12,13},{14,12},{13,13},{12,14},{15,12},{14,13},{13,14},{12,15},{15,13},{14,14},{13,15},{15,14},{14,15},{15,15}};
    static const UI8 SCAN_DIAG_32x32 [][2] = {{0,0},{1,0},{0,1},{2,0},{1,1},{0,2},{3,0},{2,1},{1,2},{0,3},{3,1},{2,2},{1,3},{3,2},{2,3},{3,3},{4,0},{5,0},{4,1},{6,0},{5,1},{4,2},{7,0},{6,1},{5,2},{4,3},{7,1},{6,2},{5,3},{7,2},{6,3},{7,3},{0,4},{1,4},{0,5},{2,4},{1,5},{0,6},{3,4},{2,5},{1,6},{0,7},{3,5},{2,6},{1,7},{3,6},{2,7},{3,7},{8,0},{9,0},{8,1},{10,0},{9,1},{8,2},{11,0},{10,1},{9,2},{8,3},{11,1},{10,2},{9,3},{11,2},{10,3},{11,3},{4,4},{5,4},{4,5},{6,4},{5,5},{4,6},{7,4},{6,5},{5,6},{4,7},{7,5},{6,6},{5,7},{7,6},{6,7},{7,7},{0,8},{1,8},{0,9},{2,8},{1,9},{0,10},{3,8},{2,9},{1,10},{0,11},{3,9},{2,10},{1,11},{3,10},{2,11},{3,11},{12,0},{13,0},{12,1},{14,0},{13,1},{12,2},{15,0},{14,1},{13,2},{12,3},{15,1},{14,2},{13,3},{15,2},{14,3},{15,3},{8,4},{9,4},{8,5},{10,4},{9,5},{8,6},{11,4},{10,5},{9,6},{8,7},{11,5},{10,6},{9,7},{11,6},{10,7},{11,7},{4,8},{5,8},{4,9},{6,8},{5,9},{4,10},{7,8},{6,9},{5,10},{4,11},{7,9},{6,10},{5,11},{7,10},{6,11},{7,11},{0,12},{1,12},{0,13},{2,12},{1,13},{0,14},{3,12},{2,13},{1,14},{0,15},{3,13},{2,14},{1,15},{3,14},{2,15},{3,15},{16,0},{17,0},{16,1},{18,0},{17,1},{16,2},{19,0},{18,1},{17,2},{16,3},{19,1},{18,2},{17,3},{19,2},{18,3},{19,3},{12,4},{13,4},{12,5},{14,4},{13,5},{12,6},{15,4},{14,5},{13,6},{12,7},{15,5},{14,6},{13,7},{15,6},{14,7},{15,7},{8,8},{9,8},{8,9},{10,8},{9,9},{8,10},{11,8},{10,9},{9,10},{8,11},{11,9},{10,10},{9,11},{11,10},{10,11},{11,11},{4,12},{5,12},{4,13},{6,12},{5,13},{4,14},{7,12},{6,13},{5,14},{4,15},{7,13},{6,14},{5,15},{7,14},{6,15},{7,15},{0,16},{1,16},{0,17},{2,16},{1,17},{0,18},{3,16},{2,17},{1,18},{0,19},{3,17},{2,18},{1,19},{3,18},{2,19},{3,19},{20,0},{21,0},{20,1},{22,0},{21,1},{20,2},{23,0},{22,1},{21,2},{20,3},{23,1},{22,2},{21,3},{23,2},{22,3},{23,3},{16,4},{17,4},{16,5},{18,4},{17,5},{16,6},{19,4},{18,5},{17,6},{16,7},{19,5},{18,6},{17,7},{19,6},{18,7},{19,7},{12,8},{13,8},{12,9},{14,8},{13,9},{12,10},{15,8},{14,9},{13,10},{12,11},{15,9},{14,10},{13,11},{15,10},{14,11},{15,11},{8,12},{9,12},{8,13},{10,12},{9,13},{8,14},{11,12},{10,13},{9,14},{8,15},{11,13},{10,14},{9,15},{11,14},{10,15},{11,15},{4,16},{5,16},{4,17},{6,16},{5,17},{4,18},{7,16},{6,17},{5,18},{4,19},{7,17},{6,18},{5,19},{7,18},{6,19},{7,19},{0,20},{1,20},{0,21},{2,20},{1,21},{0,22},{3,20},{2,21},{1,22},{0,23},{3,21},{2,22},{1,23},{3,22},{2,23},{3,23},{24,0},{25,0},{24,1},{26,0},{25,1},{24,2},{27,0},{26,1},{25,2},{24,3},{27,1},{26,2},{25,3},{27,2},{26,3},{27,3},{20,4},{21,4},{20,5},{22,4},{21,5},{20,6},{23,4},{22,5},{21,6},{20,7},{23,5},{22,6},{21,7},{23,6},{22,7},{23,7},{16,8},{17,8},{16,9},{18,8},{17,9},{16,10},{19,8},{18,9},{17,10},{16,11},{19,9},{18,10},{17,11},{19,10},{18,11},{19,11},{12,12},{13,12},{12,13},{14,12},{13,13},{12,14},{15,12},{14,13},{13,14},{12,15},{15,13},{14,14},{13,15},{15,14},{14,15},{15,15},{8,16},{9,16},{8,17},{10,16},{9,17},{8,18},{11,16},{10,17},{9,18},{8,19},{11,17},{10,18},{9,19},{11,18},{10,19},{11,19},{4,20},{5,20},{4,21},{6,20},{5,21},{4,22},{7,20},{6,21},{5,22},{4,23},{7,21},{6,22},{5,23},{7,22},{6,23},{7,23},{0,24},{1,24},{0,25},{2,24},{1,25},{0,26},{3,24},{2,25},{1,26},{0,27},{3,25},{2,26},{1,27},{3,26},{2,27},{3,27},{28,0},{29,0},{28,1},{30,0},{29,1},{28,2},{31,0},{30,1},{29,2},{28,3},{31,1},{30,2},{29,3},{31,2},{30,3},{31,3},{24,4},{25,4},{24,5},{26,4},{25,5},{24,6},{27,4},{26,5},{25,6},{24,7},{27,5},{26,6},{25,7},{27,6},{26,7},{27,7},{20,8},{21,8},{20,9},{22,8},{21,9},{20,10},{23,8},{22,9},{21,10},{20,11},{23,9},{22,10},{21,11},{23,10},{22,11},{23,11},{16,12},{17,12},{16,13},{18,12},{17,13},{16,14},{19,12},{18,13},{17,14},{16,15},{19,13},{18,14},{17,15},{19,14},{18,15},{19,15},{12,16},{13,16},{12,17},{14,16},{13,17},{12,18},{15,16},{14,17},{13,18},{12,19},{15,17},{14,18},{13,19},{15,18},{14,19},{15,19},{8,20},{9,20},{8,21},{10,20},{9,21},{8,22},{11,20},{10,21},{9,22},{8,23},{11,21},{10,22},{9,23},{11,22},{10,23},{11,23},{4,24},{5,24},{4,25},{6,24},{5,25},{4,26},{7,24},{6,25},{5,26},{4,27},{7,25},{6,26},{5,27},{7,26},{6,27},{7,27},{0,28},{1,28},{0,29},{2,28},{1,29},{0,30},{3,28},{2,29},{1,30},{0,31},{3,29},{2,30},{1,31},{3,30},{2,31},{3,31},{28,4},{29,4},{28,5},{30,4},{29,5},{28,6},{31,4},{30,5},{29,6},{28,7},{31,5},{30,6},{29,7},{31,6},{30,7},{31,7},{24,8},{25,8},{24,9},{26,8},{25,9},{24,10},{27,8},{26,9},{25,10},{24,11},{27,9},{26,10},{25,11},{27,10},{26,11},{27,11},{20,12},{21,12},{20,13},{22,12},{21,13},{20,14},{23,12},{22,13},{21,14},{20,15},{23,13},{22,14},{21,15},{23,14},{22,15},{23,15},{16,16},{17,16},{16,17},{18,16},{17,17},{16,18},{19,16},{18,17},{17,18},{16,19},{19,17},{18,18},{17,19},{19,18},{18,19},{19,19},{12,20},{13,20},{12,21},{14,20},{13,21},{12,22},{15,20},{14,21},{13,22},{12,23},{15,21},{14,22},{13,23},{15,22},{14,23},{15,23},{8,24},{9,24},{8,25},{10,24},{9,25},{8,26},{11,24},{10,25},{9,26},{8,27},{11,25},{10,26},{9,27},{11,26},{10,27},{11,27},{4,28},{5,28},{4,29},{6,28},{5,29},{4,30},{7,28},{6,29},{5,30},{4,31},{7,29},{6,30},{5,31},{7,30},{6,31},{7,31},{28,8},{29,8},{28,9},{30,8},{29,9},{28,10},{31,8},{30,9},{29,10},{28,11},{31,9},{30,10},{29,11},{31,10},{30,11},{31,11},{24,12},{25,12},{24,13},{26,12},{25,13},{24,14},{27,12},{26,13},{25,14},{24,15},{27,13},{26,14},{25,15},{27,14},{26,15},{27,15},{20,16},{21,16},{20,17},{22,16},{21,17},{20,18},{23,16},{22,17},{21,18},{20,19},{23,17},{22,18},{21,19},{23,18},{22,19},{23,19},{16,20},{17,20},{16,21},{18,20},{17,21},{16,22},{19,20},{18,21},{17,22},{16,23},{19,21},{18,22},{17,23},{19,22},{18,23},{19,23},{12,24},{13,24},{12,25},{14,24},{13,25},{12,26},{15,24},{14,25},{13,26},{12,27},{15,25},{14,26},{13,27},{15,26},{14,27},{15,27},{8,28},{9,28},{8,29},{10,28},{9,29},{8,30},{11,28},{10,29},{9,30},{8,31},{11,29},{10,30},{9,31},{11,30},{10,31},{11,31},{28,12},{29,12},{28,13},{30,12},{29,13},{28,14},{31,12},{30,13},{29,14},{28,15},{31,13},{30,14},{29,15},{31,14},{30,15},{31,15},{24,16},{25,16},{24,17},{26,16},{25,17},{24,18},{27,16},{26,17},{25,18},{24,19},{27,17},{26,18},{25,19},{27,18},{26,19},{27,19},{20,20},{21,20},{20,21},{22,20},{21,21},{20,22},{23,20},{22,21},{21,22},{20,23},{23,21},{22,22},{21,23},{23,22},{22,23},{23,23},{16,24},{17,24},{16,25},{18,24},{17,25},{16,26},{19,24},{18,25},{17,26},{16,27},{19,25},{18,26},{17,27},{19,26},{18,27},{19,27},{12,28},{13,28},{12,29},{14,28},{13,29},{12,30},{15,28},{14,29},{13,30},{12,31},{15,29},{14,30},{13,31},{15,30},{14,31},{15,31},{28,16},{29,16},{28,17},{30,16},{29,17},{28,18},{31,16},{30,17},{29,18},{28,19},{31,17},{30,18},{29,19},{31,18},{30,19},{31,19},{24,20},{25,20},{24,21},{26,20},{25,21},{24,22},{27,20},{26,21},{25,22},{24,23},{27,21},{26,22},{25,23},{27,22},{26,23},{27,23},{20,24},{21,24},{20,25},{22,24},{21,25},{20,26},{23,24},{22,25},{21,26},{20,27},{23,25},{22,26},{21,27},{23,26},{22,27},{23,27},{16,28},{17,28},{16,29},{18,28},{17,29},{16,30},{19,28},{18,29},{17,30},{16,31},{19,29},{18,30},{17,31},{19,30},{18,31},{19,31},{28,20},{29,20},{28,21},{30,20},{29,21},{28,22},{31,20},{30,21},{29,22},{28,23},{31,21},{30,22},{29,23},{31,22},{30,23},{31,23},{24,24},{25,24},{24,25},{26,24},{25,25},{24,26},{27,24},{26,25},{25,26},{24,27},{27,25},{26,26},{25,27},{27,26},{26,27},{27,27},{20,28},{21,28},{20,29},{22,28},{21,29},{20,30},{23,28},{22,29},{21,30},{20,31},{23,29},{22,30},{21,31},{23,30},{22,31},{23,31},{28,24},{29,24},{28,25},{30,24},{29,25},{28,26},{31,24},{30,25},{29,26},{28,27},{31,25},{30,26},{29,27},{31,26},{30,27},{31,27},{24,28},{25,28},{24,29},{26,28},{25,29},{24,30},{27,28},{26,29},{25,30},{24,31},{27,29},{26,30},{25,31},{27,30},{26,31},{27,31},{28,28},{29,28},{28,29},{30,28},{29,29},{28,30},{31,28},{30,29},{29,30},{28,31},{31,29},{30,30},{29,31},{31,30},{30,31},{31,31}};
    
    const I32  cu_sz = GET_CU_SZ(cudep, ch);

    if        (cudep >= 2 && ABS(pmode-PMODE_VER) <= 4) {
        *p_scan = (cu_sz == 8) ? SCAN_HOR_8x8  : (cu_sz == 16) ? SCAN_HOR_16x16  : NULL;
        return SCAN_TYPE_HOR;
    } else if (cudep >= 2 && ABS(pmode-PMODE_HOR) <= 4) {
        *p_scan = (cu_sz == 8) ? SCAN_VER_8x8  : (cu_sz == 16) ? SCAN_VER_16x16  : NULL;
        return SCAN_TYPE_VER;
    } else {
        *p_scan = (cu_sz == 8) ? SCAN_DIAG_8x8 : (cu_sz == 16) ? SCAN_DIAG_16x16 : (cu_sz == 32) ? SCAN_DIAG_32x32 : NULL;
        return SCAN_TYPE_DIAG;
    }
}



I32 getIcRate (I32 level) {
    if (level >= 3) {
        level -= 3;
        if (level < 3) {
            return 92000 + ((level+1)<<15);
        } else {
            I32 length = 0;
            level -= 3;
            while ( level >= (1<<length) ) {
                level -= 1<<length;
                length += 1;
            }
            return 92000 + ((3+length*2+1)<<15);
        }
    } else if (level == 2) {
        return 90000;
    } else if (level == 1) {
        return 70000;
    } else {
        return 0;
    }
}



// description : simplified rate-distortion optimized quantize for a CU
void rDOQuantize (
    const I32  cudep,
    const I32  ch,
    const I32  pmode,
    const I32  qpd6,
    const I32  cu_tran [CTU_SZ][CTU_SZ],
          I32  cu_quat [CTU_SZ][CTU_SZ]
) {
    const I32  cu_sz      = GET_CU_SZ     (cudep, ch);
    const I32  log2_cu_sz = GET_LOG2_CU_SZ(cudep, ch);
    
    const I32  iq_bits    = 21 + qpd6 - log2_cu_sz;
    const I32  iq_value   = (1<<iq_bits>>1);
    const I32  max_dlevel = I32_MAX_VALUE - iq_value;
    
    const I32  dist_sft1  = 10 - log2_cu_sz;
    const I32  dist_sft2  = 7;
    
    I32  i_cg , i_in_cg , y , x , sum_abs=0;
    
    const UI8 (*scan) [2];
    getScanOrder(cudep, ch, pmode, &scan);
    
    for (y=0; y<cu_sz; y++) {
        for (x=0; x<cu_sz; x++) {
            I32  absval    = ABS(cu_tran[y][x]);
            I32  dlevel    = (absval>0x1ffff) ? max_dlevel : MIN( (absval & 0x1ffff)<<14 , max_dlevel );
            I32  level     = MIN( COEF_MAX_VALUE, (dlevel+iq_value) >> iq_bits );
            I32  min_level = MAX(0, level-2);
            I32  best_cost = I32_MAX_VALUE;

            for (; level>=min_level; level--) {
                I32 dist1 = ABS( dlevel-(level<<iq_bits) ) >> dist_sft1;
                I32 dist  = (dist1<46340) ? (dist1*dist1) : I32_MAX_VALUE;           // 46340^2 ~= I32_MAX_VALUE
                I32 cost  = calcRDcost(qpd6, dist>>dist_sft2, getIcRate(level) );

                if (cost < best_cost) {                                              // if current cost is smaller than previous cost
                    best_cost = cost;
                    cu_quat[y][x] = level;
                }
            }

            if (cu_tran[y][x] < 0)
                cu_quat[y][x] *= -1;                                                 // recover the sign
        }
    }
    
    for ( i_cg=GET_I_CG(cu_sz*cu_sz)-1 ; i_cg>=0 ; i_cg-- ) {                        // reverse scan
        I32 sum_abs_cg = 0;
        for (i_in_cg=0; i_in_cg<CG_SZxSZ; i_in_cg++) {
            const I32  i = i_cg*CG_SZxSZ + i_in_cg;
            y = scan[i][0];
            x = scan[i][1];
            sum_abs_cg += ABS(cu_quat[y][x]);                                        // calculate sum of abs of this CG
        }
        if (sum_abs <= 0 && sum_abs_cg <= 2) {                                       // if the previous met CG are all zero, and this CG is too small
            for (i_in_cg=0; i_in_cg<CG_SZxSZ; i_in_cg++) {                           // set this CG to zero
                const I32  i = i_cg*CG_SZxSZ + i_in_cg;
                y = scan[i][0];
                x = scan[i][1];
                cu_quat[y][x] = 0;
            }
        } else 
            sum_abs += sum_abs_cg;
    }
    
    if (sum_abs >= 2) {                                                              // signBitHidingHDQ
        BOOL n_last_cg = 0;
        
        for ( i_cg=GET_I_CG(cu_sz*cu_sz)-1 ; i_cg>=0 ; i_cg-- ) {
            I32 i_in_cg_first=-1, i_in_cg_diff=0;
            I32 y_last=0, x_last=0;
            UI8 lsb = 0;
            
            for (i_in_cg=0; i_in_cg<CG_SZxSZ; i_in_cg++) {
                const I32  i = i_cg*CG_SZxSZ + i_in_cg;
                y = scan[i][0];
                x = scan[i][1];
                
                lsb ^= (cu_quat[y][x] & 1);
                
                if (cu_quat[y][x] != 0) {
                    if (i_in_cg_first < 0) {                                         // meet the first non-zero coefficient in CG
                        i_in_cg_first = i_in_cg;
                        if (cu_quat[y][x] < 0)
                            lsb ^= 1;
                    }
                    i_in_cg_diff = i_in_cg - i_in_cg_first;
                }
                
                if (cu_quat[y][x] != 0 || n_last_cg) {
                    y_last = y;
                    x_last = x;
                }
            }
            
            if (i_in_cg_first >= 0)
                n_last_cg = 1;
            
            if (lsb && i_in_cg_diff>=4)
                cu_quat[y_last][x_last] += (cu_tran[y_last][x_last]<0) ? 1 : -1;
        }
    }
}



// description : de-quantize
void deQuantize (
    const I32  cudep,
    const I32  ch,
    const I32  qpd6,
    const I32  cu_quat [CTU_SZ][CTU_SZ],
          I32  cu_deqt [CTU_SZ][CTU_SZ]
) {
    const I32  cu_sz = GET_CU_SZ(cudep, ch);
    const I32  sft   = 7 + qpd6 - GET_LOG2_CU_SZ(cudep, ch);
    I32 i, j;
    
    for (i=0; i<cu_sz; i++)
        for (j=0; j<cu_sz; j++)
            cu_deqt[i][j] = COEF_CLIP( cu_quat[i][j] << sft );
}





///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// functions for getting the probable prediction modes from the context
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// description : Get most probable intra prediction modes according to its above and left CU's prediction modes.
void getProbablePmodes (
    const I32  pmode_left,
    const I32  pmode_above,
          I32  pmodes [3]
) {
    if (pmode_left != pmode_above) {
        pmodes[0] = pmode_left;
        pmodes[1] = pmode_above;
        if (pmode_left != PMODE_PLANAR  &&  pmode_above != PMODE_PLANAR)
            pmodes[2] = PMODE_PLANAR;
        else if (pmode_left + pmode_above < 2)
            pmodes[2] = PMODE_VER;
        else
            pmodes[2] = PMODE_DC;
    } else if (pmode_left > PMODE_DC) {
        pmodes[0] = pmode_left;
        pmodes[1] = ((pmode_left + 29) % 32) + 2;
        pmodes[2] = ((pmode_left - 1 ) % 32) + 2;
    } else {
        pmodes[0] = PMODE_PLANAR;
        pmodes[1] = PMODE_DC;
        pmodes[2] = PMODE_VER;
    }
}





///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// definitions of context value
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const UI8 CONTEXT_NEXT_STATE_MPS [] = {2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,124,125,126,127};
const UI8 CONTEXT_NEXT_STATE_LPS [] = {1,  0,  0,  1,  2,  3,  4,  5,  4,  5,  8,  9,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 18, 19, 22, 23, 22, 23, 24, 25, 26, 27, 26, 27, 30, 31, 30, 31, 32, 33, 32, 33, 36, 37, 36, 37, 38, 39, 38, 39, 42, 43, 42, 43, 44, 45, 44, 45, 46, 47, 48, 49, 48, 49, 50, 51, 52, 53, 52, 53, 54, 55, 54, 55, 56, 57, 58, 59, 58, 59, 60, 61, 60, 61, 60, 61, 62, 63, 64, 65, 64, 65, 66, 67, 66, 67, 66, 67, 68, 69, 68, 69, 70, 71, 70, 71, 70, 71, 72, 73, 72, 73, 72, 73, 74, 75, 74, 75, 74, 75, 76, 77, 76, 77,126,127};

const UI8 CABAC_LPS_TABLE [][4] = {
  {128, 176, 208, 240}, {128, 167, 197, 227}, {128, 158, 187, 216}, {123, 150, 178, 205},
  {116, 142, 169, 195}, {111, 135, 160, 185}, {105, 128, 152, 175}, {100, 122, 144, 166},
  { 95, 116, 137, 158}, { 90, 110, 130, 150}, { 85, 104, 123, 142}, { 81,  99, 117, 135},
  { 77,  94, 111, 128}, { 73,  89, 105, 122}, { 69,  85, 100, 116}, { 66,  80,  95, 110},
  { 62,  76,  90, 104}, { 59,  72,  86,  99}, { 56,  69,  81,  94}, { 53,  65,  77,  89},
  { 51,  62,  73,  85}, { 48,  59,  69,  80}, { 46,  56,  66,  76}, { 43,  53,  63,  72},
  { 41,  50,  59,  69}, { 39,  48,  56,  65}, { 37,  45,  54,  62}, { 35,  43,  51,  59},
  { 33,  41,  48,  56}, { 32,  39,  46,  53}, { 30,  37,  43,  50}, { 29,  35,  41,  48},
  { 27,  33,  39,  45}, { 26,  31,  37,  43}, { 24,  30,  35,  41}, { 23,  28,  33,  39},
  { 22,  27,  32,  37}, { 21,  26,  30,  35}, { 20,  24,  29,  33}, { 19,  23,  27,  31},
  { 18,  22,  26,  30}, { 17,  21,  25,  28}, { 16,  20,  23,  27}, { 15,  19,  22,  25},
  { 14,  18,  21,  24}, { 14,  17,  20,  23}, { 13,  16,  19,  22}, { 12,  15,  18,  21},
  { 12,  14,  17,  20}, { 11,  14,  16,  19}, { 11,  13,  15,  18}, { 10,  12,  15,  17},
  { 10,  12,  14,  16}, {  9,  11,  13,  15}, {  9,  11,  12,  14}, {  8,  10,  12,  14},
  {  8,   9,  11,  13}, {  7,   9,  11,  12}, {  7,   9,  10,  12}, {  7,   8,  10,  11},
  {  6,   8,   9,  11}, {  6,   7,   9,  10}, {  6,   7,   8,   9}, {  2,   2,   2,   2}
};

const UI8 CABAC_RENORM_TABLE [] = { 6, 5, 4, 4, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };


// operations for context value (ctx_val), note that ctx_val must be UI8 type
#define   UPDATE_LPS(ctx_val)       { ctx_val = CONTEXT_NEXT_STATE_LPS[(ctx_val)]; }
#define   UPDATE_MPS(ctx_val)       { ctx_val = CONTEXT_NEXT_STATE_MPS[(ctx_val)]; }
#define   GET_CTX_STATE(ctx_val)    ( (ctx_val)>>1 )
#define   GET_CTX_MPS(ctx_val)      ( (ctx_val)&1  )
#define   GET_LPS(ctx_val,range)    ( CABAC_LPS_TABLE[GET_CTX_STATE(ctx_val)][((range)>>6)&3] )
#define   GET_NBIT(lps)             ( CABAC_RENORM_TABLE[(lps)>>3] )


UI8 initContextValue (UI8 init_val, I32 qpd6) {
    I32 qp = qpd6 * 6 + 4;
    I32 init_state = ((((init_val>>4)*5-45)*qp) >> 4) + ((init_val&15) << 3) - 16;
    init_state = CLIP(init_state, 1, 126);
    if (init_state >= 64)
        return (UI8)( ((init_state-64)<<1) | 1 );
    else
        return (UI8)( ((63-init_state)<<1) );
}



typedef struct {                      // context set for HEVC encoder
    UI8 splitflag     [3];
    UI8 partsize         ;
    UI8 Y_pmode          ;
    UI8 UV_pmode         ;
    UI8 Y_qt_cbf         ;
    UI8 UV_qt_cbf        ;
    UI8 last_x    [2][15];
    UI8 last_y    [2][15];
    UI8 sig_map       [2];
    UI8 sig_sc       [44];
    UI8 one_sc       [24];
    UI8 abs_sc        [6];
} ContextSet;


ContextSet newContextSet (I32 qpd6) {
    ContextSet ctxs = {
        {139, 141, 157},
        184,
        184,
        63,
        141,
        94,
        {{110, 110, 124, 125, 140, 153, 125, 127, 140, 109, 111, 143, 127, 111,  79} , {108, 123,  63, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154}},
        {{110, 110, 124, 125, 140, 153, 125, 127, 140, 109, 111, 143, 127, 111,  79} , {108, 123,  63, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154}},
        {91, 171},
        {111, 111, 125, 110, 110,  94, 124, 108, 124, 107, 125, 141, 179, 153, 125, 107, 125, 141, 179, 153, 125, 107, 125, 141, 179, 153, 125, 141, 140, 139, 182, 182, 152, 136, 152, 136, 153, 136, 139, 111, 136, 139, 111, 111},
        {140,  92, 137, 138, 140, 152, 138, 139, 153,  74, 149,  92, 139, 107, 122, 152, 140, 179, 166, 182, 140, 227, 122, 197},
        {138, 153, 136, 167, 152, 152}
    };
    
    // initial all the UI8 items in ctxs using function initContextValue
    UI8 *ptr    = (UI8*)&ctxs;
    UI8 *endptr = ptr + sizeof(ctxs);
    for(; ptr<endptr; ptr++)
        *ptr = initContextValue(*ptr, qpd6);
    
    return ctxs;
}





///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// definitions of CABAC coder
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define TMPBUF_LEN  (CTU_SZ*CTU_SZ*3+64)

typedef struct {
    UI8 tmpbuf   [TMPBUF_LEN];
    I32 tmpcnt               ;
    I32 range                ;
    I32 low                  ;
    I32 nbits                ;
    I32 nbytes               ;
    I32 bufbyte              ;
} CABACcoder;


CABACcoder newCABACcoder () {
    CABACcoder coder = { {0}, 0, 510, 0, 23, 0, 0xFF };
    return coder;
}


UI8 *CABACcommitToBuffer (
    UI8 *pbuffer,
    CABACcoder *p,
    I32 remain_cnt
) {
    I32 i, commit_cnt;
    
    if      (remain_cnt < 0)
        remain_cnt = 0;
    else if (remain_cnt > p->tmpcnt)
        remain_cnt = p->tmpcnt;
    
    commit_cnt = p->tmpcnt - remain_cnt;
    p->tmpcnt = remain_cnt;                          // update the tmpbuf's byte count with remain_cnt
    
    for (i=0; i<commit_cnt; i++)
        *(pbuffer++) = p->tmpbuf[i];                  // move a byte from tmpbuf to buffer, and increase the buffer pointer
    
    for (i=0; i<remain_cnt; i++)
        p->tmpbuf[i] = p->tmpbuf[i+commit_cnt];      // move the remain bytes from tmpbuf's end to tmpbuf's start
    
    return pbuffer;                                   // return the new buffer pointer
}


void CABACput (CABACcoder *p, I32 byte) {
    if ( byte <= 0x03 && p->tmpcnt >= 2 && p->tmpbuf[p->tmpcnt-1] == 0x00 && p->tmpbuf[p->tmpcnt-2] == 0x00 ) {
        p->tmpbuf[p->tmpcnt] = 0x03;
        p->tmpcnt ++;
    }
    p->tmpbuf[p->tmpcnt] = (UI8)byte;
    p->tmpcnt ++;
    //if ( p->tmpcnt >= TMPBUF_LEN ) // overflow: should never occur, because the p->tmpbuf (internal buffer of CABAC coder) is large enough
}


I32  CABAClen (CABACcoder *p) {
    return ( 8 * (p->tmpcnt + p->nbytes) + 23 - p->nbits );
}


void CABACfinish (CABACcoder *p) {
    I32 tmp = 0x00;
    if ( ( (p->low) >> (32-p->nbits) ) > 0 ) {
        CABACput(p, p->bufbyte+1);
        p->low -= (1<<(32-p->nbits));
    } else {
        if (p->nbytes > 0)
            CABACput(p, p->bufbyte);
        tmp = 0xff;
    }
    for (; p->nbytes>1; p->nbytes--)
        CABACput(p, tmp);
    tmp = (p->low >> 8) << p->nbits ;
    CABACput(p, tmp >> 16 );
    CABACput(p, tmp >> 8  );
    CABACput(p, tmp       );
}


void CABACupdate (CABACcoder *p) {
    //DEBUG_PRINT("    update() : range=%d  low=%d  nbits=%d  nbytes=%d  bufbyte=%02x" , p->range , p->low , p->nbits , p->nbytes , p->bufbyte );
    if (p->nbits < 12) {
        I32 lead_byte = p->low >> (24-p->nbits);
        p->nbits += 8;
        p->low &= (0xFFFFFFFF >> p->nbits);
        if (lead_byte == 0xFF) {
            p->nbytes ++;
        } else if ( p->nbytes > 0 ) {
            I32 carry = lead_byte >> 8;
            I32 byte  = carry + p->bufbyte;
            p->bufbyte = lead_byte & 0xFF;
            //DEBUG_PRINT("  obyte=%02x" , byte);
            CABACput(p, byte);
            byte = (0xFF + carry) & 0xFF;
            for (; p->nbytes>1; p->nbytes--)
                CABACput(p, byte);
        } else {
            p->nbytes = 1;
            p->bufbyte = lead_byte;
        }
    }
    //DEBUG_PRINT("\n");
}


void CABACputTerminate (CABACcoder *p, BOOL bin) {
    bin = !!bin;
    p->range -= 2;
    if (bin) {
        p->low += p->range;
        p->low   <<= 7;
        p->range = 2<<7;
        p->nbits -= 7;
    } else if (p->range < 256) {
        p->low   <<= 1;
        p->range <<= 1;
        p->nbits --;
    }
    CABACupdate(p);
}


void CABACputBins (CABACcoder *p, I32 bins, I32 len) {   // put bins without context model
    bins &= ((1<<len)-1);
    //DEBUG_PRINT("  putBins(%d, %d)\n" , bins, len);
    while (len > 0) {
        const I32 len_curr = MIN(len, 8);
        I32 bins_curr;
        len -= len_curr;
        bins_curr = (bins>>len) & ((1<<len_curr)-1);
        p->low <<= len_curr;
        p->low += p->range * bins_curr;
        p->nbits -= len_curr;
        CABACupdate(p);
    }
}


void CABACputBin (CABACcoder *p, BOOL bin, UI8 *pCtx) {   // put bin with context model
    I32 lps  = GET_LPS(*pCtx, p->range);
    I32 nbit = GET_NBIT(lps);
    bin = !!bin;
    //DEBUG_PRINT("  putBin(%d, ctx=%d)\n" , bin, *pCtx);
    p->range -= lps;
    if ( bin != GET_CTX_MPS(*pCtx) ) {
        UPDATE_LPS(*pCtx);
        p->low   = ( p->low + p->range ) << nbit;
        p->range = lps << nbit;
        p->nbits -= nbit;
    } else {
        UPDATE_MPS(*pCtx);
        if (p->range < 256) {
            p->low   <<= 1;
            p->range <<= 1;
            p->nbits --;
        }
    }
    CABACupdate(p);
}





///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// functions of put HEVC elements using CABAC coder
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void putSplitFlag (
    CABACcoder *pCABAC,
    ContextSet *pCtxs,
    const I32   cudep,
    const BOOL  splitflag,
    const BOOL  splitflag_left,
    const BOOL  splitflag_above
) {
    const UI8 ctx_idx = (!!splitflag_left) + (!!splitflag_above);
    //DEBUG_PRINT("putSplitFlag(%d)  ctx_idx=%d\n" , splitflag, ctx_idx );
    if (cudep < 2)
        CABACputBin(pCABAC, splitflag, &pCtxs->splitflag[ctx_idx]);
}


void putPartSize (
    CABACcoder *pCABAC,
    ContextSet *pCtxs,
    const I32   cudep
) {
    //DEBUG_PRINT("putPartSize(%d)\n" , cudep );
    if (cudep == 2)
        CABACputBin(pCABAC, 1, &pCtxs->partsize);
}


void putYpmode (
    CABACcoder *pCABAC,
    ContextSet *pCtxs,
          I32   pmode,
    const I32   pmode_left,
    const I32   pmode_above
) {
    I32  pmodes [3];
    I32  i;
    
    getProbablePmodes(pmode_left, pmode_above, pmodes);
    
    //DEBUG_PRINT("putYpmode(%d)  probable_pmode={%d,%d,%d}\n" , pmode, pmodes[0], pmodes[1], pmodes[2] );
    
    for (i=0; i<3; i++) {
        if (pmodes[i] == pmode) {
            CABACputBin(pCABAC, 1, &pCtxs->Y_pmode);
            CABACputBins(pCABAC, (i>0), 1);
            if (i>0)
                CABACputBins(pCABAC, (i-1), 1);
            return;
        }
    }
    
    arraySort (pmodes, 3, 1, NULL);                                                 // sort: from large to small
    
    CABACputBin(pCABAC, 0, &pCtxs->Y_pmode);
    for (i=0; i<3; i++)
        if (pmode > pmodes[i])
            pmode -= 1;
    
    CABACputBins(pCABAC, pmode, 5);
}


void putUVpmode (
    CABACcoder *pCABAC,
    ContextSet *pCtxs
) {
    //DEBUG_PRINT("putUVpmode()\n");
    CABACputBin(pCABAC, 0, &pCtxs->UV_pmode);
}


void putQtCbf (
    CABACcoder *pCABAC,
    ContextSet *pCtxs,
    const I32   ch,
    const BOOL  cbf
) {
    //DEBUG_PRINT("putQtCbf(%d, %d)\n" , ch, cbf );
    if (ch == CH_Y)
        CABACputBin(pCABAC, cbf, &pCtxs->Y_qt_cbf);
    else
        CABACputBin(pCABAC, cbf, &pCtxs->UV_qt_cbf);
}


void putLastSignificantXY (
    CABACcoder *pCABAC,
    ContextSet *pCtxs,
    const I32   cudep,
    const I32   ch,
    const I32   scan_type,
    const I32   y,
    const I32   x
) {
    static const UI8 GROUP_INDEX_TABLE [] = {0, 1, 2, 3, 4, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9};
    static const UI8 MIN_IN_GROUP_TABLE[] = {0, 1, 2, 3, 4, 6, 8, 12, 16, 24};

    const I32 cu_sz = GET_CU_SZ(cudep, ch);
    const I32 log2_cu_sz_m2 = GET_LOG2_CU_SZ(cudep, ch) - 2;
          I32 ty = (scan_type==SCAN_TYPE_VER) ? x : y;
          I32 tx = (scan_type==SCAN_TYPE_VER) ? y : x;
    const I32 gy = GROUP_INDEX_TABLE[ty];
    const I32 gx = GROUP_INDEX_TABLE[tx];
    
    I32 offset, shift, ch_type, i;
    
    //DEBUG_PRINT("putLastSignificantXY(%d, %d, %d, %d, %d)\n" , cudep, ch, scan_type, y, x);
    
    if (ch == CH_Y) {
        offset  = (log2_cu_sz_m2 * 3) + ((log2_cu_sz_m2 + 1) >> 2);
        shift   = (log2_cu_sz_m2 + 3) >> 2;
        ch_type = 0;
    } else {
        offset  = 0;
        shift   = log2_cu_sz_m2;
        ch_type = 1;
    }
    
    for (i=0; i<gx; i++)
        CABACputBin(pCABAC, 1, &pCtxs->last_x[ch_type][offset+( i>>shift)] );
    
    if (gx < GROUP_INDEX_TABLE[cu_sz-1])
        CABACputBin(pCABAC, 0, &pCtxs->last_x[ch_type][offset+(gx>>shift)] );
    
    for (i=0; i<gy; i++)
        CABACputBin(pCABAC, 1, &pCtxs->last_y[ch_type][offset+( i>>shift)] );
    
    if (gy < GROUP_INDEX_TABLE[cu_sz-1])
        CABACputBin(pCABAC, 0, &pCtxs->last_y[ch_type][offset+(gy>>shift)] );
    
    if (gx > 3) {
        tx -= MIN_IN_GROUP_TABLE[gx];
        for (i=(((gx-2)>>1)-1); i>=0; i--)
            CABACputBins(pCABAC, (tx>>i)&1 , 1 );
    }
    
    if (gy > 3) {
        ty -= MIN_IN_GROUP_TABLE[gy];
        for (i=(((gy-2)>>1)-1); i>=0; i--)
            CABACputBins(pCABAC, (ty>>i)&1 , 1 );
    }
}


// description : get the context index of significance bit-map
I32 getSigCtxIdx (          // return: ctx_idx
    const I32  ch,
    const I32  log2_cu_sz_m2,
    const I32  scan_type,
    const I32  y,
    const I32  x,
    const I32  sig_ctx
){
    static const UI8 SIG_MAP_CONTEXT_TABLE [3][4][4] = {
    //scan_type : SCAN_TYPE_DIAG   SCAN_TYPE_HOR   SCAN_TYPE_VER
    // TU size  :  4, 8, 16, 32    4, 8, 16, 32    4, 8, 16, 32
                { {0, 9, 21, 21}, {0,15, 21, 21}, {0,15, 21, 21} },   // Y
                { {0, 9, 12, 12}, {0, 9, 12, 12}, {0, 9, 12, 12} }    // U and V
    };

    static const UI8 CTX_OFFSET_4x4_TABLE [4][4] = {{0, 1, 4, 5}, {2, 3, 4, 5}, {6, 6, 8, 8}, {7, 7, 8, 8}};
    
    static const UI8 CTX_IDX_1 [7] = {2, 1, 1, 0, 0, 0, 0};
    static const UI8 CTX_IDX_2 [4] = {2, 1, 0, 0};
    
    const I32  y_in_cg = y % CG_SZ;
    const I32  x_in_cg = x % CG_SZ;
    
    I32 ctx_idx = SIG_MAP_CONTEXT_TABLE[ch!=CH_Y][scan_type][log2_cu_sz_m2];
    
    if (y==0 && x==0) {                                  // special case for the DC context variable
        ctx_idx = 0;
    } else if (log2_cu_sz_m2 == 0) {                     // for 4x4 TU
        ctx_idx += CTX_OFFSET_4x4_TABLE[y][x];
    } else {
        if      (sig_ctx == 0)
            ctx_idx += CTX_IDX_1[y_in_cg + x_in_cg];
        else if (sig_ctx == 1)
            ctx_idx += CTX_IDX_2[y_in_cg];
        else if (sig_ctx == 2)
            ctx_idx += CTX_IDX_2[x_in_cg];
        else
            ctx_idx += 2;
        
        if (ch==CH_Y && (y/CG_SZ!=0 || x/CG_SZ!=0) )     // is Y , and not the first CG
            ctx_idx += 3;
    }
    
    if (ch!=CH_Y)
        ctx_idx += 28;
    
    return ctx_idx;
}


void putRemainExGolomb (CABACcoder *pCABAC, I32 value, I32 rparam) {
    I32 len, tmp;
    if ( value < (3<<rparam) ) {
        len = value >> rparam;
        CABACputBins(pCABAC, (1<<(len+1))-2    , len+1  );
        CABACputBins(pCABAC, value%(1<<rparam) , rparam );
    } else {
        len = rparam;
        value -= (3<<rparam);
        for(; value>=(1<<len); len++)
            value -= (1<<len);
        tmp = 4+len-rparam;
        CABACputBins(pCABAC, (1<<tmp)-2 , tmp );
        CABACputBins(pCABAC, value      , len );
    }
}


void putCoef (
    CABACcoder *pCABAC,
    ContextSet *pCtxs,
    const I32   cudep,
    const I32   ch,
    const I32   pmode,
    const I32   cu_data [CTU_SZ][CTU_SZ]
) {
    const I32   cu_sz = GET_CU_SZ(cudep, ch);
    const I32   log2_cu_sz_m2 = GET_LOG2_CU_SZ(cudep, ch) - 2;
    
    const UI8 (*scan) [2];
    const I32   scan_type = getScanOrder(cudep, ch, pmode, &scan);
    
    BOOL  sig_map [CTU_SZ/CG_SZ][CTU_SZ/CG_SZ];
    
    I32  i, j, i_last, i_cg, i_cg_last;
    I32  c1=1;

    for (i=0; i<cu_sz/CG_SZ; i++)
        for (j=0; j<cu_sz/CG_SZ; j++)
            sig_map[i][j] = 0;                // initialize sig_map to all-zero
    
    sig_map[0][0] = 1;
    
    for (i=0; i<cu_sz*cu_sz; i++) {           // for all coefficient
        const I32  y = scan[i][0];
        const I32  x = scan[i][1];
        if (cu_data[y][x] != 0) {
            sig_map[y/CG_SZ][x/CG_SZ] = 1;
            i_last = i;
        }
    }
    
    putLastSignificantXY(pCABAC, pCtxs, cudep, ch, scan_type, scan[i_last][0], scan[i_last][1]);
    
    i_cg_last = GET_I_CG(i_last);
    
    for (i_cg=i_cg_last; i_cg>=0; i_cg--) {                             // for all CGs (reverse)
        const I32  y_cg = scan[i_cg*CG_SZxSZ][0] / CG_SZ;
        const I32  x_cg = scan[i_cg*CG_SZxSZ][1] / CG_SZ;
        
        I32 arr_abs_nz [CG_SZxSZ];
        I32 i_nz=0 , signs=0;
        I32 i_in_cg, i_in_cg_diff=0 , i_in_cg_last=-1;
        
        UI8 sig_ctx = 0;
        if ( x_cg < (cu_sz/CG_SZ-1)  &&  sig_map[y_cg][x_cg+1] )        // this CG is not near the TU's right border , and the right CG is significant
            sig_ctx |= 1;
        if ( y_cg < (cu_sz/CG_SZ-1)  &&  sig_map[y_cg+1][x_cg] )        // this CG is not near the TU's bottom border, and the bottom CG is significant
            sig_ctx |= 2;
        
        if ( i_cg != 0  &&  i_cg != i_cg_last )
            CABACputBin(pCABAC, sig_map[y_cg][x_cg], &pCtxs->sig_map[!!sig_ctx] );
        
        if ( sig_map[y_cg][x_cg] ) {                                    // if CG is not all zero
            for (i_in_cg=CG_SZxSZ-1; i_in_cg>=0; i_in_cg--) {           // for all coefficient in this CG (reverse)
                const I32  i = i_cg*CG_SZxSZ + i_in_cg;
                const I32  y = scan[i][0];
                const I32  x = scan[i][1];
                const BOOL sig  = (cu_data[y][x] != 0);
                
                if (i<i_last && (i_in_cg>0 || i_cg==0 || i_nz!=0)) {
                    const I32 ctx_idx = getSigCtxIdx(ch, log2_cu_sz_m2, scan_type, y, x, sig_ctx);
                    CABACputBin(pCABAC, sig, &pCtxs->sig_sc[ctx_idx] );
                }
                
                if (sig) {
                    arr_abs_nz[i_nz] = ABS(cu_data[y][x]);
                    i_nz ++;
                    signs <<= 1;
                    if (cu_data[y][x] <  0)
                        signs |= 1;
                    if (i_in_cg_last < 0)
                        i_in_cg_last = i_in_cg;
                    i_in_cg_diff = i_in_cg_last - i_in_cg;
                }
            }
        }
        
        if (i_nz > 0) {
            const I32 ctx_set = (ch==CH_Y ? 0 : 4) + ((ch==CH_Y && i_cg>0) ? 2 : 0) + (c1==0 ? 1 : 0);
            BOOL escape_flag = i_nz>8;
            I32 idx_first_c2 = -1;
            c1 = 1;
            
            for (i=0; i<8 && i<i_nz; i++) {
                const BOOL flag = (arr_abs_nz[i] > 1);
                CABACputBin(pCABAC, flag, &pCtxs->one_sc[4*ctx_set+c1] );
                if (flag) {
                    c1 = 0;
                    if (idx_first_c2 == -1)
                        idx_first_c2 = i;
                    else
                        escape_flag = 1;
                } else if (c1>0 && c1<3)
                    c1 ++;
            }
            
            if (c1==0 && idx_first_c2!=-1) {
                const BOOL flag = (arr_abs_nz[idx_first_c2] > 2);
                CABACputBin(pCABAC, flag, &pCtxs->abs_sc[ctx_set] );
                escape_flag |= flag;
            }
            
            if (i_in_cg_diff >= 4)           // sign hidden
                CABACputBins(pCABAC, signs>>1 , i_nz-1 );
            else
                CABACputBins(pCABAC, signs    , i_nz   );
            
            if (escape_flag) {
                I32 first_coeff2=1 , gorice_param=0;
                for (i=0; i<i_nz; i++) {
                    I32 base_level = (i<8) ? (2+first_coeff2) : 1;
                    I32 escape_value = arr_abs_nz[i] - base_level;
                    if (arr_abs_nz[i] >= 2)
                        first_coeff2 = 0;
                    if (escape_value >= 0) {
                        putRemainExGolomb(pCABAC, escape_value, gorice_param);
                        if (arr_abs_nz[i] > (3<<gorice_param))
                            gorice_param = MIN(gorice_param+1 , 4);
                    }
                }
            }
        }
    }
}


void putCU (
    CABACcoder *pCABAC,
    ContextSet *pCtxs,
    const I32   cudep,
    const I32   pmode,
    const I32   pmode_left,
    const I32   pmode_above,
    const I32   cu_data [CTU_SZ][CTU_SZ]
) {
    const I32   cu_sz = GET_CU_SZ(cudep, CH_Y);
    BOOL Y_cbf = 0;
    I32  i, j;
    
    for (i=0; i<cu_sz; i++)
        for (j=0; j<cu_sz; j++)
            if ( cu_data[i][j] != 0 ) {
                Y_cbf = 1;
                break;
            }
    
    putPartSize   (pCABAC, pCtxs, cudep);
    putYpmode     (pCABAC, pCtxs, pmode, pmode_left, pmode_above);
    putUVpmode    (pCABAC, pCtxs);
    //putSplitTUFlag(pCABAC, pCtxs, cudep);
    putQtCbf      (pCABAC, pCtxs, CH_U, 0);                          // there is no U data, so U_cbf = 0
    putQtCbf      (pCABAC, pCtxs, CH_V, 0);                          // there is no V data, so V_cbf = 0
    putQtCbf      (pCABAC, pCtxs, CH_Y, Y_cbf);                      // Y_cbf
    if (Y_cbf)
        putCoef   (pCABAC, pCtxs, cudep, CH_Y, pmode, cu_data);
}




///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// functions of put HEVC header to a byte array
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UI8 *putBitsToBuffer (UI8 *pbuffer, I32 *bitpos, I32 bits, I32 len, BOOL flush) {
    for (len--; len>=0; len--) {
        UI8 bit = (bits>>len) & 1;
        if (bit)
            (*pbuffer) |=  (1<<(*bitpos));      // set the bit to 1, and do not effect other bits of this byte
        else
            (*pbuffer) &= ~(1<<(*bitpos));      // set the bit to 0, and do not effect other bits of this byte
        if ((*bitpos) > 0) {                    // current byte not end
            (*bitpos) --;
        } else {                                // current byte end
            (*bitpos) = 7;
            pbuffer ++;                         // move to next byte
        }
    }
    if (flush && (*bitpos)<7) {
        (*pbuffer) &= 0xfe << (*bitpos);        // set all tail bits to 0
        (*bitpos) = 7;
        pbuffer ++;                             // move to next byte
    }
    return pbuffer;
}


UI8 *putUVLCtoBuffer (UI8 *pbuffer, I32 *bitpos, I32 val) {
    I32 tmp, len = 1;
    val ++;
    for (tmp=val+1; tmp!=1; tmp>>=1)
        len += 2;
    return putBitsToBuffer(pbuffer, bitpos, (val & ((1<<((len+1)>>1))-1)) , ((len>>1) + ((len+1)>>1)) , 0 );    // don't flush
}


UI8 *putHeaderToBuffer (UI8  *pbuffer, const I32 ysz, const I32 xsz, const I32 qpd6) {
    static const UI8 HEADER_CONTENT_1 [50] = {0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x0C, 0x01, 0xFF, 0xFF, 0x03, 0x10, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0xB4, 0xF0, 0x24, 0x00, 0x00, 0x01, 0x42, 0x01, 0x01, 0x03, 0x10, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0xB4};
    static const UI8 HEADER_CONTENT_2 [17] = {0x00, 0x00, 0x01, 0x44, 0x01, 0xC1, 0x90, 0x91, 0x81, 0xD9, 0x20, 0x00, 0x00, 0x01, 0x26, 0x01, 0xAC};
    
    //                                 qpd6 =    0              1              2              3              4
    static const UI8 HEADER_CONTENT_3 [][2] = { {0x16, 0xDE} , {0x10, 0xDE} , {0x2B, 0x78} , {0x4D, 0xE0} , {0x97, 0x80} };
    
    I32 i , bitpos=7 ;
    
    for (i=0; i<sizeof(HEADER_CONTENT_1); i++)
        *(pbuffer++) = HEADER_CONTENT_1[i];
    
    pbuffer = putBitsToBuffer(pbuffer, &bitpos, 0x0A, 4, 0);
    pbuffer = putUVLCtoBuffer(pbuffer, &bitpos, xsz);
    pbuffer = putUVLCtoBuffer(pbuffer, &bitpos, ysz);
    pbuffer = putBitsToBuffer(pbuffer, &bitpos, 0x197EE4, 22, 0);
    pbuffer = putBitsToBuffer(pbuffer, &bitpos, 0x707B44, 24, 1);    // flush=1
    
    for (i=0; i<sizeof(HEADER_CONTENT_2); i++)
        *(pbuffer++) = HEADER_CONTENT_2[i];
    
    for (i=0; i<sizeof(HEADER_CONTENT_3[qpd6]); i++)
        *(pbuffer++) = HEADER_CONTENT_3[qpd6][i];
    
    return pbuffer;
}





///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// function for calculating the sum of abs diff or the sum of square diff between two CUs
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

I32 CUsumDiff (const I32 cu_sz, UI8 cu1[CTU_SZ][CTU_SZ], UI8 cu2[CTU_SZ][CTU_SZ], BOOL square) {
    I32 i, j, diff, sum=0;
    for (i=0; i<cu_sz; i++)
        for (j=0; j<cu_sz; j++) {
            diff = ABS( (I32)cu1[i][j] - cu2[i][j] ) ;
            if (square)
                diff *= diff;
            sum += diff;
        }
    return sum;
}





///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// process a CU (recursive)
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

I32 processCURecurs (        // return: RD-cost
    CABACcoder *pCABAC,
    ContextSet *pCtxs,
    const UI8 *img_orig,
          UI8 *img_rcon,
          UI8  map_cudep [ ((1<<CUDEP_MAX)+1) ][ (MAX_XSZ>>LOG2_MIN_CU_SZ) ],
          UI8  map_pmode [ ((1<<CUDEP_MAX)+1) ][ (MAX_XSZ>>LOG2_MIN_CU_SZ) ],
    const I32  ysz,
    const I32  xsz,
    const I32  y,
    const I32  x,
    const I32  cudep,
    const I32  qpd6,
    const I32  pmode_cand
){
    const I32  cu_sz = GET_CU_SZ(cudep, CH_Y);
    
    I32 rdcost , rdcost_best = I32_MAX_VALUE ;
    
    UI8 ubla , ubllb[CTU_SZ*2] , ubaar[CTU_SZ*2];
    UI8 fbla , fbllb[CTU_SZ*2] , fbaar[CTU_SZ*2];
    UI8 cu_orig      [CTU_SZ][CTU_SZ];
    UI8 cu_pred      [CTU_SZ][CTU_SZ];
    I32 cu_resi      [CTU_SZ][CTU_SZ];
    I32 cu_tran      [CTU_SZ][CTU_SZ];
    I32 cu_quat      [CTU_SZ][CTU_SZ];
    I32 cu_deqt      [CTU_SZ][CTU_SZ];
    I32 cu_resr      [CTU_SZ][CTU_SZ];
    UI8 cu_rcon      [CTU_SZ][CTU_SZ];
    UI8 cu_rcon_best [CTU_SZ][CTU_SZ];
    
    CABACcoder tCABAC, noSplitCABAC;
    ContextSet tCtxs , noSplitCtxs;

    const I32 start_bits = CABAClen(pCABAC);

    I32 i, j, pmode_i, pmode, pmode_best, pmode_cand_list[PMODE_COUNT], diff_list[PMODE_COUNT];
    
    const BOOL splitflag_left  = (x<=0) ? 0 :   cudep < map_cudep[ ((y%CTU_SZ)>>LOG2_MIN_CU_SZ)+1 ][ (x>>LOG2_MIN_CU_SZ)-1 ];                                   // if not at the  left border of image, sample splitflag_left
    const BOOL splitflag_above = (y<=0) ? 0 :   cudep < map_cudep[ ((y%CTU_SZ)>>LOG2_MIN_CU_SZ)   ][ (x>>LOG2_MIN_CU_SZ)   ];                                   // if not at the above border of image, sample splitflag_above
    const I32  pmode_left  = (x<=0)        ? PMODE_DC : map_pmode[ ((y%CTU_SZ)>>LOG2_MIN_CU_SZ)+1 ][ (x>>LOG2_MIN_CU_SZ)-1 ];                                   // if not at the  left border of image, sample pmode_left
    const I32  pmode_above = (y%CTU_SZ<=0) ? PMODE_DC : map_pmode[ ((y%CTU_SZ)>>LOG2_MIN_CU_SZ)   ][ (x>>LOG2_MIN_CU_SZ)   ];                                   // if not at the above border of CTU  , sample pmode_above
    
    getBorder(img_rcon, ysz, xsz, y, x, cudep, CH_Y, &ubla, ubllb, ubaar);                                   // get border pixels for reconstructed image
    filterBorder(cudep, CH_Y, ubla, ubllb, ubaar, &fbla, fbllb, fbaar);                                      // filter the border pixels
    
    for (i=0; i<cu_sz; i++)                                                                                  // sample the CU from img_orig
        for (j=0; j<cu_sz; j++)
            cu_orig[i][j] = GET2D(img_orig, ysz, xsz, y+i, x+j);
    
    if (0 < pmode_cand && pmode_cand < PMODE_COUNT) {
        for (pmode=0; pmode<PMODE_COUNT; pmode++) {
            predict(cudep, CH_Y, pmode, ubla, ubllb, ubaar, fbla, fbllb, fbaar, cu_pred);
            diff_list[pmode] = CUsumDiff(cu_sz, cu_orig, cu_pred, 0);                                        // 
        }
        arraySort(diff_list, PMODE_COUNT, 0, pmode_cand_list);
        pmode_cand_list[pmode_cand] = -1;
    } else {
        for (pmode=0; pmode<PMODE_COUNT; pmode++)
            pmode_cand_list[pmode] = pmode;
    }
    
    for (pmode_i=0; pmode_i<PMODE_COUNT; pmode_i++) {
        pmode = pmode_cand_list[pmode_i];
        if (pmode < 0) break;
        
        predict(cudep, CH_Y, pmode, ubla, ubllb, ubaar, fbla, fbllb, fbaar, cu_pred);
        
        for (i=0; i<cu_sz; i++)                                                                              // calculate residual
            for (j=0; j<cu_sz; j++)
                cu_resi[i][j] = (I32)cu_orig[i][j] - cu_pred[i][j];
        
        transform   (cudep, CH_Y, cu_resi, cu_tran);
        rDOQuantize (cudep, CH_Y, pmode, qpd6, cu_tran, cu_quat);
        deQuantize  (cudep, CH_Y, qpd6, cu_quat, cu_deqt);
        invTransform(cudep, CH_Y, cu_deqt, cu_resr);
        
        for (i=0; i<cu_sz; i++)                                                                              // reconstruction
            for (j=0; j<cu_sz; j++)
                cu_rcon[i][j] = PIX_CLIP(cu_resr[i][j] + cu_pred[i][j]);

        tCABAC = *pCABAC;                                                                                    // copy for trying this pmode, note that this operation will copy all the struct elements.
        tCtxs  = *pCtxs;                                                                                     // copy for trying this pmode, note that this operation will copy all the struct elements.
        
        putSplitFlag(&tCABAC, &tCtxs, cudep, 0, splitflag_left, splitflag_above);                            // split_flag=0
        putCU(&tCABAC, &tCtxs, cudep, pmode, pmode_left, pmode_above, cu_quat);
        
        rdcost = calcRDcost(qpd6, CUsumDiff(cu_sz, cu_orig, cu_rcon, 1), (CABAClen(&tCABAC) - start_bits) ); // calculate RD-cost of this pmode

        if (rdcost_best >= rdcost) {                                                                         // if current pmode can let RD-cost be smaller than the previous best RD-cost
            rdcost_best  = rdcost;
            pmode_best   = pmode;
            noSplitCABAC = tCABAC;
            noSplitCtxs  = tCtxs;
            for (i=0; i<cu_sz; i++)
                for (j=0; j<cu_sz; j++)
                    cu_rcon_best[i][j] = cu_rcon[i][j];
        }
    }
    
    if (cudep < CUDEP_MAX) {                                                                                  // try split
        putSplitFlag(pCABAC, pCtxs, cudep, 1, splitflag_left, splitflag_above);                               // split_flag=1
        rdcost = calcRDcost(qpd6, 0, (CABAClen(pCABAC) - start_bits) );
        rdcost += processCURecurs(pCABAC, pCtxs, img_orig, img_rcon, map_cudep, map_pmode, ysz, xsz, y        , x        , cudep+1, qpd6, pmode_cand);
        rdcost += processCURecurs(pCABAC, pCtxs, img_orig, img_rcon, map_cudep, map_pmode, ysz, xsz, y        , x+cu_sz/2, cudep+1, qpd6, pmode_cand);
        rdcost += processCURecurs(pCABAC, pCtxs, img_orig, img_rcon, map_cudep, map_pmode, ysz, xsz, y+cu_sz/2, x        , cudep+1, qpd6, pmode_cand);
        rdcost += processCURecurs(pCABAC, pCtxs, img_orig, img_rcon, map_cudep, map_pmode, ysz, xsz, y+cu_sz/2, x+cu_sz/2, cudep+1, qpd6, pmode_cand);
        
        if (rdcost < rdcost_best)                                                                             // split is better than no split
            return rdcost;
    }
    
    for (i=0; i<cu_sz; i++) {
        for (j=0; j<cu_sz; j++) {
            map_cudep[ (((y+i)%CTU_SZ)>>LOG2_MIN_CU_SZ)+1 ][ (x+j)>>LOG2_MIN_CU_SZ ] = (UI8)cudep;            // fill the map_cudep. Provide context for subsequent CUs
            map_pmode[ (((y+i)%CTU_SZ)>>LOG2_MIN_CU_SZ)+1 ][ (x+j)>>LOG2_MIN_CU_SZ ] = (UI8)pmode_best;       // fill the map_pmode. Provide context for subsequent CUs
            GET2D(img_rcon, ysz, xsz, y+i, x+j) = cu_rcon_best[i][j];                                         // save the reconstructed CU. Provide reference border for subsequent CUs' prediction 
        }
    }
    
    *pCABAC = noSplitCABAC;
    *pCtxs  = noSplitCtxs;
    
    return rdcost_best;
}





///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// top function of HEVC intra-frame image encoder
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

I32 HEVCImageEncoder (           // return    -1:error    positive value:HEVC stream length (in bytes)
          UI8 *pbuffer,          // buffer to save HEVC stream
          UI8 *img,              // 2-D array in 1-D buffer, height=ysz, width=xsz. Input the image to be compressed.
          UI8 *img_rcon,         // 2-D array in 1-D buffer, height=ysz, width=xsz. The HEVC encoder will save the reconstructed image here.
          I32 *ysz,              // point to image height, will be modified (clip to a multiple of CTU_SZ)
          I32 *xsz,              // point to image width , will be modified (clip to a multiple of CTU_SZ)
    const I32  qpd6,             // quant value, must be 0~4. The larger, the higher compression ratio, but the lower quality.
    const I32  pmode_cand        // must be 1~35. The larger, the higher compression ratio, but the lower performance. The recommended value is 7.
) {
    CABACcoder tCABAC = newCABACcoder();
    ContextSet tCtxs  = newContextSet(qpd6);
    
    UI8 *pbuffer_base = pbuffer;
    
    UI8 map_cudep [ ((1<<CUDEP_MAX)+1) ][ (MAX_XSZ>>LOG2_MIN_CU_SZ) ];      // context line-buffer for CU-depth
    UI8 map_pmode [ ((1<<CUDEP_MAX)+1) ][ (MAX_XSZ>>LOG2_MIN_CU_SZ) ];      // context line-buffer for predict mode
    
    I32 y, x;
    
    const I32 yszn = (MIN(MAX_YSZ,(*ysz)) / CTU_SZ) * CTU_SZ;               // clip the image height not be larger than MAX_YSZ, and be the multiple of CTU_SZ
    const I32 xszn = (MIN(MAX_XSZ,(*xsz)) / CTU_SZ) * CTU_SZ;               // clip the image width  not be larger than MAX_YSZ, and be the multiple of CTU_SZ

    if ( yszn < CTU_SZ || xszn < CTU_SZ )                                   // image is smaller than a CTU, which cannot be encoded
        return -1;                                                          // return failed

    for (y=0; y<yszn; y++)
        for (x=0; x<xszn; x++)
            GET2D(img, yszn, xszn, y, x) = GET2D(img, *ysz, *xsz, y, x);    // construct the clipped image pixels
    
    pbuffer = putHeaderToBuffer(pbuffer, yszn, xszn, qpd6);
    
    for (y=0; y<yszn; y+=CTU_SZ) {                                          // for all CTU rows
        for (x=0; x<xszn; x+=CTU_SZ) {                                      // for all CTU columns
            processCURecurs(&tCABAC, &tCtxs, img, img_rcon, map_cudep, map_pmode, yszn, xszn, y, x, 0, qpd6, pmode_cand);      // encode a CTU
            CABACputTerminate(&tCABAC, (y+32>=yszn && x+32>=xszn) );        // encode a terminate bit
            pbuffer = CABACcommitToBuffer(pbuffer, &tCABAC, 2);             // commit the commpressed bytes from CABAC coder's buffer to output buffer (pbuffer)
        }

        for (x=0; x<(MAX_XSZ>>LOG2_MIN_CU_SZ); x++) {
            map_cudep[0][x] = map_cudep[ (1<<CUDEP_MAX) ][x];               // scroll the line-buffer
            map_pmode[0][x] = map_pmode[ (1<<CUDEP_MAX) ][x];               // scroll the line-buffer
        }
    }
    
    CABACfinish(&tCABAC);
    pbuffer = CABACcommitToBuffer(pbuffer, &tCABAC, 0);                     // commit the commpressed bytes from CABAC coder's buffer to output buffer (pbuffer)

    *ysz = yszn;                                                            // change the value of *ysz, so that the user can get the clipped image size
    *xsz = xszn;                                                            // change the value of *xsz, so that the user can get the clipped image size
    
    return pbuffer - pbuffer_base;                                          // return the compressed length
}



