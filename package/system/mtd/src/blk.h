#ifndef __blk_h
#define __blk_h

extern int blk_resetbc(const char *blk) __attribute__ ((weak));
extern int blk_getbc(const char *blk) __attribute__ ((weak));
#endif /* __blk_h */
