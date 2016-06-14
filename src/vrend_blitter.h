/**************************************************************************
 *
 * Copyright (C) 2014 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
#ifndef VREND_BLITTER_H
#define VREND_BLITTER_H

/* shaders for blitting */

#define VS_PASSTHROUGH                          \
   "#version 130\n"                             \
   "in vec4 arg0;\n"                            \
   "in vec4 arg1;\n"                            \
   "out vec4 tc;\n"                             \
   "void main() {\n"                            \
   "   gl_Position = arg0;\n"                   \
   "   tc = arg1;\n"                            \
   "}\n"

#define FS_TEXFETCH_COL                         \
   "#version 130\n"                             \
   "%s"                                         \
   "uniform sampler%s samp;\n"                  \
   "in vec4 tc;\n"                              \
   "void main() {\n"                            \
   "   gl_FragColor = texture(samp, tc%s)%s;\n" \
   "}\n"

#define FS_TEXFETCH_COL_ALPHA_DEST              \
   "#version 130\n"                             \
   "%s"                                         \
   "uniform sampler%s samp;\n"                  \
   "in vec4 tc;\n"                              \
   "void main() {\n"                            \
   "   vec4 temp = texture(samp, tc%s)%s;\n"     \
   "   gl_FragColor = temp.aaaa;\n" \
   "}\n"

#define FS_TEXFETCH_DS                                  \
   "#version 130\n"                                     \
   "uniform sampler%s samp;\n"                          \
   "in vec4 tc;\n"                                      \
   "void main() {\n"                                    \
   "   gl_FragDepth = float(texture(samp, tc%s).x);\n"  \
   "}\n"

#define FS_TEXFETCH_DS_MSAA                                             \
   "#version 130\n"                                                     \
   "#extension GL_ARB_texture_multisample : enable\n"                   \
   "uniform sampler%s samp;\n"                                          \
   "in vec4 tc;\n"                                                      \
   "void main() {\n"                                                    \
   "   gl_FragDepth = float(texelFetch(samp, %s(tc%s), int(tc.z)).x);\n" \
   "}\n"

#endif
