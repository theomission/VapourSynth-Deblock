// VapourSynth port by HolyWu
//
// DeBlock plugin for Avisynth 2.5 - takes a clip, and deblock it using H264 deblocking
// Copyright(c)2004 Manao as a function in MVTools v.0.9.6.2
// Copyright(c)2006 Alexander Balakhnin aka Fizick - separate plugin, YUY2 support
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
    int alpha, beta, c0;
    int process[3];
};

static void DeblockHorEdge_8bits(uint8_t *dstp, int stride, const DeblockData *d) {
    uint8_t * sq0 = dstp;
    uint8_t * sq1 = dstp + stride;
    uint8_t * sq2 = dstp + stride * 2;
    uint8_t * sp0 = dstp - stride;
    uint8_t * sp1 = dstp - stride * 2;
    uint8_t * sp2 = dstp - stride * 3;

    for (int i = 0; i < 4; i++) {
        if ((abs(sp0[i] - sq0[i]) < d->alpha) && (abs(sp1[i] - sp0[i]) < d->beta) && (abs(sq0[i] - sq1[i]) < d->beta)) {
            int ap = abs(sp2[i] - sp0[i]);
            int aq = abs(sq2[i] - sq0[i]);
            int c = d->c0;
            if (aq < d->beta)
                c++;
            if (ap < d->beta)
                c++;
            int avg0 = (sp0[i] + sq0[i] + 1) >> 1;
            int delta = CLAMP((((sq0[i] - sp0[i]) << 2) + (sp1[i] - sq1[i]) + 4) >> 3, -c, c);
            int deltap1 = CLAMP((sp2[i] + avg0 - (sp1[i] << 1)) >> 1, -d->c0, d->c0);
            int deltaq1 = CLAMP((sq2[i] + avg0 - (sq1[i] << 1)) >> 1, -d->c0, d->c0);
            sp0[i] = CLAMP(sp0[i] + delta, 0, 255);
            sq0[i] = CLAMP(sq0[i] - delta, 0, 255);
            if (ap < d->beta)
                sp1[i] = sp1[i] + deltap1;
            if (aq < d->beta)
                sq1[i] = sq1[i] + deltaq1;
        }
    }
}

static void DeblockHorEdge_16bits(uint16_t *dstp, int stride, const DeblockData *d) {
    const int shift = d->vi->format->bitsPerSample - 8;
    const int peak = (1 << d->vi->format->bitsPerSample) - 1;
    uint16_t * sq0 = dstp;
    uint16_t * sq1 = dstp + stride;
    uint16_t * sq2 = dstp + stride * 2;
    uint16_t * sp0 = dstp - stride;
    uint16_t * sp1 = dstp - stride * 2;
    uint16_t * sp2 = dstp - stride * 3;

    for (int i = 0; i < 4; i++) {
        if ((abs(sp0[i] - sq0[i]) < d->alpha) && (abs(sp1[i] - sp0[i]) < d->beta) && (abs(sq0[i] - sq1[i]) < d->beta)) {
            int ap = abs(sp2[i] - sp0[i]);
            int aq = abs(sq2[i] - sq0[i]);
            int c = d->c0;
            if (aq < d->beta)
                c += (1 << shift);
            if (ap < d->beta)
                c += (1 << shift);
            int avg0 = (sp0[i] + sq0[i] + 1) >> 1;
            int delta = CLAMP((((sq0[i] - sp0[i]) << 2) + (sp1[i] - sq1[i]) + 4) >> 3, -c, c);
            int deltap1 = CLAMP((sp2[i] + avg0 - (sp1[i] << 1)) >> 1, -d->c0, d->c0);
            int deltaq1 = CLAMP((sq2[i] + avg0 - (sq1[i] << 1)) >> 1, -d->c0, d->c0);
            sp0[i] = CLAMP(sp0[i] + delta, 0, peak);
            sq0[i] = CLAMP(sq0[i] - delta, 0, peak);
            if (ap < d->beta)
                sp1[i] = sp1[i] + deltap1;
            if (aq < d->beta)
                sq1[i] = sq1[i] + deltaq1;
        }
    }
}

static void DeblockVerEdge_8bits(uint8_t *dstp, int stride, const DeblockData *d) {
    for (int i = 0; i < 4; i++) {
        if ((abs(dstp[0] - dstp[-1]) < d->alpha) && (abs(dstp[1] - dstp[0]) < d->beta) && (abs(dstp[-1] - dstp[-2]) < d->beta)) {
            int ap = abs(dstp[2] - dstp[0]);
            int aq = abs(dstp[-3] - dstp[-1]);
            int c = d->c0;
            if (aq < d->beta)
                c++;
            if (ap < d->beta)
                c++;
            int avg0 = (dstp[0] + dstp[-1] + 1) >> 1;
            int delta = CLAMP((((dstp[0] - dstp[-1]) << 2) + (dstp[-2] - dstp[1]) + 4) >> 3, -c, c);
            int deltaq1 = CLAMP((dstp[2] + avg0 - (dstp[1] << 1)) >> 1, -d->c0, d->c0);
            int deltap1 = CLAMP((dstp[-3] + avg0 - (dstp[-2] << 1)) >> 1, -d->c0, d->c0);
            dstp[0] = CLAMP(dstp[0] - delta, 0, 255);
            dstp[-1] = CLAMP(dstp[-1] + delta, 0, 255);
            if (ap < d->beta)
                dstp[1] = dstp[1] + deltaq1;
            if (aq < d->beta)
                dstp[-2] = dstp[-2] + deltap1;
        }
        dstp += stride;
    }
}

