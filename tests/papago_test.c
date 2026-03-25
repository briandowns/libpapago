#include <time.h>
#include <stdlib.h>

#include "crosscheck.h"

#include "../papago.h"

void
cc_setup()
{
}

void
cc_tear_down()
{
}

int
main(void)
{
    srand(time(NULL));

    CC_INIT;

    // test runs here

    CC_COMPLETE;

}