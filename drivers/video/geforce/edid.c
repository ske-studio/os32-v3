/*
 * Copyright (c) 2006 Luc Verhaegen (quirks list)
 * Copyright (c) 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 * Copyright 2010 Red Hat, Inc.
 *
 * DDC probing routines (drm_ddc_read & drm_do_probe_ddc_edid) originally from
 * FB layer.
 *   Copyright (C) 2006 Dennis Munsie <dmunsie@cecropia.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
/*
 * Copyright © 1997-2003 by The XFree86 Project, Inc.
 * Copyright © 2007 Dave Airlie
 * Copyright © 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 * Copyright 2005-2006 Luc Verhaegen
 * Copyright (c) 2001, Andy Ritger  aritger@nvidia.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the copyright holder(s)
 * and author(s) shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from the copyright holder(s) and author(s).
 */


/* Independent EDID parser; no DRM dependency. Numeric DMT/CEA entries below
 * are transcribed from MIT-licensed Linux v4.4 drivers/gpu/drm/drm_edid.c.
 * Its file:line references below refer to that tag, not the running kernel.
 * https://github.com/torvalds/linux/blob/v4.4/drivers/gpu/drm/drm_edid.c
 * Specs: https://glenwing.github.io/docs/VESA-{EEDID-A2,DMT-1.13,GTF-1.1,CVT-1.2}.pdf
 * E-EDID tables 3.19/3.20/3.26-3.28; GTF 1.1 §7.3/7.6; CVT §3.
 * nouveau_connector.c:938-955 supplies the corresponding connector boundary.
 */
#include "../../../hwdev/gf/display/data.h"
/* PM integrates these typedefs in types.h; no 64-bit division or libgcc. */
typedef unsigned long long u64;

#define EDID_BYTES 128U
#define EDID_DTD 18U
#define NO_PREFERRED (~(u32)0)
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
/* GTF/CVT standard constants, drm_modes.c:160-178,264-281,371-388. */
enum {
    CELL=8, DOUBLE_CELL=16, SYNC_PERCENT=8, VSYNC_BP_US=550,
    GTF_FRONT=1, GTF_VSYNC=3, GTF_M=600, GTF_C2=80, GTF_K=128, GTF_J2=40,
    CVT_FRONT=3, CVT_BACK=6, CVT_CLOCK_STEP=250,
    DUTY_SCALE=1000, NS_PER_US=1000, NS_PER_SEC=1000000000,
    GTF_DEFAULT=0, RANGE_ONLY=1, GTF_SECONDARY=2, CVT=4
};
struct known_mode { u16 w,h,refresh; bool rb; struct gf4_timing t; };
/* E-EDID table 3.19, established bit order below; Apple entries are not DMT. */
static const struct known_mode established[] = {
 {640,480,60,false,{25175,640,656,752,800,480,490,492,525,GF4_NEG_H|GF4_NEG_V}},
 {640,480,67,false,{30240,640,704,768,864,480,483,486,525,GF4_NEG_H|GF4_NEG_V}},
 {640,480,72,false,{31500,640,664,704,832,480,489,492,520,GF4_NEG_H|GF4_NEG_V}},
 {640,480,75,false,{31500,640,656,720,840,480,481,484,500,GF4_NEG_H|GF4_NEG_V}},
 {800,600,56,false,{36000,800,824,896,1024,600,601,603,625,0}},
 {800,600,60,false,{40000,800,840,968,1056,600,601,605,628,0}},
 {800,600,72,false,{50000,800,856,976,1040,600,637,643,666,0}},
 {800,600,75,false,{49500,800,816,896,1056,600,601,604,625,0}},
 {832,624,75,false,{57284,832,864,928,1152,624,625,628,667,GF4_NEG_H|GF4_NEG_V}},
 {1024,768,87,false,{44900,1024,1032,1208,1264,768,768,776,817,GF4_INTERLACE}},
 {1024,768,60,false,{65000,1024,1048,1184,1344,768,771,777,806,GF4_NEG_H|GF4_NEG_V}},
 {1024,768,70,false,{75000,1024,1048,1184,1328,768,771,777,806,GF4_NEG_H|GF4_NEG_V}},
 {1024,768,75,false,{78750,1024,1040,1136,1312,768,769,772,800,0}},
 {1280,1024,75,false,{135000,1280,1296,1440,1688,1024,1025,1028,1066,0}},
 {1280,1024,60,false,{108000,1280,1328,1440,1688,1024,1025,1028,1066,0}},
 {1152,870,75,false,{100000,1152,1168,1280,1456,870,871,874,915,0}},
 {720,400,70,false,{28320,720,738,846,900,400,412,414,449,GF4_NEG_H}},
 {720,400,88,false,{35500,720,738,846,900,400,421,423,449,GF4_NEG_H|GF4_NEG_V}}
};

