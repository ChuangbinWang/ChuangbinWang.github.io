/* rio.c is a simple stream-oriented I/O abstraction that provides an interface
 * to write code that can consume/produce data using different concrete input
 * and output devices. For instance the same rdb.c code using the rio
 * abstraction can be used to read and write the RDB format using in-memory
 * buffers or files.
 *
 * A rio object provides the following methods:
 *  read: read from stream.
 *  write: write to stream.
 *  tell: get the current offset.
 *
 * It is also possible to set a 'checksum' method that is used by rio.c in order
 * to compute a checksum of the data written or read, or to query the rio object
 * for the current checksum.
 *
 * ----------------------------------------------------------------------------
 *
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
// rio.c��һ�������IO���󣬿�������ͬ����������豸������һ��������IO���ļ�IO��socket IO

#include "fmacros.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "rio.h"
#include "util.h"
#include "crc64.h"
#include "config.h"
#include "server.h"

/* ------------------------- Buffer I/O implementation ----------------------- */
// ������IOʵ��

/* Returns 1 or 0 for success/failure. */
// ��len����bufд��һ������������r��
static size_t rioBufferWrite(rio *r, const void *buf, size_t len) {
    r->io.buffer.ptr = sdscatlen(r->io.buffer.ptr,(char*)buf,len);  //׷�Ӳ���
    r->io.buffer.pos += len;    //����ƫ����
    return 1;
}

/* Returns 1 or 0 for success/failure. */
// ������������r����buf�У���len��
static size_t rioBufferRead(rio *r, void *buf, size_t len) {
    // ����������ĳ���С��len��������������0
    if (sdslen(r->io.buffer.ptr)-r->io.buffer.pos < len)
        return 0; /* not enough buffer to return len bytes. */
    // ����buf��
    memcpy(buf,r->io.buffer.ptr+r->io.buffer.pos,len);
    // ����ƫ����
    r->io.buffer.pos += len;
    return 1;
}

/* Returns read/write position in buffer. */
// ���ػ���������r��ǰ��ƫ����
static off_t rioBufferTell(rio *r) {
    return r->io.buffer.pos;
}

/* Flushes any buffer to target device if applicable. Returns 1 on success
 * and 0 on failures. */
// ��ϴ������
static int rioBufferFlush(rio *r) {
    UNUSED(r);  //void r��ǿת��void���Ͷ��󣬻��������൱���ͷ�
    return 1; /* Nothing to do, our write just appends to the buffer. */
}

// ����һ�����������󲢳�ʼ�������ͳ�Ա
static const rio rioBufferIO = {
    rioBufferRead,
    rioBufferWrite,
    rioBufferTell,
    rioBufferFlush,
    NULL,           /* update_checksum */
    0,              /* current checksum */
    0,              /* bytes read or written */
    0,              /* read/write chunk size */
    { { NULL, 0 } } /* union for io-specific vars */
};

// ��ʼ������������r�����û������ĵ�ַ
void rioInitWithBuffer(rio *r, sds s) {
    *r = rioBufferIO;
    r->io.buffer.ptr = s;
    r->io.buffer.pos = 0;
}

/* --------------------- Stdio file pointer implementation ------------------- */
// ��׼�ļ�IOʵ��
/* Returns 1 or 0 for success/failure. */
// ��len����bufд��һ���ļ�������
static size_t rioFileWrite(rio *r, const void *buf, size_t len) {
    size_t retval;

    // ���õײ�⺯��
    retval = fwrite(buf,len,1,r->io.file.fp);
    // ����д��ĳ���
    r->io.file.buffered += len;

    // ����Ѿ��ﵽ�Զ���ͬ��autosync�����õ��ֽ���
    if (r->io.file.autosync &&
        r->io.file.buffered >= r->io.file.autosync)
    {
        // ��ϴ���̻������е����ݵ��ļ���
        fflush(r->io.file.fp);
        // ͬ������
        aof_fsync(fileno(r->io.file.fp)); //����ȡ�ò���streamָ�����ļ�����ʹ�õ��ļ���������
        // ���ȳ�ʼ��Ϊ0
        r->io.file.buffered = 0;
    }
    return retval;
}

