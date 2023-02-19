#include <stdio.h>
#include "HEVCe.h"                                             // contains a function (HEVCImageEncoder), for compressing a image to HEVC stream.



// description : get next integer (parse from ASCII-string) from a FILE stream
int  fgetInt (FILE *fp) {                                      // return:   -1:failed   >=0:value getted
    int ch, val=0;
    do {
        if (feof(fp)) return -1;
        ch = fgetc(fp);
    } while ('0' >  ch || ch >  '9');
    do {
        val = val*10 + ch - '0';
        if (feof(fp)) return val;
        ch = fgetc(fp);
    } while ('0' <= ch && ch <= '9');
    return val;
}



int loadPGMfile (                                         // return:   -1:failed   0:success
    const char    *filename, 
    unsigned char *img_buffer,
    int           *ysz,                                   // get image height here.
    int           *xsz,                                   // get image width  here.
    int           *pix_max_val                            // get pixel max value  here.
) {
    int i;
    FILE *fp;

    *ysz = *xsz = *pix_max_val = -1;
    
    if ( (fp = fopen(filename, "rb")) == NULL )
        return -1;

    if ( fgetc(fp) != 'P' )
        goto failed;
    if ( fgetc(fp) != '5' )
        goto failed;

    if ( (*xsz=fgetInt(fp)) < 0 )
        goto failed;
    if ( (*ysz=fgetInt(fp)) < 0 )
        goto failed;

    if ( (*pix_max_val=fgetInt(fp)) < 0 )
        goto failed;

    if ( *pix_max_val > 255 )
        goto failed;

    for (i=(*xsz)*(*ysz); i>0; i--) {
        if (feof(fp)) goto failed;                      // pixels not enough
        *(img_buffer++) = (unsigned char)fgetc(fp);
    }

    fclose(fp);
    return 0;
failed:
    fclose(fp);
    return -1;
}



int writePGMfile (                                             // return:   -1:failed   0:success
    const char          *filename, 
    const unsigned char *img_buffer,
    const int            ysz,                                  // image height.
    const int            xsz                                   // image width .
) {
    int i;
    FILE *fp;
    
    if ( (fp = fopen(filename, "wb")) == NULL )
        return -1;

    if (fprintf(fp, "P5\n%d %d\n255\n", xsz, ysz) <= 0)
        goto failed;

    for (i=xsz*ysz; i>0; i--)
        if ( fputc( *(img_buffer++) , fp) == EOF )
            goto failed;

    fclose(fp);
    return 0;
failed:
    fclose(fp);
    return -1;
}



int writeBytesToFile (                       // return:   -1:failed   0:success
    const char          *filename,
    const unsigned char *buffer,
    const int            len
) {
    const unsigned char *buffer_end_ptr = buffer + len;
    
    FILE *fp;
    
    if ( (fp = fopen(filename, "wb")) == NULL )
        return -1;

    for (; buffer<buffer_end_ptr; buffer++)
        if ( fputc( *buffer , fp) == EOF )
            goto failed;

    fclose(fp);
    return 0;
failed:
    fclose(fp);
    return -1;
}



int parseIntFromStr (const char *str) {       // return:   -1:failed   >=0:value getted
    int val = 0;
    if ( *str == '\0' ) return -1;
    for ( ; '0' <= *str && *str <= '9' ; str++ )
        val = val*10 + *str - '0';
    if ( *str == '\0' ) return val;
    return -1;
}





