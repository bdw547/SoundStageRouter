/*++

Copyright (c) Microsoft Corporation All Rights Reserved

Module Name:

    speakerwavtable.h

Abstract:

    Declaration of wave miniport tables for the render endpoints.

    SoundStage Router adaptation:
    Reduced to a single 48 kHz, 32-bit IEEE float, 5.1 render endpoint with
    loopback support. The offload pin is intentionally retained so the SysVAD
    WaveRT render contract remains close to upstream while exposing only the
    required shared-mode format.

--*/

#ifndef _SYSVAD_SPEAKERWAVTABLE_H_
#define _SYSVAD_SPEAKERWAVTABLE_H_

#include "simple.h"

#define SPEAKER_DEVICE_MAX_CHANNELS                 6

#define SPEAKER_HOST_MAX_CHANNELS                   6
#define SPEAKER_HOST_MIN_BITS_PER_SAMPLE            32
#define SPEAKER_HOST_MAX_BITS_PER_SAMPLE            32
#define SPEAKER_HOST_MIN_SAMPLE_RATE                48000
#define SPEAKER_HOST_MAX_SAMPLE_RATE                48000

#define SPEAKER_OFFLOAD_MAX_CHANNELS                6
#define SPEAKER_OFFLOAD_MIN_BITS_PER_SAMPLE         32
#define SPEAKER_OFFLOAD_MAX_BITS_PER_SAMPLE         32
#define SPEAKER_OFFLOAD_MIN_SAMPLE_RATE             48000
#define SPEAKER_OFFLOAD_MAX_SAMPLE_RATE             48000

#define SPEAKER_LOOPBACK_MAX_CHANNELS               SPEAKER_HOST_MAX_CHANNELS
#define SPEAKER_LOOPBACK_MIN_BITS_PER_SAMPLE        SPEAKER_HOST_MIN_BITS_PER_SAMPLE
#define SPEAKER_LOOPBACK_MAX_BITS_PER_SAMPLE        SPEAKER_HOST_MAX_BITS_PER_SAMPLE
#define SPEAKER_LOOPBACK_MIN_SAMPLE_RATE            SPEAKER_HOST_MIN_SAMPLE_RATE
#define SPEAKER_LOOPBACK_MAX_SAMPLE_RATE            SPEAKER_HOST_MAX_SAMPLE_RATE

#define SPEAKER_MAX_INPUT_SYSTEM_STREAMS            6
#define SPEAKER_MAX_INPUT_OFFLOAD_STREAMS           MAX_INPUT_OFFLOAD_STREAMS
#define SPEAKER_MAX_OUTPUT_LOOPBACK_STREAMS         MAX_OUTPUT_LOOPBACK_STREAMS

static
KSDATAFORMAT_WAVEFORMATEXTENSIBLE SpeakerAudioEngineSupportedDeviceFormats[] =
{
    {
        {
            sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE),
            0,
            0,
            0,
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT),
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
        },
        {
            {
                WAVE_FORMAT_EXTENSIBLE,
                6,
                48000,
                1152000,
                24,
                32,
                sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)
            },
            32,
            KSAUDIO_SPEAKER_5POINT1,
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
        }
    }
};

static
KSDATAFORMAT_WAVEFORMATEXTENSIBLE SpeakerHostPinSupportedDeviceFormats[] =
{
    {
        {
            sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE),
            0,
            0,
            0,
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT),
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
        },
        {
            {
                WAVE_FORMAT_EXTENSIBLE,
                6,
                48000,
                1152000,
                24,
                32,
                sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)
            },
            32,
            KSAUDIO_SPEAKER_5POINT1,
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
        }
    }
};

static
KSDATAFORMAT_WAVEFORMATEXTENSIBLE SpeakerOffloadPinSupportedDeviceFormats[] =
{
    {
        {
            sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE),
            0,
            0,
            0,
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT),
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
        },
        {
            {
                WAVE_FORMAT_EXTENSIBLE,
                6,
                48000,
                1152000,
                24,
                32,
                sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)
            },
            32,
            KSAUDIO_SPEAKER_5POINT1,
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
        }
    }
};

static
MODE_AND_DEFAULT_FORMAT SpeakerHostPinSupportedDeviceModes[] =
{
    { STATIC_AUDIO_SIGNALPROCESSINGMODE_RAW,            &SpeakerHostPinSupportedDeviceFormats[0].DataFormat },
    { STATIC_AUDIO_SIGNALPROCESSINGMODE_DEFAULT,        &SpeakerHostPinSupportedDeviceFormats[0].DataFormat },
    { STATIC_AUDIO_SIGNALPROCESSINGMODE_MEDIA,          &SpeakerHostPinSupportedDeviceFormats[0].DataFormat },
    { STATIC_AUDIO_SIGNALPROCESSINGMODE_MOVIE,          &SpeakerHostPinSupportedDeviceFormats[0].DataFormat },
    { STATIC_AUDIO_SIGNALPROCESSINGMODE_COMMUNICATIONS, &SpeakerHostPinSupportedDeviceFormats[0].DataFormat },
    { STATIC_AUDIO_SIGNALPROCESSINGMODE_NOTIFICATION,   &SpeakerHostPinSupportedDeviceFormats[0].DataFormat }
};

