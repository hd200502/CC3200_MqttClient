#ifndef MQTT_TYPEDEF_H
#define MQTT_TYPEDEF_H

typedef signed char                       int8;
typedef signed short                      int16;
typedef signed long                       int32;
typedef signed int                        intx;
typedef unsigned char                     uint8;
typedef unsigned short                    uint16;
typedef unsigned long                     uint32;
typedef unsigned int                      uintx;

typedef unsigned char                     U8;
typedef signed char                       S8;
typedef unsigned short                    U16;
typedef signed short                      S16;
typedef unsigned int                      U32;
typedef signed int                        S32;


#define BOOL    U32
#define FALSE   0
#define TRUE    1

#define ERROR_TRACE(...) 
#define DEBUG_TRACE(...)

#define MQTT_ALLOC(x) malloc(x)
#define MQTT_FREE(x)  free(x)

#endif
