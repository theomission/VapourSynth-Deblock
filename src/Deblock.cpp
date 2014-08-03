// DeBlock plugin for Avisynth 2.5 - takes a clip, and deblock it using H264 deblocking
// Copyright(c)2004 Manao as a function in MVTools v.0.9.6.2
// Copyright(c)2006 Alexander Balakhnin aka Fizick - separate plugin, YUY2 support
//
// VapourSynth port by HolyWu   2014/08/03
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA, or visit
// http://www.gnu.org/copyleft/gpl.html .

#include <string>
#include "VapourSynth.h"
#include "VSHelper.h"

// generalized by Fizick (was max=51)
#define DEBLOCK_QUANT_MAX 60

#define CLAMP(x, min, max) (x < min ? min : (x > max ? max : x))
#define ABS(x) (x < 0 ? -x : x)

const int alphas[DEBLOCK_QUANT_MAX + 1] = {
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 4, 4,
    5, 6, 7, 8, 9, 10,
    12, 13, 15, 17, 20,
    22, 25, 28, 32, 36,
    40, 45, 50, 56, 63,
    71, 80, 90, 101, 113,
    127, 144, 162, 182,
    203, 226, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255 // added by Fizick 
};

const int betas[DEBLOCK_QUANT_MAX + 1] = {
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 2, 2,
    2, 3, 3, 3, 3, 4,
    4, 4, 6, 6,
    7, 7, 8, 8, 9, 9,
    10, 10, 11, 11, 12,
    12, 13, 13, 14, 14,
    15, 15, 16, 16, 17,
    17, 18, 18,
    19, 20, 21, 22, 23, 24, 25, 26, 27 // added by Fizick 
};

const int cs[DEBLOCK_QUANT_MAX + 1] = {
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 1, 1, 1,
    1, 1, 1, 1, 1, 1,
    1, 2, 2, 2, 2, 3,
    3, 3, 4, 4, 5, 5,
    6, 7, 8, 8, 10,
    11, 12, 13, 15, 17,
    19, 21, 23, 25, 27, 29, 31, 33, 35 // added by Fizick for really strong deblocking :)
};

struct DeblockData {
    VSNodeRef * node;
    const VSVideoInfo * vi;
    int quant, aOffset, bOffset;
    int process[3];
};

