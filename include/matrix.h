#ifndef CH555_MATRIX_H
#define CH555_MATRIX_H

#include "ch555.h"

/* Candidate matrix pins, in order: P0.0..P0.7, P1.0..P1.7,
 * P2.0..P2.7, P3.0..P3.7, P4.0..P4.7. */
#define MATRIX_PINS 40

void matrix_init(void);
void matrix_drive(UINT8 pin);          /* drive one pin low, others pulled up */

UINT8 matrix_port(UINT8 port);
void matrix_all_high(void);
void matrix_settle(void);

#endif
