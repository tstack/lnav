
#include "config.h"

#include <stdlib.h>
#include <assert.h>

#include "chunky_index.hh"
#include "../src/chunky_index.hh"

int main(int argc, char *argv[])
{
    int retval = EXIT_SUCCESS;

    {
        chunky_index<int> ci;

        ci.reset();
        ci.finish();
        assert(ci.chunk_count() == 0);
    }

    {
        chunky_index<int> ci;

        ci.reset();
        ci.merge_value(1);
        ci.finish();
        ci.reset();
        ci.merge_value(2);
        ci.finish();

        assert(ci.size() == 2);
        assert(ci[0] == 1);
        assert(ci[1] == 2);
        assert(ci.chunk_count() == 1);

        ci.clear();
        assert(ci.size() == 0);
        assert(ci.chunk_count() == 0);
    }

    {
        int expected[] = {0, 10, 11, 20, 30, 40, 50, 60, 70, 80, 90, 100};
        chunky_index<int, 4> ci;

        ci.reset();
        for (int lpc = 0; lpc < 11; lpc++) {
            ci.merge_value(lpc * 10);
        }
        ci.finish();
        ci.reset();
        ci.merge_value(11);
        ci.finish();
        for (int lpc = 0; lpc < 12; lpc++) {
            assert(expected[lpc] == ci[lpc]);
        }
        assert(ci.chunk_count() == 3);
    }

    {
       int expected[] = {0, 10, 20, 30, 40, 50, 51, 60, 70, 80, 90, 100};
        chunky_index<int, 4> ci;

        ci.reset();
        for (int lpc = 0; lpc < 11; lpc++) {
            ci.merge_value(lpc * 10);
        }
        ci.finish();
        ci.reset();
        ci.merge_value(51);
        ci.finish();
        for (int lpc = 0; lpc < 12; lpc++) {
            assert(expected[lpc] == ci[lpc]);
        }
        assert(ci.chunk_count() == 3);
    }

    {
        int expected[] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110};
        chunky_index<int, 4> ci;

        ci.reset();
        for (int lpc = 0; lpc < 11; lpc++) {
            ci.merge_value(lpc * 10);
        }
        ci.finish();
        ci.reset();
        ci.merge_value(110);
        ci.finish();
        for (int lpc = 0; lpc < 12; lpc++) {
            assert(expected[lpc] == ci[lpc]);
        }
        assert(ci.chunk_count() == 3);
    }

    return retval;
}
