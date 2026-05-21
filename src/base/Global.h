#ifndef __GLOBAL_20220116_H__
#define __GLOBAL_20220116_H__

#include "Random.h"

namespace cncpp
{

    extern Random   rnd;  //(0xdeadbeef);
    extern uint64_t process_tick_ms;

    extern uint32_t random();
    extern uint64_t getProcessTickMs();

}  // namespace cncpp
#endif