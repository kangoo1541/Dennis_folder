/*
 * ap.h  (AP : Application)
 *
 *  실제 하고 싶은 일(응용 로직)을 담는 계층
 */

#ifndef AP_H_
#define AP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw.h"
#include "bsp.h"


void apInit(void);
void apMain(void);


#ifdef __cplusplus
}
#endif

#endif /* AP_H_ */
