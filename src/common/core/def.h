/*
 * def.h
 *
 *  프로젝트 전체에서 공통으로 쓰는 기본 타입/에러코드 정의
 *
 *  [초보자 설명]
 *   - stdint.h : uint8_t, int32_t 처럼 "몇 비트짜리 정수인지"가 이름에 드러나는 타입
 *   - stdbool.h: true / false 를 쓸 수 있게 해주는 표준 헤더
 *   임베디드에서는 int 의 크기가 컴파일러마다 다를 수 있어서
 *   uint8_t(0~255), uint16_t(0~65535) 처럼 크기가 확실한 타입을 씁니다.
 */

#ifndef DEF_H_
#define DEF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


/* 함수 실행 결과를 알려주는 공통 에러 코드 */
typedef enum
{
  OK              = 0,   /* 정상 처리       */
  ERR_INVALID_PARAM,     /* 인자가 잘못됨   */
  ERR_TIMEOUT,           /* 시간 초과       */
  ERR_FULL,              /* 공간이 가득 참  */
  ERR_NOT_OPEN,          /* 아직 열리지 않음*/
} err_code_t;


/* 배열의 원소 개수를 구하는 매크로 (sizeof 배열 / sizeof 원소1개) */
#define DEF_ARRAY_SIZE(x)   (sizeof(x) / sizeof(x[0]))

/* 두 값 중 작은 값 / 큰 값 */
#ifndef constrain
#define constrain(amt, low, high)  ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif


#ifdef __cplusplus
}
#endif

#endif /* DEF_H_ */