/* DMT 1.0 rev.13 IDs 01..58. Both normal and reduced blanking are retained. */
static const struct known_mode dmt[] = {
 {640,350,85,false,{31500,640,672,736,832,350,382,385,445,GF4_NEG_V}}, /* DMT 0x01, drm_edid.c:149 */
 {640,400,85,false,{31500,640,672,736,832,400,401,404,445,GF4_NEG_H}}, /* DMT 0x02, drm_edid.c:153 */
 {720,400,85,false,{35500,720,756,828,936,400,401,404,446,GF4_NEG_H}}, /* DMT 0x03, drm_edid.c:157 */
 {640,480,60,false,{25175,640,656,752,800,480,490,492,525,GF4_NEG_H|GF4_NEG_V}}, /* DMT 0x04, drm_edid.c:161 */
 {640,480,72,false,{31500,640,664,704,832,480,489,492,520,GF4_NEG_H|GF4_NEG_V}}, /* DMT 0x05, drm_edid.c:165 */
 {640,480,75,false,{31500,640,656,720,840,480,481,484,500,GF4_NEG_H|GF4_NEG_V}}, /* DMT 0x06, drm_edid.c:169 */
 {640,480,85,false,{36000,640,696,752,832,480,481,484,509,GF4_NEG_H|GF4_NEG_V}}, /* DMT 0x07, drm_edid.c:173 */
 {800,600,56,false,{36000,800,824,896,1024,600,601,603,625,0}}, /* DMT 0x08, drm_edid.c:177 */
 {800,600,60,false,{40000,800,840,968,1056,600,601,605,628,0}}, /* DMT 0x09, drm_edid.c:181 */
 {800,600,72,false,{50000,800,856,976,1040,600,637,643,666,0}}, /* DMT 0x0a, drm_edid.c:185 */
 {800,600,75,false,{49500,800,816,896,1056,600,601,604,625,0}}, /* DMT 0x0b, drm_edid.c:189 */
 {800,600,85,false,{56250,800,832,896,1048,600,601,604,631,0}}, /* DMT 0x0c, drm_edid.c:193 */
 {800,600,120,true,{73250,800,848,880,960,600,603,607,636,GF4_NEG_V}}, /* DMT 0x0d, drm_edid.c:197 */
 {848,480,60,false,{33750,848,864,976,1088,480,486,494,517,0}}, /* DMT 0x0e, drm_edid.c:201 */
 {1024,768,87,false,{44900,1024,1032,1208,1264,768,768,776,817,GF4_INTERLACE}}, /* DMT 0x0f, v6.12 drm_edid.c:312 corrects v4.4:205 */
 {1024,768,60,false,{65000,1024,1048,1184,1344,768,771,777,806,GF4_NEG_H|GF4_NEG_V}}, /* DMT 0x10, drm_edid.c:210 */
 {1024,768,70,false,{75000,1024,1048,1184,1328,768,771,777,806,GF4_NEG_H|GF4_NEG_V}}, /* DMT 0x11, drm_edid.c:214 */
 {1024,768,75,false,{78750,1024,1040,1136,1312,768,769,772,800,0}}, /* DMT 0x12, drm_edid.c:218 */
 {1024,768,85,false,{94500,1024,1072,1168,1376,768,769,772,808,0}}, /* DMT 0x13, drm_edid.c:222 */
 {1024,768,120,true,{115500,1024,1072,1104,1184,768,771,775,813,GF4_NEG_V}}, /* DMT 0x14, drm_edid.c:226 */
 {1152,864,75,false,{108000,1152,1216,1344,1600,864,865,868,900,0}}, /* DMT 0x15, drm_edid.c:230 */
 {1280,720,60,false,{74250,1280,1390,1430,1650,720,725,730,750,0}}, /* DMT 0x55, drm_edid.c:234 */
 {1280,768,60,true,{68250,1280,1328,1360,1440,768,771,778,790,GF4_NEG_V}}, /* DMT 0x16, drm_edid.c:238 */
 {1280,768,60,false,{79500,1280,1344,1472,1664,768,771,778,798,GF4_NEG_H}}, /* DMT 0x17, drm_edid.c:242 */
 {1280,768,75,false,{102250,1280,1360,1488,1696,768,771,778,805,GF4_NEG_H}}, /* DMT 0x18, drm_edid.c:246 */
 {1280,768,85,false,{117500,1280,1360,1496,1712,768,771,778,809,GF4_NEG_H}}, /* DMT 0x19, drm_edid.c:250 */
 {1280,768,120,true,{140250,1280,1328,1360,1440,768,771,778,813,GF4_NEG_V}}, /* DMT 0x1a, drm_edid.c:254 */
 {1280,800,60,true,{71000,1280,1328,1360,1440,800,803,809,823,GF4_NEG_V}}, /* DMT 0x1b, drm_edid.c:258 */
 {1280,800,60,false,{83500,1280,1352,1480,1680,800,803,809,831,GF4_NEG_H}}, /* DMT 0x1c, drm_edid.c:262 */
 {1280,800,75,false,{106500,1280,1360,1488,1696,800,803,809,838,GF4_NEG_H}}, /* DMT 0x1d, drm_edid.c:266 */
 {1280,800,85,false,{122500,1280,1360,1496,1712,800,803,809,843,GF4_NEG_H}}, /* DMT 0x1e, drm_edid.c:270 */
 {1280,800,120,true,{146250,1280,1328,1360,1440,800,803,809,847,GF4_NEG_V}}, /* DMT 0x1f, drm_edid.c:274 */
 {1280,960,60,false,{108000,1280,1376,1488,1800,960,961,964,1000,0}}, /* DMT 0x20, drm_edid.c:278 */
 {1280,960,85,false,{148500,1280,1344,1504,1728,960,961,964,1011,0}}, /* DMT 0x21, drm_edid.c:282 */
 {1280,960,120,true,{175500,1280,1328,1360,1440,960,963,967,1017,GF4_NEG_V}}, /* DMT 0x22, drm_edid.c:286 */
 {1280,1024,60,false,{108000,1280,1328,1440,1688,1024,1025,1028,1066,0}}, /* DMT 0x23, drm_edid.c:290 */
 {1280,1024,75,false,{135000,1280,1296,1440,1688,1024,1025,1028,1066,0}}, /* DMT 0x24, drm_edid.c:294 */
 {1280,1024,85,false,{157500,1280,1344,1504,1728,1024,1025,1028,1072,0}}, /* DMT 0x25, drm_edid.c:298 */
 {1280,1024,120,true,{187250,1280,1328,1360,1440,1024,1027,1034,1084,GF4_NEG_V}}, /* DMT 0x26, drm_edid.c:302 */
 {1360,768,60,false,{85500,1360,1424,1536,1792,768,771,777,795,0}}, /* DMT 0x27, drm_edid.c:306 */
 {1360,768,120,true,{148250,1360,1408,1440,1520,768,771,776,813,GF4_NEG_V}}, /* DMT 0x28, drm_edid.c:310 */
 {1366,768,60,false,{85500,1366,1436,1579,1792,768,771,774,798,0}}, /* DMT 0x51, drm_edid.c:314 */
 {1366,768,60,false,{72000,1366,1380,1436,1500,768,769,772,800,0}}, /* DMT 0x56, drm_edid.c:318 */
 {1400,1050,60,true,{101000,1400,1448,1480,1560,1050,1053,1057,1080,GF4_NEG_V}}, /* DMT 0x29, drm_edid.c:322 */
 {1400,1050,60,false,{121750,1400,1488,1632,1864,1050,1053,1057,1089,GF4_NEG_H}}, /* DMT 0x2a, drm_edid.c:326 */
 {1400,1050,75,false,{156000,1400,1504,1648,1896,1050,1053,1057,1099,GF4_NEG_H}}, /* DMT 0x2b, drm_edid.c:330 */
 {1400,1050,85,false,{179500,1400,1504,1656,1912,1050,1053,1057,1105,GF4_NEG_H}}, /* DMT 0x2c, drm_edid.c:334 */
 {1400,1050,120,true,{208000,1400,1448,1480,1560,1050,1053,1057,1112,GF4_NEG_V}}, /* DMT 0x2d, drm_edid.c:338 */
 {1440,900,60,true,{88750,1440,1488,1520,1600,900,903,909,926,GF4_NEG_V}}, /* DMT 0x2e, drm_edid.c:342 */
 {1440,900,60,false,{106500,1440,1520,1672,1904,900,903,909,934,GF4_NEG_H}}, /* DMT 0x2f, drm_edid.c:346 */
 {1440,900,75,false,{136750,1440,1536,1688,1936,900,903,909,942,GF4_NEG_H}}, /* DMT 0x30, drm_edid.c:350 */
 {1440,900,85,false,{157000,1440,1544,1696,1952,900,903,909,948,GF4_NEG_H}}, /* DMT 0x31, drm_edid.c:354 */
 {1440,900,120,true,{182750,1440,1488,1520,1600,900,903,909,953,GF4_NEG_V}}, /* DMT 0x32, drm_edid.c:358 */
 {1600,900,60,false,{108000,1600,1624,1704,1800,900,901,904,1000,0}}, /* DMT 0x53, drm_edid.c:362 */
 {1600,1200,60,false,{162000,1600,1664,1856,2160,1200,1201,1204,1250,0}}, /* DMT 0x33, drm_edid.c:366 */
 {1600,1200,65,false,{175500,1600,1664,1856,2160,1200,1201,1204,1250,0}}, /* DMT 0x34, drm_edid.c:370 */
 {1600,1200,70,false,{189000,1600,1664,1856,2160,1200,1201,1204,1250,0}}, /* DMT 0x35, drm_edid.c:374 */
 {1600,1200,75,false,{202500,1600,1664,1856,2160,1200,1201,1204,1250,0}}, /* DMT 0x36, drm_edid.c:378 */
 {1600,1200,85,false,{229500,1600,1664,1856,2160,1200,1201,1204,1250,0}}, /* DMT 0x37, drm_edid.c:382 */
 {1600,1200,120,true,{268250,1600,1648,1680,1760,1200,1203,1207,1271,GF4_NEG_V}}, /* DMT 0x38, drm_edid.c:386 */
 {1680,1050,60,true,{119000,1680,1728,1760,1840,1050,1053,1059,1080,GF4_NEG_V}}, /* DMT 0x39, drm_edid.c:390 */
 {1680,1050,60,false,{146250,1680,1784,1960,2240,1050,1053,1059,1089,GF4_NEG_H}}, /* DMT 0x3a, drm_edid.c:394 */
 {1680,1050,75,false,{187000,1680,1800,1976,2272,1050,1053,1059,1099,GF4_NEG_H}}, /* DMT 0x3b, drm_edid.c:398 */
 {1680,1050,85,false,{214750,1680,1808,1984,2288,1050,1053,1059,1105,GF4_NEG_H}}, /* DMT 0x3c, drm_edid.c:402 */
 {1680,1050,120,true,{245500,1680,1728,1760,1840,1050,1053,1059,1112,GF4_NEG_V}}, /* DMT 0x3d, drm_edid.c:406 */
 {1792,1344,60,false,{204750,1792,1920,2120,2448,1344,1345,1348,1394,GF4_NEG_H}}, /* DMT 0x3e, drm_edid.c:410 */
 {1792,1344,75,false,{261000,1792,1888,2104,2456,1344,1345,1348,1417,GF4_NEG_H}}, /* DMT 0x3f, drm_edid.c:414 */
 {1792,1344,120,true,{333250,1792,1840,1872,1952,1344,1347,1351,1423,GF4_NEG_V}}, /* DMT 0x40, drm_edid.c:418 */
 {1856,1392,60,false,{218250,1856,1952,2176,2528,1392,1393,1396,1439,GF4_NEG_H}}, /* DMT 0x41, drm_edid.c:422 */
 {1856,1392,75,false,{288000,1856,1984,2208,2560,1392,1393,1396,1500,GF4_NEG_H}}, /* DMT 0x42, drm_edid.c:426 */
 {1856,1392,120,true,{356500,1856,1904,1936,2016,1392,1395,1399,1474,GF4_NEG_V}}, /* DMT 0x43, drm_edid.c:430 */
 {1920,1080,60,false,{148500,1920,2008,2052,2200,1080,1084,1089,1125,0}}, /* DMT 0x52, DMT rev13 p89 overrides drm_edid.c:434 polarity */
 {1920,1200,60,true,{154000,1920,1968,2000,2080,1200,1203,1209,1235,GF4_NEG_V}}, /* DMT 0x44, drm_edid.c:438 */
 {1920,1200,60,false,{193250,1920,2056,2256,2592,1200,1203,1209,1245,GF4_NEG_H}}, /* DMT 0x45, drm_edid.c:442 */
 {1920,1200,75,false,{245250,1920,2056,2264,2608,1200,1203,1209,1255,GF4_NEG_H}}, /* DMT 0x46, drm_edid.c:446 */
 {1920,1200,85,false,{281250,1920,2064,2272,2624,1200,1203,1209,1262,GF4_NEG_H}}, /* DMT 0x47, drm_edid.c:450 */
 {1920,1200,120,true,{317000,1920,1968,2000,2080,1200,1203,1209,1271,GF4_NEG_V}}, /* DMT 0x48, drm_edid.c:454 */
 {1920,1440,60,false,{234000,1920,2048,2256,2600,1440,1441,1444,1500,GF4_NEG_H}}, /* DMT 0x49, drm_edid.c:458 */
 {1920,1440,75,false,{297000,1920,2064,2288,2640,1440,1441,1444,1500,GF4_NEG_H}}, /* DMT 0x4a, drm_edid.c:462 */
 {1920,1440,120,true,{380500,1920,1968,2000,2080,1440,1443,1447,1525,GF4_NEG_V}}, /* DMT 0x4b, drm_edid.c:466 */
 {2048,1152,60,false,{162000,2048,2074,2154,2250,1152,1153,1156,1200,0}}, /* DMT 0x54, drm_edid.c:470 */
 {2560,1600,60,true,{268500,2560,2608,2640,2720,1600,1603,1609,1646,GF4_NEG_V}}, /* DMT 0x4c, drm_edid.c:474 */
 {2560,1600,60,false,{348500,2560,2752,3032,3504,1600,1603,1609,1658,GF4_NEG_H}}, /* DMT 0x4d, drm_edid.c:478 */
 {2560,1600,75,false,{443250,2560,2768,3048,3536,1600,1603,1609,1672,GF4_NEG_H}}, /* DMT 0x4e, drm_edid.c:482 */
 {2560,1600,85,false,{505250,2560,2768,3048,3536,1600,1603,1609,1682,GF4_NEG_H}}, /* DMT 0x4f, drm_edid.c:486 */
 {2560,1600,120,true,{552750,2560,2608,2640,2720,1600,1603,1609,1694,GF4_NEG_V}}, /* DMT 0x50, drm_edid.c:490 */
 {4096,2160,60,true,{556744,4096,4104,4136,4176,2160,2208,2216,2222,GF4_NEG_V}}, /* DMT 0x57, drm_edid.c:494 */
 {4096,2160,60,true,{556188,4096,4104,4136,4176,2160,2208,2216,2222,GF4_NEG_V}}, /* DMT 0x58, drm_edid.c:498 */
};
/* CEA-861 VIC 1..64 (drm_edid.c:641-987). Pixel repetition has no
 * representation in gf4_timing; those IDs are explicitly unsupported. */