static void VS_CC deblockInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    DeblockData * d = (DeblockData *)*instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef *VS_CC deblockGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    DeblockData * d = (DeblockData *)*instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef * src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrameRef * dst = vsapi->copyFrame(src, core);
        vsapi->freeFrame(src);
        int plane;

        for (plane = 0; plane < d->vi->format->numPlanes; plane++) {
            if (d->process[plane]) {
                const int w = vsapi->getFrameWidth(dst, plane);
                const int h = vsapi->getFrameHeight(dst, plane);
                const int indexa = CLAMP(d->quant + d->aOffset, 0, DEBLOCK_QUANT_MAX);
                const int indexb = CLAMP(d->quant + d->bOffset, 0, DEBLOCK_QUANT_MAX);
                int ap, aq, c, delta, deltap1, deltaq1;
                int x, y, i;
                uint8_t * dstp = vsapi->getWritePtr(dst, plane);

                if (d->vi->format->bytesPerSample == 1) {
                    const int stride = vsapi->getStride(dst, plane);
                    const int alpha = alphas[indexa];
                    const int beta = betas[indexb];
                    const int c0 = cs[indexa];

                    for (y = 0; y < h; y += 4) {
                        for (x = 0; x < w; x += 4) {
                            if (y > 0) {
                                uint8_t * s = dstp + x;
                                uint8_t * sq0 = s;
                                uint8_t * sq1 = s + stride;
                                uint8_t * sq2 = s + stride * 2;
                                uint8_t * sp0 = s - stride;
                                uint8_t * sp1 = s - stride * 2;
                                uint8_t * sp2 = s - stride * 3;

                                for (i = 0; i < 4; i++) {
                                    if ((ABS(sp0[i] - sq0[i]) < alpha) && (ABS(sp1[i] - sp0[i]) < beta) && (ABS(sq0[i] - sq1[i]) < beta)) {
                                        ap = ABS(sp2[i] - sp0[i]);
                                        aq = ABS(sq2[i] - sq0[i]);
                                        c = c0;
                                        if (aq < beta)
                                            c++;
                                        if (ap < beta)
                                            c++;
                                        delta = CLAMP((((sq0[i] - sp0[i]) << 2) + (sp1[i] - sq1[i]) + 4) >> 3, -c, c);
                                        deltap1 = CLAMP((sp2[i] + ((sp0[i] + sq0[i] + 1) >> 1) - (sp1[i] << 1)) >> 1, -c0, c0);
                                        deltaq1 = CLAMP((sq2[i] + ((sp0[i] + sq0[i] + 1) >> 1) - (sq1[i] << 1)) >> 1, -c0, c0);
                                        sp0[i] = CLAMP(sp0[i] + delta, 0, 255);
                                        sq0[i] = CLAMP(sq0[i] - delta, 0, 255);
                                        if (ap < beta)
                                            sp1[i] = sp1[i] + deltap1;
                                        if (aq < beta)
                                            sq1[i] = sq1[i] + deltaq1;
                                    }
                                }
                            }

                            if (x > 0) {
                                uint8_t * s = dstp + x;

                                for (i = 0; i < 4; i++) {
                                    if ((ABS(s[0] - s[-1]) < alpha) && (ABS(s[1] - s[0]) < beta) && (ABS(s[-1] - s[-2]) < beta)) {
                                        ap = ABS(s[2] - s[0]);
                                        aq = ABS(s[-3] - s[-1]);
                                        c = c0;
                                        if (aq < beta)
                                            c++;
                                        if (ap < beta)
                                            c++;
                                        delta = CLAMP((((s[0] - s[-1]) << 2) + (s[-2] - s[1]) + 4) >> 3, -c, c);
                                        deltaq1 = CLAMP((s[2] + ((s[0] + s[-1] + 1) >> 1) - (s[1] << 1)) >> 1, -c0, c0);
                                        deltap1 = CLAMP((s[-3] + ((s[0] + s[-1] + 1) >> 1) - (s[-2] << 1)) >> 1, -c0, c0);
                                        s[0] = CLAMP(s[0] - delta, 0, 255);
                                        s[-1] = CLAMP(s[-1] + delta, 0, 255);
                                        if (ap < beta)
                                            s[1] = s[1] + deltaq1;
                                        if (aq < beta)
                                            s[-2] = s[-2] + deltap1;
                                    }
                                    s += stride;
                                }
                            }
                        }
                        dstp += stride * 4;
                    }
                } else if (d->vi->format->bytesPerSample == 2) {
                    const int stride = vsapi->getStride(dst, plane) / 2;
                    const int alpha = alphas[indexa] * 257;
                    const int beta = betas[indexb] * 257;
                    const int c0 = cs[indexa] * 257;

                    for (y = 0; y < h; y += 4) {
                        for (x = 0; x < w; x += 4) {
                            if (y > 0) {
                                uint16_t * s = (uint16_t *)dstp + x;
                                uint16_t * sq0 = s;
                                uint16_t * sq1 = s + stride;
                                uint16_t * sq2 = s + stride * 2;
                                uint16_t * sp0 = s - stride;
                                uint16_t * sp1 = s - stride * 2;
                                uint16_t * sp2 = s - stride * 3;

                                for (i = 0; i < 4; i++) {
                                    if ((ABS(sp0[i] - sq0[i]) < alpha) && (ABS(sp1[i] - sp0[i]) < beta) && (ABS(sq0[i] - sq1[i]) < beta)) {
                                        ap = ABS(sp2[i] - sp0[i]);
                                        aq = ABS(sq2[i] - sq0[i]);
                                        c = c0;
                                        if (aq < beta)
                                            c += 257;
                                        if (ap < beta)
                                            c += 257;
                                        delta = CLAMP((((sq0[i] - sp0[i]) << 2) + (sp1[i] - sq1[i]) + 4) >> 3, -c, c);
                                        deltap1 = CLAMP((sp2[i] + ((sp0[i] + sq0[i] + 1) >> 1) - (sp1[i] << 1)) >> 1, -c0, c0);
                                        deltaq1 = CLAMP((sq2[i] + ((sp0[i] + sq0[i] + 1) >> 1) - (sq1[i] << 1)) >> 1, -c0, c0);
                                        sp0[i] = CLAMP(sp0[i] + delta, 0, 65535);
                                        sq0[i] = CLAMP(sq0[i] - delta, 0, 65535);
                                        if (ap < beta)
                                            sp1[i] = sp1[i] + deltap1;
                                        if (aq < beta)
                                            sq1[i] = sq1[i] + deltaq1;
                                    }
                                }
                            }

                            if (x > 0) {
                                uint16_t * s = (uint16_t *)dstp + x;

                                for (i = 0; i < 4; i++) {
                                    if ((ABS(s[0] - s[-1]) < alpha) && (ABS(s[1] - s[0]) < beta) && (ABS(s[-1] - s[-2]) < beta)) {
                                        ap = ABS(s[2] - s[0]);
                                        aq = ABS(s[-3] - s[-1]);
                                        c = c0;
                                        if (aq < beta)
                                            c += 257;
                                        if (ap < beta)
                                            c += 257;
                                        delta = CLAMP((((s[0] - s[-1]) << 2) + (s[-2] - s[1]) + 4) >> 3, -c, c);
                                        deltaq1 = CLAMP((s[2] + ((s[0] + s[-1] + 1) >> 1) - (s[1] << 1)) >> 1, -c0, c0);
                                        deltap1 = CLAMP((s[-3] + ((s[0] + s[-1] + 1) >> 1) - (s[-2] << 1)) >> 1, -c0, c0);
                                        s[0] = CLAMP(s[0] - delta, 0, 65535);
                                        s[-1] = CLAMP(s[-1] + delta, 0, 65535);
                                        if (ap < beta)
                                            s[1] = s[1] + deltaq1;
                                        if (aq < beta)
                                            s[-2] = s[-2] + deltap1;
                                    }
                                    s += stride;
                                }
                            }
                        }
                        dstp += stride * 8;
                    }
                }
            }
        }

        return dst;
    }

    return nullptr;
}

