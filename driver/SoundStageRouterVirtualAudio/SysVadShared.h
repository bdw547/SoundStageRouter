/*++

Copyright (c) Microsoft Corporation All Rights Reserved

Module Name:

    SysvadShared.h

Abstract:

    Header file for common stuffs between sample SWAP APO and the SysVad sample.
*/
#ifndef _SYSVADSHARED_H_
#define _SYSVADSHARED_H_

// {5536DE5A-3E80-4610-B8C4-A0C5FBE23DE5}
//DEFINE_GUID(KSPROPSETID_SysVAD, 0x5536de5a, 0x3e80, 0x4610, 0xb8, 0xc4, 0xa0, 0xc5, 0xfb, 0xe2, 0x3d, 0xe5);


#define STATIC_KSPROPSETID_SysVAD\
    0x5536de5a, 0x3e80, 0x4610, 0xb8, 0xc4, 0xa0, 0xc5, 0xfb, 0xe2, 0x3d, 0xe5
DEFINE_GUIDSTRUCT("5536DE5A-3E80-4610-B8C4-A0C5FBE23DE5", KSPROPSETID_SysVAD);
#define KSPROPSETID_SysVAD DEFINE_GUIDNAMED(KSPROPSETID_SysVAD)


typedef enum{
    KSPROPERTY_SYSVAD_DEFAULTSTREAMEFFECTS
} KSPROPERTY_SYSVAD;

#endif