/* Returns 1 or 0 for success/failure. */
// ���ļ�������r�ж���len���ȵ��ֽڵ�buf��
static size_t rioFileRead(rio *r, void *buf, size_t len) {
    return fread(buf,len,1,r->io.file.fp);
}

/* Returns read/write position in file. */
// �����ļ��������ƫ����
static off_t rioFileTell(rio *r) {
    return ftello(r->io.file.fp);
}

/* Flushes any buffer to target device if applicable. Returns 1 on success
 * and 0 on failures. */
// ��ϴ�ļ���
static int rioFileFlush(rio *r) {
    return (fflush(r->io.file.fp) == 0) ? 1 : 0;
}

// ��ʼ��һ���ļ�������
static const rio rioFileIO = {
    rioFileRead,
    rioFileWrite,
    rioFileTell,
    rioFileFlush,
    NULL,           /* update_checksum */
    0,              /* current checksum */
    0,              /* bytes read or written */
    0,              /* read/write chunk size */
    { { NULL, 0 } } /* union for io-specific vars */
};

// ��ʼ��һ���ļ������������ö�Ӧ�ļ�
void rioInitWithFile(rio *r, FILE *fp) {
    *r = rioFileIO;
    r->io.file.fp = fp;
    r->io.file.buffered = 0;
    r->io.file.autosync = 0;
}

/* ------------------- File descriptors set implementation ------------------- */
// �ļ��������ϼ���ʵ��
/* Returns 1 or 0 for success/failure.
 * The function returns success as long as we are able to correctly write
 * to at least one file descriptor.
 *
 * When buf is NULL and len is 0, the function performs a flush operation
 * if there is some pending buffer, so this function is also used in order
 * to implement rioFdsetFlush(). */
// ��bufд���ļ����������϶���
static size_t rioFdsetWrite(rio *r, const void *buf, size_t len) {
    ssize_t retval;
    int j;
    unsigned char *p = (unsigned char*) buf;
    int doflush = (buf == NULL && len == 0);    //���bufΪ����lenΪ0���൱��flush����

    /* To start we always append to our buffer. If it gets larger than
     * a given size, we actually write to the sockets. */
    // ��buf�е�����д���ļ����������϶���Ļ�������
    if (len) {
        r->io.fdset.buf = sdscatlen(r->io.fdset.buf,buf,len);
        // ����д��ı�־
        len = 0; /* Prevent entering the while below if we don't flush. */
        if (sdslen(r->io.fdset.buf) > PROTO_IOBUF_LEN) doflush = 1; //���������̫����Ҫ��ˢ��socket��
    }

    // ��ϴ�ļ����������϶������ü��ϻ��������Ⱥͼ��ϻ�������ַ
    if (doflush) {
        p = (unsigned char*) r->io.fdset.buf;
        len = sdslen(r->io.fdset.buf);
    }

    /* Write in little chunchs so that when there are big writes we
     * parallelize while the kernel is sending data in background to
     * the TCP socket. */
    // һ�ο����޷���ϴ�꣬��Ҫѭ�����
    while(len) {
        // һ������ϴ1M�ֽ�
        size_t count = len < 1024 ? len : 1024;
        int broken = 0;
        for (j = 0; j < r->io.fdset.numfds; j++) {
            // errnoΪ0��ʾok����¼��Ϊ0���ļ�����������
            if (r->io.fdset.state[j] != 0) {
                /* Skip FDs alraedy in error. */
                broken++;
                continue;
            }

            /* Make sure to write 'count' bytes to the socket regardless
             * of short writes. */
            size_t nwritten = 0;
            // ��д������һ�λ���д��count���ֽ�����һ���ļ�������fd
            while(nwritten != count) {
                retval = write(r->io.fdset.fds[j],p+nwritten,count-nwritten);
                // дʧ�ܣ��ж��ǲ���д�������������ó�ʱ
                if (retval <= 0) {
                    /* With blocking sockets, which is the sole user of this
                     * rio target, EWOULDBLOCK is returned only because of
                     * the SO_SNDTIMEO socket option, so we translate the error
                     * into one more recognizable by the user. */
                    if (retval == -1 && errno == EWOULDBLOCK) errno = ETIMEDOUT;
                    break;
                }
                nwritten += retval; //ÿ�μ���д�ɹ����ֽ���
            }

            // ����ղ�дʧ�ܵ�������򽫵�ǰ���ļ�������״̬����Ϊ����ı����
            if (nwritten != count) {
                /* Mark this FD as broken. */
                r->io.fdset.state[j] = errno;
                if (r->io.fdset.state[j] == 0) r->io.fdset.state[j] = EIO;
            }
        }
        // ���е��ļ���������������0
        if (broken == r->io.fdset.numfds) return 0; /* All the FDs in error. */
        // �����´�Ҫд��ĵ�ַ�ͳ���
        p += count;
        len -= count;
        r->io.fdset.pos += count;   //��д���ƫ����
    }

    if (doflush) sdsclear(r->io.fdset.buf); //�ͷż��ϻ�����
    return 1;
}

