/********************************************************************************
=================================================================================
                Copyright 2024 chenyiliang

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
================================================================================
author     : chenyiliang
description: 
    字体及参考文档: 
    使用BIOS自带字体: <https://wiki.osdev.org/VGA_Fonts>
    加载使PSCF用字体: <https://wiki.osdev.org/PC_Screen_Font>
    不同格式字体下载: <https://www.zap.org.au/projects/console-fonts-zap/#download>
                      <https://int10h.org/oldschool-pc-fonts/>

================================================================================
********************************************************************************/
#ifndef __VEDIO_FONT__
#define __VEDIO_FONT__

#include <types.h>

#define PSF_FONT_MAGIC 0x864AB572

typedef struct {
    unsigned int magic;         /* magic bytes to identify PSF */
    unsigned int version;       /* zero */
    unsigned int offset;        /* offset of bitmaps in file, 32 */
    unsigned int flags;         /* 0 if there's no unicode table */
    unsigned int glyphs;        /* number of glyphs */
    unsigned int bytes;         /* size of each glyph */
    unsigned int height;        /* height in pixels */
    unsigned int width;         /* width in pixels */
} PSF_font;

extern PSF_font* font;
extern void __font_initialize__();
extern unsigned short* bitmap(char value);
extern unsigned short* unicode();
#endif /** __VEDIO_FONT__ */