static void VS_CC deblockFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    DeblockData * d = (DeblockData *)instanceData;
    vsapi->freeNode(d->node);
    delete d;
}

static void VS_CC deblockCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    DeblockData d;
    DeblockData * data;
    int err;
    int m, n, i;

    d.quant = int64ToIntS(vsapi->propGetInt(in, "quant", 0, &err));
    if (err)
        d.quant = 25;
    d.aOffset = int64ToIntS(vsapi->propGetInt(in, "aoffset", 0, &err));
    if (err)
        d.aOffset = 0;
    d.bOffset = int64ToIntS(vsapi->propGetInt(in, "boffset", 0, &err));
    if (err)
        d.bOffset = 0;

    if (d.quant < 0 || d.quant > DEBLOCK_QUANT_MAX) {
        vsapi->setError(out, ("Deblock: quant must be between 0 and " + std::to_string(DEBLOCK_QUANT_MAX)).c_str());
        return;
    }

    d.aOffset = CLAMP(d.aOffset, -d.quant, DEBLOCK_QUANT_MAX - d.quant);
    d.bOffset = CLAMP(d.bOffset, -d.quant, DEBLOCK_QUANT_MAX - d.quant);

    d.node = vsapi->propGetNode(in, "clip", 0, nullptr);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(d.vi) || d.vi->format->colorFamily == cmCompat || d.vi->format->sampleType != stInteger || d.vi->format->bytesPerSample > 2) {
        vsapi->setError(out, "Deblock: only constant format 8-16 bits integer input supported");
        vsapi->freeNode(d.node);
        return;
    }

    if (d.vi->width & 7 || d.vi->height & 7) {
        vsapi->setError(out, "Deblock: width and height must be mod 8");
        vsapi->freeNode(d.node);
        return;
    }

    m = vsapi->propNumElements(in, "planes");

    for (i = 0; i < 3; i++)
        d.process[i] = m <= 0;

    for (i = 0; i < m; i++) {
        n = int64ToIntS(vsapi->propGetInt(in, "planes", i, nullptr));

        if (n < 0 || n >= d.vi->format->numPlanes) {
            vsapi->setError(out, "Deblock: plane index out of range");
            vsapi->freeNode(d.node);
            return;
        }

        if (d.process[n]) {
            vsapi->setError(out, "Deblock: plane specified twice");
            vsapi->freeNode(d.node);
            return;
        }

        d.process[n] = 1;
    }

    data = new DeblockData;
    *data = d;

    vsapi->createFilter(in, out, "Deblock", deblockInit, deblockGetFrame, deblockFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("com.holywu.deblock", "deblock", "Deblock a clip using H264 deblocking", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("Deblock", "clip:clip;quant:int:opt;aoffset:int:opt;boffset:int:opt;planes:int[]:opt;", deblockCreate, nullptr, plugin);
}