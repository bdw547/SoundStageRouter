//===========================================================================
// HARDWARE OFFLOAD PIN DEFINITIONS
//===========================================================================
#define STATIC_KSPROPSETID_OffloadPin\
    0x143b5653, 0x4923, 0x4c2b, 0x8b, 0x17, 0x14, 0x06, 0xde, 0x69, 0xb6, 0x9d

DEFINE_GUIDSTRUCT("143B5653-4923-4C2B-8B17-1406DE69B69D", KSPROPSETID_OffloadPin);

#define KSPROPSETID_OffloadPin DEFINE_GUIDNAMED(KSPROPSETID_OffloadPin)

typedef enum {
    KSPROPERTY_OFFLOAD_PIN_GET_STREAM_OBJECT_POINTER,
    KSPROPERTY_OFFLOAD_PIN_VERIFY_STREAM_OBJECT_POINTER
} KSPROPERTY_OFFLOAD_PIN;