static
MODE_AND_DEFAULT_FORMAT SpeakerOffloadPinSupportedDeviceModes[] =
{
    { STATIC_AUDIO_SIGNALPROCESSINGMODE_DEFAULT, &SpeakerOffloadPinSupportedDeviceFormats[0].DataFormat },
    { STATIC_AUDIO_SIGNALPROCESSINGMODE_MEDIA,   &SpeakerOffloadPinSupportedDeviceFormats[0].DataFormat }
};

static
PIN_DEVICE_FORMATS_AND_MODES SpeakerPinDeviceFormatsAndModes[] =
{
    {
        SystemRenderPin,
        SpeakerHostPinSupportedDeviceFormats,
        SIZEOF_ARRAY(SpeakerHostPinSupportedDeviceFormats),
        SpeakerHostPinSupportedDeviceModes,
        SIZEOF_ARRAY(SpeakerHostPinSupportedDeviceModes)
    },
    {
        OffloadRenderPin,
        SpeakerOffloadPinSupportedDeviceFormats,
        SIZEOF_ARRAY(SpeakerOffloadPinSupportedDeviceFormats),
        SpeakerOffloadPinSupportedDeviceModes,
        SIZEOF_ARRAY(SpeakerOffloadPinSupportedDeviceModes)
    },
    {
        RenderLoopbackPin,
        SpeakerHostPinSupportedDeviceFormats,
        SIZEOF_ARRAY(SpeakerHostPinSupportedDeviceFormats),
        NULL,
        0
    },
    {
        BridgePin,
        NULL,
        0,
        NULL,
        0
    },
    {
        NoPin,
        SpeakerAudioEngineSupportedDeviceFormats,
        SIZEOF_ARRAY(SpeakerAudioEngineSupportedDeviceFormats),
        NULL,
        0
    }
};

static
KSDATARANGE_AUDIO SpeakerPinDataRangesStream[] =
{
    {
        {
            sizeof(KSDATARANGE_AUDIO),
            KSDATARANGE_ATTRIBUTES,
            0,
            0,
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT),
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
        },
        SPEAKER_HOST_MAX_CHANNELS,
        SPEAKER_HOST_MIN_BITS_PER_SAMPLE,
        SPEAKER_HOST_MAX_BITS_PER_SAMPLE,
        SPEAKER_HOST_MIN_SAMPLE_RATE,
        SPEAKER_HOST_MAX_SAMPLE_RATE
    },
    {
        {
            sizeof(KSDATARANGE_AUDIO),
            KSDATARANGE_ATTRIBUTES,
            0,
            0,
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT),
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
        },
        SPEAKER_OFFLOAD_MAX_CHANNELS,
        SPEAKER_OFFLOAD_MIN_BITS_PER_SAMPLE,
        SPEAKER_OFFLOAD_MAX_BITS_PER_SAMPLE,
        SPEAKER_OFFLOAD_MIN_SAMPLE_RATE,
        SPEAKER_OFFLOAD_MAX_SAMPLE_RATE
    },
    {
        {
            sizeof(KSDATARANGE_AUDIO),
            0,
            0,
            0,
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT),
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
        },
        SPEAKER_LOOPBACK_MAX_CHANNELS,
        SPEAKER_LOOPBACK_MIN_BITS_PER_SAMPLE,
        SPEAKER_LOOPBACK_MAX_BITS_PER_SAMPLE,
        SPEAKER_LOOPBACK_MIN_SAMPLE_RATE,
        SPEAKER_LOOPBACK_MAX_SAMPLE_RATE
    }
};

static
PKSDATARANGE SpeakerPinDataRangePointersStream[] =
{
    PKSDATARANGE(&SpeakerPinDataRangesStream[0]),
    PKSDATARANGE(&PinDataRangeAttributeList),
};

static
PKSDATARANGE SpeakerPinDataRangePointersOffloadStream[] =
{
    PKSDATARANGE(&SpeakerPinDataRangesStream[1]),
    PKSDATARANGE(&PinDataRangeAttributeList),
};

static
PKSDATARANGE SpeakerPinDataRangePointersLoopbackStream[] =
{
    PKSDATARANGE(&SpeakerPinDataRangesStream[2])
};

