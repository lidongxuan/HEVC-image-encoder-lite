#include <stdio.h>
#include <math.h>

#include "HEVCe.h"                                             // contains a function (HEVCImageEncoder), for compressing a image to HEVC stream.



// return:   -1:failed   0:success
int loadPGMfile (const char *filename, unsigned char *img_buffer, int *ysz, int *xsz, int *pix_max_val) {
    int i;
    FILE *fp;

    *ysz = *xsz = *pix_max_val = -1;
    
    if ( (fp = fopen(filename, "rb")) == NULL )
        return -1;

    if ( fgetc(fp) != 'P' ) {
        fclose(fp);
        return -1;
    }
    
    if ( fgetc(fp) != '5' ) {
        fclose(fp);
        return -1;
    }

    if ( fscanf(fp, "%d", xsz) < 1 ) {
        fclose(fp);
        return -1;
    }
    
    if ( fscanf(fp, "%d", ysz) < 1 ) {
        fclose(fp);
        return -1;
    }

    if ( fscanf(fp, "%d", pix_max_val) < 1 ) {
        fclose(fp);
        return -1;
    }

    if ( *pix_max_val > 255 ) {
        fclose(fp);
        return -1;
    }
    
    i = fgetc(fp);
    if ( i != ' ' && i != '\n' && i != '\r' && i != '\t') {
        fclose(fp);
        return -1;
    }

    for (i=(*xsz)*(*ysz); i>0; i--) {
        if (feof(fp)) {                                        // pixels not enough
            fclose(fp);
            return -1;
        }
        *(img_buffer++) = (unsigned char)fgetc(fp);
    }

    fclose(fp);
    return 0;
}



