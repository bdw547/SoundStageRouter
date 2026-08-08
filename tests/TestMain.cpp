#include "TestHarness.h"

int main()
{
    int failures = 0;
    for (const auto& [name, function] : test::Registry())
    {
        try
        {
            function();
            std::cout << "PASS " << name << '\n';
        }
        catch (const std::exception& error)
        {
            ++failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << test::Registry().size() - failures << "/"
              << test::Registry().size() << " passed\n";
    return failures == 0 ? 0 : 1;
}
