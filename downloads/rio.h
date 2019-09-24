/*
 * Copyright (c) 2009-2012, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#ifndef __REDIS_RIO_H
#define __REDIS_RIO_H

#include <stdio.h>
#include <stdint.h>
#include "sds.h"

// Redis IO API�ӿڣ����ڶ�������µĶ�д
struct _rio {
    /* Backend functions.
     * Since this functions do not tolerate short writes or reads the return
     * value is simplified to: zero on error, non zero on complete success. */
    // ����д����дƫ��������ϴ�����ĺ���ָ�룬��0��ʾ�ɹ�
    size_t (*read)(struct _rio *, void *buf, size_t len);
    size_t (*write)(struct _rio *, const void *buf, size_t len);
    off_t (*tell)(struct _rio *);
    int (*flush)(struct _rio *);
    /* The update_cksum method if not NULL is used to compute the checksum of
     * all the data that was read or written so far. The method should be
     * designed so that can be called with the current checksum, and the buf
     * and len fields pointing to the new block of data to add to the checksum
     * computation. */
    // �����У�麯��
    void (*update_cksum)(struct _rio *, const void *buf, size_t len);

    /* The current checksum */
    // ��ǰУ���
    uint64_t cksum;

    /* number of bytes read or written */
    // ����д���ֽ���
    size_t processed_bytes;

    /* maximum single read or write chunk size */
    // ÿ�ζ���д������ֽ���
    size_t max_processing_chunk;

    /* Backend-specific vars. */
    // ��д�ĸ��ֶ���
    union {
        /*�ڴ滺���� In-memory buffer target. */
        struct {
            sds ptr;    //��������ָ�룬������char *
            off_t pos;  //��������ƫ����
        } buffer;

        /*��׼�ļ�IO Stdio file pointer target. */
        struct {
            FILE *fp;       // �ļ�ָ�룬ָ�򱻴򿪵��ļ�
            off_t buffered; /* ���һ��ͬ��֮����д���ֽ��� Bytes written since last fsync. */
            off_t autosync; /* д�����õ�autosync�ֽں󣬻�ִ��fsync()ͬ�� fsync after 'autosync' bytes written. */
        } file;

        /*�ļ������� Multiple FDs target (used to write to N sockets). */
        struct {
            int *fds;       /*�ļ����������� File descriptors. */
            int *state;     /*ÿһ��fd����Ӧ��errno  Error state of each fd. 0 (if ok) or errno. */
            int numfds;     // ���鳤�ȣ��ļ�����������
            off_t pos;      // ƫ����
            sds buf;        // ������
        } fdset;
    } io;
};

typedef struct _rio rio;

/* The following functions are our interface with the stream. They'll call the
 * actual implementation of read / write / tell, and will update the checksum
 * if needed. */

static inline size_t rioWrite(rio *r, const void *buf, size_t len) {
    while (len) {
        // д���ֽڳ��ȣ����ܳ���ÿ�ζ���д������ֽ���max_processing_chunk
        size_t bytes_to_write = (r->max_processing_chunk && r->max_processing_chunk < len) ? r->max_processing_chunk : len;
        // ���º�У��
        if (r->update_cksum) r->update_cksum(r,buf,bytes_to_write);
        // ���������write����д��
        if (r->write(r,buf,bytes_to_write) == 0)
            return 0;
        // ����ƫ������ָ����һ��д��λ��
        buf = (char*)buf + bytes_to_writ;
        // ����ʣ��д��ĳ���
        len -= bytes_to_write;
        // ���¶���д���ֽ���
        r->processed_bytes += bytes_to_write;
    }
    return 1;
}

static inline size_t rioRead(rio *r, void *buf, size_t len) {
    while (len) {
        // �����ֽڳ��ȣ����ܳ���ÿ�ζ���д������ֽ���max_processing_chunk
        size_t bytes_to_read = (r->max_processing_chunk && r->max_processing_chunk < len) ? r->max_processing_chunk : len;
        // ���������read��������buf��
        if (r->read(r,buf,bytes_to_read) == 0)
            return 0;
        // ���º�У��
        if (r->update_cksum) r->update_cksum(r,buf,bytes_to_read);
        // ����ƫ������ָ����һ������λ��
        buf = (char*)buf + bytes_to_read;
        // ����ʣ��Ҫ���ĳ���
        len -= bytes_to_read;
        // ���¶���д���ֽ���
        r->processed_bytes += bytes_to_read;
    }
    return 1;
}

// ���ص�ǰƫ����
static inline off_t rioTell(rio *r) {
    return r->tell(r);
}

// ����flush����
static inline int rioFlush(rio *r) {
    return r->flush(r);
}

void rioInitWithFile(rio *r, FILE *fp);
void rioInitWithBuffer(rio *r, sds s);
void rioInitWithFdset(rio *r, int *fds, int numfds);

void rioFreeFdset(rio *r);

size_t rioWriteBulkCount(rio *r, char prefix, int count);
size_t rioWriteBulkString(rio *r, const char *buf, size_t len);
size_t rioWriteBulkLongLong(rio *r, long long l);
size_t rioWriteBulkDouble(rio *r, double d);

void rioGenericUpdateChecksum(rio *r, const void *buf, size_t len);
void rioSetAutoSync(rio *r, off_t bytes);

#endif