static
KSDATARANGE SpeakerPinDataRangesBridge[] =
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
PKSDATARANGE SpeakerPinDataRangePointersBridge[] =
{
    &SpeakerPinDataRangesBridge[0]
};

static
PCPROPERTY_ITEM PropertiesSpeakerOffloadPin[] =
{
    {
        &KSPROPSETID_OffloadPin,
        KSPROPERTY_OFFLOAD_PIN_GET_STREAM_OBJECT_POINTER,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_OffloadPin
    },
    {
        &KSPROPSETID_OffloadPin,
        KSPROPERTY_OFFLOAD_PIN_VERIFY_STREAM_OBJECT_POINTER,
        KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_OffloadPin
    }
};

DEFINE_PCAUTOMATION_TABLE_PROP(AutomationSpeakerOffloadPin, PropertiesSpeakerOffloadPin);

static
PCPIN_DESCRIPTOR SpeakerWaveMiniportPins[] =
{
    {
        SPEAKER_MAX_INPUT_SYSTEM_STREAMS,
        SPEAKER_MAX_INPUT_SYSTEM_STREAMS,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            SIZEOF_ARRAY(SpeakerPinDataRangePointersStream),
            SpeakerPinDataRangePointersStream,
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_SINK,
            &KSCATEGORY_AUDIO,
            NULL,
            0
        }
    },
    {
        SPEAKER_MAX_INPUT_OFFLOAD_STREAMS,
        SPEAKER_MAX_INPUT_OFFLOAD_STREAMS,
        0,
        &AutomationSpeakerOffloadPin,
        {
            0,
            NULL,
            0,
            NULL,
            SIZEOF_ARRAY(SpeakerPinDataRangePointersOffloadStream),
            SpeakerPinDataRangePointersOffloadStream,
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_SINK,
            &KSCATEGORY_AUDIO,
            NULL,
            0
        }
    },
    {
        SPEAKER_MAX_OUTPUT_LOOPBACK_STREAMS,
        SPEAKER_MAX_OUTPUT_LOOPBACK_STREAMS,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            SIZEOF_ARRAY(SpeakerPinDataRangePointersLoopbackStream),
            SpeakerPinDataRangePointersLoopbackStream,
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_SINK,
            &KSNODETYPE_AUDIO_LOOPBACK,
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
            SIZEOF_ARRAY(SpeakerPinDataRangePointersBridge),
            SpeakerPinDataRangePointersBridge,
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            NULL,
            0
        }
    },
};

static
PCNODE_DESCRIPTOR SpeakerWaveMiniportNodes[] =
{
    {
        0,
        NULL,
        &KSNODETYPE_AUDIO_ENGINE,
        NULL
    }
};

static
PCCONNECTION_DESCRIPTOR SpeakerWaveMiniportConnections[] =
{
    { PCFILTER_NODE,            KSPIN_WAVE_RENDER_SINK_SYSTEM,  KSNODE_WAVE_AUDIO_ENGINE, 1 },
    { PCFILTER_NODE,            KSPIN_WAVE_RENDER_SINK_OFFLOAD, KSNODE_WAVE_AUDIO_ENGINE, 2 },
    { KSNODE_WAVE_AUDIO_ENGINE, 3,                              PCFILTER_NODE,             KSPIN_WAVE_RENDER_SINK_LOOPBACK },
    { KSNODE_WAVE_AUDIO_ENGINE, 0,                              PCFILTER_NODE,             KSPIN_WAVE_RENDER_SOURCE },
};

static
PCPROPERTY_ITEM PropertiesSpeakerWaveFilter[] =
{
    {
        &KSPROPSETID_Pin,
        KSPROPERTY_PIN_PROPOSEDATAFORMAT,
        KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_WaveFilter
    },
    {
        &KSPROPSETID_Pin,
        KSPROPERTY_PIN_PROPOSEDATAFORMAT2,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_WaveFilter
    }
};

DEFINE_PCAUTOMATION_TABLE_PROP(AutomationSpeakerWaveFilter, PropertiesSpeakerWaveFilter);

static
PCFILTER_DESCRIPTOR SpeakerWaveMiniportFilterDescriptor =
{
    0,
    &AutomationSpeakerWaveFilter,
    sizeof(PCPIN_DESCRIPTOR),
    SIZEOF_ARRAY(SpeakerWaveMiniportPins),
    SpeakerWaveMiniportPins,
    sizeof(PCNODE_DESCRIPTOR),
    SIZEOF_ARRAY(SpeakerWaveMiniportNodes),
    SpeakerWaveMiniportNodes,
    SIZEOF_ARRAY(SpeakerWaveMiniportConnections),
    SpeakerWaveMiniportConnections,
    0,
    NULL
};

#endif // _SYSVAD_SPEAKERWAVTABLE_H_