/* Returns 1 or 0 for success/failure. */
// �ļ����������϶���֧�ֶ���ֱ�ӷ���0
static size_t rioFdsetRead(rio *r, void *buf, size_t len) {
    UNUSED(r);
    UNUSED(buf);
    UNUSED(len);
    return 0; /* Error, this target does not support reading. */
}

/* Returns read/write position in file. */
// ��ȡƫ����
static off_t rioFdsetTell(rio *r) {
    return r->io.fdset.pos;
}

/* Flushes any buffer to target device if applicable. Returns 1 on success
 * and 0 on failures. */
// ��ϴ��������ֵ
static int rioFdsetFlush(rio *r) {
    /* Our flush is implemented by the write method, that recognizes a
     * buffer set to NULL with a count of zero as a flush request. */
    return rioFdsetWrite(r,NULL,0);
}

// ��ʼ��һ���ļ����������϶���
static const rio rioFdsetIO = {
    rioFdsetRead,
    rioFdsetWrite,
    rioFdsetTell,
    rioFdsetFlush,
    NULL,           /* update_checksum */
    0,              /* current checksum */
    0,              /* bytes read or written */
    0,              /* read/write chunk size */
    { { NULL, 0 } } /* union for io-specific vars */
};

// ��ʼ��һ���ļ����������϶������ó�Ա����
void rioInitWithFdset(rio *r, int *fds, int numfds) {
    int j;

    *r = rioFdsetIO;
    r->io.fdset.fds = zmalloc(sizeof(int)*numfds);
    r->io.fdset.state = zmalloc(sizeof(int)*numfds);
    memcpy(r->io.fdset.fds,fds,sizeof(int)*numfds);
    for (j = 0; j < numfds; j++) r->io.fdset.state[j] = 0;
    r->io.fdset.numfds = numfds;
    r->io.fdset.pos = 0;
    r->io.fdset.buf = sdsempty();
}

/* release the rio stream. */
// �ͷ��ļ�����������������
void rioFreeFdset(rio *r) {
    zfree(r->io.fdset.fds);
    zfree(r->io.fdset.state);
    sdsfree(r->io.fdset.buf);
}

/* ---------------------------- Generic functions ---------------------------- */
// ͨ�ú���
/* This function can be installed both in memory and file streams when checksum
 * computation is needed. */
// ����CRC64�㷨����У���
void rioGenericUpdateChecksum(rio *r, const void *buf, size_t len) {
    r->cksum = crc64(r->cksum,buf,len);
}