int main (int argc, char **argv) {

    int i, qpd6=-1, pmode_cand=-1;
    const char *in_img_fname=NULL, *out_img_rcon_fname=NULL, *out_stream_fname=NULL;
    int ysz=-1, xsz=-1, pix_max_val=-1, stream_len;

    static unsigned char img           [8192*8192];
    static unsigned char img_rcon      [8192*8192];
    static unsigned char stream_buffer [8192*8192];


    // parse command line args ---------------------------------------------------------------------------------------------------------------------------------
    for (i=1; i<argc; i++) {
        const char *arg = argv[i];
        int argint;

        if ( (argint=parseIntFromStr(arg)) >= 0 ) {                                                 // arg is a int
            if (qpd6 < 0)
                qpd6 = argint;                                                                      // 1st int arg -> qpd6
            else if (pmode_cand < 0)
                pmode_cand = argint;                                                                // 2nd int arg -> pmode_cand

        } else {                                                                                    // arg is a string
            if      (in_img_fname == NULL)
                in_img_fname = arg;                                                                 // 1st string arg -> in_img_fname
            else if (out_stream_fname == NULL)
                out_stream_fname = arg;                                                             // 2nd string arg -> out_stream_fname
            else if (out_img_rcon_fname == NULL)
                out_img_rcon_fname = arg;                                                           // 3rd string arg -> out_img_rcon_fname
        }
    }

    if (in_img_fname == NULL || out_stream_fname == NULL) {                                         // illegal arguments: print USAGE and exit
        printf("Usage:\n");
        printf("    %s  <input-image-file(.pgm)>  <output-file(.hevc/.h265)>  [<qpd6>]  [<pmode_cand>]  [<reconstructed-image-file(.pgm)>]\n" , argv[0] );
        printf("\n");
        return -1;
    }

    if (qpd6 < 0)  qpd6 = 3;                                                                        // set default value of a argument if the user doesn't specify it
    if (pmode_cand <= 0) pmode_cand = 7;                                                            // set default value of a argument if the user doesn't specify it


    // print configurations ---------------------------------------------------------------------------------------------------------------------------------
    printf("arguments:\n");
    printf("  input  image file        = %s\n" , in_img_fname);
    printf("  output stream file       = %s\n" , out_stream_fname);
    printf("  Qp%%6                     = %d     (Qp=%d)\n" , qpd6, qpd6*6+4 );
    printf("  pmode_cand               = %d\n" , pmode_cand );
    if ( out_img_rcon_fname != NULL )
        printf("  reconstructed image file = %s\n" , out_img_rcon_fname);

    
    // load PGM file ---------------------------------------------------------------------------------------------------------------------------------
    if ( loadPGMfile(in_img_fname, img, &ysz, &xsz, &pix_max_val) ) {
        printf("open %s failed\n", in_img_fname);
        return -1;
    }

    
    printf("  image size               = %d x %d\n" , xsz , ysz );
    printf("\n");


    // HEVC encode ---------------------------------------------------------------------------------------------------------------------------------
    stream_len = HEVCImageEncoder(stream_buffer, img, img_rcon, &ysz, &xsz, qpd6, pmode_cand);
    if (stream_len <= 0) {
        printf("HEVC encode failed\n");
        return -1;
    }

    
    // print compressed result ---------------------------------------------------------------------------------------------------------------------------------
    printf("summary:\n");
    printf("  clipped image size       = %d x %d\n"  , xsz , ysz );
    printf("  original   length        = %d Bytes\n" , xsz*ysz );
    printf("  compressed length        = %d Bytes\n" , stream_len );
    printf("  compression ratio        = %.5f\n" , 1.0*xsz*ysz/stream_len );
    printf("  bits per pixel           = %.5f\n" , 8.0*stream_len/(xsz*ysz) );


    // write HEVC stream to file ---------------------------------------------------------------------------------------------------------------------------------
    if ( writeBytesToFile(out_stream_fname, stream_buffer, stream_len) ) {
        printf("write file %s failed\n", out_stream_fname);
        return -1;
    }

    
    // write reconstructed image to file ---------------------------------------------------------------------------------------------------------------------------------
    if (out_img_rcon_fname != NULL) {
        if ( writePGMfile(out_img_rcon_fname, img_rcon, ysz, xsz) ) {
            printf("write file %s failed\n", out_img_rcon_fname);
            return -1;
        }
    }


    return 0;
}
