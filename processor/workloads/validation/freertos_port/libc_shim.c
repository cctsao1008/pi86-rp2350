#include <stddef.h>

void * memcpy( void * pvDestination, const void * pvSource, size_t xLength )
{
    unsigned char * pucDestination = ( unsigned char * ) pvDestination;
    const unsigned char * pucSource = ( const unsigned char * ) pvSource;

    while( xLength > 0U )
    {
        *pucDestination++ = *pucSource++;
        xLength--;
    }

    return pvDestination;
}

void * memset( void * pvDestination, int iValue, size_t xLength )
{
    unsigned char * pucDestination = ( unsigned char * ) pvDestination;
    const unsigned char ucValue = ( unsigned char ) iValue;

    while( xLength > 0U )
    {
        *pucDestination++ = ucValue;
        xLength--;
    }

    return pvDestination;
}
