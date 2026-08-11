/*++

Copyright (c) Microsoft Corporation All Rights Reserved

Module Name:

    speakertoptable.h

Abstract:

    Declaration of topology tables.

    SoundStage Router adaptation:
    Kept as a single line-out topology for the trimmed render-only endpoint and
    updated the jack description to advertise the maximum 7.1 speaker layout.

--*/

#ifndef _SYSVAD_SPEAKERTOPTABLE_H_
#define _SYSVAD_SPEAKERTOPTABLE_H_

static
KSDATARANGE SpeakerTopoPinDataRangesBridge[] =
{
 {
   sizeof(KSDATARANGE),
   0,
   0,
   0,
   STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
   STATICGUIDOF(KSDATAFORMAT_SUBTYPE_ANALOG),
   STATICGUIDOF(KSDATAFORMAT_SPECIFIER_NONE)
 }
};

static
PKSDATARANGE SpeakerTopoPinDataRangePointersBridge[] =
{
  &SpeakerTopoPinDataRangesBridge[0]
};

static
PCPIN_DESCRIPTOR SpeakerTopoMiniportPins[] =
{
  {
    0,
    0,
    0,
    NULL,
    {
      0,
      NULL,
      0,
      NULL,
      SIZEOF_ARRAY(SpeakerTopoPinDataRangePointersBridge),
      SpeakerTopoPinDataRangePointersBridge,
      KSPIN_DATAFLOW_IN,
      KSPIN_COMMUNICATION_NONE,
      &KSCATEGORY_AUDIO,
      NULL,
      0
    }
  },
  {
    0,
    0,
    0,
    NULL,
    {
      0,
      NULL,
      0,
      NULL,
      SIZEOF_ARRAY(SpeakerTopoPinDataRangePointersBridge),
      SpeakerTopoPinDataRangePointersBridge,
      KSPIN_DATAFLOW_OUT,
      KSPIN_COMMUNICATION_NONE,
      &KSNODETYPE_SPEAKER,
      NULL,
      0
    }
  }
};

static
KSJACK_DESCRIPTION SpeakerJackDescBridge =
{
    KSAUDIO_SPEAKER_7POINT1_SURROUND,
    0xB3C98C,
    eConnTypeUnknown,
    eGeoLocFront,
    eGenLocPrimaryBox,
    ePortConnIntegratedDevice,
    TRUE
};

static
PKSJACK_DESCRIPTION SpeakerJackDescriptions[] =
{
    NULL,
    &SpeakerJackDescBridge
};

static SYSVAD_AUDIOPOSTURE_INFO SpeakerAudioPostureInfo = { TRUE };

static
PSYSVAD_AUDIOPOSTURE_INFO SpeakerAudioPostureInfoPointers[]
{
    NULL,
    &SpeakerAudioPostureInfo
};

static
PCCONNECTION_DESCRIPTOR SpeakerTopoMiniportConnections[] =
{
  { PCFILTER_NODE, KSPIN_TOPO_WAVEOUT_SOURCE, PCFILTER_NODE, KSPIN_TOPO_LINEOUT_DEST }
};

static
PCPROPERTY_ITEM PropertiesSpeakerTopoFilter[] =
{
    {
        &KSPROPSETID_Jack,
        KSPROPERTY_JACK_DESCRIPTION,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_SpeakerTopoFilter
    },
    {
        &KSPROPSETID_Jack,
        KSPROPERTY_JACK_DESCRIPTION2,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_SpeakerTopoFilter
    },
    {
        &KSPROPSETID_Jack,
        KSPROPERTY_JACK_DESCRIPTION3,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_SpeakerTopoFilter
    },
    {
        &KSPROPSETID_AudioResourceManagement,
        KSPROPERTY_AUDIORESOURCEMANAGEMENT_RESOURCEGROUP,
        KSPROPERTY_TYPE_SET,
        PropertyHandler_SpeakerTopoFilter
    },
    {
        &KSPROPSETID_AudioPosture,
        KSPROPERTY_AUDIOPOSTURE_ORIENTATION,
        KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_SpeakerTopoFilter
    }
};

DEFINE_PCAUTOMATION_TABLE_PROP(AutomationSpeakerTopoFilter, PropertiesSpeakerTopoFilter);

static
PCFILTER_DESCRIPTOR SpeakerTopoMiniportFilterDescriptor =
{
  0,
  &AutomationSpeakerTopoFilter,
  sizeof(PCPIN_DESCRIPTOR),
  SIZEOF_ARRAY(SpeakerTopoMiniportPins),
  SpeakerTopoMiniportPins,
  sizeof(PCNODE_DESCRIPTOR),
  0,
  NULL,
  SIZEOF_ARRAY(SpeakerTopoMiniportConnections),
  SpeakerTopoMiniportConnections,
  0,
  NULL
};

#endif // _SYSVAD_SPEAKERTOPTABLE_H_
