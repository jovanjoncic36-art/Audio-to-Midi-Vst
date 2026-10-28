#include "TestFramework.h"

int main()
{
    int failedTests = 0;
    for (const auto& t : tf::registry())
    {
        tf::currentFailures() = 0;
        std::printf ("[ RUN  ] %s\n", t.name);
        try
        {
            t.fn();
        }
        catch (const std::exception& e)
        {
            std::printf ("    EXCEPTION: %s\n", e.what());
            ++tf::currentFailures();
        }

        if (tf::currentFailures() == 0)
            std::printf ("[  OK  ] %s\n", t.name);
        else
        {
            std::printf ("[ FAIL ] %s (%d failed checks)\n", t.name, tf::currentFailures());
            ++failedTests;
        }
    }

    std::printf ("\n%d tests, %d failed\n", (int) tf::registry().size(), failedTests);
    return failedTests == 0 ? 0 : 1;
}