// return:   -1:failed   0:success
int writePGMfile (const char *filename, const unsigned char *img_buffer, const int ysz, const int xsz) {
    int i;
    FILE *fp;
    
    if ( (fp = fopen(filename, "wb")) == NULL )
        return -1;

    if (fprintf(fp, "P5\n%d %d\n255\n", xsz, ysz) <= 0) {
        fclose(fp);
        return -1;
    }

    for (i=xsz*ysz; i>0; i--) {
        if ( fputc( *(img_buffer++) , fp) == EOF ) {
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    return 0;
}



// return:   -1:failed   0:success
int writeBytesToFile (const char *filename, const unsigned char *buffer, const int len) {
    const unsigned char *buffer_end_ptr = buffer + len;
    
    FILE *fp;
    
    if ( (fp = fopen(filename, "wb")) == NULL )
        return -1;

    for (; buffer<buffer_end_ptr; buffer++) {
        if ( fputc( *buffer , fp) == EOF ) {
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    return 0;
}



double calcImagePSNR (const unsigned char *buffer1, const int ysz1, const int xsz1, const unsigned char *buffer2, const int ysz2, const int xsz2, double *mse) {
    const int ymin = (ysz1 < ysz2) ? ysz1 : ysz2;
    const int xmin = (xsz1 < xsz2) ? xsz1 : xsz2;
    long long diff, sse = 0ULL;
    int y, x;
    for (y=0; y<ymin; y++) {
        for (x=0; x<xmin; x++) {
            diff = (long long)buffer1[x] - buffer2[x];
            sse += diff * diff;
        }
        buffer1 += xsz1;
        buffer2 += xsz2;
    }
    *mse = ((double)sse) / ymin / xmin;
    if ( *mse < 1e-9 )
        *mse = 1e-9;
    return  10.0 * log10( 255*255 / (*mse) );
}




int main (int argc, char **argv) {

    static unsigned char img           [8192*8192];
    static unsigned char img_rcon      [8192*8192];
    static unsigned char stream_buffer [8192*8192];

    const char *in_img_fname=NULL, *out_img_rcon_fname=NULL, *out_stream_fname=NULL;
    int i , qpd6=-1 , ysz=-1, xsz=-1, yszn=-1, xszn=-1, pix_max_val=-1, stream_len;
    double psnr, mse;


    // parse command line args ---------------------------------------------------------------------------------------------------------------------------------
    for (i=1; i<argc; i++) {
        const char *arg = argv[i];
        
        if ( arg[0] >= '0'  &&  arg[0] <= '4'  &&  arg[1] == '\0' )                                 // arg is a single digit in range '0'~'4'
            qpd6 = arg[0] - '0';                                                                    //   get quantize parameter
        else if (in_img_fname == NULL)
            in_img_fname = arg;                                                                     //   1st string arg -> in_img_fname
        else if (out_stream_fname == NULL)
            out_stream_fname = arg;                                                                 //   2nd string arg -> out_stream_fname
        else if (out_img_rcon_fname == NULL)
            out_img_rcon_fname = arg;                                                               //   3rd string arg -> out_img_rcon_fname
    }

    if (in_img_fname == NULL || out_stream_fname == NULL) {                                         // illegal arguments: print USAGE and exit
        printf("Usage:\n");
        printf("    %s  <input-image-file(.pgm)>  <output-file(.hevc/.h265)>  [<qpd6>]  [<output-reconstructed-image-file(.pgm)>]\n" , argv[0] );
        printf("\n");
        return -1;
    }

    if (qpd6 < 0 || qpd6 > 4)  qpd6 = 3;                                                            // set default value of a argument if the user doesn't specify it


    // print configurations ---------------------------------------------------------------------------------------------------------------------------------
    printf("arguments:\n");
    printf("  input  image file               = %s\n" , in_img_fname);
    printf("  output stream file              = %s\n" , out_stream_fname);
    printf("  Qp%%6                            = %d     (Qp=%d)\n" , qpd6, qpd6*6+4 );
    if ( out_img_rcon_fname != NULL )
        printf("  output reconstructed image file = %s\n" , out_img_rcon_fname);

    
    // load PGM file ---------------------------------------------------------------------------------------------------------------------------------
    if ( loadPGMfile(in_img_fname, img, &ysz, &xsz, &pix_max_val) ) {
        printf("open %s failed\n", in_img_fname);
        return -1;
    }
    
    printf("  image size                      = %d x %d\n" , xsz , ysz );


    // HEVC encode ---------------------------------------------------------------------------------------------------------------------------------
    printf("compressing...\n");

    yszn = ysz;
    xszn = xsz;

    stream_len = HEVCImageEncoder(stream_buffer, img, img_rcon, &yszn, &xszn, qpd6);


    // calculate distortion (MSE and PSNR) ---------------------------------------------------------------------------------------------------------------------------------
    psnr = calcImagePSNR(img, ysz, xsz, img_rcon, yszn, xszn, &mse);

    
    // print compressed result ---------------------------------------------------------------------------------------------------------------------------------
    printf("  padded image size               = %d x %d\n"  , xszn , yszn );
    printf("  original   length               = %d Bytes\n" , xszn*yszn );
    printf("  compressed length               = %d Bytes\n" , stream_len );
    printf("  compression ratio               = %.5f\n" , 1.0*xszn*yszn/stream_len );
    printf("  bits per pixel                  = %.5f\n" , 8.0*stream_len/(xszn*yszn) );
    printf("  mean square error (MSE)         = %.7lf\n" , mse);
    printf("  peak signal/noise ratio (PSNR)  = %.4lf dB\n" , psnr);


    // write HEVC stream to file ---------------------------------------------------------------------------------------------------------------------------------
    if ( writeBytesToFile(out_stream_fname, stream_buffer, stream_len) ) {
        printf("write file %s failed\n", out_stream_fname);
        return -1;
    }

    
    // write reconstructed image to file ---------------------------------------------------------------------------------------------------------------------------------
    if (out_img_rcon_fname != NULL) {
        if ( writePGMfile(out_img_rcon_fname, img_rcon, yszn, xszn) ) {
            printf("write file %s failed\n", out_img_rcon_fname);
            return -1;
        }
    }

    return 0;
}
