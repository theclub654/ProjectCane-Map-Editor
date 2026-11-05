#include "EditorApplication.h"

#include <bit>
#include <cstdint>
#include <cstddef>

// The native binary readers and current project snapshots use this ABI.
static_assert(std::endian::native == std::endian::little, "ProjectCane requires a little-endian host.");

#ifdef __linux__
static_assert(sizeof(void *) == 8 && sizeof(std::size_t) == 8, "Use a 64-bit Linux editor build.");
#include <sys/resource.h>
#include <cstdio>
#endif

int main()
{
#ifdef __linux__
    // Match the existing Visual Studio stack reservation for the large editor
    // application frame and bounded recursive native serializers.
    rlimit stack{};
    constexpr rlim_t desiredStack = 64u * 1024u * 1024u;
    if (getrlimit(RLIMIT_STACK, &stack) == 0 && stack.rlim_cur < desiredStack)
    {
        stack.rlim_cur = stack.rlim_max == RLIM_INFINITY ? desiredStack :
            (stack.rlim_max < desiredStack ? stack.rlim_max : desiredStack);
        if (setrlimit(RLIMIT_STACK, &stack) != 0)
            std::fputs("Could not raise the editor stack limit; large documents may exceed the host limit.\n", stderr);
    }
#endif
    return RunEditorApplication();
}
