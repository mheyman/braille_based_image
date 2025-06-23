#include <iostream>
#include <vector>
#include <mdspan>
#include <string>
#include <braille_based_image.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <clocale>
#include <Windows.h>
#endif
int main()
{
#ifdef _WIN32
    std::setlocale(LC_CTYPE, ".UTF8");
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    std::array<bool, 48> flat_mask = { {
        true, true, false, false, true, true,
        false, true, true, true, true, false,
        false, false, true, true, false, false,
        false, false, true, true, false, false,
        false, false, true, true, false, false,
        false, true, true, true, true, false,
        true, true, false, false, true, true,
        true, true, true, true, true, true,
    } };

    constexpr std::size_t rows = 8;
    constexpr std::size_t cols = 6;
    auto const mask = std::mdspan(flat_mask.data(), rows, cols);
    std::cout << brma::mask_braille(mask, brma::border::line) << "\n";
    std::cout << brma::mask_braille(mask) << "\n";
    return 0;
}