static void DeblockVerEdge_16bits(uint16_t *dstp, int stride, const DeblockData *d) {
    const int shift = d->vi->format->bitsPerSample - 8;
    const int peak = (1 << d->vi->format->bitsPerSample) - 1;

    for (int i = 0; i < 4; i++) {
        if ((abs(dstp[0] - dstp[-1]) < d->alpha) && (abs(dstp[1] - dstp[0]) < d->beta) && (abs(dstp[-1] - dstp[-2]) < d->beta)) {
            int ap = abs(dstp[2] - dstp[0]);
            int aq = abs(dstp[-3] - dstp[-1]);
            int c = d->c0;
            if (aq < d->beta)
                c += (1 << shift);
            if (ap < d->beta)
                c += (1 << shift);
            int avg0 = (dstp[0] + dstp[-1] + 1) >> 1;
            int delta = CLAMP((((dstp[0] - dstp[-1]) << 2) + (dstp[-2] - dstp[1]) + 4) >> 3, -c, c);
            int deltaq1 = CLAMP((dstp[2] + avg0 - (dstp[1] << 1)) >> 1, -d->c0, d->c0);
            int deltap1 = CLAMP((dstp[-3] + avg0 - (dstp[-2] << 1)) >> 1, -d->c0, d->c0);
            dstp[0] = CLAMP(dstp[0] - delta, 0, peak);
            dstp[-1] = CLAMP(dstp[-1] + delta, 0, peak);
            if (ap < d->beta)
                dstp[1] = dstp[1] + deltaq1;
            if (aq < d->beta)
                dstp[-2] = dstp[-2] + deltap1;
        }
        dstp += stride;
    }
}

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

        for (int plane = 0; plane < d->vi->format->numPlanes; plane++) {
            if (d->process[plane]) {
                const int w = vsapi->getFrameWidth(dst, plane);
                const int h = vsapi->getFrameHeight(dst, plane);
                uint8_t * dstp = vsapi->getWritePtr(dst, plane);

                if (d->vi->format->bytesPerSample == 1) {
                    const int stride = vsapi->getStride(dst, plane);
                    for (int x = 4; x < w; x += 4)
                        DeblockVerEdge_8bits(dstp + x, stride, d);
                    dstp += stride * 4;
                    for (int y = 4; y < h; y += 4) {
                        DeblockHorEdge_8bits(dstp, stride, d);
                        for (int x = 4; x < w; x += 4) {
                            DeblockHorEdge_8bits(dstp + x, stride, d);
                            DeblockVerEdge_8bits(dstp + x, stride, d);
                        }
                        dstp += stride * 4;
                    }
                } else if (d->vi->format->bytesPerSample == 2) {
                    const int stride = vsapi->getStride(dst, plane) / 2;
                    for (int x = 4; x < w; x += 4)
                        DeblockVerEdge_16bits((uint16_t *)dstp + x, stride, d);
                    dstp += stride * 8;
                    for (int y = 4; y < h; y += 4) {
                        DeblockHorEdge_16bits((uint16_t *)dstp, stride, d);
                        for (int x = 4; x < w; x += 4) {
                            DeblockHorEdge_16bits((uint16_t *)dstp + x, stride, d);
                            DeblockVerEdge_16bits((uint16_t *)dstp + x, stride, d);
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
    const int indexa = d.quant + d.aOffset;
    const int indexb = d.quant + d.bOffset;
    d.alpha = alphas[indexa];
    d.beta = betas[indexb];
    d.c0 = cs[indexa];

    d.node = vsapi->propGetNode(in, "clip", 0, nullptr);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(d.vi) || d.vi->format->sampleType != stInteger || d.vi->format->bytesPerSample > 2) {
        vsapi->setError(out, "Deblock: only constant format 8-16 bits integer input supported");
        vsapi->freeNode(d.node);
        return;
    }

    if (d.vi->width & 7 || d.vi->height & 7) {
        vsapi->setError(out, "Deblock: width and height must be mod 8");
        vsapi->freeNode(d.node);
        return;
    }

    if (d.vi->format->bytesPerSample == 2) {
        const int shift = d.vi->format->bitsPerSample - 8;
        d.alpha <<= shift;
        d.beta <<= shift;
        d.c0 <<= shift;
    }

    const int m = vsapi->propNumElements(in, "planes");

    for (int i = 0; i < 3; i++)
        d.process[i] = m <= 0;

    for (int i = 0; i < m; i++) {
        const int n = int64ToIntS(vsapi->propGetInt(in, "planes", i, nullptr));

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
