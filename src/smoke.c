/* ISP smoke test: does nothing but prove we can build + flash the chip. */
#include "ch555.h"

void main(void)
{
    /* Keep all pins in their reset state; do not enable USB. */
    while (1) {
        _nop_();
    }
}
