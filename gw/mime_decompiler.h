/*
 * mime_decompiler.h - decompiling application/vnd.wap.multipart.* 
 *                     to multipart/ *
 *
 * This is a header for Mime decompiler for decompiling binary mime
 * format to text mime format, which is used for transmitting POST  
 * data from mobile terminal to decrease the use of the bandwidth.
 *
 * See comments below for explanations on individual functions.
 *
 * Bruno Rodrigues
 */


#ifndef MIME_DECOMPILER_H
#define MIME_DECOMPILER_H

int mime_decompile(Octstr *binary_mime, Octstr **mime);

#endif