/* Set the file-based rio object to auto-fsync every 'bytes' file written.
 * By default this is set to zero that means no automatic file sync is
 * performed.
 *
 * This feature is useful in a few contexts since when we rely on OS write
 * buffers sometimes the OS buffers way too much, resulting in too many
 * disk I/O concentrated in very little time. When we fsync in an explicit
 * way instead the I/O pressure is more distributed across time. 
 * ���������һЩ�������к����ã���Ϊ������������OSд������ʱ����ʱ��OS������̫���ˣ�
 * �����ں̵ܶ�ʱ���ڼ�����̫��Ĵ���I/O����������һ����ʽ�ķ�ʽ����fsyncʱ��I/Oѹ��������ʱ��ֲ��ø����ȡ�
 * */
// �����Զ�ͬ�����ֽ������ƣ����bytesΪ0������ζ�Ų�ִ��
void rioSetAutoSync(rio *r, off_t bytes) {
    serverAssert(r->read == rioFileIO.read);    //����Ϊ�ļ������󣬲�������������������
    r->io.file.autosync = bytes;
}

/* --------------------------- Higher level interface --------------------------
 *
 * The following higher level functions use lower level rio.c functions to help
 * generating the Redis protocol for the Append Only File. */
// ����ĸ߼�������������ĵͼ���������������AOF�ļ���Э��

/* Write multi bulk count in the format: "*<count>\r\n". */
// ��"*<count>\r\n"��ʽΪд��һ��int���͵�count
size_t rioWriteBulkCount(rio *r, char prefix, int count) {
    char cbuf[128];
    int clen;

    // ����һ�� "*<count>\r\n"
    cbuf[0] = prefix;
    clen = 1+ll2string(cbuf+1,sizeof(cbuf)-1,count);
    cbuf[clen++] = '\r';
    cbuf[clen++] = '\n';
    // ����rio�Ľӿڣ���cbufд��r��
    if (rioWrite(r,cbuf,clen) == 0) return 0;
    return clen;
}

/* Write binary-safe string in the format: "$<count>\r\n<payload>\r\n". */
// ��"$<count>\r\n<payload>\r\n"Ϊ��ʽд��һ���ַ���
size_t rioWriteBulkString(rio *r, const char *buf, size_t len) {
    size_t nwritten;

    // д��"$<len>\r\n"
    if ((nwritten = rioWriteBulkCount(r,'$',len)) == 0) return 0;
    // ׷��д��һ��buf��Ҳ����<payload>����
    if (len > 0 && rioWrite(r,buf,len) == 0) return 0;
    // ׷��"\r\n"
    if (rioWrite(r,"\r\n",2) == 0) return 0;
    return nwritten+len+2;  //���س���
}

/* Write a long long value in format: "$<count>\r\n<payload>\r\n". */
// ��"$<count>\r\n<payload>\r\n"Ϊ��ʽд��һ��longlong ֵ
size_t rioWriteBulkLongLong(rio *r, long long l) {
    char lbuf[32];
    unsigned int llen;

    // ��longlongתΪ�ַ��������ַ����ĸ�ʽд��
    llen = ll2string(lbuf,sizeof(lbuf),l);
    return rioWriteBulkString(r,lbuf,llen);
}

/* Write a double value in the format: "$<count>\r\n<payload>\r\n" */
// ��"$<count>\r\n<payload>\r\n"Ϊ��ʽд��һ�� double ֵ
size_t rioWriteBulkDouble(rio *r, double d) {
    char dbuf[128];
    unsigned int dlen;

    //�Կ��Ϊ17λ�ķ�ʽд��dbuf�У�17λ��double˫���ȸ������ĳ������������
    dlen = snprintf(dbuf,sizeof(dbuf),"%.17g",d);
    return rioWriteBulkString(r,dbuf,dlen);
}
