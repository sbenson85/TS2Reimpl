// Forced include for the probes (/FI, -DKN=bits): what the game's units keep out of line although a small unit inlines it.
// Each is declared here and defined nowhere, so the call stays; the objects are only read, never linked (lane XI).
//   KN bit 0: the over-aligned allocation; 1: string's copy constructor; 2: string's copy assignment; 3: string::assign(ptr, n)
//   KN bit 4: the over-aligned allocation, 5: string's copy constructor, DEFINED here, not inlined: the compiler then
//   knows they do not throw and what they clobber, as the original's did (rendering.md, "XI result")
#include <string>
#include <xmemory>
#if KN & 1
template <> void *std::_Allocate_manually_vector_aligned<std::_Default_allocate_traits>(const size_t);
#endif
#if KN & 2
template <> std::string::basic_string(const std::string &);
#endif
#if KN & 4
template <> std::string &std::string::operator=(const std::string &);
#endif
#if KN & 8
template <> std::string &std::string::assign(const char *const, const size_t);
#endif
#if KN & 16
template <> __declspec(noinline) void *std::_Allocate_manually_vector_aligned<std::_Default_allocate_traits>(const size_t bytes)
{
    const size_t block = std::_Non_user_size + bytes;
    if (block <= bytes) std::_Throw_bad_array_new_length();
    const uintptr_t container = reinterpret_cast<uintptr_t>(std::_Default_allocate_traits::_Allocate(block));
    _STL_VERIFY(container != 0, "invalid argument");
    void *const result = reinterpret_cast<void *>((container + std::_Non_user_size) & ~(std::_Big_allocation_alignment - 1));
    static_cast<uintptr_t *>(result)[-1] = container;
    return result;
}
#endif
#if KN & 32
template <> __declspec(noinline) std::string::basic_string(const std::string &right)
    : _Mypair(_One_then_variadic_args_t{}, _Alty_traits::select_on_container_copy_construction(right._Getal()))
{
    _Construct<_Construct_strategy::_From_string>(right._Mypair._Myval2._Myptr(), right._Mypair._Myval2._Mysize);
}
#endif