static const struct gf4_timing cea[] = {
 [1]={25175,640,656,752,800,480,490,492,525,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:642 */
 [2]={27000,720,736,798,858,480,489,495,525,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:647 */
 [3]={27000,720,736,798,858,480,489,495,525,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:652 */
 [4]={74250,1280,1390,1430,1650,720,725,730,750,0}, /* drm_edid.c:657 */
 [5]={74250,1920,2008,2052,2200,1080,1084,1094,1125,GF4_INTERLACE}, /* drm_edid.c:662 */
 [6]={0}, /* drm_edid.c:668, pixel repetition unsupported */
 [7]={0}, /* drm_edid.c:674, pixel repetition unsupported */
 [8]={0}, /* drm_edid.c:680, pixel repetition unsupported */
 [9]={0}, /* drm_edid.c:686, pixel repetition unsupported */
 [10]={54000,2880,2956,3204,3432,480,488,494,525,GF4_NEG_H|GF4_NEG_V|GF4_INTERLACE}, /* drm_edid.c:692 */
 [11]={54000,2880,2956,3204,3432,480,488,494,525,GF4_NEG_H|GF4_NEG_V|GF4_INTERLACE}, /* drm_edid.c:698 */
 [12]={54000,2880,2956,3204,3432,240,244,247,262,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:704 */
 [13]={54000,2880,2956,3204,3432,240,244,247,262,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:709 */
 [14]={54000,1440,1472,1596,1716,480,489,495,525,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:714 */
 [15]={54000,1440,1472,1596,1716,480,489,495,525,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:719 */
 [16]={148500,1920,2008,2052,2200,1080,1084,1089,1125,0}, /* drm_edid.c:724 */
 [17]={27000,720,732,796,864,576,581,586,625,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:729 */
 [18]={27000,720,732,796,864,576,581,586,625,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:734 */
 [19]={74250,1280,1720,1760,1980,720,725,730,750,0}, /* drm_edid.c:739 */
 [20]={74250,1920,2448,2492,2640,1080,1084,1094,1125,GF4_INTERLACE}, /* drm_edid.c:744 */
 [21]={0}, /* drm_edid.c:750, pixel repetition unsupported */
 [22]={0}, /* drm_edid.c:756, pixel repetition unsupported */
 [23]={0}, /* drm_edid.c:762, pixel repetition unsupported */
 [24]={0}, /* drm_edid.c:768, pixel repetition unsupported */
 [25]={54000,2880,2928,3180,3456,576,580,586,625,GF4_NEG_H|GF4_NEG_V|GF4_INTERLACE}, /* drm_edid.c:774 */
 [26]={54000,2880,2928,3180,3456,576,580,586,625,GF4_NEG_H|GF4_NEG_V|GF4_INTERLACE}, /* drm_edid.c:780 */
 [27]={54000,2880,2928,3180,3456,288,290,293,312,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:786 */
 [28]={54000,2880,2928,3180,3456,288,290,293,312,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:791 */
 [29]={54000,1440,1464,1592,1728,576,581,586,625,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:796 */
 [30]={54000,1440,1464,1592,1728,576,581,586,625,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:801 */
 [31]={148500,1920,2448,2492,2640,1080,1084,1089,1125,0}, /* drm_edid.c:806 */
 [32]={74250,1920,2558,2602,2750,1080,1084,1089,1125,0}, /* drm_edid.c:811 */
 [33]={74250,1920,2448,2492,2640,1080,1084,1089,1125,0}, /* drm_edid.c:816 */
 [34]={74250,1920,2008,2052,2200,1080,1084,1089,1125,0}, /* drm_edid.c:821 */
 [35]={108000,2880,2944,3192,3432,480,489,495,525,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:826 */
 [36]={108000,2880,2944,3192,3432,480,489,495,525,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:831 */
 [37]={108000,2880,2928,3184,3456,576,581,586,625,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:836 */
 [38]={108000,2880,2928,3184,3456,576,581,586,625,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:841 */
 [39]={72000,1920,1952,2120,2304,1080,1126,1136,1250,GF4_NEG_V|GF4_INTERLACE}, /* drm_edid.c:846 */
 [40]={148500,1920,2448,2492,2640,1080,1084,1094,1125,GF4_INTERLACE}, /* drm_edid.c:852 */
 [41]={148500,1280,1720,1760,1980,720,725,730,750,0}, /* drm_edid.c:858 */
 [42]={54000,720,732,796,864,576,581,586,625,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:863 */
 [43]={54000,720,732,796,864,576,581,586,625,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:868 */
 [44]={0}, /* drm_edid.c:873, pixel repetition unsupported */
 [45]={0}, /* drm_edid.c:879, pixel repetition unsupported */
 [46]={148500,1920,2008,2052,2200,1080,1084,1094,1125,GF4_INTERLACE}, /* drm_edid.c:885 */
 [47]={148500,1280,1390,1430,1650,720,725,730,750,0}, /* drm_edid.c:891 */
 [48]={54000,720,736,798,858,480,489,495,525,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:896 */
 [49]={54000,720,736,798,858,480,489,495,525,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:901 */
 [50]={0}, /* drm_edid.c:906, pixel repetition unsupported */
 [51]={0}, /* drm_edid.c:912, pixel repetition unsupported */
 [52]={108000,720,732,796,864,576,581,586,625,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:918 */
 [53]={108000,720,732,796,864,576,581,586,625,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:923 */
 [54]={0}, /* drm_edid.c:928, pixel repetition unsupported */
 [55]={0}, /* drm_edid.c:934, pixel repetition unsupported */
 [56]={108000,720,736,798,858,480,489,495,525,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:940 */
 [57]={108000,720,736,798,858,480,489,495,525,GF4_NEG_H|GF4_NEG_V}, /* drm_edid.c:945 */
 [58]={0}, /* drm_edid.c:950, pixel repetition unsupported */
 [59]={0}, /* drm_edid.c:956, pixel repetition unsupported */
 [60]={59400,1280,3040,3080,3300,720,725,730,750,0}, /* drm_edid.c:962 */
 [61]={74250,1280,3700,3740,3960,720,725,730,750,0}, /* drm_edid.c:967 */
 [62]={74250,1280,3040,3080,3300,720,725,730,750,0}, /* drm_edid.c:972 */
 [63]={297000,1920,2008,2052,2200,1080,1084,1089,1125,0}, /* drm_edid.c:977 */
 [64]={297000,1920,2448,2492,2640,1080,1084,1089,1125,0}, /* v6.12 drm_edid.c:1090 corrects v4.4:982 */
};

static bool same(const struct gf4_timing *a,const struct gf4_timing *b)
{
    return a->khz==b->khz && a->hd==b->hd && a->hs==b->hs &&
        a->he==b->he && a->ht==b->ht && a->vd==b->vd &&
        a->vs==b->vs && a->ve==b->ve && a->vt==b->vt && a->flags==b->flags;
}
static void add(struct gf4_edid_state *s,const struct gf4_timing *t,bool preferred)
{
    u32 i;
    for(i=0;i<s->ntiming;i++) if(same(&s->timing[i],t)) {
        if(preferred && s->preferred==NO_PREFERRED) s->preferred=i;
        return;
    }
    if(s->ntiming==GF4_TIMINGS) {
        s->truncated=true;
        if(!preferred || s->preferred!=NO_PREFERRED) return;
        i=GF4_TIMINGS-1;
    } else i=s->ntiming++;
    s->timing[i]=*t;
    if(preferred && s->preferred==NO_PREFERRED) s->preferred=i;
}
/* Tables describe full frames. GF_FEAT4_DESIGN §3 requires CRTC field
 * values, as nouveau crtc.c:245-257's adjusted_mode->crtc_v* inputs do.
 * DTDs already describe fields and bypass this conversion. */
static void add_table(struct gf4_edid_state *s,const struct gf4_timing *source,bool preferred)
{
    struct gf4_timing t=*source;
    if(t.flags&GF4_INTERLACE) { t.vd/=2; t.vs/=2; t.ve/=2; t.vt/=2; }
    if(t.flags&GF4_DBLSCAN) { t.vd*=2; t.vs*=2; t.ve*=2; t.vt*=2; }
    add(s,&t,preferred);
}
/* Quotients here fit u32. Compare products to avoid compiler-generated
 * 64-bit division. Each multiplication has two 32-bit operands. */
static u32 muldiv(u32 a,u32 b,u32 d)
{
    u64 n=(u64)a*b;
    u32 q=0;
    for(u32 bit=1U<<31;bit;bit>>=1) {
        u32 next=q|bit;
        if((u64)next*d<=n) q=next;
    }
    return q;
}
static bool in_range(const struct gf4_edid_state *s,const struct gf4_timing *t)
{
    if(!s->range_valid) return true;
    /* Generated standard timings are progressive. Compare exact products,
     * as B0 modes.c:timing_fits does; rounding must not admit out-of-range
     * clocks. E-EDID table 3.26 supplies the limits. */
    u64 pixels=(u64)t->ht*t->vt, hz=(u64)t->khz*1000U;
    return t->khz>=(u64)s->min_h_khz*t->ht &&
        t->khz<=(u64)s->max_h_khz*t->ht &&
        hz>=pixels*s->min_v_hz && hz<=pixels*s->max_v_hz &&
        t->khz<=s->max_khz;
}
/* GTF 1.1 §7.3 steps 7-21, §7.6 sync rounding. Unlike the approximate
 * drm_modes.c:451-509 implementation, use corrected H_PERIOD (step 12)
 * for duty and clock; ns precision, clock rounded to the nearest kHz. */
static bool gtf(u32 w,u32 h,u32 hz,u32 m,u32 c2,u32 k,u32 j2,
                struct gf4_timing *t)
{
    u32 period=(NS_PER_SEC-VSYNC_BP_US*NS_PER_US*hz)/(hz*(h+GTF_FRONT));
    if(!period) return false;
    u32 bp=(VSYNC_BP_US*NS_PER_US+period/2U)/period;
    if(bp<GTF_VSYNC) bp=GTF_VSYNC;
    u32 vt=h+GTF_FRONT+bp;
    /* Keep the secondary curve's half-unit C/J and /256 K precision. */
    i32 cp=((i32)c2-(i32)j2)*(i32)k+(i32)(256U*j2);
    if(cp<=0) return false;
    u32 cprime=(u32)cp*DUTY_SCALE/512U;
    u32 slope=muldiv(m*k,1000000U,256U*hz*vt);
    if(slope>=cprime || cprime-slope>=100U*DUTY_SCALE) return false;
    u32 duty=cprime-slope;
    u32 blank=(w*duty/(100U*DUTY_SCALE-duty)+CELL)/DOUBLE_CELL*DOUBLE_CELL;
    u32 ht=w+blank, sync=(ht*SYNC_PERCENT/100U+CELL/2U)/CELL*CELL;
    if(blank/2U<=sync || ht>65535U || vt>65535U) return false;
    u32 clock=muldiv(ht,hz*vt,1000U);
    /* Round the exact Hz clock to the nearest kHz, without u64 division. */
    if((u64)ht*(hz*vt)-(u64)clock*1000U>=500U) clock++;
    *t=(struct gf4_timing){clock,w,w+blank/2U-sync,w+blank/2U,ht,
                          h,h+GTF_FRONT,h+GTF_FRONT+GTF_VSYNC,vt,
                          (m==GTF_M && c2==GTF_C2 && k==GTF_K && j2==GTF_J2)
                          ? GF4_NEG_H : GF4_NEG_V};
    return true;
}
/* CVT 1.1/1.2 §5.2-5.4; compare drm_modes.c:218-337. Only progressive,
 * no-margin timings are generated for EDID standard timing identifiers. */
static bool cvt(u32 w,u32 h,u32 hz,struct gf4_timing *t)
{
    u32 vsync;
    if(w*3U==h*4U) vsync=4;
    else if(w*9U==h*16U) vsync=5;
    else if(w*10U==h*16U) vsync=6;
    else if(w*4U==h*5U || w*9U==h*15U) vsync=7;
    else vsync=10;
    u32 period=(NS_PER_SEC-VSYNC_BP_US*NS_PER_US*hz)/(hz*(h+CVT_FRONT));
    if(!period) return false;
    u32 bp=VSYNC_BP_US*NS_PER_US/period+1U;
    u32 minimum=vsync+CVT_BACK;
    if(bp<minimum) bp=minimum;
    u32 vt=h+bp+CVT_FRONT, ht, hs, he, clock;
    {
        i32 duty=30*DUTY_SCALE-(i32)(300U*period/NS_PER_US);
        if(duty<20*DUTY_SCALE) duty=20*DUTY_SCALE;
        u32 blank=w*(u32)duty/(100U*DUTY_SCALE-(u32)duty)/DOUBLE_CELL*DOUBLE_CELL;
        ht=w+blank; he=w+blank/2U;
        /* Sync width rounded down to a character cell (CVT §5.6). */
        hs=he-(ht*SYNC_PERCENT/100U/CELL)*CELL;
        clock=muldiv(ht,1000000U,period);
    }
    clock=clock/CVT_CLOCK_STEP*CVT_CLOCK_STEP;
    if(!clock || ht>65535U || vt>65535U || hs<=w || he>ht) return false;
    *t=(struct gf4_timing){clock,w,hs,he,ht,h,h+CVT_FRONT,
                          h+CVT_FRONT+vsync,vt,GF4_NEG_H};
    return true;
}
/* E-EDID table 3.26-3.28, drm_edid.c:1970-2043. Descriptor pointer is
 * local to block0 parsing; no ROM/EDID pointer survives the call. */
static bool range(struct gf4_edid_state *s,const u8 *p,u32 revision)
{
    u32 offsets=revision>=4 ? p[4] : 0;
    u32 vmin=p[5]+((offsets&1U) ? 255U : 0U);
    u32 vmax=p[6]+((offsets&2U) ? 255U : 0U);
    u32 hmin=p[7]+((offsets&4U) ? 255U : 0U);
    u32 hmax=p[8]+((offsets&8U) ? 255U : 0U);
    if(!vmin || vmin>vmax || !hmin || hmin>hmax) return false;
    u32 max=p[9] ? p[9]*10000U : ~(u32)0;
    if(revision>=4 && p[10]==CVT && p[9]) {
        u32 extra=(p[12]>>2)*CVT_CLOCK_STEP;
        if(extra>=max) return false;
        max-=extra;
    }
    s->min_v_hz=vmin; s->max_v_hz=vmax;
    s->min_h_khz=hmin; s->max_h_khz=hmax; s->max_khz=max;
    s->range_valid=true;
    return true;
}
static void standard(struct gf4_edid_state *s,const u8 *p,const u8 *r,u32 revision)
{
    if(p[0]==1 && p[1]==1) return;
    if(!p[0]) { s->last_rc=GF4_EEDID; return; }
    u32 w=(p[0]+31U)*CELL, aspect=p[1]>>6, hz=(p[1]&63U)+60U;
    u32 h=aspect==0 ? (revision<3 ? w : w*10U/16U) :
        aspect==1 ? w*3U/4U : aspect==2 ? w*4U/5U : w*9U/16U;
    bool rb=r && r[10]==CVT && (r[15]&0x10);
    for(u32 pass=0;pass<2;pass++) {
        bool reduced=rb && pass==0;
        for(u32 i=0;i<COUNT(dmt);i++) {
            const struct known_mode *m=&dmt[i];
            if(m->w==w && m->h==h && m->refresh==hz && m->rb==reduced) {
                add_table(s,&m->t,false); return;
            }
        }
        if(!rb) break;
    }
    /* Preserve the inherited policy: no guessing without explicit limits. */
    if(revision<2 || !r || !s->range_valid || r[10]==RANGE_ONLY) {
        s->last_rc=GF4_EUNSUPPORTED; return;
    }
    struct gf4_timing t;
    bool ok;
    if(r[10]==CVT) {
        u32 maxw=8U*(r[13]+256U*(r[12]&3U));
        if((r[11]>>4)!=1) {
            s->last_rc=GF4_EUNSUPPORTED; return;
        }
        if(maxw && w>maxw) { s->last_rc=GF4_ERANGE; return; }
        /* E-EDID Appendix B 1.2 mandates normal blanking for standard codes. */
        ok=cvt(w,h,hz,&t);
    } else if(r[10]==GTF_DEFAULT || r[10]==GTF_SECONDARY) {
        ok=gtf(w,h,hz,GTF_M,GTF_C2,GTF_K,GTF_J2,&t);
        if(ok && r[10]==GTF_SECONDARY && t.khz>2U*r[12]*t.ht)
            ok=gtf(w,h,hz,r[14]+256U*r[15],r[13],r[16],r[17],&t);
    } else { s->last_rc=GF4_EUNSUPPORTED; return; }
    if(!ok || !in_range(s,&t)) { s->last_rc=GF4_ERANGE; return; }
    add(s,&t,false);
}
/* EDID 1.4 table 3.21: interlaced DTD vertical values describe one field.
 * Keep them in CRTC field units; B1 encodes the interlace half-line. */
static i32 dtd(const u8 *p,struct gf4_timing *t)
{
    u32 clk=(p[0]|(u32)p[1]<<8)*10U;
    if(!clk) return 0;
    u32 hd=p[2]|(u32)(p[4]&0xf0)<<4, hb=p[3]|(u32)(p[4]&15)<<8;
    u32 vd=p[5]|(u32)(p[7]&0xf0)<<4, vb=p[6]|(u32)(p[7]&15)<<8;
    u32 ho=p[8]|(u32)(p[11]&0xc0)<<2, hw=p[9]|(u32)(p[11]&0x30)<<4;
    u32 vo=(p[10]>>4)|((u32)(p[11]&12)<<2), vw=(p[10]&15)|((u32)(p[11]&3)<<4);
    if(!hd || !vd || !hb || !vb || !hw || !vw || ho+hw>hb || vo+vw>vb)
        return GF4_EEDID;
    /* Composite/stereo sync needs an interface the frozen timing lacks. */
    if((p[17]&0x18)!=0x18 || (p[17]&0x60)) return GF4_EUNSUPPORTED;
    u32 flags=0;
    if(!(p[17]&2)) flags|=GF4_NEG_H;
    if(!(p[17]&4)) flags|=GF4_NEG_V;
    if(p[17]&0x80) flags|=GF4_INTERLACE;
    *t=(struct gf4_timing){clk,hd,hd+ho,hd+ho+hw,hd+hb,
                          vd,vd+vo,vd+vo+vw,vd+vb,flags};
    return 1;
}
static bool checksum(const u8 *p)
{
    u32 sum=0;
    for(u32 i=0;i<EDID_BYTES;i++) sum+=p[i];
    return (sum&255U)==0;
}
static i32 base(struct gf4_edid_state *s,const u8 *p)
{
    static const u8 est[][3]={
        {35,7,16},{35,6,17},{35,5,0},{35,4,1},{35,3,2},{35,2,3},
        {35,1,4},{35,0,5},{36,7,6},{36,6,7},{36,5,8},{36,4,9},
        {36,3,10},{36,2,11},{36,1,12},{36,0,13},{37,7,15}
    };
    const u8 *r=NULL;
    struct gf4_timing t;
    /* Validate all DTDs before adding anything, including descriptor bounds. */
    for(u32 i=54;i<126;i+=EDID_DTD) {
        i32 rc=dtd(p+i,&t);
        if(rc==GF4_EEDID) return rc;
        if(rc<0) s->last_rc=rc;
        if(!p[i] && !p[i+1] && p[i+3]==0xfd) {
            if(p[i+2] || !range(s,p+i,p[19])) return GF4_EEDID;
            r=p+i;
        }
    }
    for(u32 i=54;i<126;i+=EDID_DTD) {
        if(dtd(p+i,&t)==1) add(s,&t,i==54 && ((p[24]&2) || p[19]>=4));
        /* Additional standard timings are the same 2-byte format. */
        if(!p[i] && !p[i+1] && !p[i+2] && p[i+3]==0xfa)
            for(u32 j=5;j<17;j+=2) standard(s,p+i+j,r,p[19]);
        if(!p[i] && !p[i+1] && (p[i+3]==0xf7 || p[i+3]==0xf8))
            s->last_rc=GF4_EUNSUPPORTED;
    }
    for(u32 i=0;i<COUNT(est);i++)
        if(p[est[i][0]]&(1U<<est[i][1])) add_table(s,&established[est[i][2]].t,false);
    for(u32 i=38;i<54;i+=2) standard(s,p+i,r,p[19]);
    return 0;
}
static i32 cta(struct gf4_edid_state *s,const u8 *p)
{
    u32 end=p[2];
    if(p[1]!=3) { s->last_rc=GF4_EUNSUPPORTED; return 0; }
    if(!end) return 0;
    if(end<4 || end>127) return GF4_EEDID;
    struct gf4_timing t;
    /* Transactional preflight: a broken later block/DTD cannot leave a
     * partial extension in the already accepted base mode list. */
    for(u32 i=4;i<end;) {
        u32 n=p[i++]&31U;
        if(n>end-i) return GF4_EEDID;
        i+=n;
    }
    for(u32 i=end;i+EDID_DTD<=127;i+=EDID_DTD)
        if(dtd(p+i,&t)==GF4_EEDID) return GF4_EEDID;
    for(u32 i=4;i<end;) {
        u32 tag=p[i]>>5, n=p[i]&31U; i++;
        if(tag==2) for(u32 j=0;j<n;j++) {
            u32 vic=p[i+j]&127U;
            if(vic && vic<COUNT(cea) && cea[vic].khz)
                add_table(s,&cea[vic],(p[i+j]&128U)!=0);
            else s->last_rc=GF4_EUNSUPPORTED;
        }
        i+=n;
    }
    u32 native=p[3]&15U, number=0;
    for(u32 i=end;i+EDID_DTD<=127;i+=EDID_DTD) {
        i32 rc=dtd(p+i,&t);
        if(rc==1) { add(s,&t,number<native); number++; }
        else if(rc<0) s->last_rc=rc;
    }
    return 0;
}
i32 gf4_edid_modes(const u8 block[128],u32 block_no,struct gf4_edid_state *s)
{
    static const u8 header[8]={0,255,255,255,255,255,255,0};
    if(!block || !s) return GF4_EINVAL;
    if(!block_no) {
        *s=(struct gf4_edid_state){0};
        s->version=GF4_EDID_VERSION; s->size=sizeof(*s); s->preferred=NO_PREFERRED;
        for(u32 i=0;i<COUNT(header);i++) if(block[i]!=header[i]) {
            s->last_rc=GF4_EEDID; return GF4_EEDID;
        }
        if(!checksum(block) || block[18]!=1 || block[19]>4) {
            s->last_rc=GF4_EEDID; return GF4_EEDID;
        }
        i32 rc=base(s,block);
        if(rc) { s->ntiming=0; s->preferred=NO_PREFERRED; s->last_rc=rc; return rc; }
        s->base_valid=true; s->digital=(block[20]&0x80)!=0;
        s->extensions=block[126]; s->next_block=1;
        return 0;
    }
    if(s->version!=GF4_EDID_VERSION || s->size!=sizeof(*s) ||
       !s->base_valid || block_no!=s->next_block || block_no>s->extensions) {
        s->last_rc=GF4_ESTATE; return GF4_ESTATE;
    }
    s->next_block++;
    if(!checksum(block)) { s->last_rc=GF4_EEDID; return GF4_EEDID; }
    if(block[0]!=2) { s->last_rc=GF4_EUNSUPPORTED; return 0; }
    i32 rc=cta(s,block);
    if(rc) s->last_rc=rc;
    return rc;
}
